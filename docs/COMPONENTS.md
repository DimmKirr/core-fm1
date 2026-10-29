# Components

## Module dependency graph

<!-- module-diagram:start -->
```mermaid
%%{init: {"flowchart": {"curve": "step"}} }%%
flowchart TB
    AUDIO["<b>Audio</b><br/><small>Core · Effects · Engines · Scales · Sequencer</small>"]
    BLE["<b>BLE</b><br/><small>midi · central · hid · bond · diag · file_transfer</small>"]
    BOOT["<b>Boot</b><br/><small>Loader · OTA</small>"]
    DEBUG["<b>Debug</b><br/><small>log · dap</small>"]
    HAL["<b>HAL</b><br/><small>sys · irq · gpio · time · lcd · input<br/>flash · uart · usb · timer · mc<br/>cache · clock · power · eq · led_pwm · ble_rf</small>"]
    MIDI["<b>MIDI</b><br/><small>UART Transport · Routing<br/>Editor · Console</small>"]
    USB["<b>USB</b><br/><small>Device Driver · USB Audio · Host Controller · UDisk</small>"]
    STORAGE["<b>Storage</b><br/><small>Flash · Settings</small>"]
    SCHED["<b>Sched</b><br/><small>Cooperative round-robin · ctx.S</small>"]
    UI["<b>UI</b><br/><small>Shell · Controls · Settings Menu</small>"]
    WIFI["<b>WiFi</b><br/><small>STA · lwIP · WPA · MIDI · CDP</small>"]

    BOOT -.-> STORAGE
    BOOT -.-> USB
    MIDI --> AUDIO
    MIDI -.->|transport| BLE
    UI --> AUDIO
    UI --> STORAGE
    UI --> MIDI
    UI -.->|pages| WIFI
    UI -.->|pages| DEBUG
    WIFI -.->|mutex| BLE
    UI -.-> USB
    UI -.-> BLE
    DEBUG --> USB
    MIDI --> USB
    WIFI --> DEBUG
    SCHED --> HAL
    UI -.-> SCHED
    WIFI -.-> SCHED
    AUDIO --> HAL
    BLE --> HAL
    BOOT --> HAL
    DEBUG --> HAL
    MIDI --> HAL
    USB --> HAL
    STORAGE --> HAL
    UI --> HAL
    WIFI --> HAL
```
<!-- module-diagram:end -->

> Solid arrows: hard dependency. Dashed arrows: optional/conditional.

## Terminology

| Tier | What it answers | Kconfig pattern | Example |
|------|-----------------|-----------------|---------|
| **Module** | What domain? | top-level `menu` + `CONFIG_*` | Audio, UI, MIDI, USB, WiFi, BLE, HAL |
| **Component** | What thing? | `CONFIG_ENGINE_FM6`, `CONFIG_OTA` | FM6, Shell, Weather, Effects, OTA |
| **Upstream** | Whose code? (omit if native) | `choice` radio / `CONFIG_*_FELUCCA` | Felucca, Melodee, SLOOP, Optimist, FMLab |

Qualifiers (orthogonal to the tiers):

| Qualifier | What it answers | Example |
|-----------|-----------------|---------|
| **Capability** | What gates it on/off? | `CONFIG_WEATHER`, `CONFIG_BLE`, `CONFIG_ENGINE_FM6` |
| **Contract** | What interface does it implement? | `cfm1_page.h`, `cfm1_engine.h`, `cfm1_fx.h` |

**Module boundary rule:** a module has its own Kconfig top-level menu AND its own directory under `firmware/src/`.

**Grouping convention:** components registered in `engines.c` are engines; components registered in `pages.c` are pages. These are naming conventions, not taxonomy tiers.

## Unity build inclusion order

Shows the `#include` order in `core_fm1.c`. Every `.c` is compiled into a single translation unit; all functions are `static`.

