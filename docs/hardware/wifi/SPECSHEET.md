# Wi-Fi hardware facts for the AC791N / WL82

Written 2026-10-25 by the clean-room reader for the GPL-3.0 Wi-Fi driver on the JieLi AC791N (WL82)
radio. **The implementers must not read vendor code, vendor IR or vendor disassembly.** They work from
this sheet, the IEEE 802.11 standard, and permitted open-source references (Ralink RT2x00
GPL-2.0+, lwIP 2.x BSD, wpa_supplicant BSD, ESP-IDF Apache 2.0).

This sheet contains facts only: addresses, bit fields, values, sequences, timing, interrupts, bus
protocols and observed behaviour. It contains no vendor code or pseudo-code. Long vendor tables (BBP
init, RF-die LUT, AGC) are described by location, size and capture method, not transcribed.

Update 2026-10-26: added SDIO internal bus protocol (§7), RTOS shim API contract (§8), BBP programming
detail (§6), BLE/WiFi coexistence (§9), and evidence tags on all facts.

Update 2026-10-28: **ERRATA** §7 rewritten. The 0x50000 addresses are GPIO Port A, not SDIO. The
AC791N WiFi MAC uses direct memory-mapped registers, not an SDIO bus. Added RT2800 IP ancestry
evidence, DMA/interrupt register map (§7.1), MAC register addressing (§7.2). Added [IP:RT2x00] tag.

Update 2026-10-28 (hardware probes v15-v18): **[M:hw]** First real-hardware register probes on FM-1.
Key findings: (1) `0x50000` confirmed as GPIO, not SDIO [M:hw]. (2) DMA range `0x0200`-`0x0210`
is open bus on AC791N: write-readback returns zero [M:hw]. (3) **WL_CTL0 (`0x14000`) is the bus
gate** for the `0x30000`+ MAC register space: MAC reads fault when WL_CTL0=0, succeed when written
[M:hw]. (4) MAC_SYS_CTRL (`0x31004`) confirmed readable, returns `0x0C` (TX_EN + RX_EN) [M:hw].
(5) Full wireless control block scanned `0x14000`-`0x140A0` with 13 non-zero registers identified
[M:hw]. §4.2 and §7 updated accordingly.

Update 2026-10-28 (hardware probes v19-v22): **[M:hw]** MAC space mapping and DMA hunt.
Key findings: (1) MAC register space `0x30000`-`0x31800` confirmed alive with multiple non-zero
registers [M:hw v19-v20]. (2) `0x30200` returns `0x0BAD0BAD` sentinel (decoded but unimplemented)
[M:hw v19]. (3) Chip version at `0x31000` = `0x00300100` [M:hw v19]. (4) BBP port `0x3101C` =
`0x0008D000` confirmed on real hardware [M:hw v19]. (5) 18-address DMA hunt found two non-zero
ranges: `0x34200` (SRAM, high-entropy) and **`0x40200` (DMA candidate)** [M:hw v21-v22].
(6) `0x40000` block: base zeros, non-zero at RT2800 DMA offsets (`+0x200`=`0x809803B5`,
`+0x204`=`0x1`, `+0x208`=`0xF1FE`) [M:hw v22]. (7) Indirect access via `0x14090`/`0x14080`
disproven: `0x14080` always returns `0x800` regardless of `0x14090` value [M:hw v22].
§7.1, §7.3, §7.8 updated.

---

## 0. Sources, tags and caveats

**Tags.** Every fact carries one or more tags:

| Tag              | Meaning                                                                                       |
|------------------|-----------------------------------------------------------------------------------------------|
| **[M:t]**        | measured: an emulator MMIO trace of stock V15 or SDK demo (order, values, timing)             |
| **[M:d]**        | measured: a RAM or flash image taken from the emulator after the reference firmware ran       |
| **[M:hw]**       | measured: real FM-1 hardware observation (register probe via custom firmware)                  |
| **[M:s]**        | value seen: a constant, address or field offset read from the vendor binary or its debug info |
| **[IP:RT2x00]**  | derived from identified IP ancestor: Ralink RT2x00 family (see AC791N_IP_ANCESTRY.md §3)     |
| **[I]**          | inferred from names, structure or consistency; plausible, not proven                          |

Several facts are [M] for the *value* and [I] for the *meaning*; the text says which.

**IP ancestry.** The WiFi MAC/BBP descends from the Ralink RT2x00 family (Level 2+ ancestry,
high confidence). Evidence: BBP indirect access protocol, RF-die SPI, host interface register
layout all match RT2800. The Linux RT2x00 driver (`drivers/net/wireless/ralink/rt2x00/rt2800.h`,
GPL-2.0+) is a Tier 1 architectural reference. See `docs/hardware/AC791N_IP_ANCESTRY.md` §3 for
the full evidence chain. Facts derived from RT2800 public documentation are tagged [IP:RT2x00].

**Reference firmwares (oracles).**

- **Stock V15**: `fm1-firmware/FM-1.fwsc`. Run unchanged in the emulator. It initializes the full RF
  analog block (WiFi + BT) during BLE bring-up [M:t].
- **SDK demo_ble**: the AC79 SDK `apps/demo/demo_ble`, built unchanged, packaged for the FM-1.
  Simpler oracle: BLE only, no classic Bluetooth [M:t].
- **Vendor binary libraries**: pre-compiled radio driver libraries from the SDK. Analyzed by the
  reader only using standard disassembly tools to identify register addresses and behavioral
  sequences. No encryption or access controls were bypassed [M:s].
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

Facts tagged [M:hw] were measured on real FM-1 hardware using the `wifi_clean_room_probes` firmware
(probes v9-v18, 2026-10-28). The probe firmware boots the device, enables WiFi clocks, reads/writes
specific register addresses, and reports results over USB SysEx (DAP protocol). All other facts are
from the emulator [M:t] or vendor binary analysis [M:s].

---

## 1. Overview

The AC791N (marketed as WL82) is a JieLi dual-core MCU with a **shared 2.4 GHz radio** supporting
both Bluetooth (BR/EDR + BLE) and Wi-Fi [M:s]. The FM-1 synthesizer uses this SoC but only exercises
the Bluetooth path: stock firmware V15 uses BR/EDR for audio, and Optimist adds BLE MIDI. The Wi-Fi
radio hardware is present on the die and initialized as part of the BLE RF bring-up [M:t], but no
community firmware has ever enabled Wi-Fi data transfer.

