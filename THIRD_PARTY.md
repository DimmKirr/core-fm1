# Non-GPL components

core-fm1's firmware and project code are licensed under
[GPL-3.0-only](LICENSE). The components below retain their own licenses.
This inventory covers the current minimal build, not engines planned for future
integration. Updated: 2026-10-21.

## Included in firmware packages

These three files belong to the **JieLi AC79 SDK**, published by Zhuhai JieLi
Technology under Apache-2.0. They are downloaded by the SDK task in
[Taskfile.yml](Taskfile.yml), outside this repository. During the build,
[`flash_image()` in the packager](tools/fm1pkg_make.py) reads them from
`$AC79_SDK/cpu/wl82/tools/` and incorporates their bytes into the flash image
inside `build/core-fm1.fwsc`.

| Component                 | SDK path and upstream source                                                                                                                            | Purpose                                                    | License    |
|---------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------|------------------------------------------------------------|------------|
| JieLi SPL                 | [`cpu/wl82/tools/uboot.boot`](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK/blob/AC79NN_SDK_V1.2.14_2026-10-13/cpu/wl82/tools/uboot.boot)               | First-stage boot loader in the package's flash header area | Apache-2.0 |
| Chip configuration        | [`cpu/wl82/tools/cfg_tool.bin`](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK/blob/AC79NN_SDK_V1.2.14_2026-10-13/cpu/wl82/tools/cfg_tool.bin)           | Configuration data in the package's application area       | Apache-2.0 |
| Hardware EQ configuration | [`cpu/wl82/tools/cfg/eq_cfg_hw.bin`](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK/blob/AC79NN_SDK_V1.2.14_2026-10-13/cpu/wl82/tools/cfg/eq_cfg_hw.bin) | Default EQ data in the package's application area          | Apache-2.0 |

