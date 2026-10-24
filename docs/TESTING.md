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
task test           # both suites (unit + e2e)
task test:unit      # Python unit tests only
task test:e2e       # emulator + Playwright e2e tests (core suite)
```

With mise activated in your shell, `mise exec --` prefix is optional.
On first use, Task asks you to trust the two remote setup-fm1 Taskfiles
listed in `Taskfile.yml`. CI uses `task --yes` to accept them non-interactively.

Run one E2E test with:

```sh
task test:e2e -- -k test_firmware_boots
```

Run a specific E2E file outside the Taskfile (useful for tests not in the core suite):

```sh
uv run python -m pytest tests/e2e_ui_controls_test.py -v
```

## Test architecture

### Unit tests (`tests/test_*.py`)

Host-only, no hardware or emulator. Cover configuration generation, package
checksums/compression, defconfig validation, and control helper validation.

### E2E tests (`tests/e2e_*_test.py`)

Browser-based tests using the fm1-emu WASM emulator and Playwright.
Each test class inherits from `E2ETestCase` (in `tests/e2e_fixtures.py`), which:

- Starts a local HTTP server serving the emulator
- Launches headless Chromium via Playwright
- Provides `controls(page)` for named encoders, buttons, and notes
- Provides helpers: `push()`, `long_push()`, `send_enc()`, `screenshot()`
- Saves screenshots and logs in `tests/results/<timestamp>-<module>/`

A typical E2E test uploads the built `.fwsc` into the emulator, waits for boot,
then interacts with the virtual panel and asserts LCD canvas state via pixel
hash comparison.

## E2E test inventory

### Core suite (run by `task test:e2e`)

| File | What it tests | Controls exercised |
|------|--------------|-------------------|
| `e2e_boot_test.py` | Firmware boots, LCD renders, panel messages reach emulator | All encoders (0-6), all 14 buttons, 3 notes, hold+encoder, master pot |
| `e2e_components_test.py` | Build manifest matches defconfig, binary manifest | None (build verification) |
| `e2e_usb_safety_test.py` | USB SysEx keys present/absent per defconfig | None (binary/manifest checks) |
| `e2e_audio_test.py` | Oscillator output, analog acid sequence | Notes (C4-G4) |
| `e2e_audio_engines_test.py` | Engine switching, audio output per engine, level normalization, master volume | SELECT encoder, HOME button, master pot, notes |

### Extended suite (run individually or via pytest glob)

| File | What it tests | Controls exercised |
|------|--------------|-------------------|
| `e2e_ui_controls_test.py` | **All Felucca UI controls on all pages** (59 tests). Button-tap navigation, long-press, encoder CW/CCW on every param page, dead buttons, hold+encoder combos, full page sweep | All buttons, all encoders, notes, hold+encoder combos |
| `e2e_page_nav_test.py` | Page-change latency via SELECT encoder | SELECT encoder CW/CCW |
| `e2e_sys_menu_test.py` | SYS page opens, tab switching, value cycling, HOME return | HOME (long-press), ALGO encoder, SELECT encoder |
| `e2e_debug_log_test.py` | DEBUG page renders boot log, scroll works | GLO button, SELECT encoder |
| `e2e_stuck_notes_test.py` | Notes don't stick after engine switch or page change | SELECT encoder, notes |
| `e2e_ble_test.py` | BLE subsystem integration | (varies) |

### Coverage summary

| Area | Covered by |
|------|-----------|
| Button → page navigation (all 9 nav buttons) | `e2e_ui_controls_test.py::TestButtonNavigation` |
| Long-press HOME → SYS | `e2e_ui_controls_test.py::TestLongPressNavigation`, `e2e_sys_menu_test.py` |
| SELECT encoder page nav (CW/CCW) | `e2e_ui_controls_test.py::TestSelectEncoder`, `e2e_page_nav_test.py` |
| HOME: engine select, track select, K1-K3 | `e2e_ui_controls_test.py::TestHomePage` |
| Param pages: K1-K3 on ENV/LFO/FX/SCL/EDIT/ARP | `e2e_ui_controls_test.py::Test{ENV,LFO,FX,SCL,EDIT,ARP}Page` |
| VOICE, MIXER pages | `e2e_ui_controls_test.py::TestVOICEPage`, `TestMIXERPage` |
| Dead buttons (OCT+/-, PLAY, REC, SAVE) | `e2e_ui_controls_test.py::TestDeadButtons` |
| Hold+encoder combos | `e2e_ui_controls_test.py::TestHoldEncoderCombo` |
| Full page sweep (all registered pages) | `e2e_ui_controls_test.py::TestFullPageSweep` |
| Engine audio output | `e2e_audio_engines_test.py` |
| Master volume | `e2e_audio_engines_test.py::TestMasterVolume` |

### Known gaps

- **OCT+/-, PLAY, REC, SAVE** have no firmware handler. Tests verify they don't
  crash but can't verify function.
- **PRESET encoder (ENC 6)** is tested for message delivery (`e2e_boot_test`)
  but has no page-level handler to verify.

## Panel controls in tests

```python
ctrl = self.controls(page)
ctrl.rotate_encoder('presets', direction=1, steps=3)
ctrl.press_button('home')
ctrl.press_button('home', hold_ms=600)     # long-press
with ctrl.hold_button('edit'):
    ctrl.rotate_encoder('knob1', direction=-1, steps=2)
