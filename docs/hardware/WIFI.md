# AC791N / WL82 Wi-Fi Hardware Reference

## Overview

The AC791N (marketed as WL82) is a JieLi dual-core MCU with a **shared 2.4 GHz
radio** supporting both Bluetooth (BR/EDR + BLE) and Wi-Fi. The FM-1 synthesizer
uses this SoC but only exercises the Bluetooth path: stock firmware V15 uses
BR/EDR for audio, and Optimist adds BLE MIDI. The Wi-Fi radio hardware is
present on the die and initialized as part of the BLE RF bring-up, but no
community firmware has ever enabled Wi-Fi data transfer.

### Wi-Fi capabilities (from JieLi SDK and datasheet)

| Parameter | Value |
|-----------|-------|
| Standard | IEEE 802.11 b/g/n (Wi-Fi 4) |
| Band | 2.4 GHz only |
| Channels | 1-14 (region-dependent) |
| Max PHY rate | 72.2 Mbps (802.11n MCS7, 20 MHz, 1SS, SGI) |
| Antenna | Single, shared with Bluetooth |
| Radio arbitration | Hardware priority controller (RFPRIO) manages BT/BLE/WiFi |
| Security | WPA/WPA2 (in SDK) |
| Modes | STA, AP, STA+AP (in SDK) |
| Power | ~120 mA TX, ~60 mA RX (typical, from design guide) |

### SoC context

| Feature | Detail |
|---------|--------|
| CPU | Dual-core pi32v2 (proprietary RISC) |
| Flash | 1 MB SPI NOR (FM-1 board) |
| RAM | 256 KB SRAM + cache |
| Clock | 24 MHz crystal (FM-1), PLL to ~120 MHz |
| Package | QFN48 (FM-1 board) |

---

## GPL / Open-Source Driver Status

**No GPL-compatible Wi-Fi driver exists for the AC791N.**

### JieLi SDK (binary-only)

The official SDK (`fw-AC79_AIoT_SDK` on Gitee) provides Wi-Fi functionality, but
the radio drivers are distributed as **pre-compiled static libraries** (`.a`
files containing LLVM bitcode for the pi32v2 target). The relevant libraries:

| Library | Contents |
|---------|----------|
| `lib_btctrler.a` | Bluetooth controller (BLE link layer, BR/EDR) |
| `lib_wifi.a` | Wi-Fi MAC, PHY control, power management |
| `lib_net.a` | lwIP network stack integration |
| `lib_ccm_aes.a` | AES-128 for BLE CCM and WiFi WPA |

Source code for these libraries is not available. The SDK application layer
(lwIP integration, HTTP server examples, etc.) is provided in C source, but
the actual radio driver and MAC are opaque.

### Optimist BLE (community, GPL-3.0)

The Optimist project (`firmware/upstream/optimist/`) implements a BLE link layer
from scratch by reverse-engineering the baseband engine registers (documented in
`docs/BLE-HW-FACTS.md`). This was feasible because:

1. The BLE baseband engine is relatively simple (advertising, one connection)
2. Register behavior was traced via the `fm1-emulator`
3. BLE protocol is well-specified with small state machines

Wi-Fi would require reimplementing:

- The full 802.11 MAC (association, authentication, frame aggregation, power save)
- The BBP (baseband processor) configuration for OFDM/DSSS
- Channel estimation, equalization, and rate adaptation
- WPA2 4-way handshake and CCMP encryption
- The firmware/hardware interface for TX/RX frame DMA

This is orders of magnitude more complex than the BLE link layer and not
practically achievable without vendor documentation.

### fm1-radio project (MPL-2.0 workaround)

A project called `fm1-radio` exists that uses the JieLi SDK's binary drivers
under MPL-2.0 licensing as a workaround. It treats the binary libraries as
system libraries (similar to how Linux userspace links against proprietary GPU
drivers). This is not a clean GPL path.

### Verdict

There is **no path to GPL Wi-Fi** on the AC791N without either:
- JieLi releasing driver source (unlikely)
- A multi-person-year reverse engineering effort of the WiFi MAC/PHY
- Using the binary SDK libraries under a permissive-linking exception

For core-fm1, Wi-Fi is not a target. The BLE path (Optimist) serves MIDI
connectivity needs.

---

## Radio Hardware Architecture

The AC791N has a **single radio front-end** shared between Bluetooth and Wi-Fi.
Both subsystems use the same:

- PLL / VCO (tuned per channel)
- PA (power amplifier) with per-rate digital gain
- LNA (low noise amplifier) and mixer
- ADC/DAC for baseband
- Antenna switch / matching network

A hardware priority controller (RFPRIO at `0x05E`/`0x060`) arbitrates access
when both BT and WiFi are active. In practice on the FM-1, only one subsystem
is active at a time.

### RF initialization dependency

**BLE requires the WiFi RF analog block to be initialized first.** The BLE
bring-up sequence (BLE-HW-FACTS.md section 16) begins with a full WiFi analog init
(groups 2-6, 8-9) before touching any BLE-specific registers. This includes:

1. Clock configuration (`0x10010`, `0x10008`)
2. WiFi analog block init (`0x11900-0x11964`, 20 registers, ~240 writes)
3. VCO bank scan (live calibration, ~380 steps)
4. PA trim application from stored calibration (VM records 106, 107, 108, 187)
5. BBP (baseband processor) table loading (~3,300 transactions)
6. RF-die LUT programming (2 x 256 words via SPI)

Only after this shared RF init completes does the BLE-specific baseband
initialization begin (BT block at `0x2FC00`, BLE engine at `0x28000`).

---

## Wi-Fi Register Map

All addresses are physical MMIO. Extracted from BLE-HW-FACTS.md, HAL headers,
and emulator traces.

### Wi-Fi / RF Analog Block: `0x11900-0x1197C`

The shared radio front-end. Initialized during both WiFi and BLE startup.

| Address | Name | Bits | Function |
|---------|------|------|----------|
| `0x11900` | WL_A00 | [14] periodic routine bit (not a trim); [31:0] first analog word | Master analog control. Bit 14 toggled by a periodic routine (~2 ms); `rf_init` preserves it as found |
| `0x11904` | WL_A04 | | Analog configuration (constant, set during post-scan) |
| `0x11908` | WL_A08 | [7], [15], [17], [25] | Trim fields from VM 187 bytes 32, 33, 36, 37 |
| `0x11910` | WL_A10 | [15:14] from VM 187 byte 42; [22:26] from T6 table | Analog configuration with stored trims |
| `0x11914` | WL_A14 | [17:15], [14:12], [7:5] from T6 table | Analog configuration |
| `0x1191C` | WL_A1C | [9:8] | Trim from VM 187 byte 24 (three RMW writes during init) |
| `0x11920` | WL_A20 | [1:0] from VM 187 byte 25; [11:10] from VM 187 byte 42 | Analog trim |
| `0x11924` | WL_PA0 | [26:24] | PA trim from VM 107 byte 0. Written multiple times during init and LUT load |
| `0x11928` | WL_PA1 | [2:0], [11:9], [17:15], [25:22] | PA trims from VM 107 bytes 1-4 (widths: 3, 3, 3, 4 bits) |
| `0x1192C` | WL_PA2 | [2:0] | PA trim from VM 107 byte 5 |
| `0x11930` | WL_XOSC | [16:13] xosc_l, [22:19] xosc_r | Crystal oscillator load trims from VM 106 (4 bits each; stored 0x1F lands as 0xF) |
| `0x11934` | WL_VCO_EN | [24] pulsed per VCO scan step | VCO enable control |
| `0x11938` | WL_VCO_BAND | [25:19] band select; [16], [18] pulsed per step | VCO band selection for scan |
| `0x1193C` | WL_VCO_FINE | | VCO fine tuning code |
| `0x11940` | WL_A40 | | Post-scan analog setup (constant) |
| `0x11950` | WL_A50 | | Written 32 times around LUT load (constant) |
| `0x11954` | WL_A54 | | Post-scan analog setup |
| `0x1195C` | WL_A5C | bytes 0-3 from VM 187 bytes 40-43 | Filter calibration result (boot 1: live; boot 2: from VM 187) |
| `0x11960` | WL_A60 | bytes 0-3 from VM 187 bytes 44-47 | Filter calibration result |
| `0x11964` | WL_A64 | | Analog init (written near end of sequence) |
| `0x11968` | WL_CAL_START | [28] start measurement; [0] filter cal start | Calibration trigger |
| `0x11978` | WL_CAL_STROBE | [17] comparator low; [18] comparator high; [15:8] filter result | VCO comparator readout and filter calibration result |

### Wireless Control Block: `0x14000-0x14064`

System-level wireless control: radio enables, clock gating, and the RF-die SPI
interface.

