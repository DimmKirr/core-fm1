# core-fm1

A modular build system for [M-VAVE FM-1](https://github.com/aroum/fm1-custom-fw) custom firmware that lets you mix and match components from different community firmware projects into a single build.

## Why core-fm1?

The FM-1 has a thriving community firmware scene: [Felucca](https://github.com/hugelton/Felucca), [Melodee](https://github.com/keremimo/melodee), [SLOOP](https://github.com/isod89/sloop-fm1), [ChoralRoot](https://github.com/Quixotic7/ChoralRootFM1), [X0X](https://github.com/charlesvestal/fm1-x0x), and more. Each firmware has unique engines, effects, and features, but they all share the same hardware and a common architecture rooted in Felucca's `engine_t` plugin interface.

core-fm1 unifies these firmware lineages into one configurable source tree. Pick a Prophet-5 engine from Melodee, pair it with Felucca's granular engine, choose your effects chain, and build a single `.fwsc` firmware image. No manual code surgery required.

## Quick Start

```bash
pip install kconfiglib

# Load a preset configuration
python tools/genconfig.py --defconfig melodee

# Or configure interactively
python tools/menuconfig.py

# Inspect the resolved build
python tools/build_core.py --dry-run
```

## Features

### Kconfig-driven configuration

The same system Linux uses. Run `menuconfig` to select HAL variants, synthesis engines, effects, sequencer, MIDI routing, UI framework, and more. Save your configuration and reproduce builds exactly.

### Mix engines across firmware lineages

Synthesis engines implement a shared `engine_t` vtable (MIT licensed). Felucca, Melodee, and SLOOP engines can be compiled into the same build. Enable or disable any combination: FM6 (DX7), Prophet-5, CZ-1, SID, virtual analog, granular, sample playback, physical modeling, and more.

### Preset configurations for every firmware

Defconfigs reproduce each reference firmware exactly:

| Defconfig | Base | Engines | Voices |
|-----------|------|---------|--------|
| `melodee` | Melodee | Prophet-5, CZ-1, FM6 (FPU), 808/909 drums | 18 |
| `felucca` | Felucca | Analog, Digital, FM6, Sample, Granular, Formant | 14 |
| `sloop` | SLOOP | Analog, Drawbar, FM6, Sample | 14 |
| `minimal` | Melodee | FM6 + drums only | 8 |
| `full` | Melodee | Everything enabled | 24 |

## How It Works

All FM-1 community firmwares compile as a single translation unit (unity build): one `.c` file `#include`s all others. core-fm1 preserves this pattern but wraps it with Kconfig:

1. **Configure**: `menuconfig` writes a `.config` file with your choices
2. **Generate**: `genconfig.py` turns `.config` into `build/gen/autoconf.h` with `#define CONFIG_*` symbols
3. **Build**: the unity `.c` root uses `#ifdef CONFIG_*` guards to include only the selected modules
4. **Package**: the JieLi toolchain compiles to a `.fwsc` firmware image

## Architecture

```
┌─────────────────────────────────────────┐
│  UI + Sequencer + Storage               │  Radio: pick one per firmware lineage
├─────────────────────────────────────────┤
│  Synthesis Engines (engine_t vtable)    │  Mix-and-match from any lineage
├─────────────────────────────────────────┤
│  Audio Core (voice pool + mixer)        │  Radio: Felucca / SLOOP / Melodee
├─────────────────────────────────────────┤
│  HAL (header-only register macros)      │  Radio per peripheral, shared core
├─────────────────────────────────────────┤
│  JieLi AC791N Hardware                  │  44.1kHz stereo, dual-core, SPI flash
└─────────────────────────────────────────┘
```

The engine interface (`engine_t` vtable) is the cleanest boundary: `note_on`, `render`, plus optional hooks. Any engine that implements this interface works with any audio core.

## Project Status

**Phase 1 (current):** Kconfig infrastructure, configuration menus, defconfig profiles, build driver. The configuration system is functional; firmware source files are being organized from reference firmwares.

**Phase 2 (next):** Populate the source tree from Felucca, Melodee, and SLOOP. Wire the JieLi clang toolchain into the config-driven build.

**Phase 3:** Glue contracts (note dispatch, effects chain, storage serialization) that enable deeper cross-firmware mixing beyond engines.

## Requirements

- Python 3.10+
- [Kconfiglib](https://github.com/ulfalizer/Kconfiglib) (`pip install kconfiglib`)
- JieLi pi32v2 clang 4.0.1 toolchain (for compilation; not needed for configuration)

## License

- **Engine interface** (`felucca_engine.h`): MIT
- **Firmware code and engine implementations**: GPL-3.0

See [LICENSE](LICENSE) for details.

## Acknowledgments

core-fm1 builds on the work of the FM-1 community firmware developers:

- [hugelton](https://github.com/hugelton/Felucca) (Felucca)
- [keremimo](https://github.com/keremimo/melodee) (Melodee)
- [isod89](https://github.com/isod89/sloop-fm1) (SLOOP)
- [aroum](https://github.com/aroum/fm1-custom-fw) (FM-1 custom firmware catalog)