### Wi-Fi capabilities (from JieLi SDK and datasheet)

| Parameter         | Value                                                                        | Tag |
|-------------------|------------------------------------------------------------------------------|-----|
| Standard          | IEEE 802.11 b/g/n (Wi-Fi 4)                                                  | M:s |
| Band              | 2.4 GHz only                                                                 | M:s |
| Channels          | 1-14 (region-dependent)                                                      | M:s |
| Max PHY rate      | 72.2 Mbps (802.11n MCS7, 20 MHz, 1SS, SGI)                                   | M:s |
| Antenna           | Single, shared with Bluetooth                                                | I   |
| Radio arbitration | Hardware priority controller (RFPRIO) at `0x05E`/`0x060` manages BT/BLE/WiFi | M:t |
| Security          | WPA/WPA2 (in SDK)                                                            | M:s |
| Modes             | STA, AP, STA+AP (in SDK)                                                     | M:s |
| Power             | ~120 mA TX, ~60 mA RX (typical, from design guide)                           | M:s |

### SoC context

| Feature | Detail                                 | Tag |
|---------|----------------------------------------|-----|
| CPU     | Dual-core pi32v2 (proprietary RISC)    | M:s |
| Flash   | 1 MB SPI NOR (FM-1 board)              | M:s |
| RAM     | 256 KB SRAM + cache                    | M:s |
| Clock   | 24 MHz crystal (FM-1), PLL to ~120 MHz | M:s |
| Package | QFN48 (FM-1 board)                     | M:s |

---

## 2. GPL / open-source driver status

**No GPL-compatible Wi-Fi driver exists for the AC791N.**

### JieLi SDK (binary-only)

The official SDK provides Wi-Fi functionality, but the radio drivers are distributed as **pre-compiled static
libraries** without source code [M:s]. The SDK provides libraries covering
the Bluetooth controller, Wi-Fi MAC/PHY, network stack integration, and cryptographic primitives.
Source code for these libraries is not available. The SDK application layer (lwIP integration, HTTP
server examples, etc.) is provided in C source, but the actual radio driver and MAC are opaque [M:s].

### Optimist BLE (community, GPL-3.0)

The Optimist project (`firmware/upstream/optimist/`) implements a BLE link layer from scratch by
reverse-engineering the baseband engine registers (documented in `BLE-HW-FACTS.md`). This was
feasible because: (1) the BLE baseband engine is relatively simple (advertising, one connection); (2) register behavior
was traced via the `fm1-emulator`; (3) BLE protocol is well-specified with
small state machines.

Wi-Fi would require reimplementing: the full 802.11 MAC (association, authentication, frame
aggregation, power save); the BBP (baseband processor) configuration for OFDM/DSSS; channel
estimation, equalization, and rate adaptation; WPA2 4-way handshake and CCMP encryption; the
firmware/hardware interface for TX/RX frame DMA. This is orders of magnitude more complex than the
BLE link layer.

### Permitted open-source references

The implementers may use the following open-source codebases as architectural references. These are **not** JieLi code
and do not require clean-room treatment:

| Reference                                                   | License    | Use for                                            |
|-------------------------------------------------------------|------------|----------------------------------------------------|
| Ralink RT2x00 (Linux `drivers/net/wireless/ralink/rt2x00/`) | GPL-2.0+   | MAC frame handling, STA state machine architecture |
| wpa_supplicant / hostapd (hostap.w1.fi)                     | BSD        | WPA2-PSK, 4-way handshake, CCMP, EAPOL             |
| lwIP 2.x (savannah.nongnu.org)                              | BSD        | TCP/IP stack, netif driver pattern                 |
| ESP-IDF WiFi (github.com/espressif)                         | Apache 2.0 | STA state machine patterns, WiFi API surface       |

---

## 3. Radio hardware architecture

The AC791N has a **single radio front-end** shared between Bluetooth and Wi-Fi [M:t]. Both
subsystems use the same: PLL / VCO (tuned per channel) [M:t]; PA (power amplifier) with per-rate
digital gain [M:s]; LNA (low noise amplifier) and mixer [I]; ADC/DAC for baseband [I]; antenna
switch / matching network [I].

A hardware priority controller (RFPRIO at `0x05E`/`0x060`) arbitrates access when both BT and WiFi
are active [M:t]. In practice on the FM-1, only one subsystem is active at a time.

### RF initialization dependency

**BLE requires the WiFi RF analog block to be initialized first** [M:t]. The BLE bring-up sequence (BLE-HW-FACTS.md §16)
begins with a full WiFi analog init (groups 2-6, 8-9) before touching any
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

All addresses are physical MMIO. Sources: BLE-HW-FACTS.md, vendor binary analysis [M:s], and
emulator traces [M:t]. Register names from vendor debug info where available [M:s], otherwise
assigned by the reader [I].

### 4.1 Wi-Fi / RF analog block: `0x11900-0x1197C`

The shared radio front-end. Initialized during both WiFi and BLE startup [M:t].

