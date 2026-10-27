# Wi-Fi hardware facts for the AC791N / WL82

Written 2026-10-25 by the clean-room reader for the GPL-3.0 Wi-Fi driver on the JieLi AC791N (WL82)
radio. **The implementers must not read vendor code, vendor IR or vendor disassembly.** They work from
this sheet, the IEEE 802.11 standard, the SDIO Simplified Specification, and permitted open-source
references (Ralink RT2x00 GPL-2.0+, lwIP 2.x BSD, wpa_supplicant BSD, ESP-IDF Apache 2.0).

This sheet contains facts only: addresses, bit fields, values, sequences, timing, interrupts, bus
protocols and observed behaviour. It contains no vendor code or pseudo-code. Long vendor tables (BBP
init, RF-die LUT, AGC) are described by location, size and capture method, not transcribed.

Update 2026-10-26: added SDIO internal bus protocol (§7), RTOS shim API contract (§8), BBP programming
detail (§6), BLE/WiFi coexistence (§9), and evidence tags on all facts.

---

## 0. Sources, tags and caveats

**Tags.** Every fact carries one tag:

| Tag | Meaning |
|-----|---------|
| **[M:t]** | measured: an emulator MMIO trace of stock V15 or SDK demo (order, values, timing) |
| **[M:d]** | measured: a RAM or flash image taken from the emulator after the reference firmware ran |
| **[M:s]** | value seen: a constant, address or field offset read from the vendor binary or its debug info |
| **[I]** | inferred from names, structure or consistency; plausible, not proven |

Several facts are [M] for the *value* and [I] for the *meaning*; the text says which.

**Reference firmwares (oracles).**

- **Stock V15**: `fm1-firmware/FM-1.fwsc`. Run unchanged in the emulator. It initializes the full RF
  analog block (WiFi + BT) during BLE bring-up [M:t].
- **SDK demo_ble**: the AC79 SDK `apps/demo/demo_ble`, built unchanged, packaged for the FM-1.
  Simpler oracle: BLE only, no classic Bluetooth [M:t].
- **Vendor libraries** (`wl_rf_common.a` 14 modules, `wl_wifi_sta.a` 91 objects, `btctrler.a`):
  LLVM bitcode for pi32v2. Decompiled with `llvm-dis` to LLVM IR with full debug info (source paths,
  function names, variable names, string literals, register addresses as integer constants). Read by
  the reader only, to name what the traces show [M:s].
- **SDK source** (`fw-AC79_AIoT_SDK` on Gitee): application-layer WiFi API (`wifi_connect.h`,
  `wifi_conf.c`, `wifi_demo_task.c`). Readable source, not a binary [M:s for API surface].

**Emulator caveats.** The emulator is `fm1-emulator` with trace hooks. Its wireless model stores
configuration words and models the PLL lock comparator (thresholds measured on one FM-1) and the
filter-calibration result (a fixed value). It does **not** model the WiFi MAC engine, packet TX/RX,
the BBP calibration results, or the RF-die read-back. So:

- every value a firmware **writes** is genuine firmware behaviour [M:t];
- every value a firmware **reads** from the radio is a model output, and every write that depends
  on such a read (calibration results) shows the model, not a real FM-1;
- no WiFi data transfer happens in the emulator. MAC/STA facts come from the vendor binary [M:s]/[I].

Nothing in this sheet ran on hardware unless tagged [M:hw].

---

## 1. Overview

The AC791N (marketed as WL82) is a JieLi dual-core MCU with a **shared 2.4 GHz radio** supporting
both Bluetooth (BR/EDR + BLE) and Wi-Fi [M:s]. The FM-1 synthesizer uses this SoC but only exercises
the Bluetooth path: stock firmware V15 uses BR/EDR for audio, and Optimist adds BLE MIDI. The Wi-Fi
radio hardware is present on the die and initialized as part of the BLE RF bring-up [M:t], but no
community firmware has ever enabled Wi-Fi data transfer.

### Wi-Fi capabilities (from JieLi SDK and datasheet)