```mermaid
graph TD
    subgraph "1. HAL (header-only)"
        CC["fm1_cc.h"]
        TIME["fm1_time.h"]
        SYS["fm1_sys*.h"]
        LCD["fm1_lcd_hw*.h"]
        GUARD["fm1_guard*.h"]
        GPIO["fm1_gpio.h"]
        IRQ["fm1_irq.h"]
        POWER["fm1_power.h"]
        AUDIO_HW["fm1_audio.h + fm1_adc.h"]
        INPUT_HW["fm1_input*.h"]
        FLASH_HW["fm1_flash*.h"]
        UART_HW["fm1_uart*.h"]
        USB_HW["fm1_usb*.h"]
        MC_HW["fm1_multicore*.h"]
    end

    subgraph "2. Runtime"
        LIBC["libc.c"]
        LCD_DRV["lcd.c driver"]
        FONT["font5x7 + boot_text"]
        LOGO["boot_logo.c"]
        MANIFEST["manifest.h"]
    end

    subgraph "2b. CFM1_USB_MIDI define"
        USB_FLAG["#define CFM1_USB_MIDI<br/><small>before adapters, gates midi stub</small>"]
    end

    subgraph "3. Adapters"
        A_AUDIO["cfm1_audio.h"]
        A_SEQ["cfm1_seq.h"]
        A_FX["cfm1_fx.h"]
        A_MIDI["cfm1_midi.h"]
        A_STOR["cfm1_storage.h"]
        A_MOD["cfm1_mod.h"]
        A_PAGE["cfm1_page.h"]
    end

    subgraph "4. Engine plugins"
        FE_TAB["fe_tables.h"]
        DSP["dsp.c"]
        ENG["engines.c"]
    end

    subgraph "5. USB driver + OTA"
        USB_DRV["midi/usb.c<br/><small>MIDI IN/OUT, SysEx, UBOOT</small>"]
        OTA_ADAPT["cfm1_ota.h<br/><small>flash hooks (CONFIG_OTA)</small>"]
        OTA_CORE["boot/ota.c<br/><small>M-UPGRADE protocol</small>"]
    end

    subgraph "5b. ISR timing globals"
        ISR_TIMING["fm1_audio_irq_start_ticks<br/>fm1_audio_irq_elapsed_ticks<br/><small>non-static, used by fm1_isr.S</small>"]
    end

    subgraph "5c. Scheduler (CONFIG_SCHED)"
        SCHED_CORE["sched/sched.c<br/><small>round-robin, yield, sleep_ms</small>"]
        CTX_S["sched/ctx.S<br/><small>context switch (asm)</small>"]
    end

    subgraph "6. Audio core"
        PARAMS["params.c"]
        VOICE["voice.c"]
        CORE_AUDIO["audio.c<br/><small>DMA ISR + mix_block</small>"]
        FX["effects/fx.c"]
    end

    subgraph "7. UI"
        GFX["gfx.c"]
        SHELL["ui shell + pages"]
    end

    subgraph "8. Main / Scheduler tasks"
        MAIN["fm1_main()"]
        TASKS["usb_task · settings_task<br/>services_task · ui_task<br/>wifi_task"]
    end
```

## HAL

Registers and low-level hardware access. Header-only. No logic, no state.

### Common (always included)

- `fm1_cc.h` (compiler intrinsics)
- `fm1_time.h` (tick counter, delays, watchdog)
- `fm1_gpio.h` (pin mux, read/write)
- `fm1_irq.h` (interrupt controller)

### Upstream (pick one per slot via Kconfig)

| Component | Felucca | SLOOP | Melodee |
|---|---|---|---|
| sys | yes | yes | yes |
| irq | yes | yes | yes |
| lcd | yes | shared w/ Melodee | yes |
| input | yes | yes | yes |
| guard | yes | yes | yes |
| flash | yes | yes | yes |
| uart | yes | shared w/ Felucca | yes |
| usb | yes | shared w/ Felucca | yes |
| timer | yes | shared w/ Felucca | yes |
| multicore | yes | yes | yes |

### Extensions (optional)

- **Melodee**: cache, clock, power, eq, led_pwm
- **Optimist**: ble_rf
- **Felucca**: led_anim

## USB

USB MIDI device class with SysEx protocol. Compiled independently of the audio core so that every firmware config can be reflashed over USB.

Enabled by `CONFIG_USB`. Always on in all defconfigs.

### What it provides

