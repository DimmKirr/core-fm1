# SysEx Protocol

The FM-1 editor protocol uses SysEx messages over USB MIDI or BLE MIDI.
Both the Felucca and Melodee web editors speak this protocol.

## Framing

All messages: `F0 7D 46 4C <cmd> [args...] F7`

| Byte | Value | Meaning |
|------|-------|---------|
| F0 | SysEx start | |
| 7D | Manufacturer ID | Educational / non-commercial |
| 46 4C | "FL" | Felucca/Melodee identifier |
| cmd | 1-127 | Command ID |
| args | 0-N bytes | Command-specific, all < 0x80 |
| F7 | SysEx end | |

Request/response: the device echoes the same cmd byte in replies.
Push notifications use the CHANGED/STEP_CHANGED/TRACK_CHANGED cmd bytes.

## Transports

| Transport | Throughput | Notes |
|-----------|-----------|-------|
| USB MIDI | ~50 KB/s | Used by both web editors today |
| BLE MIDI | ~2-4 KB/s | Same SysEx frames, no webapp support yet |

The web editors use `navigator.requestMIDIAccess({ sysex: true })`.
No WebBluetooth is used by either editor.

### Device name matching

| Editor | Regex | Matches |
|--------|-------|---------|
| Felucca | `/felucca/i` | "Felucca" |
| Melodee | `/melodee\|felucca/i` | "Melodee", "Felucca" |
| core-fm1 | (needs adding) | "Core FM1" |

## Command Reference

### Shared commands (IDs 1-71)

Both Felucca and Melodee editors use these identically.

#### Parameters (1-5)

| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 1 | INFO | req/rsp | Device info: firmware version, engine count, param count |
| 2 | GET | req/rsp | Read parameter value(s) |
| 3 | SET | host->dev | Write parameter value |
| 4 | DUMP | req/rsp | Bulk dump all parameters |
| 5 | DESC | req/rsp | Parameter descriptor: name, range, type |

#### Sequencer (6-7, 27-33)

| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 6 | STEP_GET | req/rsp | Read sequencer step data |
| 7 | STEP_SET | host->dev | Write sequencer step data |
| 27 | TRACK | req/rsp | Track operations |
| 28 | TRACK_MIX | req/rsp | Track mix settings (volume, pan) |
| 29 | TRACK_DUMP | req/rsp | Bulk dump track data |
| 30 | TRACK_STEP | req/rsp | Track step read/write |
| 31 | TRACK_PARAM | req/rsp | Track parameter automation |
| 32 | TRACK_CHANGED | dev->host | Push: track data changed on device |
| 33 | SONG | req/rsp | Song chain operations |

#### Presets (8, 10, 16-21)

| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 8 | PRESET | req/rsp | Current preset operations |
| 10 | NAMES | req/rsp | List preset names in a bank |
| 16 | UP_LIST | req/rsp | List user preset slots |
| 17 | UP_GET | req/rsp | Read user preset (238-byte record) |
| 18 | UP_PUT | host->dev | Write user preset |
| 19 | UP_STORE | host->dev | Store current state to preset slot |
| 20 | UP_LOAD | host->dev | Load preset slot into current state |
| 21 | UP_ERASE | host->dev | Erase user preset slot |

#### Projects (9, 65-67)

| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 9 | PROJECT | req/rsp | Project save/load/info |
| 65 | BACKUP_LIST | req/rsp | List backup slots |
| 66 | BACKUP_GET | req/rsp | Download backup from device |
| 67 | BACKUP_PUT | host->dev | Upload backup to device |

#### Samples (11-15)

| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 11 | SMP_BEGIN | host->dev | Begin sample upload (size, format) |
| 12 | SMP_WRITE | host->dev | Sample data chunk |
| 13 | SMP_END | host->dev | End sample upload, commit |
| 14 | SMP_ERASE | host->dev | Erase user sample |
| 15 | SMP_INFO | req/rsp | Sample slot info |

#### FM6/DX7 Patches (68-71)

| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 68 | FM6_GET | req/rsp | Read FM6 voice |
| 69 | FM6_PUT | host->dev | Write FM6 voice |
| 70 | FM6_LIST | req/rsp | List FM6 voice bank entries |
| 71 | FM6_ERASE | host->dev | Erase FM6 voice slot |

#### UI (34-38)

| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 34 | UI_STATE | req/rsp | Current UI state (page, selection) |
| 35 | UI_SET | host->dev | Change UI state |
| 36 | UI_PALETTES | req/rsp | Read UI palette colours |
| 37 | FAV_GET | req/rsp | Read favorites list |
| 38 | FAV_SET | host->dev | Set favorite |

#### Live sync (22-26)

| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 22 | WATCH | host->dev | Subscribe to change notifications |
| 23 | CHANGED | dev->host | Push: parameter/preset changed on device |
| 24 | RELOAD | host->dev | Request full state resync |
| 25 | PING | req/rsp | Connection keepalive |
| 26 | STEP_CHANGED | dev->host | Push: sequencer step changed on device |