| Parameter | Value | Tag |
|-----------|-------|-----|
| Standard | IEEE 802.11 b/g/n (Wi-Fi 4) | M:s |
| Band | 2.4 GHz only | M:s |
| Channels | 1-14 (region-dependent) | M:s |
| Max PHY rate | 72.2 Mbps (802.11n MCS7, 20 MHz, 1SS, SGI) | M:s |
| Antenna | Single, shared with Bluetooth | I |
| Radio arbitration | Hardware priority controller (RFPRIO) at `0x05E`/`0x060` manages BT/BLE/WiFi | M:t |
| Security | WPA/WPA2 (in SDK) | M:s |
| Modes | STA, AP, STA+AP (in SDK) | M:s |
| Power | ~120 mA TX, ~60 mA RX (typical, from design guide) | M:s |

### SoC context

| Feature | Detail | Tag |
|---------|--------|-----|
| CPU | Dual-core pi32v2 (proprietary RISC) | M:s |
| Flash | 1 MB SPI NOR (FM-1 board) | M:s |
| RAM | 256 KB SRAM + cache | M:s |
| Clock | 24 MHz crystal (FM-1), PLL to ~120 MHz | M:s |
| Package | QFN48 (FM-1 board) | M:s |

---

## 2. GPL / open-source driver status

**No GPL-compatible Wi-Fi driver exists for the AC791N.**

### JieLi SDK (binary-only)

The official SDK (`fw-AC79_AIoT_SDK` on Gitee) provides Wi-Fi functionality, but the radio drivers
are distributed as **pre-compiled static libraries** (`.a` files containing LLVM bitcode for the
pi32v2 target) [M:s]. The relevant libraries:

| Library | Contents | Tag |
|---------|----------|-----|
| `lib_btctrler.a` | Bluetooth controller (BLE link layer, BR/EDR) | M:s |
| `lib_wifi.a` | Wi-Fi MAC, PHY control, power management | M:s |
| `lib_net.a` | lwIP network stack integration | M:s |
| `lib_ccm_aes.a` | AES-128 for BLE CCM and WiFi WPA | M:s |

Source code for these libraries is not available. The SDK application layer (lwIP integration, HTTP
server examples, etc.) is provided in C source, but the actual radio driver and MAC are opaque [M:s].

### Optimist BLE (community, GPL-3.0)

The Optimist project (`firmware/upstream/optimist/`) implements a BLE link layer from scratch by
reverse-engineering the baseband engine registers (documented in `BLE-HW-FACTS.md`). This was
feasible because: (1) the BLE baseband engine is relatively simple (advertising, one connection);
(2) register behavior was traced via the `fm1-emulator`; (3) BLE protocol is well-specified with
small state machines.

Wi-Fi would require reimplementing: the full 802.11 MAC (association, authentication, frame
aggregation, power save); the BBP (baseband processor) configuration for OFDM/DSSS; channel
estimation, equalization, and rate adaptation; WPA2 4-way handshake and CCMP encryption; the
firmware/hardware interface for TX/RX frame DMA. This is orders of magnitude more complex than the
BLE link layer.

### Permitted open-source references

The implementers may use the following open-source codebases as architectural references. These are
**not** JieLi code and do not require clean-room treatment:

| Reference | License | Use for |
|-----------|---------|---------|
| Ralink RT2x00 (Linux `drivers/net/wireless/ralink/rt2x00/`) | GPL-2.0+ | MAC frame handling, STA state machine architecture |
| wpa_supplicant / hostapd (hostap.w1.fi) | BSD | WPA2-PSK, 4-way handshake, CCMP, EAPOL |
| lwIP 2.x (savannah.nongnu.org) | BSD | TCP/IP stack, netif driver pattern |
| ESP-IDF WiFi (github.com/espressif) | Apache 2.0 | STA state machine patterns, WiFi API surface |

---

## 3. Radio hardware architecture

The AC791N has a **single radio front-end** shared between Bluetooth and Wi-Fi [M:t]. Both
subsystems use the same: PLL / VCO (tuned per channel) [M:t]; PA (power amplifier) with per-rate
digital gain [M:s]; LNA (low noise amplifier) and mixer [I]; ADC/DAC for baseband [I]; antenna
switch / matching network [I].