- USB enumeration (EP0 control, EP1 bulk MIDI)
- MIDI IN/OUT ring buffers
- SysEx assembly and dispatch
- **UBOOT soft key** (`F0 22 24 35 7D F7`): triggers bootloader entry for reflashing
- Optional CDC-ACM serial (`MELODEE_CDC`)
- Optional USB Audio class (`CONFIG_USB_AUDIO`, Melodee only)
- Optional OTA update over SysEx (`MELODEE_OTA`)

### Data flow

```mermaid
sequenceDiagram
    participant Host as Host (DAW/Flasher)
    participant EP0 as USB EP0
    participant EP1 as USB EP1
    participant Ring as MIDI Ring
    participant Main as Main Loop

    Host->>EP0: SET_CONFIGURATION
    EP0-->>Host: ACK (enumerated)

    Host->>EP1: MIDI note/CC
    EP1->>Ring: midi_in_push()
    Main->>Ring: midi_in_pop()

    Host->>EP1: SysEx F0 22 24 35 7D F7
    EP1->>Main: usb.uboot_req = 1
    Main->>Main: usb_detach() + fm1_enter_uboot()
```

### Integration in the main loop

```c
// startup
usb_start();

// main loop
usb_poll();
usb_retry(fm1_ms);
if (usb.uboot_req) {
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_uboot();
}
```

### Source

- HAL: `firmware/hal/fm1_usb.h` (register-level, USB0 peripheral)
- Driver: `firmware/src/midi/usb.c` (~1000 lines, Melodee upstream)
- Adapter guard: `firmware/adapters/cfm1_midi.h` (`CFM1_USB_MIDI` flag avoids stub conflict)

## Audio

Voice allocator, render pipeline, DMA half-buffer (128 samples @ 44.1kHz stereo), mixer. The signal chain: main loop input scan -> page key handler -> voice allocator (`trk_note_on`) -> engine `note_on` -> DMA ISR calls `mix_block` -> engine `render` per voice -> I2S output.

Enabled by `CONFIG_AUDIO`. Requires `fm1_irq_enable_all()` after `audio_init()` or the ALNK0 DMA ISR never fires. Optionally uses `CONFIG_DUAL_CORE` for render on core 1.

### Core component (pick one upstream)

- **Felucca**: 8+6 voices (14 total)
- **SLOOP**: inherited from Felucca
- **Melodee**: 18 budget units, FPU, per-voice cost (used by `felucca_mock` and `minimal` defconfigs)

### Engines (pick any combination)

Engines implement the `engine_t` vtable. Each compiles in/out independently via `CONFIG_ENGINE_*`.

#### Multiple upstreams

- **FM6**: Felucca | Melodee (FPU, MARK I/MODERN/OPL modes)
- **Drum**: Felucca (GM) | SLOOP | Melodee (808/909/SID)

#### Felucca upstream only

Analog (ported), Digital, Phase, Sample, Granular, LoFi, Trio, Tonewheel, Formant, Physical, Noise (ported)

#### Melodee upstream only

Prophet-5, CZ-1, SID

### Effects component (pick one upstream)

- **Felucca**
- **SLOOP**
- **Melodee**

### Modulation (optional)

- 4-slot matrix

## Sequencer

Step sequencer, pattern management, recording. Depends on Audio.

### Sequencer component (pick one upstream)

- **Felucca**: 64-step, chords, ties, live loop
- **SLOOP**: arranger, scenes
- **Melodee**: 64-step, 8 banks, 1024 notes, recording, undo, motion, song

## MIDI

Routing, clock sync, transport control.

Requires at least one transport: `CONFIG_MIDI_UART` (TRS) or `CONFIG_USB`.

### Routing component (pick one upstream)

- **Felucca**
- **SLOOP**
- **Melodee**: multi-channel, MPC pads, clock sources

### Transports

| Transport | Kconfig | Notes |
|-----------|---------|-------|
| UART (TRS DIN) | `CONFIG_MIDI_UART` | 31.25 kbaud |
| USB MIDI | `CONFIG_USB` | Class-compliant, SysEx |
| BLE MIDI | `CONFIG_BLE` | Via Optimist BLE stack |
| USB Audio | `CONFIG_USB_AUDIO` | Melodee only, stereo/4-track capture |

## BLE