| Address   | Name          | Bits                                                             | Function                                                                                             | Tag            |
|-----------|---------------|------------------------------------------------------------------|------------------------------------------------------------------------------------------------------|----------------|
| `0x11900` | WL_A00        | [14] periodic routine bit (not a trim); [31:0] first analog word | Master analog control. Bit 14 toggled by a periodic routine (~2 ms); `rf_init` preserves it as found | M:t, meaning I |
| `0x11904` | WL_A04        |                                                                  | Analog configuration (constant, set during post-scan)                                                | M:t            |
| `0x11908` | WL_A08        | [7], [15], [17], [25]                                            | Trim fields from VM 187 bytes 32, 33, 36, 37                                                         | M:s            |
| `0x11910` | WL_A10        | [15:14] from VM 187 byte 42; [22:26] from T6 table               | Analog configuration with stored trims                                                               | M:s            |
| `0x11914` | WL_A14        | [17:15], [14:12], [7:5] from T6 table                            | Analog configuration                                                                                 | M:s            |
| `0x1191C` | WL_A1C        | [9:8]                                                            | Trim from VM 187 byte 24 (three RMW writes during init)                                              | M:s            |
| `0x11920` | WL_A20        | [1:0] from VM 187 byte 25; [11:10] from VM 187 byte 42           | Analog trim                                                                                          | M:s            |
| `0x11924` | WL_PA0        | [26:24]                                                          | PA trim from VM 107 byte 0. Written multiple times during init and LUT load                          | M:t, M:s       |
| `0x11928` | WL_PA1        | [2:0], [11:9], [17:15], [25:22]                                  | PA trims from VM 107 bytes 1-4 (widths: 3, 3, 3, 4 bits)                                             | M:s            |
| `0x1192C` | WL_PA2        | [2:0]                                                            | PA trim from VM 107 byte 5                                                                           | M:s            |
| `0x11930` | WL_XOSC       | [16:13] xosc_l, [22:19] xosc_r                                   | Crystal oscillator load trims from VM 106 (4 bits each; stored 0x1F lands as 0xF)                    | M:t, M:s       |
| `0x11934` | WL_VCO_EN     | [24] pulsed per VCO scan step                                    | VCO enable control                                                                                   | M:t, M:s       |
| `0x11938` | WL_VCO_BAND   | [25:19] band select; [16], [18] pulsed per step                  | VCO band selection for scan                                                                          | M:t, M:s       |
| `0x1193C` | WL_VCO_FINE   |                                                                  | VCO fine tuning code                                                                                 | M:s            |
| `0x11940` | WL_A40        |                                                                  | Post-scan analog setup (constant)                                                                    | M:t            |
| `0x11950` | WL_A50        |                                                                  | Written 32 times around LUT load (constant)                                                          | M:t            |
| `0x11954` | WL_A54        |                                                                  | Post-scan analog setup                                                                               | M:t            |
| `0x1195C` | WL_A5C        | bytes 0-3 from VM 187 bytes 40-43                                | Filter calibration result (boot 1: live; boot 2: from VM 187)                                        | M:t, M:s       |
| `0x11960` | WL_A60        | bytes 0-3 from VM 187 bytes 44-47                                | Filter calibration result                                                                            | M:t, M:s       |
| `0x11964` | WL_A64        |                                                                  | Analog init (written near end of sequence)                                                           | M:t            |
| `0x11968` | WL_CAL_START  | [28] start measurement; [0] filter cal start                     | Calibration trigger                                                                                  | M:t, M:s       |
| `0x11978` | WL_CAL_STROBE | [17] comparator low; [18] comparator high; [15:8] filter result  | VCO comparator readout and filter calibration result                                                 | M:t, M:s       |

### 4.2 Wireless control block: `0x14000-0x140A0`

System-level wireless control: bus gating, clock gating, reset, and the RF-die SPI interface [M:t, M:hw].

**Bus gate discovery [M:hw]:** WL_CTL0 (`0x14000`) gates the `0x30000`+ MAC register address space.
When WL_CTL0 is zero (default after reset), any read to `0x30000`+ causes a bus fault (confirmed:
probe v13 crashed on `0x31004` read with WL_CTL0=0). Writing `0xFFFF` to WL_CTL0 (reads back
`0xF103`) enables MAC register access (confirmed: probe v18 read `0x31004`=`0x0C` after gate).
WL_CTL8 (`0x14008`) is also fully writable (`0xFFFF` sticks) and may participate in gating [M:hw].

Full hardware scan of `0x14000`-`0x140A0` (probes v17, v18). Registers reading non-zero after WiFi
clock/reset init are marked; zero-reading addresses between non-zero ones may be reserved or
mode-dependent [M:hw].

| Address   | Name       | HW value     | Bits                                                                     | Function                                                                                      | Tag           |
|-----------|------------|--------------|--------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------|---------------|
| `0x14000` | WL_CTL0    | `0x00000000` | [15:0] writable; write `0xFFFF` reads `0xF103`; write `0xBEEF` reads `0xB003` | **MAC bus gate.** Must be written non-zero before accessing `0x30000`+ registers. Bit field semantics unknown beyond gating function | M:t, M:hw |
| `0x14004` |            | `0x00000000` |                                                                          | Unknown (reads zero) [M:hw]                                                                   | M:hw          |
| `0x14008` | WL_CTL8    | `0x00000000` | [15:0] fully writable; write `0xFFFF` reads `0xFFFF`                     | WiFi control. May participate in bus gating. Written during clock/reset init                   | M:s, M:hw     |
| `0x1400C` | WL_ID      | `0x5B000000` |                                                                          | Unknown. Stable non-zero value across boots; possibly chip/block version ID                    | M:hw, I       |
| `0x14010`-`0x14024` |  | `0x00000000` |                                                                          | Zero (7 registers) [M:hw]                                                                     | M:hw          |
| `0x14028` | RFSPI_CTL  | `0x00010000` | [22:20] busy; [16] start; [15:8] address; [7:0] command; [4] kick toggle | RF-die SPI control port. Reads `0x10000` (start bit set) at rest. Commands: 0xE = LUT word 0 write, 0xD = LUT word 1 write, 0x6 = read | M:t, M:s, M:hw |
| `0x1402C` | RFSPI_DATA | `0x00000000` | [31:0]                                                                   | RF-die SPI data (written before CTL)                                                          | M:t, M:hw     |
| `0x14030`-`0x1403C` |  | `0x00000000` |                                                                          | Zero (4 registers, includes RFSPI_WAIT at `0x14034`) [M:hw]                                   | M:hw          |
| `0x14040` | WL_CLK0    | `0x0000014F` |                                                                          | WiFi clock control. Non-zero at boot (set by WiFi driver or BLE RF init)                      | M:t, M:s, M:hw |
| `0x14044` | WL_CLK1    | `0x00000000` |                                                                          | WiFi clock control                                                                            | M:t, M:s, M:hw |
| `0x14048` | WL_CLK2    | `0x00000000` |                                                                          | WiFi clock control                                                                            | M:t, M:s, M:hw |
| `0x1404C` | WL_CLK3    | `0x01000000` |                                                                          | WiFi clock control                                                                            | M:t, M:s, M:hw |
| `0x14050` | WL_CLK4    | `0x00480001` |                                                                          | WiFi clock control (bit 0: SDIO/bus clock enable)                                             | M:t, M:s, M:hw |
| `0x14054` | WL_CFG54   | `0x82FF82FF` |                                                                          | Unknown. Symmetric 16-bit halves; possibly calibration mask or filter config                   | M:hw, I       |
| `0x14058` | WL_CFG58   | `0x00FF82FF` |                                                                          | Unknown. Similar pattern to `0x14054`; upper half differs                                     | M:hw, I       |
| `0x1405C` | WL_RST     | `0x00000001` | [0] reset state: 0=asserted, 1=de-asserted                              | WiFi reset control. Sequence: write 0 (assert), delay 10 us, write 1 (de-assert), delay 100 us | M:t, M:s, M:hw |
| `0x14060` | WL_CFG60   | `0x00480000` |                                                                          | Unknown. Upper bits match WL_CLK4 pattern; possibly related clock/power config                 | M:hw, I       |
| `0x14064`-`0x1407C` |  | `0x00000000` |                                                                          | Zero (7 registers) [M:hw]                                                                     | M:hw          |
| `0x14080` | WL_CFG80   | `0x00000800` |                                                                          | Unknown                                                                                       | M:hw          |
| `0x14084` | WL_CFG84   | `0x0000000E` |                                                                          | Unknown                                                                                       | M:hw          |
| `0x14088` | WL_CFG88   | `0x000001FF` |                                                                          | Unknown. 9 low bits set; possibly a mask or channel bitmap                                    | M:hw, I       |
| `0x1408C` |            | `0x00000000` |                                                                          | Zero [M:hw]                                                                                   | M:hw          |
| `0x14090` | WL_CFG90   | `0x0000234C` |                                                                          | Unknown                                                                                       | M:hw          |
| `0x14094`-`0x140A0` |  | `0x00000000` |                                                                          | Zero (4 registers) [M:hw]. End of scanned range; registers beyond `0x140A0` untested          | M:hw          |

