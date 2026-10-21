# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

core-fm1 is a modular firmware framework for the M-VAVE FM-1 synthesizer (JieLi AC791N SoC, pi32v2 architecture). It unifies code from three community firmware lineages: **Felucca** (hugelton), **Melodee** (keremimo), and **SLOOP** (isod89) into a single configurable source tree. All firmwares are GPL-3.0; engine interfaces are MIT.

## Build System

Kconfiglib-based configuration. No firmware sources are in the tree yet (Phase 1: config infrastructure).

```bash
pip install kconfiglib                          # one-time
python tools/menuconfig.py                      # interactive config
python tools/genconfig.py --defconfig melodee   # load a preset
python tools/build_core.py --dry-run            # inspect resolved config
```

Defconfig profiles live in `configs/`: `defconfig_melodee`, `defconfig_felucca`, `defconfig_sloop`, `defconfig_minimal`, `defconfig_full`.

Generated files go to `build/gen/autoconf.h`. The `.config` file is gitignored.

## Architecture

All three firmwares compile as a **single translation unit** (unity build): one `.c` file `#include`s all others. Every function is `static`. Kconfig controls which `#include` directives are active via `CONFIG_*` defines in `autoconf.h`.

### Layer Stack (bottom to top)

1. **HAL** (header-only, register-level): `fm1_*.h` files for AC791N peripherals. Identical files shared; diverged files selected via Kconfig radio choices.
2. **Audio Core**: voice allocator, render pipeline, DMA half-buffer (128 samples @ 44.1kHz stereo). Three variants: Felucca (14 voices), SLOOP, Melodee (18 voice budget units, FPU).
3. **Engine Plugin** (`engine_t` vtable): the cleanest boundary. `note_on`, `render`, `amp`, `block`, `done` + optional fields. Melodee's 14-field superset is canonical. Engines compile in/out independently.
4. **Effects, Sequencer, MIDI, UI, Storage**: radio-selected per firmware lineage. UI stays bundled with its firmware (too many direct global reads to mix).

### Key Contracts (defined in `.scratch/GLUE.md`)

| Contract | Header | Enables |
|----------|--------|---------|
| Note Dispatch | `cfm1_note_dispatch.h` | Mix any sequencer with any audio core |
| Engine Plugin | `cfm1_engine.h` | Any engine on any audio core |
| Effects Chain | `cfm1_fx.h` | Swap FX implementations |
| Storage | `cfm1_storage.h` | Tagged-blob project format |
| MIDI Routing | `cfm1_midi.h` | Swap MIDI implementations |
| Scales/Tuning | `cfm1_scales.h` | Swap scale sets |

### The P_COUNT Problem

Each firmware defines different parameter counts and IDs. core-fm1 defines a `P_*` superset; unused params are stored but ignored. Engine params `P_E0..P_E7` are always the last 8.

## Directory Layout

```
Kconfig                     # root Kconfig (sources kconfig/*.kconfig)
kconfig/                    # per-subsystem Kconfig menus
configs/                    # saved defconfigs per firmware lineage
firmware/
  hal/                      # HAL headers (common + per-variant)
  src/                      # source tree (future: populated from reference firmwares)
    core_fm1.c              # unity root with CONFIG_* conditional includes
    engines.c               # engine registry (conditional includes)
tools/
  genconfig.py              # .config -> build/gen/autoconf.h
  menuconfig.py             # Kconfiglib TUI wrapper
  build_core.py             # config-driven build driver (dry-run for now)
  build.py                  # full build entry point (genconfig + compile)
docs/plans/                 # implementation plans
.scratch/                   # research data, examples, analysis (not shipped)
.product/                   # product specs and architecture docs
```

## Toolchain

- **Compiler**: JieLi clang 4.0.1 for pi32v2 (proprietary target, runs in Docker)
- **Target**: AC791N dual-core MCU, bare-metal, no OS
- **Audio**: 44.1kHz stereo, 128-sample half-buffer DMA, I2S codec
- **Flash**: SPI NOR for storage, projects, presets, OTA
- **Output**: `.fwsc` firmware package (lz4-compressed, signed)

## Working with Engines

Engines implement the `engine_t` vtable (defined in `felucca_engine.h`, MIT licensed). Required fields: `note_on`, `render`. Optional: `amp`, `block`, `done`, `desc`, `keys`, `alloc`, `legato`, `mono_key`, `post`, `cap`, `units`, `preset_make`. Felucca engines set extra optional fields to NULL/0.

## Reference Firmwares

Source code for the three lineages lives outside this repo:
- Melodee: `/Users/dmitry/dev/dimmkirr/melodee`
- Felucca: check `.scratch/examples/felucca/`
- SLOOP: check `.scratch/examples/sloop/`

Research data: `.scratch/fw-test.json` (firmware catalog), `.scratch/kconfig-layers.json` (layer analysis), `.scratch/GLUE.md` (contract specs).

## Conventions

- Unity build: all `.c` files are `#include`d into one translation unit. All functions are `static`.
- HAL is header-only: register macros, no `.c` files.
- Kconfig symbols use `CONFIG_` prefix. Engine symbols: `CONFIG_ENGINE_*`. HAL variants: `CONFIG_HAL_*_FELUCCA/SLOOP/MELODEE`.
- Voice budget is a shared resource pool. Engine cost is measured in VBUDGET units (Prophet: 2-3, FM6: 2, CZ-1: 1, etc.).
- License boundary: engine interface (`felucca_engine.h`) is MIT. Engine implementations and firmware code are GPL-3.0.
