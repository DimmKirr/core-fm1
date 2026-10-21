# Testing

## Setup

Install [mise](https://mise.jdx.dev/getting-started.html), then run from the
repository root:

```sh
mise trust
mise install
mise exec -- uv sync --locked
```

[.mise.toml](../.mise.toml) pins Python, uv, and Task. mise provides the Python
interpreter; uv creates `.venv` and installs dependencies from
[pyproject.toml](../pyproject.toml) and [uv.lock](../uv.lock).
No separate Python installation is needed.

For browser tests, use an installed `chromium`, set `CHROMIUM_PATH`, or install
Playwright's browser:

```sh
mise exec -- uv run playwright install chromium
```

On Ubuntu, add `--with-deps` to install required system libraries too.

## Run tests

```sh
mise exec -- task test:unit
mise exec -- task config:defconfig -- minimal
mise exec -- task test:e2e
```

Or run both suites with `mise exec -- task test` after selecting the configuration.
With mise activated in your shell, the `mise exec --` prefix is optional.
On first use, Task asks you to trust the two remote setup-fm1 Taskfiles
listed in `Taskfile.yml`. CI uses `task --yes` to accept them non-interactively.

Unit tests cover Kconfig generation, package checksums/compression, and control
helper validation. E2E builds `build/core-fm1.fwsc`, downloads the SDK/toolchain
and WASM emulator when missing, and opens the package in headless Chromium.

The E2E tests verify the LCD title pixels, check the display again after ten
seconds, and verify knob/button/note messages reach the emulator. The minimal
firmware does not yet handle panel input or produce audio. Three preset tests
skip because the Melodee, Felucca, and SLOOP defconfigs are absent.

Run one E2E test with:

```sh
mise exec -- task test:e2e -- -k test_panel_controls_reach_emulator
```

## Screenshots and logs

Each run writes under `tests/results/<UTC-timestamp>-<test-module>/`:

- `screenshots/*.png`: panel canvas only, captured automatically.
- `fm1-emulator.log`: boot test diagnostics.
- `panel-controls.log`: control test diagnostics.

No `E2E_DEBUG` setting is required. Results are ignored by Git.

## CI results

GitHub Actions runs unit tests and the minimal firmware E2E suite on pull
requests and pushes to `main`. It can also be started manually. Setup uses
`mise install` and `uv sync --locked`. The `dimmkirr/setup-fm1-toolchain@v1`
and `dimmkirr/setup-fm1-emulator@v1` actions install the SDK, compiler, and
emulator. CI checks `fm1-emu --help`, installs Chromium with its system
libraries using the locked Playwright version, and passes the actions' paths
to the E2E task. Task reuses those installations and builds the firmware
before testing.

Each run that reaches E2E saves screenshots, logs, and JUnit XML in the
`e2e-results` Actions artifact for 14 days, including when tests fail.
`main` push/manual runs also attach captured PNGs to the rolling
`ci-screenshots` prerelease from a step in the test job, including after E2E
failures, and embed them in the job summary. No separate screenshot job runs.
Release notes
identify the run, commit, and test outcome; older screenshot assets remain.
Pull requests never publish releases. This is a screenshot prerelease, not a
firmware release or a claim that hardware testing passed.

The test job requests `contents: write` for this publication step. The
main-only condition prevents PR publication; it is not a token-permission
boundary. Same-repository PR jobs can receive write permission, while fork PR
tokens are normally read-only. Runs on the same ref are serialized.

## Tagged releases

Pushing a tag matching `v[0-9]*` starts `release.yml`. It calls the same CI
build and test workflow, then publishes only if that workflow succeeds.
The release uses the exact firmware tested by E2E; it does not rebuild it.

Release assets are `core-fm1.fwsc`, `minimal.config`, the tagged source archive,
license notices, `SHA256SUMS`, and the panel screenshots. Screenshots are also
embedded in the release notes and job summary. Failed test runs retain their
diagnostic artifact but do not publish a versioned release.

The minimal LCD-only milestone is marked as a prerelease and is not hardware
verified. Existing releases are not overwritten on reruns, and no moving
major-version tag is created.

## Panel controls in tests

```python
panel = self.controls(page)
panel.rotate_encoder('presets', direction=1, steps=3)
panel.press_button('home')
with panel.hold_button('edit'):
    panel.rotate_encoder('knob1', direction=-1, steps=2)
panel.play_note('C#4', hold_ms=500)
```

Encoder names: `select`, `presets`, `algorithm`, `knob1` through `knob4`.
`master` controls the analog pot. The upstream aliases `k1` and `k2` map to
`presets` and `algorithm`, not `knob1` and `knob2`.
Button names match the panel labels; `press_button(..., hold_ms=900)` holds a
button longer. Notes span C4 through F5.

## Emulator setup fallback

`FM1_WEB_DIR` selects an existing emulator web distribution; the default is
`~/.fm1-emulator/web`. The download task's source-build fallback needs Rust
and `rustup`. On Nix with the WASM target already installed, build a local
fm1-emu v0.2.0 checkout directly:

```sh
mise exec -- task emulator:build EMU_SOURCE=/path/to/fm1-emu
```

The JieLi compiler runs natively on x86-64 Linux, through QEMU on supported
ARM Linux setups, or through Docker. These tests do not flash hardware.