### 4.3 Wi-Fi MAC / BBP window: `0x30000-0x31FFF`

The Wi-Fi MAC and baseband processor (BBP) register space [M:t].

| Address   | Name     | Function                                                                     | Tag      |
|-----------|----------|------------------------------------------------------------------------------|----------|
| `0x30308` | MAC_308  | MAC configuration (part of first BBP load)                                   | M:t      |
| `0x3030C` | MAC_30C  | MAC configuration                                                            | M:t      |
| `0x30F00` | RF_CFG0  | Radio configuration (written during clock/reset init group 3)                | M:t, M:s |
| `0x30F04` | RF_CFG1  | Radio configuration (also used in SDIO DMA and RX path)                      | M:t, M:s |
| `0x30F10` | MAC_F10  | MAC configuration                                                            | M:s      |
| `0x30F14` | MAC_F14  | MAC configuration                                                            | M:s      |
| `0x30F18` | MAC_F18  | MAC configuration                                                            | M:s      |
| `0x30F1C` | MAC_F1C  | MAC configuration                                                            | M:s      |
| `0x31004` | MAC_SYS_CTRL | MAC system control. HW probe: `0x0000000C` (bits 2+3 = TX_EN + RX_EN per RT2800). **Requires WL_CTL0 bus gate** | M:t, M:hw, IP:RT2x00 |
| `0x3101C` | BBP_PORT | BBP indirect access port: [17] start, [16] read, [15:8] register, [7:0] data | M:t, M:s |
| `0x31100` | MAC_1100 | MAC configuration                                                            | M:t      |
| `0x31104` | MAC_1104 | MAC configuration                                                            | M:t      |
| `0x31330` | MAC_1330 | MAC configuration                                                            | M:t      |
| `0x31334` | MAC_1334 | MAC configuration                                                            | M:t      |
| `0x31338` | MAC_1338 | MAC configuration                                                            | M:t      |
| `0x3133C` | MAC_133C | MAC configuration                                                            | M:t      |
| `0x31340` | MAC_1340 | MAC configuration                                                            | M:t      |
| `0x31344` | MAC_1344 | MAC configuration                                                            | M:t      |
| `0x31348` | MAC_1348 | MAC configuration                                                            | M:t      |

### 4.4 Radio priority (RFPRIO)

Arbitrates the shared radio between BT, BLE, and WiFi [M:t, M:s].

| Address | Name       | Function               | Tag      |
|---------|------------|------------------------|----------|
| `0x05E` | RFPRIOSTAT | Radio priority status  | M:t      |
| `0x060` | RFPRIOCNTL | Radio priority control | M:t, M:s |

Values observed: 17 (scanning), 26 (initiating/idle), 28 (connection data),
30 (connection instants/control) [M:t].

### 4.5 Clock and system registers

| Address   | Name     | Function                                       | Tag      |
|-----------|----------|------------------------------------------------|----------|
| `0x10008` | SYS_CLK1 | System clock configuration                     | M:t      |
| `0x10010` | SYS_CLK2 | System clock configuration (WiFi clock source) | M:t, M:s |

---

## 5. RF-die LUT (via SPI at `0x14028`/`0x1402C`)

256-entry lookup table on the RF die, programmed via SPI [M:t]. Two words per entry [M:s].

| Command | Function                   | Tag |
|---------|----------------------------|-----|
| `0xE`   | Write LUT word 0 for entry | M:s |
| `0xD`   | Write LUT word 1 for entry | M:s |
| `0x6`   | Read LUT entry             | M:s |

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

| Window | Commit | Entry addr | Data   | Read-back | Entry range | Boot transactions | Tag      |
|--------|--------|------------|--------|-----------|-------------|-------------------|----------|
| C      | `0xC8` | `0xC9`     | `0xCA` | `0xCB`    | 0x00-0x7A   | 893               | M:t, M:s |
| C'     | `0xCC` | `0xCD`     | `0xCE` | --        | 0x00-0x47   | 73                | M:t, M:s |
| D      | `0xD0` | `0xD1`     | `0xD2` | `0xD3`    | 0x00-0x5F   | 101               | M:t, M:s |
| D'     | `0xD4` | `0xD5`     | `0xD6` | `0xD7`    | 0x00-0x1C   | 27                | M:t, M:s |

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

| Phase                    | Window       | Writes        | Read-backs     | Notes                                   | Tag |
|--------------------------|--------------|---------------|----------------|-----------------------------------------|-----|
| Group 4 (first BBP load) | C, C', D, D' | ~3,300        | 0              | Constant table data                     | M:t |
| Group 10 (second phase)  | C, D         | ~4,600 writes | 539 read-backs | D read-backs are DC/IQ trim calibration | M:t |