Bluetooth Low Energy protocol stack. Depends on HAL (radio registers).
Enables BLE MIDI transport in the MIDI module.

### BLE Stack (Optimist)

- Link Layer
- Host (L2CAP, ATT, SMP)
- GATT

### BT Classic (Optimist)

- BR/EDR (L2CAP, LMP, AVDTP)

## UI

Screen rendering, event dispatch, view navigation.
Enabled by `CONFIG_INPUT`.

For the cross-theme framework (view/page/element model, navigation rules, theme contract), see [UI.md](UI.md).

### Shell component (pick one upstream)

- **Felucca**: page-based shell with encoder/button dispatch
- **SLOOP**
- **Melodee**: Rubik font, track colors, 8 palettes, partial updates
- **Native** (Core): minimal bitmap-font shell (default when no upstream UI is selected)

### Event flow

```
Hardware (key matrix, encoders, ADC)
  -> fm1_input_tick()           [Timer5 ISR, 10kHz]
  -> core_fm1.c::ui_input()     [main loop, 15ms frame]
    -> fsh_key(note, vel)        -> page->key()
    -> fsh_button(id, down)      -> hold lookup -> tap lookup -> page->button()
    -> fsh_knob(id, clicks)      -> page->knob() -> SELECT fallback
```

Button dispatch is hybrid: a centralized table (`cfm1_controls.h`) maps
button taps and holds to page names, and per-page `knob()`/`button()`/`key()`
handlers process encoder and keyboard input. The shell tries the page handler
first; if it returns 0 (unhandled), the shell falls through to the nav table.

### Source files (Felucca upstream)

| File | Role |
|------|------|
| `firmware/adapters/cfm1_controls.h` | Button/encoder mapping tables (tap, hold, thresholds) |
| `firmware/adapters/cfm1_page.h` | Page vtable: name, draw, knob, button, key, enter, leave |
| `firmware/src/ui/felucca/ui.c` | Layout constants, includes all page files |
| `firmware/src/ui/felucca/shell.c` | Page array, navigation, input routing, draw loop |
| `firmware/src/ui/felucca/pages.c` | Page registration order |
| `firmware/src/ui/felucca/page_home.c` | HOME: engine select, track select, knob params, notes |
| `firmware/src/ui/felucca/page_params.c` | ENV, LFO, FX, SCL, EDIT, ARP, VOICE: 4-card param pages |
| `firmware/src/ui/felucca/page_seq.c` | SEQ: step sequencer display |
| `firmware/src/ui/felucca/page_mixer.c` | MIXER: per-track level controls |
| `firmware/src/ui/felucca/page_about.c` | ABOUT: firmware info (display only) |
| `firmware/src/ui/felucca/page_sys.c` | Settings View pages (conditional, `CONFIG_SYS_MENU`) |
| `firmware/src/ui/felucca/page_files.c` | FILES: USB disk browser (conditional, `CONFIG_UDISK`) |
| `firmware/src/ui/felucca/page_ble.c` | BLE: device list (conditional, `CONFIG_BLE`) |
| `firmware/src/ui/felucca/page_debug.c` | DEBUG: log viewer (conditional, `CONFIG_DEBUG_LOG`) |

### Felucca views and pages