License basis: the
SDK's [upstream license](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK/blob/AC79NN_SDK_V1.2.14_2026-10-13/LICENSE)
and [publisher's license statement](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK/blob/release/AC79NN_SDK_V1.2.0/README-en.md).
The downloaded SDK's root `LICENSE` was checked and is byte-identical to our
local copy, [Apache-2.0.txt](LICENSES/Apache-2.0.txt). No separate license or NOTICE was
found alongside these three files. This attribution follows the SDK-wide
license; the binaries do not carry individual source-license headers.

`uboot.boot` here means JieLi's SDK SPL, not the separate Das U-Boot project.
It is also distinct from core-fm1's GPL-3.0 boot stub in `firmware/stub/` and
update loader in `firmware/loader/`.

The current downloader selects SDK tag `AC79NN_SDK_V1.2.14_2026-10-13`.
An existing SDK installation or an `AC79_SDK` override can supply different
bytes. The SDK files checked for this build have these SHA-256 digests:

```text
64ccc63e4df3238046fadb200f1b134311d8fe6ec6e3ccb8c8b81379231b867e  uboot.boot
276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b  cfg_tool.bin
179ac9f4501d2cf8c3bdbc06fcda1e46228b7ace7e339342c767bb6568de57d8  cfg/eq_cfg_hw.bin
```

Distributions containing these files must include their Apache-2.0 license
and applicable notices. Apache-2.0 is compatible with GPL-3.0; retaining these
notices does not change core-fm1's project license. See the
[Apache Software Foundation's compatibility explanation](https://apache.org/licenses/GPL-compatibility.html).

## Upstream firmware subtrees

Community firmware source trees embedded via `git subtree add` (no squash, full
history preserved). Each subtree is pinned to a specific tag or commit. Update
with `git subtree pull --prefix=<path> <url> <ref>`.

| Subtree | Source | Ref | License | Use |
|---------|--------|-----|---------|-----|
| [`firmware/upstream/felucca/`](firmware/upstream/felucca/) | [hugelton/Felucca](https://github.com/hugelton/Felucca) | v1.6.1 | GPL-3.0 | UI renderer (`gfx.c`, `lcd.c`), audio core reference, sequencer reference |
| [`firmware/upstream/felucca-engines/`](firmware/upstream/felucca-engines/) | [hugelton/Felucca-Engines](https://github.com/hugelton/Felucca-Engines) | c46e008 | MIT | ANALOG, NOISE, LOFI, TRIO, WHEEL engines; `dsp.c` DSP helpers; `felucca_engine.h` vtable |
| [`firmware/upstream/melodee/`](firmware/upstream/melodee/) | [keremimo/melodee](https://github.com/keremimo/melodee) | v1.4.5 | GPL-3.0 | CZ, SID, PROPHET, FORMANT + 7 more engines; extended `dsp.c`; FPU-optimized audio core reference |
| [`firmware/upstream/sloop/`](firmware/upstream/sloop/) | [isod89/sloop-fm1](https://github.com/isod89/sloop-fm1) | v2.8 | GPL-3.0 | DRAWBAR, GRAIN, SAMPLE, SLICE engines; arranger/scene system reference |
| [`firmware/upstream/fmlab/`](firmware/upstream/fmlab/) | [jsmith13/fm_lab](https://github.com/jsmith13/fm_lab) | v1.1.0 | GPL-3.0 | Q23 24-bit audio pipeline; DX7 engine; 2x oversampling; FDN reverb; TPDF dithering |

The Felucca-Engines subtree is a separate repository from the main Felucca
subtree because upstream Felucca references it as a git submodule (MIT-licensed
engines kept separate from the GPL-3.0 firmware). The subtree commit `c46e008`
matches the submodule pointer in Felucca v1.6.1.

core-fm1 includes upstream source via `#include` directives (unity build), not
by copying. Engine source files are included conditionally from the subtree
paths (e.g. `#include "upstream/felucca-engines/src/eng_noise.c"`), gated by
Kconfig symbols. See [docs/PORTING.md](docs/PORTING.md) for the adapter pattern.

## Included in the source tree

| Component | Local path | Source and license | Use |
| --- | --- | --- | --- |
| FatFs R0.15 (stubs) | [`firmware/vendor/fatfs/`](firmware/vendor/fatfs/) | [1-clause BSD (FatFs license)](http://elm-chan.org/fsw/ff/doc/appnote.html#license) | FAT32 filesystem for USB mass storage; read-only config, no exFAT |
| setup-fm1-emulator control helpers | [tests/fm1_controls.py](tests/fm1_controls.py) | [MIT-licensed upstream example](https://github.com/DimmKirr/setup-fm1-emulator/blob/9d0fdd259343edbe29f1e4a3bfea85c7739f5d3c/examples/e2e_firmware_ui_test.py), [local license](LICENSES/MIT.txt) | Named encoder, button, and note controls for E2E tests; not included in firmware |

The port retains the example's Kerem Kilic (Ellic Studio) copyright and the
repository's DimmKirr notice. Coordinates and encoder aliases are adapted to
fm1-emu v0.2.0; key release on errors and input validation are added locally.

## Build and test dependencies

These run on the development host and are not copied into the firmware.
Python package versions are recorded in [uv.lock](uv.lock).

| Component  | License / upstream evidence                                                    | Use                                          |
|------------|--------------------------------------------------------------------------------|----------------------------------------------|
| Kconfiglib | [ISC](https://github.com/ulfalizer/Kconfiglib/blob/master/LICENSE.txt)         | Configuration parser and menuconfig          |
| pytest     | [MIT](https://github.com/pytest-dev/pytest/blob/main/LICENSE)                  | Unit and E2E test runner                     |
| Playwright | [Apache-2.0](https://github.com/microsoft/playwright-python/blob/main/LICENSE) | Browser automation                           |
| Task       | [MIT](https://github.com/go-task/task/blob/main/LICENSE)                       | Build/download/test orchestration            |
| uv         | [MIT OR Apache-2.0](https://github.com/astral-sh/uv#license)                   | Python environment and dependency management |
| mise       | [MIT](https://github.com/jdx/mise/blob/main/LICENSE)                         | Pinned Python, uv, and Task installations     |

Transitive Python dependencies currently used by pytest and Playwright:

| Component | License / upstream evidence                                                        |
|-----------|------------------------------------------------------------------------------------|
| iniconfig | [MIT](https://github.com/pytest-dev/iniconfig/blob/main/LICENSE)                   |
| pluggy    | [MIT](https://github.com/pytest-dev/pluggy/blob/main/LICENSE)                      |
| packaging | [Apache-2.0 OR BSD-2-Clause](https://github.com/pypa/packaging/blob/main/LICENSE)  |
| Pygments  | [BSD-2-Clause](https://github.com/pygments/pygments/blob/master/LICENSE)           |
| pyee      | [MIT](https://github.com/jfhbrook/pyee/blob/main/LICENSE)                          |
| greenlet  | [MIT AND PSF-2.0](https://github.com/python-greenlet/greenlet/blob/master/LICENSE) |

The development environment also supplies Python, Chromium, Rust for optional
emulator builds, and the JieLi compiler toolchain. Their own distributions carry
their component licenses; this document is not a full host-environment SBOM.
In particular, the SDK's Apache-2.0 license must not be assumed to cover the
separately downloaded proprietary pi32v2 compiler. No compiler runtime library
is linked by the current freestanding build.

## Updating this inventory

Add an entry when importing a non-GPL source, asset, binary, or dependency.
Record its exact path, origin, license evidence, and whether it ships in the
source tree, firmware package, or only the development environment. Planned
MIT engine interfaces and third-party engines should be listed when imported.