---

## 7. CPU-to-WiFi MAC register interface

**ERRATA (2026-10-28):** the previous version of this section described an SDIO controller at
`0x50000`. That was incorrect: `0x50000` / `0x50008` / `0x5000C` are **GPIO Port A** registers
(`PA_OUT`, `PA_DIR`, `PA_DIE`) per `fm1_gpio.h` [M:t, M:hw]. The AC791N does not use an SDIO bus
for CPU-to-WiFi communication. The vendor's `wl_sdio.c` naming is a software abstraction borrowed
from the Linux WiFi driver stack; the actual interface is direct memory-mapped registers [M:s, I].

**ERRATA (2026-10-28, probes v15-v18):** the DMA register range `0x0200`-`0x0298` listed in the
vendor binary is **open bus** on real hardware [M:hw]. The actual DMA registers are at base
`0x40000` + RT2800 offset, confirmed by probes v21-v23c [M:hw].

The CPU accesses the WiFi MAC engine through direct memory-mapped registers, but access to the
`0x30000`+ MAC space requires a **bus gate** at WL_CTL0 (`0x14000`):

1. **Bus gate**: WL_CTL0 at `0x14000` must be written non-zero before MAC registers are accessible [M:hw]
2. **MAC / BBP control**: `0x30000` + RT2800 offset (documented in §4.3). Confirmed readable after bus gate [M:hw]
3. **DMA / interrupt engine**: `0x40000` + RT2800 offset. **Confirmed** [M:hw v23c]. Control registers live, TX/RX ring pointers zeroed (uninitialized), writes effective (crash proof from v23b)

Clock and reset are at `0x14040`-`0x1405C` (documented in §4.2) [M:t, M:s, M:hw].

### 7.1 DMA / interrupt registers: base `0x40000`

**CONFIRMED [M:hw v21-v23c].** The DMA/interrupt engine uses RT2800 register offsets from base
`0x40000` (not `0x0000` as in standard RT2800, and not `0x0200` as seen in vendor binary [M:s]).

The vendor binary references offsets `0x0200`-`0x0298` [M:s], which are **open bus** on real hardware
[M:hw v15-v16]. The vendor's software abstraction layer likely adds the `0x40000` base at runtime.

**Evidence chain:**
- v21 [M:hw]: wide DMA hunt found non-zero value at `0x40200` (0x809803B5), all other tested bases returned zero
- v22 [M:hw]: deep scan showed `0x40000`-`0x40100` = zeros, but `0x40200`/`0x40204`/`0x40208` non-zero
- v23c [M:hw]: full read of RT2800 DMA offsets at `0x40000` base: control regs non-zero, TX/RX ring pointers zero (uninitialized)
- v23b [M:hw]: write to `0x40230` (TX_BASE_PTR0) with garbage value caused immediate bus fault, proving writes reach the DMA engine
- WPDMA_GLO_CFG (`0x40208`) value changed between boots (0xF1FE vs 0x61FE), confirming live register

| Address   | RT2800 offset | RT2800 name        | HW value (v23c)     | Function                              | Tag                    |
|-----------|---------------|---------------------|---------------------|---------------------------------------|------------------------|
| `0x40200` | `0x0200`      | INT_SOURCE_CSR      | `0x809803B5`        | Interrupt source (W1C to ACK)         | M:hw, IP:RT2x00        |
| `0x40204` | `0x0204`      | INT_MASK_CSR        | `0x00000001`        | Interrupt mask                        | M:hw, IP:RT2x00        |
| `0x40208` | `0x0208`      | WPDMA_GLO_CFG       | `0x000061FE` (varies) | DMA global config (enable TX/RX DMA) | M:hw, IP:RT2x00        |
| `0x4020C` | `0x020C`      | WPDMA_RST_IDX       | `0x00004000`        | DMA ring index reset                  | M:hw, IP:RT2x00        |
| `0x40210` | `0x0210`      | DELAY_INT_CFG       | `0x0000001F`        | Interrupt coalescing / delay          | M:hw, IP:RT2x00        |
| `0x40230` | `0x0230`      | TX_BASE_PTR0        | `0x00000000`        | TX ring 0 base address (DMA desc)     | M:hw, IP:RT2x00        |
| `0x40234` | `0x0234`      | TX_MAX_CNT0         | `0x00000000`        | TX ring 0 max descriptor count        | M:hw, IP:RT2x00        |
| `0x40238` | `0x0238`      | TX_CTX_IDX0         | `0x00000000`        | TX ring 0 CPU index (next to submit)  | M:hw, IP:RT2x00        |
| `0x4023C` | `0x023C`      | TX_DTX_IDX0         | `0x00000000`        | TX ring 0 DMA index (last completed)  | M:hw, IP:RT2x00        |
| `0x40240` | `0x0240`      | TX_BASE_PTR1        | `0x00000000`        | TX ring 1 base address                | M:hw, IP:RT2x00        |
| `0x40290` | `0x0290`      | RX_BASE_PTR         | `0x00000000`        | RX ring base address (DMA desc)       | M:hw, IP:RT2x00        |
| `0x40294` | `0x0294`      | RX_MAX_CNT          | `0x00000000`        | RX ring max descriptor count          | M:hw, IP:RT2x00        |
| `0x40298` | `0x0298`      | RX_CRX_IDX          | `0x00000000`        | RX ring CPU read index                | M:hw, IP:RT2x00        |
| `0x4029C` | `0x029C`      | RX_DRX_IDX          | `0x00000000`        | RX ring DMA read index                | M:hw, IP:RT2x00        |
| `0x40300` | `0x0300`      | (past DMA range)    | `0x00000003`        | Non-zero; may be PBF region           | M:hw                   |

TX/RX ring pointers are all zero because probes run before the WiFi driver programs DMA descriptors.
The remaining TX queues (2-5) at `0x40260`-`0x40288` are expected to follow the same layout [IP:RT2x00, I].

**Address mapping rule:** AC791N DMA address = `0x40000` + RT2800 offset [M:hw, IP:RT2x00].

### 7.2 MAC control register addressing

MAC control registers reside at `0x30000` + RT2800 offset [M:t, M:hw, IP:RT2x00].