A hardware priority controller (RFPRIO at `0x05E`/`0x060`) arbitrates access when both BT and WiFi
are active [M:t]. In practice on the FM-1, only one subsystem is active at a time.

### RF initialization dependency

**BLE requires the WiFi RF analog block to be initialized first** [M:t]. The BLE bring-up sequence
(BLE-HW-FACTS.md §16) begins with a full WiFi analog init (groups 2-6, 8-9) before touching any
BLE-specific registers. This includes:

1. Clock configuration (`0x10010`, `0x10008`) [M:t]
2. WiFi analog block init (`0x11900-0x11964`, 20 registers, ~240 writes) [M:t]
3. VCO bank scan (live calibration, ~380 steps) [M:t]
4. PA trim application from stored calibration (VM records 106, 107, 108, 187) [M:t, M:d]
5. BBP (baseband processor) table loading (~3,300 transactions) [M:t]
6. RF-die LUT programming (2 x 256 words via SPI) [M:t]

Only after this shared RF init completes does the BLE-specific baseband initialization begin (BT
block at `0x2FC00`, BLE engine at `0x28000`) [M:t].

---

## 4. Wi-Fi register map

All addresses are physical MMIO. Sources: BLE-HW-FACTS.md, decompiled vendor IR [M:s], and emulator
traces [M:t]. Register names from JL_WL_TypeDef debug info where available [M:s], otherwise
assigned by the reader [I].

### 4.1 Wi-Fi / RF analog block: `0x11900-0x1197C`

The shared radio front-end. Initialized during both WiFi and BLE startup [M:t].

| Address | Name | Bits | Function | Tag |
|---------|------|------|----------|-----|
| `0x11900` | WL_A00 | [14] periodic routine bit (not a trim); [31:0] first analog word | Master analog control. Bit 14 toggled by a periodic routine (~2 ms); `rf_init` preserves it as found | M:t, meaning I |
| `0x11904` | WL_A04 | | Analog configuration (constant, set during post-scan) | M:t |
| `0x11908` | WL_A08 | [7], [15], [17], [25] | Trim fields from VM 187 bytes 32, 33, 36, 37 | M:s |
| `0x11910` | WL_A10 | [15:14] from VM 187 byte 42; [22:26] from T6 table | Analog configuration with stored trims | M:s |
| `0x11914` | WL_A14 | [17:15], [14:12], [7:5] from T6 table | Analog configuration | M:s |
| `0x1191C` | WL_A1C | [9:8] | Trim from VM 187 byte 24 (three RMW writes during init) | M:s |
| `0x11920` | WL_A20 | [1:0] from VM 187 byte 25; [11:10] from VM 187 byte 42 | Analog trim | M:s |
| `0x11924` | WL_PA0 | [26:24] | PA trim from VM 107 byte 0. Written multiple times during init and LUT load | M:t, M:s |
| `0x11928` | WL_PA1 | [2:0], [11:9], [17:15], [25:22] | PA trims from VM 107 bytes 1-4 (widths: 3, 3, 3, 4 bits) | M:s |
| `0x1192C` | WL_PA2 | [2:0] | PA trim from VM 107 byte 5 | M:s |
| `0x11930` | WL_XOSC | [16:13] xosc_l, [22:19] xosc_r | Crystal oscillator load trims from VM 106 (4 bits each; stored 0x1F lands as 0xF) | M:t, M:s |
| `0x11934` | WL_VCO_EN | [24] pulsed per VCO scan step | VCO enable control | M:t, M:s |
| `0x11938` | WL_VCO_BAND | [25:19] band select; [16], [18] pulsed per step | VCO band selection for scan | M:t, M:s |
| `0x1193C` | WL_VCO_FINE | | VCO fine tuning code | M:s |
| `0x11940` | WL_A40 | | Post-scan analog setup (constant) | M:t |
| `0x11950` | WL_A50 | | Written 32 times around LUT load (constant) | M:t |
| `0x11954` | WL_A54 | | Post-scan analog setup | M:t |
| `0x1195C` | WL_A5C | bytes 0-3 from VM 187 bytes 40-43 | Filter calibration result (boot 1: live; boot 2: from VM 187) | M:t, M:s |
| `0x11960` | WL_A60 | bytes 0-3 from VM 187 bytes 44-47 | Filter calibration result | M:t, M:s |
| `0x11964` | WL_A64 | | Analog init (written near end of sequence) | M:t |
| `0x11968` | WL_CAL_START | [28] start measurement; [0] filter cal start | Calibration trigger | M:t, M:s |
| `0x11978` | WL_CAL_STROBE | [17] comparator low; [18] comparator high; [15:8] filter result | VCO comparator readout and filter calibration result | M:t, M:s |