| Address | Name | Bits | Function |
|---------|------|------|----------|
| `0x14000` | WL_CTL0 | | Wireless system control |
| `0x14028` | RFSPI_CTL | [22:20] busy; [16] start; [15:8] address; [7:0] command; [4] kick toggle | RF-die SPI control port. Commands: 0xE = LUT word 0 write, 0xD = LUT word 1 write, 0x6 = read |
| `0x1402C` | RFSPI_DATA | [31:0] | RF-die SPI data (written before CTL) |
| `0x14034` | RFSPI_WAIT | | Written 0 while polling busy (purpose unclear) |
| `0x14040` | WL_CLK0 | | WiFi clock control |
| `0x14044` | WL_CLK1 | | WiFi clock control |
| `0x14048` | WL_CLK2 | | WiFi clock control |
| `0x1404C` | WL_CLK3 | | WiFi clock control |
| `0x14050` | WL_CLK4 | | WiFi clock control |
| `0x1405C` | WL_RST | | WiFi reset control (written 3-5 times during init) |

### Wi-Fi MAC / BBP Window: `0x30000-0x31FFF`

The Wi-Fi MAC and baseband processor (BBP) register space.

| Address | Name | Function |
|---------|------|----------|
| `0x30308` | MAC_308 | MAC configuration (part of first BBP load) |
| `0x3030C` | MAC_30C | MAC configuration |
| `0x30F00` | RF_CFG0 | Radio configuration (written during group 3) |
| `0x30F04` | RF_CFG1 | Radio configuration |
| `0x31004` | MAC_1004 | MAC configuration |
| `0x3101C` | BBP_PORT | BBP indirect access port: [17] start, [16] read, [15:8] register, [7:0] data |
| `0x31100` | MAC_1100 | MAC configuration |
| `0x31104` | MAC_1104 | MAC configuration |
| `0x31330` | MAC_1330 | MAC configuration |
| `0x31334` | MAC_1334 | MAC configuration |
| `0x31338` | MAC_1338 | MAC configuration |
| `0x3133C` | MAC_133C | MAC configuration |
| `0x31340` | MAC_1340 | MAC configuration |
| `0x31344` | MAC_1344 | MAC configuration |
| `0x31348` | MAC_1348 | MAC configuration |

### BBP Indirect Windows (via `0x3101C`)

The BBP is programmed indirectly through four register windows. Each window has
four BBP registers: commit, entry address, data, and read-back.

| Window | Commit | Entry addr | Data | Read-back | Entry range | Boot-1 transactions |
|--------|--------|------------|------|-----------|-------------|---------------------|
| C | `0xC8` | `0xC9` | `0xCA` | `0xCB` | 0x00-0x7A | 893 |
| C' | `0xCC` | `0xCD` | `0xCE` | -- | 0x00-0x47 | 73 |
| D | `0xD0` | `0xD1` | `0xD2` | `0xD3` | 0x00-0x5F | 101 |
| D' | `0xD4` | `0xD5` | `0xD6` | `0xD7` | 0x00-0x1C | 27 |

Protocol: write entry address, write data, write commit = 1.
Read: write entry address, write commit = 2, read the read-back register.

Direct BBP registers also used: `0x15`, `0x16`, `0x19`, `0x54`, `0x6A`.

### Radio Priority (RFPRIO)

Arbitrates the shared radio between BT, BLE, and WiFi.

| Address | Name | Function |
|---------|------|----------|
| `0x05E` | RFPRIOSTAT | Radio priority status |
| `0x060` | RFPRIOCNTL | Radio priority control |

Values observed: 17 (scanning), 26 (initiating/idle), 28 (connection data),
30 (connection instants/control).

### Clock and System Registers

| Address | Name | Function |
|---------|------|----------|
| `0x10008` | SYS_CLK1 | System clock configuration |
| `0x10010` | SYS_CLK2 | System clock configuration (WiFi clock source) |

### RF-Die LUT (via SPI at `0x14028`/`0x1402C`)

256-entry lookup table on the RF die, programmed via SPI. Two words per entry.

| Command | Function |
|---------|----------|
| `0xE` | Write LUT word 0 for entry |
| `0xD` | Write LUT word 1 for entry |
| `0x6` | Read LUT entry |

Protocol: write data to `0x1402C`, then `0x14028` = `0x10000 | entry<<8 | cmd`,
then toggle bit 4 five times high, five times low (the "kick").

LUT patches at runtime:
- Word 0, entries `0xE0-0xFF`: patched from VM 107 (PA calibration)
- Word 1, entries `0x00-0x7F`: patched from VCO scan results
- Word 1, entries `0xE0-0xFF`: patched from VM 107
- Word 0, entries `0x80-0xDF`: patched from VM 187 bytes 48-63