| Address   | RT2800 offset | RT2800 name      | HW value (v23c)     | Tag                     |
|-----------|---------------|-------------------|---------------------|-------------------------|
| `0x31000` | `0x1000`      | CHIP_VERSION      | `0x00300100`        | M:hw, IP:RT2x00         |
| `0x31004` | `0x1004`      | MAC_SYS_CTRL      | `0x0000000C`        | M:hw, IP:RT2x00         |
| `0x3101C` | `0x101C`      | BBP_CSR_CFG       | `0x0008D000`        | M:hw, IP:RT2x00         |
| `0x31100` | `0x1100`      | XIFS_TIME_CFG     | `0x33A40C0B`        | M:hw, IP:RT2x00         |
| `0x31104` | `0x1104`      | BKOFF_SLOT_CFG    | `0x00140314`        | M:hw, IP:RT2x00         |
| `0x31330` | `0x1330`      | TX_SW_CFG0        | `0x00040804`        | M:hw, IP:RT2x00         |
| `0x30300` | `0x0300`      | MAC_ADDR_DW0      | `0x00007001`        | M:hw, IP:RT2x00         |
| `0x30304` | `0x0304`      | MAC_ADDR_DW1      | `0x00000000`        | M:hw, IP:RT2x00         |
| `0x30308` | `0x0308`      | (MAC config)      | `0x00009407`        | M:hw, IP:RT2x00         |
| `0x30F00` | `0x0F00`      | (RF config)       | (untested on HW)    | M:t, M:s                |

**Address mapping rule:** AC791N MAC address = `0x30000` + RT2800 offset [M:hw, IP:RT2x00].

### 7.3 Address region summary

| Region            | Address range         | Base       | RT2800 equivalent        | HW status                              | Tag                    |
|-------------------|-----------------------|------------|--------------------------|----------------------------------------|------------------------|
| DMA / interrupts  | `0x40200`-`0x4029C`  | `0x40000`  | WPDMA, INT_SOURCE/MASK   | **Confirmed** [M:hw v23c]              | M:hw, IP:RT2x00        |
| DMA (legacy)      | `0x0200`-`0x0298`    | raw `0`    | (vendor binary refs)     | **Open bus** [M:hw v15-v16]            | M:s, IP:RT2x00         |
| Bus gate          | `0x14000`-`0x14008`  | (JieLi)    | (no RT2800 equivalent)   | **Confirmed** [M:hw v18]               | M:t, M:s, M:hw        |
| MAC / BBP control | `0x30000`-`0x31FFF`  | `0x30000`  | MAC_SYS_CTRL, BBP_CSR    | **Confirmed** (after bus gate) [M:hw]  | M:hw, IP:RT2x00       |
| RF analog         | `0x11900`-`0x11978`  | (JieLi)    | (no RT2800 equivalent)   | Untested on HW                         | M:t                    |
| WL control        | `0x14000`-`0x140A0`  | (JieLi)    | (no RT2800 equivalent)   | **Confirmed** [M:hw v17-v18]           | M:t, M:s, M:hw        |
| Clock / reset     | `0x14040`-`0x1405C`  | (JieLi)    | (no RT2800 equivalent)   | **Confirmed** [M:hw v15-v17]           | M:t, M:s, M:hw        |
| BBP indirect      | via `0x3101C`        | `0x30000`  | BBP_CSR_CFG (0x101C)     | Read confirmed [M:hw v23c]             | M:hw, IP:RT2x00       |

**Base address splitting:** JieLi split the RT2800 IP block across two MMIO bases: MAC/BBP at
`0x30000`, DMA/interrupts at `0x40000`. Both use RT2800 register offsets. This is a common SoC
integration pattern for licensed IP [I].

### 7.4 Initialization sequence (revised)

The WiFi MAC initialization follows this order [M:s, M:hw]:

1. Clock and reset init via `0x14040`-`0x1405C` (WiFi clock gates and reset) [M:t, M:hw]
2. **Bus gate**: write WL_CTL0 (`0x14000`) non-zero to enable `0x30000`+ and `0x40000`+ access [M:hw]. Write WL_CTL8 (`0x14008`) as needed [M:hw]. **Without this step, MAC/DMA reads will bus fault** [M:hw]
3. MAC init: configure MAC_SYS_CTRL (`0x31004`) and related registers [M:t, M:hw, IP:RT2x00]
4. DMA init: program TX/RX ring base pointers and counts at `0x40230`+/`0x40290`+, configure WPDMA_GLO_CFG (`0x40208`) to enable DMA [M:hw, IP:RT2x00]
5. Interrupt setup: configure INT_MASK_CSR (`0x40204`) to enable desired interrupts [M:hw, IP:RT2x00]
6. RF common init (full RF analog bring-up, §11) [M:t]

### 7.5 DMA descriptor format

The RT2800 uses scatter-gather DMA with ring buffers [IP:RT2x00]. Each TX descriptor contains:
base address, length, and control fields. The exact AC791N descriptor format has not been confirmed
by probe; the RT2800 format is the working hypothesis [IP:RT2x00, I].

RT2800 TX descriptor (from Linux rt2800.h):
- Word 0: `SD_PTR0` (scatter buffer 0 pointer)
- Word 1: `SD_LEN0` [15:0], `LAST_SEC0` [16], `BURST` [17], `SD_LEN1` [31:18] (or similar)
- Word 2: `SD_PTR1` (scatter buffer 1 pointer)
- Word 3: `LAST_SEC1` [16], `DMA_DONE` [17]

**Needs confirmation by hardware probe** [I].

### 7.6 IRQ handling

The MAC signals events via INT_SOURCE_CSR (`0x40200`) [M:hw, IP:RT2x00]. The vendor registers an
interrupt handler that reads this register, ACKs by writing back the active bits (W1C), and defers
processing to a background task [M:s]. The interrupt mask at `0x40204` controls which events
generate interrupts [M:hw, IP:RT2x00].

Observed values [M:hw v23c]: INT_SOURCE_CSR = `0x809803B5` (multiple pending interrupts),
INT_MASK_CSR = `0x00000001` (only bit 0 enabled).

RT2800-compatible interrupt bits [IP:RT2x00, I]:
- Bit 0: TX done (ring 0)
- Bit 1: TX done (ring 1)
- Bit 2: RX done
- Bit 6: TX status (TXFIFO)
- Other bits: per RT2800 documentation