### 4.2 Wireless control block: `0x14000-0x14064`

System-level wireless control: radio enables, clock gating, and the RF-die SPI interface [M:t].

| Address | Name | Bits | Function | Tag |
|---------|------|------|----------|-----|
| `0x14000` | WL_CTL0 | | Wireless system control | M:t |
| `0x14008` | WL_CTL8 | | WiFi control (written during clock/reset init) | M:s |
| `0x14028` | RFSPI_CTL | [22:20] busy; [16] start; [15:8] address; [7:0] command; [4] kick toggle | RF-die SPI control port. Commands: 0xE = LUT word 0 write, 0xD = LUT word 1 write, 0x6 = read | M:t, M:s |
| `0x1402C` | RFSPI_DATA | [31:0] | RF-die SPI data (written before CTL) | M:t |
| `0x14034` | RFSPI_WAIT | | Written 0 while polling busy (purpose unclear) | M:t, I |
| `0x14040` | WL_CLK0 | | WiFi clock control | M:t, M:s |
| `0x14044` | WL_CLK1 | | WiFi clock control | M:t, M:s |
| `0x14048` | WL_CLK2 | | WiFi clock control | M:t, M:s |
| `0x1404C` | WL_CLK3 | | WiFi clock control | M:t, M:s |
| `0x14050` | WL_CLK4 | | WiFi clock control | M:t, M:s |
| `0x1405C` | WL_RST | | WiFi reset control (written 3-5 times during init) | M:t, M:s |

### 4.3 Wi-Fi MAC / BBP window: `0x30000-0x31FFF`

The Wi-Fi MAC and baseband processor (BBP) register space [M:t].

| Address | Name | Function | Tag |
|---------|------|----------|-----|
| `0x30308` | MAC_308 | MAC configuration (part of first BBP load) | M:t |
| `0x3030C` | MAC_30C | MAC configuration | M:t |
| `0x30F00` | RF_CFG0 | Radio configuration (written during clock/reset init group 3) | M:t, M:s |
| `0x30F04` | RF_CFG1 | Radio configuration (also used in SDIO DMA and RX path) | M:t, M:s |
| `0x30F10` | MAC_F10 | MAC configuration | M:s |
| `0x30F14` | MAC_F14 | MAC configuration | M:s |
| `0x30F18` | MAC_F18 | MAC configuration | M:s |
| `0x30F1C` | MAC_F1C | MAC configuration | M:s |
| `0x31004` | MAC_1004 | MAC configuration | M:t |
| `0x3101C` | BBP_PORT | BBP indirect access port: [17] start, [16] read, [15:8] register, [7:0] data | M:t, M:s |
| `0x31100` | MAC_1100 | MAC configuration | M:t |
| `0x31104` | MAC_1104 | MAC configuration | M:t |
| `0x31330` | MAC_1330 | MAC configuration | M:t |
| `0x31334` | MAC_1334 | MAC configuration | M:t |
| `0x31338` | MAC_1338 | MAC configuration | M:t |
| `0x3133C` | MAC_133C | MAC configuration | M:t |
| `0x31340` | MAC_1340 | MAC configuration | M:t |
| `0x31344` | MAC_1344 | MAC configuration | M:t |
| `0x31348` | MAC_1348 | MAC configuration | M:t |