---

## Stored Calibration (VM Records)

Factory calibration data stored in the SDK's VM (key-value store). On a real
FM-1, the VM is at flash `0x0E8000` (not `0x093000` as in the emulator).

| VM ID | Size | Content | Drives |
|-------|------|---------|--------|
| 106 | 2 B | `xosc_l`, `xosc_r` (crystal load trims) | `0x11930` bits [16:13], [22:19] |
| 107 | 7 B | PA_C_I values (7 calibration bytes) | `0x11924` [26:24], `0x11928` [2:0]/[11:9]/[17:15]/[25:22], `0x1192C` [2:0], plus 32 LUT words each |
| 108 | 20 B | WiFi PA per-rate digital gain | 12 BBP window-D entries |
| 187 | 68 B | Full calibration cache (64 B payload + CRC-16) | Window-D/D' entries, `0x11908`, `0x1191C`, `0x11920`, `0x1195C`, `0x11960`, LUT word patches |

Record format: 4-byte header (CRC-16/XMODEM low byte, ID, length) + data.
VM 187 has an inner CRC-16 at bytes 64-65 over the 64-byte payload.

See BLE-HW-FACTS.md sections 14 and 16.4 for the complete byte-to-register
mapping.

---

## RF Init Sequence (Boot-2 Order)

The full RF initialization as measured in the emulator (BLE-HW-FACTS.md section
16.1). Boot-2 = stored trims present (the normal path). Total: 34,274 register
writes.

| Group | Description | Writes | Source |
|-------|-------------|--------|--------|
| 1 | Clocks (`0x10010`, `0x10008`) | 5 | Constant |
| 2 | First analog word `0x11900` | 8 | Constant (bit 14 preserved) |
| 3 | WiFi clock/reset, radio config | 37 | Constant |
| 4 | BBP + MAC first load (3,300 BBP transactions) | 6,624 | Constant |
| 5 | Crystal trim `0x11930` | 16 | VM 106 |
| 6 | WiFi analog init (20 registers) | 269 | Constant |
| 7 | VCO bank scan | ~380 | **Live every boot** |
| 8 | Post-scan analog setup | ~90 | Mixed: VM 187 + constant |
| 9 | PA trims | 6 | VM 107 |
| 10 | BBP second phase (reload + RMW) | 13,901 | Tables constant; RMW loop live |
| 11 | RF-die LUT (2 x 256 words) | ~6,100 | Constant base + VM 107/187/VCO patches |
| 12 | Final analog writes | ~50 | Constant (except VM 107) |
| 13 | VM 108 -> BBP window D | 36 | VM 108 |
| 14 | BT block | 187 | Constant + live BT TX trims |

### What runs live on every boot

- VCO bank scan: binary search over 64 bands, comparator at `0x11978` bits 17/18
- BBP window-D read-modify-write loop: 539 read-backs (DC/IQ trimming)
- BT TX trims: from BBP read-backs into `0x2FC08`/`0x2FC10`
- Bit 14 of `0x11900`: periodic routine, not a trim

---

## Datasheets

| Document | Location |
|----------|----------|
| AC7916A Datasheet V2.3 | `docs/hardware/AC7916A_Datasheet_V2.3.pdf` |
| AC791N Hardware Design Guide | `docs/hardware/AC791N_Hardware_Design_Guide.pdf` |
| BLE Hardware Facts (reverse-engineered) | `firmware/upstream/optimist/docs/BLE-HW-FACTS.md` |

The AC7916A datasheet covers the chip family. The AC791N hardware design guide
covers board-level integration (crystal, antenna, power). Neither document
provides the register-level detail found in BLE-HW-FACTS.md, which was
reverse-engineered from emulator traces.

The JieLi SDK repository (`fw-AC79_AIoT_SDK` on Gitee) contains additional
Chinese-language documentation under `doc/datasheet/AC791N规格书/` but these
primarily cover the application-level SDK API, not register maps.

---

## References

- `firmware/upstream/optimist/docs/BLE-HW-FACTS.md`: primary hardware reference
  (21 sections, 1,749 lines of measured register behavior)
- `firmware/hal/fm1_ble_rf.h`: RF init implementation in core-fm1
- `firmware/hal/fm1_ble.h`: BLE subsystem HAL
- JieLi SDK: `https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK` (binary drivers,
  Chinese docs, example applications)