### 7.7 TX/RX data path

Based on the DMA ring architecture [M:s, IP:RT2x00]:

- **TX**: CPU writes frame into a TX descriptor buffer, updates TX_CTX_IDX to advance the ring.
  MAC DMA fetches the descriptor, transmits, advances TX_DTX_IDX, and raises TX-done interrupt.
- **RX**: MAC DMA writes received frames into pre-posted RX descriptor buffers, advances
  RX_DRX_IDX (read-only by CPU), and raises RX-done interrupt. CPU reads frames up to RX_CRX_IDX.
- **Rate control**: per-packet TX rate selection via a rate control table [M:s].

### 7.8 What needs hardware probes

**Confirmed by probes v14-v23c [M:hw]:**

| Fact | Result | Probe | Tag |
|------|--------|-------|-----|
| **DMA at `0x40000` base** | **Yes.** Control regs non-zero, TX/RX rings zero, writes crash (effective) | v21-v23c | M:hw, IP:RT2x00 |
| MAC_SYS_CTRL (`0x31004`) readable | Yes, returns `0x0C` (TX_EN + RX_EN per RT2800) | v18, v23c | M:hw, IP:RT2x00 |
| CHIP_VERSION (`0x31000`) | `0x00300100` | v19, v23c | M:hw |
| BBP_CSR_CFG (`0x3101C`) readable | Yes, returns `0x0008D000` | v19, v23c | M:hw, IP:RT2x00 |
| MAC_ADDR_DW0 (`0x30300`) | `0x00007001` | v23c | M:hw |
| XIFS_TIME_CFG (`0x31100`) | `0x33A40C0B` | v23c | M:hw, IP:RT2x00 |
| BKOFF_SLOT_CFG (`0x31104`) | `0x00140314` | v23c | M:hw, IP:RT2x00 |
| TX_SW_CFG0 (`0x31330`) | `0x00040804` | v23c | M:hw, IP:RT2x00 |
| WL_CTL0 (`0x14000`) is bus gate | Yes, `0x30000`+/`0x40000`+ fault without it | v13 vs v18 | M:hw |
| DMA range (`0x0200`-`0x0210`) | **Open bus** (vendor binary offset, not real address) | v15-v16 | M:hw |
| `0x50000` is SDIO controller | **No, it is GPIO PA_OUT** | v14 | M:hw |
| Clock regs (`0x14040`-`0x1405C`) | Non-zero values confirmed | v15, v17 | M:hw |
| WL control block (`0x14000`-`0x140A0`) | 13 non-zero registers mapped | v17, v18 | M:hw |
| `0x34000`+ block | SRAM/flash (high-entropy, not registers) | v21, v22 | M:hw |
| `0x14090`/`0x14080` indirect port | **Disproven**: independent registers, not addr/data pair | v22 | M:hw |

**Still unconfirmed (lower priority, not blocking WiFi STA):**

| Fact                              | Hypothesis source | Notes                                                  |
|-----------------------------------|-------------------|--------------------------------------------------------|
| DMA descriptor format             | [IP:RT2x00]       | RT2800 format is working hypothesis; confirm during TX/RX bring-up |
| BBP indirect write protocol       | [IP:RT2x00]       | Read confirmed; write protocol needed for calibration |
| RF analog block (`0x11900`+)      | [M:t]             | Read on real HW to confirm emulator model accuracy |
| WL_CTL0 minimal gate bits         | [M:hw]            | Which bit(s) enable MAC/DMA access? |
| TX queues 2-5                     | [IP:RT2x00]       | Expected at `0x40260`-`0x40288`, untested |

---

## 8. OS abstraction requirements

The WiFi MAC/PHY hardware expects software to provide frame buffer management, interrupt deferral,
synchronization, and heap allocation [M:s]. This section describes the behavioral contracts the
hardware interaction requires, not how any particular driver implements them. The implementer is
free to choose any API design.

### 8.1 Frame buffer management [M:s]

The MAC engine exchanges frames with software via DMA. Software must provide:

- **Allocate**: reserve a contiguous buffer (up to MTU + headers) with metadata tracking head, tail,
  and length [M:s].
- **Prepend/trim**: adjust the data region within the buffer (for adding/removing protocol headers)
  [M:s].
- **Copy**: both shallow (shared data, reference-counted) and deep copies for frame duplication [M:s].
- **Free**: release the buffer when no longer referenced [M:s].

For bare-metal single-threaded operation, reference counting can be simplified to a pool allocator
with immediate free [I].

### 8.2 Link state signaling [M:s]

The MAC needs to signal carrier up/down and control TX flow (start/stop/wake) to the network stack
integration layer [M:s].

### 8.3 Deferred work [M:s]

The MAC interrupt path must defer non-critical processing outside IRQ context [M:s]. On bare-metal,
this maps to a deferred-work queue polled from the main loop [I].

### 8.4 Synchronization [M:s]

The hardware interaction requires interrupt-safe critical sections (disable/restore IRQ state) and
a mechanism to block until a condition becomes true (for bus operations and scan completion) [M:s].
On bare-metal, critical sections are interrupt disable/enable pairs; blocking waits are polled
flags [I].

### 8.5 Memory [M:s]

Heap allocation and free for dynamic structures (DMA descriptors, state) [M:s].

---

## 9. BLE/WiFi coexistence

### 9.1 RFPRIO hardware arbitration

The radio priority controller at `0x05E`/`0x060` manages time-division access to the shared radio
[M:t]. Functions in the BT controller set priority levels for different operations [M:s]:

| Priority user | Operation                          | Tag |
|---------------|------------------------------------|-----|
| BR/EDR frame  | ACL, SCO links                     | M:s |
| BLE           | Advertising, scanning, connections | M:s |
| WiFi          | STA activity (when implemented)    | M:s |

### 9.2 WiFi detection by BLE

The BLE link layer includes a vendor-specific WiFi detection subsystem that probes for WiFi
activity to enable adaptive frequency hopping (AFH) [M:s]. This uses custom LL control PDUs [M:s]. For STA-only WiFi
mode, this subsystem can remain in its current form: the BLE
side detects WiFi activity and adapts its channel map [I].

### 9.3 Shared RF analog

Both BLE and WiFi require the RF analog block to be initialized [M:t]. Key cross-module
dependencies [M:s]:

- WiFi analog init is called during BLE RF bring-up (WiFi analog must init before BLE)
- A shared PHY timing counter is used by both BLE and WiFi stacks
- WiFi registers are saved and restored across deep sleep

The BT controller directly accesses WiFi registers: `0x11924` (WL_PA0), `0x11950` (WL_A50),
`0x14000` (WL_CTL0), `0x10010` (SYS_CLK2) [M:s].

---

## 10. Stored calibration (VM records)

Factory calibration data stored in the SDK's VM (key-value store) [M:d]. On a real FM-1, the VM is
at flash `0x0E8000` [M:hw] (not `0x093000` as in the emulator [M:d]).

| VM ID | Size | Content                                        | Drives                                                                                             | Tag      |
|-------|------|------------------------------------------------|----------------------------------------------------------------------------------------------------|----------|
| 106   | 2 B  | `xosc_l`, `xosc_r` (crystal load trims)        | `0x11930` bits [16:13], [22:19]                                                                    | M:t, M:s |
| 107   | 7 B  | PA_C_I values (7 calibration bytes)            | `0x11924` [26:24], `0x11928` [2:0]/[11:9]/[17:15]/[25:22], `0x1192C` [2:0], plus 32 LUT words each | M:t, M:s |
| 108   | 20 B | WiFi PA per-rate digital gain                  | 12 BBP window-D entries                                                                            | M:s      |
| 187   | 68 B | Full calibration cache (64 B payload + CRC-16) | Window-D/D' entries, `0x11908`, `0x1191C`, `0x11920`, `0x1195C`, `0x11960`, LUT word patches       | M:t, M:s |

Record format: 4-byte header (CRC-16/XMODEM low byte, ID, length) + data [M:s].
VM 187 has an inner CRC-16 at bytes 64-65 over the 64-byte payload [M:s].

See BLE-HW-FACTS.md §14 and §16.4 for the complete byte-to-register mapping.

---

## 11. RF init sequence (boot-2 order)

The full RF initialization as measured in the emulator (BLE-HW-FACTS.md §16.1) [M:t]. Boot-2 =
stored trims present (the normal path). Total: 34,274 register writes [M:t].

| Group | Description                                   | Writes | Source                                 | Tag |
|-------|-----------------------------------------------|--------|----------------------------------------|-----|
| 1     | Clocks (`0x10010`, `0x10008`)                 | 5      | Constant                               | M:t |
| 2     | First analog word `0x11900`                   | 8      | Constant (bit 14 preserved)            | M:t |
| 3     | WiFi clock/reset, radio config                | 37     | Constant                               | M:t |
| 4     | BBP + MAC first load (3,300 BBP transactions) | 6,624  | Constant                               | M:t |
| 5     | Crystal trim `0x11930`                        | 16     | VM 106                                 | M:t |
| 6     | WiFi analog init (20 registers)               | 269    | Constant                               | M:t |
| 7     | VCO bank scan                                 | ~380   | **Live every boot**                    | M:t |
| 8     | Post-scan analog setup                        | ~90    | Mixed: VM 187 + constant               | M:t |
| 9     | PA trims                                      | 6      | VM 107                                 | M:t |
| 10    | BBP second phase (reload + RMW)               | 13,901 | Tables constant; RMW loop live         | M:t |
| 11    | RF-die LUT (2 x 256 words)                    | ~6,100 | Constant base + VM 107/187/VCO patches | M:t |
| 12    | Final analog writes                           | ~50    | Constant (except VM 107)               | M:t |
| 13    | VM 108 -> BBP window D                        | 36     | VM 108                                 | M:t |
| 14    | BT block                                      | 187    | Constant + live BT TX trims            | M:t |

### 11.1 What runs live on every boot

- VCO bank scan: binary search over 64 bands, comparator at `0x11978` bits 17/18 [M:t]
- BBP window-D read-modify-write loop: 539 read-backs (DC/IQ trimming) [M:t]
- BT TX trims: from BBP read-backs into `0x2FC08`/`0x2FC10` [M:t]
- Bit 14 of `0x11900`: periodic routine, not a trim [M:t]

---

## 12. Datasheets

| Document                                | Location                                          | Tag       |
|-----------------------------------------|---------------------------------------------------|-----------|
| AC7916A Datasheet V2.3                  | `docs/hardware/AC7916A_Datasheet_V2.3.pdf`        | published |
| AC791N Hardware Design Guide            | `docs/hardware/AC791N_Hardware_Design_Guide.pdf`  | published |
| BLE Hardware Facts (reverse-engineered) | `firmware/upstream/optimist/docs/BLE-HW-FACTS.md` | M:t, M:s  |

The AC7916A datasheet covers the chip family. The AC791N hardware design guide covers board-level
integration (crystal, antenna, power). Neither document provides the register-level detail found in
BLE-HW-FACTS.md, which was reverse-engineered from emulator traces [M:t].

The JieLi SDK repository (`fw-AC79_AIoT_SDK` on Gitee) contains additional Chinese-language
documentation under `doc/datasheet/AC791N规格书/` but these primarily cover the application-level
SDK API, not register maps [M:s].

---

## 13. References

- `firmware/upstream/optimist/docs/BLE-HW-FACTS.md`: primary hardware reference (21 sections, 1,749 lines of measured
  register behavior)
- `firmware/hal/fm1_ble_rf.h`: RF init implementation in core-fm1
- `firmware/hal/fm1_ble.h`: BLE subsystem HAL
- Linux RT2x00 driver (`drivers/net/wireless/ralink/rt2x00/rt2800.h`): GPL-2.0+ Tier 1 reference
  for RT2800 DMA/interrupt/MAC register map. IP ancestor of the AC791N WiFi MAC (see
  `docs/hardware/AC791N_IP_ANCESTRY.md` §3)
- `docs/hardware/AC791N_IP_ANCESTRY.md`: IP ancestry analysis for all AC791N subsystems
- JieLi SDK: `https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK` (binary drivers,
  Chinese docs, example applications)
- IEEE 802.11-2020: WLAN standard (MAC, PHY)
- FIPS 197 (AES), FIPS 180-4 (SHA-1), RFC 2104 (HMAC), RFC 2898 (PBKDF2): crypto standards
- IEEE 802.11i: WPA2/CCMP security