### 4.4 Radio priority (RFPRIO)

Arbitrates the shared radio between BT, BLE, and WiFi [M:t, M:s].

| Address | Name | Function | Tag |
|---------|------|----------|-----|
| `0x05E` | RFPRIOSTAT | Radio priority status | M:t |
| `0x060` | RFPRIOCNTL | Radio priority control | M:t, M:s |

Values observed: 17 (scanning), 26 (initiating/idle), 28 (connection data),
30 (connection instants/control) [M:t].

### 4.5 Clock and system registers

| Address | Name | Function | Tag |
|---------|------|----------|-----|
| `0x10008` | SYS_CLK1 | System clock configuration | M:t |
| `0x10010` | SYS_CLK2 | System clock configuration (WiFi clock source) | M:t, M:s |

---

## 5. RF-die LUT (via SPI at `0x14028`/`0x1402C`)

256-entry lookup table on the RF die, programmed via SPI [M:t]. Two words per entry [M:s].

| Command | Function | Tag |
|---------|----------|-----|
| `0xE` | Write LUT word 0 for entry | M:s |
| `0xD` | Write LUT word 1 for entry | M:s |
| `0x6` | Read LUT entry | M:s |

Protocol: write data to `0x1402C`, then `0x14028` = `0x10000 | entry<<8 | cmd`, then toggle bit 4
five times high, five times low (the "kick") [M:t, I].

LUT patches at runtime [M:t, M:s]:
- Word 0, entries `0xE0-0xFF`: patched from VM 107 (PA calibration)
- Word 1, entries `0x00-0x7F`: patched from VCO scan results
- Word 1, entries `0xE0-0xFF`: patched from VM 107
- Word 0, entries `0x80-0xDF`: patched from VM 187 bytes 48-63

---

## 6. BBP programming (via `0x3101C`)

The BBP (baseband processor) is programmed indirectly through four register windows, each accessed
via the BBP_PORT at `0x3101C` [M:t, M:s]. Each window has up to four BBP registers: commit, entry
address, data, and optionally read-back.

### 6.1 Window registers

| Window | Commit | Entry addr | Data | Read-back | Entry range | Boot transactions | Tag |
|--------|--------|------------|------|-----------|-------------|-------------------|-----|
| C | `0xC8` | `0xC9` | `0xCA` | `0xCB` | 0x00-0x7A | 893 | M:t, M:s |
| C' | `0xCC` | `0xCD` | `0xCE` | -- | 0x00-0x47 | 73 | M:t, M:s |
| D | `0xD0` | `0xD1` | `0xD2` | `0xD3` | 0x00-0x5F | 101 | M:t, M:s |
| D' | `0xD4` | `0xD5` | `0xD6` | `0xD7` | 0x00-0x1C | 27 | M:t, M:s |

### 6.2 Write protocol

To write a BBP register in a given window [M:t]:

1. Write the entry address to the window's entry-address BBP register via `0x3101C`:
   set `0x3101C` = `(1 << 17) | (entry_addr_reg << 8) | value` [M:t]
2. Write the data value to the window's data BBP register via `0x3101C`:
   set `0x3101C` = `(1 << 17) | (data_reg << 8) | value` [M:t]
3. Write the commit register with value 1 via `0x3101C`:
   set `0x3101C` = `(1 << 17) | (commit_reg << 8) | 1` [M:t]

Each BBP write is therefore three MMIO writes to `0x3101C` [M:t].

### 6.3 Read protocol

To read a BBP register (windows C and D only, which have read-back registers) [M:s]:

1. Write the entry address (same as write step 1)
2. Write the commit register with value 2 (not 1)
3. Read the read-back register via `0x3101C`: set `0x3101C` = `(1 << 16) | (readback_reg << 8)`,
   then read `0x3101C` and extract bits [7:0]

### 6.4 Direct BBP registers

Certain BBP registers are accessed directly (not through windows): `0x15`, `0x16`, `0x19`, `0x54`,
`0x6A` [M:t]. These are written with `0x3101C` = `(1 << 17) | (reg << 8) | value` [M:t].

### 6.5 Boot transaction counts