Each button opens a view; the encoder cycles pages within that view. See [UI.md](UI.md#views) for the full view map.

**Synth views**: HOME, ENV, LFO, FX, SCL, EDIT, GLO, SAVE, ARP, SEQ
**System views**: Settings (HOME hold), Conn (GLO hold: [WiFi], [Weather], [BLE])

Pages in brackets are conditional on their Kconfig symbol.

### Physical controls

#### Encoders

| ID | Name | HOME View | Param Views (ENV, LFO, etc.) | Settings View | FILES | DEBUG |
|----|------|-----------|------------------------------|---------------|-------|-------|
| 0 | SELECT | Engine select | Page nav (CW/CCW) | Cycle value | Scroll entries | Scroll log |
| 1 | ALGO | Track select | -- | Switch pages | -- | -- |
| 2 | KNOB1 | Engine knob[0] | Param card 1 | -- | -- | -- |
| 3 | KNOB2 | Engine knob[1] | Param card 2 | -- | -- | -- |
| 4 | KNOB3 | Engine knob[2] | Param card 3 | -- | -- | -- |
| 5 | KNOB4 | Engine knob[3] | Param card 4 | -- | -- | -- |
| 6 | PRESET | -- | -- | Scroll rows | -- | -- |
| -- | MASTER | Volume (ADC) | Volume (ADC) | Volume (ADC) | Volume (ADC) | Volume (ADC) |

ALGO (ENC 1) has no knob handler on param views: it is used only on HOME
(track select) and Settings (page switch).

#### Buttons

| ID | Name | Tap action | Hold action (500ms) |
|----|------|-----------|-------------------|
| 0 | OCT- | **dead** | -- |
| 1 | OCT+ | **dead** | -- |
| 2 | FX | -> FX View | -- |
| 3 | SCL | -> SCL View | -- |
| 4 | ENV | -> ENV View | -- |
| 5 | LFO | -> LFO View | -- |
| 6 | EDIT | -> EDIT View | -- |
| 7 | GLO | -> GLO View | -> CONN View (WiFi/Weather/BLE) |
| 8 | HOME | -> HOME View | -> Settings View |
| 9 | SAVE | -> SAVE View | -- |
| 10 | ARP | -> ARP View | -- |
| 11 | SEQ | -> SEQ View | -- |
| 12 | PLAY | **dead** | -- |
| 13 | REC | **dead** | -- |

Dead buttons have no handler in the firmware. They reach `fsh_button()` but
are ignored (no tap nav, no hold nav, no page button handler).

#### Special hardware combos

- **OCT- + OCT+ held > 5 seconds**: reboots to UBOOT (in `core_fm1.c` main
  loop, bypasses the page system entirely)

### Parameter page card layout

Each param page shows 4 cards with values. The encoders map as:

| Card position | Encoder | `fparams_knob()` id |
|--------------|---------|-------------------|
| Card 1 | ALGO (ENC 1) | 1 |
| Card 2 | KNOB1 (ENC 2) | 2 |
| Card 3 | KNOB2 (ENC 3) | 3 |
| Card 4 | KNOB3 (ENC 4) | 4 |

### Param pages: card definitions

| Page | Card 1 | Card 2 | Card 3 | Card 4 |
|------|--------|--------|--------|--------|
| ENV | ATK (ms) | DEC (ms) | SUS (%) | REL (ms) |
| LFO | LFO (%) | RATE (Hz) | SHAPE (%) | DELAY (ms) |
| FX | DST (%) | CHO (%) | DLY (%) | REV (%) |
| SCL | ROOT | SCL | OCT | TRN |
| EDIT | eng edit[0] | eng edit[1] | eng edit[2] | eng edit[3] |
| ARP | MODE | TIME | RANGE (oct) | GATE (%) |
| VOICE | VCE | HOLD | GLD (ms) | UNI |

### HOME page card layout

HOME uses a different mapping. The engine's `knob[]` descriptor selects
which parameters appear:

| Card position | Encoder | Source |
|--------------|---------|--------|
| Card 1 | KNOB1 (ENC 2) | `engine->knob[0]` |
| Card 2 | KNOB2 (ENC 3) | `engine->knob[1]` |
| Card 3 | KNOB3 (ENC 4) | `engine->knob[2]` |
| Card 4 | KNOB4 (ENC 5) | `engine->knob[3]` |

## Storage

Flash storage, project save/load, presets.
Enabled by `CONFIG_FLASH`.

### Flash component (pick one upstream)

- **Felucca**
- **SLOOP**
- **Melodee**: FBKG format, 384 presets, migration, backup/restore

## Scales

Scale selection, key quantization, tuning. Component within the Audio module.

### Scales component (pick one upstream)

- **Felucca**: 16 scales, white-key QNT
- **Melodee**: 70 scales incl. 54 microtonal, concert pitch 400-480Hz

## Boot

Startup, loader, OTA update mechanism.

### Loader component (pick one upstream)

- **Felucca**: stock DFU
- **SLOOP**: stock DFU
- **Melodee**: M-UPGRADE SysEx OTA, boot protection, safe-recovery

## OTA

M-UPGRADE SysEx firmware update protocol. The running firmware reads the package from the host over USB-MIDI SysEx, stages the update loader in a dedicated flash area (0xE0000, 20 KiB), then reboots into it. The loader writes the new firmware and restarts.

Enabled by `CONFIG_OTA`. Depends on `CONFIG_BOOT_MELODEE` and `CONFIG_FLASH`.

### Protocol flow

```mermaid
sequenceDiagram
    participant Host as Host (fm1_install.py)
    participant FW as Running Firmware
    participant Loader as Update Loader

    Host->>FW: HS_QUERY (identity handshake)
    FW-->>Host: FM-1_900 (package identity)
    Host->>FW: UPGRADE (F0 22 24 35 7F F7)
    FW->>FW: ota_session(): read package via SysEx
    loop Check phase (1148 reads typical)
        FW-->>Host: read request (addr, len)
        Host->>FW: response (data)
    end
    FW-->>Host: FINISH_CHECK
    FW->>FW: Stage loader, write UPDATA_PARM, reset
    Note over FW,Loader: Device reboots into loader
    Host->>Loader: UPGRADE
    loop Write phase
        Loader-->>Host: read request
        Host->>Loader: response
    end
    Loader-->>Host: FINISH_WRITE
    Note over Loader: Loader writes firmware, restarts
```

### Source

- Protocol core: `firmware/src/boot/ota.c` (Melodee upstream, portable)
- Hardware hooks: `firmware/adapters/cfm1_ota.h` (flash ops, clock, progress display, commit)
- Wire transport: `firmware/src/midi/usb.c` (`ota_wire_send`, `ota_frame_get`, gated by `MELODEE_OTA`)
- Host tool: `firmware/upstream/felucca/tools/fm1_install.py`

## Contracts (adapters)

Thin compatibility headers (`cfm1_*.h`) that let components from different upstreams plug into core-fm1. Each contract defines the interface (types, vtable, signatures) that both sides conform to.

```mermaid
graph LR
    subgraph "Producers"
        ENG_F["Felucca upstream"]
        ENG_M["Melodee upstream"]
    end
    subgraph "Contract"
        EP["cfm1_engine.h"]
    end
    subgraph "Consumers"
        AC_F["Felucca Audio Core"]
        AC_M["Melodee Audio Core"]
    end

    ENG_F --> EP
    ENG_M --> EP
    EP --> AC_F
    EP --> AC_M
```

| Contract | Header | Enables |
|----------|--------|---------|
| Audio Core | `cfm1_audio.h` | Common audio types and voice interface |
| Engine | `cfm1_engine.h` | Any engine on any audio core |
| Effects | `cfm1_fx.h` | Swap effects upstreams |
| Sequencer | `cfm1_seq.h` | Swap sequencer upstreams |
| Storage | `cfm1_storage.h` | Tagged-blob project format |
| MIDI Routing | `cfm1_midi.h` | Swap MIDI upstreams (`CFM1_USB_MIDI` gates stub) |
| Modulation | `cfm1_mod.h` | Swap modulation upstreams |
| Page | `cfm1_page.h` | Page vtable for UI shell dispatch |
| OTA | `cfm1_ota.h` | Flash ops, clock, progress, commit hooks for `ota.c` |

## Defconfig presets

| Preset | Audio | USB | Engines | UI | Extras | Buildable |
|--------|-------|-----|---------|-----|--------|-----------|
| `minimal` | Melodee core + FX | yes | none | none | -- | yes |
| `core` | none | yes | none | Core shell | -- | yes |
| `core_fm1_dev` | Melodee core + FX | yes | FM6, Analog, LoFi, Trio, Noise, Stub | Felucca | BLE, OTA, storage, modulation, settings menu, USB disk | yes (29 components) |
| `felucca_noise` | Melodee core + FX | yes | Analog, Noise | Felucca | debug log, settings menu | yes |
| `felucca` | Felucca | yes | all Felucca | Felucca | -- | not yet |
| `melodee` | Melodee | yes | all Melodee | Melodee | -- | not yet |
| `sloop` | SLOOP | yes | Felucca set | SLOOP | -- | not yet |

All presets have `CONFIG_USB=y` so the device is always reflashable over USB.