#### Modulation (64)

| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 64 | MOTION | req/rsp | Motion recording data |

### Felucca-only commands (72-81)

These exist in the Felucca editor but not Melodee.

| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 72 | MENU_DESC | req/rsp | Describe a settings menu item (tab, range, name) |
| 73 | MENU_SET | host->dev | Set settings menu value |
| 74 | FM6B_BEGIN | host->dev | Begin FM6 voice bank upload |
| 75 | FM6B_WRITE | host->dev | FM6 voice bank data chunk |
| 76 | FM6B_END | host->dev | End FM6 voice bank upload |
| 77 | FM6B_LIST | req/rsp | List FM6 voice bank entries |
| 78 | LEARN_GET | req/rsp | Get MIDI learn assignment |
| 79 | LEARN_SET | host->dev | Set MIDI learn assignment |
| 80 | PALETTE_GET | req/rsp | Read custom UI palette colour |
| 81 | PALETTE_SET | host->dev | Set custom UI palette colour |

### Melodee-only commands (73-78, 80, 95)

These exist in the Melodee editor but not Felucca.
IDs 73-78 and 80 conflict with Felucca: same ID, different purpose.

| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 73 | PATTERN | req/rsp | Pattern operations |
| 74 | BANK_SONG | req/rsp | Bank song operations |
| 75 | CZ_GET | req/rsp | Read CZ-1 patch |
| 76 | CZ_PUT | host->dev | Write CZ-1 patch |
| 77 | CZ_BANK | req/rsp | CZ-1 bank operations |
| 78 | NATIVE | req/rsp | Native preset read/write |
| 80 | LIBRARY | req/rsp | Patch library operations |
| 95 | PROPHET | req/rsp | Prophet patch read/write |

### core-fm1 diagnostic commands

These are outside the editor range and do not conflict.

| ID | Name | Direction | Description |
|----|------|-----------|-------------|
| 0x7E (126) | LOG_DUMP | req/rsp | Dump debug log ring buffer (CONFIG_DEBUG_LOG) |
| 0x7F (127) | SETTINGS_RESET | host->dev | Erase settings, reboot to defaults (CONFIG_SYS_MENU) |

## ID conflicts (72-81)

IDs 72+ diverge between Felucca and Melodee. core-fm1 needs both
command sets since it supports engines from both lineages. Proposed
resolution for the superset:

| Range | Assignment |
|-------|-----------|
| 1-71 | Shared (unchanged) |
| 72-81 | Felucca extensions (keep as-is) |
| 82-94 | Melodee extensions (renumber from 73-78, 80) |
| 95 | PROPHET (unchanged, no conflict) |
| 96-125 | Reserved for future core-fm1 commands |
| 126 | LOG_DUMP (core-fm1 diagnostic) |
| 127 | SETTINGS_RESET (core-fm1 diagnostic) |

### Proposed Melodee renumbering

| Old ID | New ID | Name |
|--------|--------|------|
| 73 | 82 | PATTERN |
| 74 | 83 | BANK_SONG |
| 75 | 84 | CZ_GET |
| 76 | 85 | CZ_PUT |
| 77 | 86 | CZ_BANK |
| 78 | 87 | NATIVE |
| 80 | 88 | LIBRARY |

## Other SysEx commands (non-editor)

These use different manufacturer headers and are handled separately.

### Identity query (Felucca OTA)

| | Message |
|---|---------|
| Query | `F0 00 32 45 00 00 00 40 7F F7` |
| Reply | `F0 <7-bit packed identity> F7` |

The reply contains the firmware package ID (e.g., `FM-1_900`).
Used by the Felucca update installer to identify the device.

### UBOOT entry

| | Message |
|---|---------|
| Command | `F0 22 24 35 7D F7` |

Triggers entry into the UBOOT firmware update mode.

### OTA upgrade (Melodee)

| | Message |
|---|---------|
| Command | `F0 22 24 35 7F F7` |

Triggers entry into the M-UPGRADE OTA update mode.

### DX7 SysEx

Standard Yamaha DX7 voice/bank SysEx (`F0 43 ...`) is handled by the
FM6 engine's `fm6_sx_byte()` parser for direct DX7 patch loading.

## Implementation status in core-fm1

| Feature | Status |
|---------|--------|
| Editor protocol framing | Stubbed (`editor.c`: header match, empty handler) |
| Shared commands 1-71 | Not ported (needs Melodee `editor.c` + storage) |
| Felucca extensions 72-81 | Not ported |
| Melodee extensions | Not ported |
| Identity query | Working |
| UBOOT entry | Working |
| DX7 SysEx | Working (FM6 engine) |
| LOG_DUMP (126) | Working (CONFIG_DEBUG_LOG, needs cmd ID update) |
| SETTINGS_RESET (127) | Working (CONFIG_SYS_MENU, needs cmd ID update) |