The full RF init (§10) performs approximately 34,274 total register writes, of which the majority
are BBP transactions [M:t]:

| Phase | Window | Writes | Read-backs | Notes | Tag |
|-------|--------|--------|------------|-------|-----|
| Group 4 (first BBP load) | C, C', D, D' | ~3,300 | 0 | Constant table data | M:t |
| Group 10 (second phase) | C, D | ~4,600 writes | 539 read-backs | D read-backs are DC/IQ trim calibration | M:t |

---

## 7. SDIO internal bus protocol

The AC791N uses an internal SDIO-like bus to connect the WiFi MAC/BBP engine to the main CPU [M:s].
This is not an external SDIO card interface; it is an on-chip interconnect [I]. The bus is
implemented in three vendor modules (`wl_sdio.c`, `wl_sdio_host.c`, `wl_usb_to_sdio.c`) totalling
~55 functions [M:s].

### 7.1 SDIO controller registers

| Address | Purpose | Tag |
|---------|---------|-----|
| `0x50000` | SDIO base control register | M:s |
| `0x50008` | SDIO data/status (read during operations) | M:s |
| `0x5000C` | SDIO data/status (read during operations) | M:s |

Note: these addresses were discovered in the vendor binary [M:s] but are not in the emulator trace
(the emulator does not model the SDIO controller). Their detailed bit fields are unknown [I].

### 7.2 Bus operations

The bus uses standard SDIO CMD52 (single-byte) and CMD53 (block transfer) semantics [M:s]:

- **CMD52** (`io_rw_direct`): single-byte read or write to a function/address. Used for control
  register access [M:s].
- **CMD53** (`io_rw_extended`): block DMA transfer. Used for bulk data (TX/RX frames). Parameters:
  write flag, function number, address, increment mode, buffer pointer, length, block size [M:s].

### 7.3 Initialization sequence

The bus initialization follows this order [M:s]:

1. Clock and reset init via `0x14040-0x1405C` (WiFi clock gates and reset) [M:t]
2. SDIO enumeration (bus discovery and function enable) [M:s]
3. WiFi DMA init (sets up DMA descriptors for TX/RX) [M:s]
4. PA enable (power amplifier brought up) [M:s]
5. RF common init (full RF analog bring-up, §10) [M:t]

### 7.4 IRQ handling

- An SDIO interrupt handler is registered for WiFi MAC events [M:s].
- An IRQ thread runs in the background, woken by a semaphore post from the interrupt [M:s].
- The receive interrupt handler dispatches frames to the RX data path [M:s].

### 7.5 TX/RX data path

The internal SDIO bus carries WiFi frame data between the MAC engine and the CPU [M:s]:

- **TX**: frame DMA write over SDIO [M:s].
- **RX**: SDIO IRQ triggers receive, frames passed through a software filter [M:s].
- **Rate control**: per-packet TX rate selection via a rate control table [M:s].

### 7.6 Host controller

- Bus locking via mutex (claim/release host) [M:s].
- Semaphore-based synchronization for bus operations [M:s].
- The host controller manages timing and arbitration for the internal bus [M:s].

---

## 8. RTOS shim behavioral contracts

The WiFi MAC engine requires OS-level abstractions that the vendor firmware provides via its
proprietary RTOS [M:s]. A clean-room driver must provide equivalent behavior. This section
describes the behavioral contracts (what each abstraction must do), not the naming or
implementation.

### 8.1 Frame buffer management [M:s]

The MAC layer requires a frame buffer abstraction with these capabilities:

| Capability | Contract | Tag |
|------------|----------|-----|
| Allocate | Allocate a buffer with space for frame data plus metadata. Returns a handle. | M:s |
| Allocate with headroom | Same, but reserves space at the front for protocol headers. | M:s |
| Shallow copy | Create a copy sharing the same underlying data. | M:s |
| Deep copy | Create an independent copy of the data. | M:s |
| Prepend | Move the data start pointer backward, increasing usable length. | M:s |
| Trim | Reduce the buffer to a given length (truncate tail). | M:s |
| Extract | Copy a range of bytes from the buffer to a destination. | M:s |
| Free | Release the buffer back to the pool. | M:s |