ctrl.play_note('C#4', hold_ms=500)
ctrl.set_master_volume(512)
```

### Physical controls map

| Encoder ID | Name | Test name | What it does |
|-----------|------|-----------|-------------|
| 0 | SELECT | `select` | Engine select (HOME), page nav (others), scroll (DEBUG/FILES/BLE) |
| 1 | ALGO | `algorithm` | Track select (HOME), tab switch (SYS) |
| 2 | KNOB1 | `knob1` | Param knob[0] (HOME), param[0] (param pages) |
| 3 | KNOB2 | `knob2` | Param knob[1] (HOME), param[1] (param pages) |
| 4 | KNOB3 | `knob3` | Param knob[2] (HOME), param[2] (param pages) |
| 5 | KNOB4 | `knob4` | Param knob[3] (HOME), param[3] (param pages) |
| 6 | PRESET | `presets` | (no page handler yet) |
| — | MASTER | `master` | Analog volume pot (hardware ADC, not an encoder) |

### Button map

| ID | Name | Test name | Tap action | Hold action (500ms) |
|----|------|-----------|-----------|-------------------|
| 0 | OCT- | `oct-` | — | — |
| 1 | OCT+ | `oct+` | — | — |
| 2 | FX | `fx` | → FX page | — |
| 3 | SCL | `scl` | → SCL page | — |
| 4 | ENV | `env` | → ENV page | — |
| 5 | LFO | `lfo` | → LFO page | — |
| 6 | EDIT | `edit` | → EDIT page | — |
| 7 | GLO | `glo` | → DEBUG page* | — |
| 8 | HOME | `home` | → HOME page | → SYS page** |
| 9 | SAVE | `save` | — | — |
| 10 | ARP | `arp` | → ARP page | — |
| 11 | SEQ | `seq` | → SEQ page | — |
| 12 | PLAY | `play` | — | — |
| 13 | REC | `rec` | — | — |

\* Only when `CONFIG_DEBUG_LOG` is enabled, otherwise no action.
\** Only when `CONFIG_SYS_MENU` is enabled, otherwise → ABOUT page.

### Assertion patterns

E2E tests detect UI changes by comparing pixel hashes of LCD canvas regions:

```python
# Full LCD region (native canvas coords: 1280x820, LCD at 370,42 size 480x480)
LCD_REGION = (370, 42, 480, 480)

# Hash a region before and after an action
before = _lcd_region_hash(page, *LCD_REGION)
ctrl.press_button('env', delay_ms=500)
after = _lcd_region_hash(page, *LCD_REGION)
self.assertNotEqual(before, after, 'LCD did not change')
```

For finer assertions, use sub-regions (cards row, footer) to detect which
part of the screen changed. The `_canvas_hash()` function hashes the entire
canvas including the panel frame.

## Screenshots and logs

Each run writes under `tests/results/<UTC-timestamp>-<test-module>/`:

- `screenshots/*.png`: panel canvas captures at named test points.
- `*.log`: emulator status, firmware log, JS console output.

Results are gitignored. No `E2E_DEBUG` setting is required.

## CI results

GitHub Actions runs unit tests and the core E2E suite on pull requests and
pushes to `main`. Setup uses `mise install` and `uv sync --locked`. The
`dimmkirr/setup-fm1-toolchain@v1` and `dimmkirr/setup-fm1-emulator@v1` actions
install the SDK, compiler, and emulator.

Each run that reaches E2E saves screenshots, logs, and JUnit XML in the
`e2e-results` Actions artifact for 14 days, including when tests fail.
`main` push/manual runs also attach captured PNGs to the rolling
`ci-screenshot` prerelease and embed them in the job summary.

Pull requests never publish releases.

## Tagged releases

Pushing a tag matching `v[0-9]*` starts `release.yml`. It calls the same CI
build and test workflow, then publishes only if that workflow succeeds.
The release uses the exact firmware tested by E2E; it does not rebuild it.

Release assets: `core-fm1.fwsc`, `minimal.config`, tagged source archive,
license notices, `SHA256SUMS`, and panel screenshots.

## Emulator setup fallback

`FM1_WEB_DIR` selects an existing emulator web distribution; the default is
`~/.fm1-emulator/web`. The download task's source-build fallback needs Rust
and `rustup`. On Nix with the WASM target already installed, build a local
fm1-emu v0.2.0 checkout directly:

```sh
task emulator:build EMU_SOURCE=/path/to/fm1-emu
```

## Toolchain and cross-compilation

The JieLi clang 4.0.1 compiler is an x86-64 Linux binary. The build system
(`tools/build.py`) auto-detects the host architecture and picks the right
runner:

- **x86-64 Linux**: runs the compiler natively.
- **ARM/aarch64 Linux**: requires `qemu-x86_64` plus x86-64 glibc and
  libstdc++ in the nix store.
- **macOS / Docker fallback**: set `JIELI_DOCKER=1` to compile inside a
  `linux/amd64` Docker container.

These tests do not flash hardware.