For bare-metal single-threaded operation, a static pool allocator with immediate free is
sufficient [I].

### 8.2 Link state signaling [M:s]

The MAC layer needs to signal link state changes (up/down) and control TX flow (start/stop/wake
the transmit path). On bare-metal, these map to simple state flags [I].

### 8.3 Deferred work [M:s]

Interrupt handlers must not perform long-running work. The MAC layer requires a mechanism to
register a function for deferred execution outside interrupt context, schedule it, and cancel it.
On bare-metal, this maps to a work queue polled from the main loop [I].

### 8.4 Synchronization [M:s]

The MAC layer requires interrupt-safe critical sections (disable interrupts, save state, restore)
and a condition-wait mechanism (block until a flag becomes true). On bare-metal, critical sections
are interrupt disable/enable pairs; condition waits are polled flags [I].

### 8.5 Heap memory [M:s]

Small dynamic allocations for MAC-layer state. On bare-metal with bounded usage, a bump allocator
is sufficient [I].

### 8.6 Vendor RTOS primitives [M:s]

The vendor's RTOS provides underlying primitives in several categories: mutex management, semaphore
management, queue management, task lifecycle, memory allocation, and time services [M:s]. The
behavioral contracts above (§8.1-8.5) are what the WiFi MAC expects; the RTOS primitives are how
the vendor implements them internally.

---

## 9. BLE/WiFi coexistence

### 9.1 RFPRIO hardware arbitration

The radio priority controller at `0x05E`/`0x060` manages time-division access to the shared radio
[M:t]. Functions in the BT controller set priority levels for different operations [M:s]:

| Priority user | Operation | Tag |
|---------------|-----------|-----|
| BR/EDR frame | ACL, SCO links | M:s |
| BLE | Advertising, scanning, connections | M:s |
| WiFi | STA activity (when implemented) | M:s |

### 9.2 WiFi detection by BLE

The BLE link layer includes a vendor-specific WiFi detection subsystem (`ll_wifi_detect.c`) that
probes for WiFi activity to enable adaptive frequency hopping (AFH) [M:s]. This uses custom LL
control PDUs [M:s]. For STA-only WiFi mode, this subsystem can remain in its current form: the BLE
side detects WiFi activity and adapts its channel map [I].

### 9.3 Shared RF analog

Both BLE and WiFi require the RF analog block to be initialized [M:t]. Key cross-module
dependencies [M:s]:

- `wf_rf_common_init`: called during BLE RF bring-up (WiFi analog must init first)
- `wf_get_phcom_cnt`: PHY timing counter shared between BLE and WiFi
- `wf_deepsl_reg_pop`: restore WiFi registers after deep sleep

The BT controller directly accesses WiFi registers: `0x11924` (WL_PA0), `0x11950` (WL_A50),
`0x14000` (WL_CTL0), `0x10010` (SYS_CLK2) [M:s].

---

## 10. Stored calibration (VM records)

Factory calibration data stored in the SDK's VM (key-value store) [M:d]. On a real FM-1, the VM is
at flash `0x0E8000` [M:hw] (not `0x093000` as in the emulator [M:d]).

| VM ID | Size | Content | Drives | Tag |
|-------|------|---------|--------|-----|
| 106 | 2 B | `xosc_l`, `xosc_r` (crystal load trims) | `0x11930` bits [16:13], [22:19] | M:t, M:s |
| 107 | 7 B | PA_C_I values (7 calibration bytes) | `0x11924` [26:24], `0x11928` [2:0]/[11:9]/[17:15]/[25:22], `0x1192C` [2:0], plus 32 LUT words each | M:t, M:s |
| 108 | 20 B | WiFi PA per-rate digital gain | 12 BBP window-D entries | M:s |
| 187 | 68 B | Full calibration cache (64 B payload + CRC-16) | Window-D/D' entries, `0x11908`, `0x1191C`, `0x11920`, `0x1195C`, `0x11960`, LUT word patches | M:t, M:s |

Record format: 4-byte header (CRC-16/XMODEM low byte, ID, length) + data [M:s].
VM 187 has an inner CRC-16 at bytes 64-65 over the 64-byte payload [M:s].

See BLE-HW-FACTS.md §14 and §16.4 for the complete byte-to-register mapping.

---

## 11. RF init sequence (boot-2 order)

The full RF initialization as measured in the emulator (BLE-HW-FACTS.md §16.1) [M:t]. Boot-2 =
stored trims present (the normal path). Total: 34,274 register writes [M:t].

| Group | Description | Writes | Source | Tag |
|-------|-------------|--------|--------|-----|
| 1 | Clocks (`0x10010`, `0x10008`) | 5 | Constant | M:t |
| 2 | First analog word `0x11900` | 8 | Constant (bit 14 preserved) | M:t |
| 3 | WiFi clock/reset, radio config | 37 | Constant | M:t |
| 4 | BBP + MAC first load (3,300 BBP transactions) | 6,624 | Constant | M:t |
| 5 | Crystal trim `0x11930` | 16 | VM 106 | M:t |
| 6 | WiFi analog init (20 registers) | 269 | Constant | M:t |
| 7 | VCO bank scan | ~380 | **Live every boot** | M:t |
| 8 | Post-scan analog setup | ~90 | Mixed: VM 187 + constant | M:t |
| 9 | PA trims | 6 | VM 107 | M:t |
| 10 | BBP second phase (reload + RMW) | 13,901 | Tables constant; RMW loop live | M:t |
| 11 | RF-die LUT (2 x 256 words) | ~6,100 | Constant base + VM 107/187/VCO patches | M:t |
| 12 | Final analog writes | ~50 | Constant (except VM 107) | M:t |
| 13 | VM 108 -> BBP window D | 36 | VM 108 | M:t |
| 14 | BT block | 187 | Constant + live BT TX trims | M:t |

### 11.1 What runs live on every boot

- VCO bank scan: binary search over 64 bands, comparator at `0x11978` bits 17/18 [M:t]
- BBP window-D read-modify-write loop: 539 read-backs (DC/IQ trimming) [M:t]
- BT TX trims: from BBP read-backs into `0x2FC08`/`0x2FC10` [M:t]
- Bit 14 of `0x11900`: periodic routine, not a trim [M:t]

---

## 12. Datasheets

| Document | Location | Tag |
|----------|----------|-----|
| AC7916A Datasheet V2.3 | `docs/hardware/AC7916A_Datasheet_V2.3.pdf` | published |
| AC791N Hardware Design Guide | `docs/hardware/AC791N_Hardware_Design_Guide.pdf` | published |
| BLE Hardware Facts (reverse-engineered) | `firmware/upstream/optimist/docs/BLE-HW-FACTS.md` | M:t, M:s |

The AC7916A datasheet covers the chip family. The AC791N hardware design guide covers board-level
integration (crystal, antenna, power). Neither document provides the register-level detail found in
BLE-HW-FACTS.md, which was reverse-engineered from emulator traces [M:t].

The JieLi SDK repository (`fw-AC79_AIoT_SDK` on Gitee) contains additional Chinese-language
documentation under `doc/datasheet/AC791N规格书/` but these primarily cover the application-level
SDK API, not register maps [M:s].

---

## 13. References

- `firmware/upstream/optimist/docs/BLE-HW-FACTS.md`: primary hardware reference
  (21 sections, 1,749 lines of measured register behavior)
- `firmware/hal/fm1_ble_rf.h`: RF init implementation in core-fm1
- `firmware/hal/fm1_ble.h`: BLE subsystem HAL
- JieLi SDK: `https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK` (binary drivers,
  Chinese docs, example applications)
- IEEE 802.11-2020: WLAN standard (MAC, PHY)
- SDIO Simplified Specification v3.0: bus protocol
- FIPS 197 (AES), FIPS 180-4 (SHA-1), RFC 2104 (HMAC), RFC 2898 (PBKDF2): crypto standards
- IEEE 802.11i: WPA2/CCMP security
