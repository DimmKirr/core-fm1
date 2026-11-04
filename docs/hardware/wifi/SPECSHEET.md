# Wi-Fi hardware facts for the AC791N / WL82

Written 2026-10-25 by the clean-room reader for the GPL-3.0 Wi-Fi driver on the JieLi AC791N (WL82)
radio. **The implementers must not read vendor code, vendor IR or vendor disassembly.** They work from
this sheet, the IEEE 802.11 standard, and permitted open-source references (Ralink RT2x00
GPL-2.0+, lwIP 2.x BSD, wpa_supplicant BSD, ESP-IDF Apache 2.0).

This sheet contains facts only: addresses, bit fields, values, sequences, timing, interrupts, bus
protocols and observed behaviour. It contains no vendor code or pseudo-code. Long vendor tables (BBP
init, RF-die LUT, AGC) are described by location, size and capture method, not transcribed.

Update 2026-11-04 (reader pass): **[M:s wl_rf_common.a, wl_wifi_sta.a, cpu and BT controller
libraries; PUB WL82.h; M:hw 12a-12k]** A Tier 4 pass over the vendor WiFi libraries, every fact
re-checked in the binaries before it was recorded (source search record in §0). New: §4.2 rewritten
with bit semantics and the published register names; §4.2.1 is now the complete ordered bring-up
from clock tree to first transmit with a column comparing the GPL driver; §6.4-§6.9 (direct BBP
writes, window load order, AGC, per-channel behaviour, window D final state); §7.11.3-§7.11.6 and
the new §7.11.9 (queue selection, TX readiness gate, kick, status, RX notify, interrupt-driven
steps); new §14 "Facts awaiting hardware confirmation", ordered by how likely each explains the dead
TX/RX. **Errata** (old text kept and marked where it stood):
(1) `0x14000` is `WL_CON0`, a BT/WiFi radio arbitration and debug-mux control; the vendor never
writes it on a WiFi-only start and accesses the MAC with it at 0 (§4.2).
(2) `0x1405C` (`WF_CON7`, "WL_RST") is not a PLL divider/multiplier: bits [7:0] are the channel
code that indexes the RF-die LUT, [23:16] the TX power level (§4.2, old §4.2.1 step 3).
(3) `0x40000`-`0x40FFF` is the SoC high-speed bus (the flash controller sits at `0x40200`), not an
RT2800 WPDMA block; no WiFi code touches it (§7 header, §7.1, §7.3, §7.4, §7.6).
(4) The WDMA TX transfer starts at a 4-byte queue word in front of the TXWI, and its length is
MPDU length + 26; the measured length word is MPDU length + 22, not 16 + pad4 + 4 (§7.11.5).
(5) BBP R106 is a constant 4 written once per PHY init; the vendor writes no BBP register on a
channel change (§6.4, §6.8). BBP R94 (`0x5E`) is never written by WiFi code (§4.4).
(6) The FM-1 crystal is 24 MHz; the 40 MHz targets in §11.2.9 do not apply to this board.
(7) The five read-only indicators are never read, polled, set or cleared by any vendor code; they
are outputs of states the vendor reaches (§4.2). `0x1400C` is a low-power control/counter register,
not an ID register.
(8) The RT2800 names of several MAC table registers in §7.10 were wrong (`0x3042C`/`0x30430` are
BCN_OFFSET0/1, `0x3040C` PBF_MAX_PCNT, `0x30408` PBF_CFG, `0x31340` TXOP_CTRL_CFG, ...).
(9) All four BBP windows have a read-back register (§6.1, §6.3).
(10) `0x14080`-`0x14098` belong to an SD host controller, not to WiFi (§4.2).
Vendor function names that earlier revisions carried in §5.1, §6, §7.11, §10.1 and §11.2 were
replaced by behavioural descriptions; section numbers are unchanged.

Update 2026-11-04 (iteration 12i, WL_CTL0 release): **[M:hw]** WL_CTL0 (`0x14000`) is an
assert/release control, not a bus gate: writing 0 after init keeps the MAC readable and starts the
baseband CCA detector (false CCA 201-240 per 100 ms, BBP R60 live) while TX and the five indicators
stay unchanged. Every init so far ran on a held block. §4.2 row updated; new §7.{n} is the
stage-by-stage RX status with the open items.

Update 2026-11-03 (iteration 12h, SoC configuration probes): **[M:hw]** With every writable SoC-level
register at the vendor's steady-state value (SYS_CLK1, SYS_CLK2, WL_CLK0, WL_CLK3, `0x14054`, WL_RST,
`0x14080`-`0x14090`), five bits remain different and refuse writes: `0x1400C` b29, `0x14050` b23,
`0x14058` bits 22/21/7/1, `0x14060` b23, `WDMA_CTL` b16. They are read-only indicators of a running
MAC/BBP, not configuration (§4.2 rows and the paragraph before §4.2.1). SYS_CLK1 bit 12 is writable
and mirrors into `0x14090` bit 4 (§4.5). First direct BBP R0-R31 comparison recorded in §6.4.

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

Update 2026-10-31 (RT2800 init gap analysis + write-only registers): **[IP:RT2x00, M:hw]**
(1) Missing init steps identified from Linux rt2800mmio driver: PBF_SYS_CTRL reset, MAC/BBP reset,
WPDMA_RST_IDX. PBF confirmed at 0x40408 (write succeeds [M:hw]). RST_IDX write still crashes (even
with 0x1003f). (2) WPDMA_GLO_CFG was 0xE1FF at boot: BIG_ENDIAN set, BUSY bits stuck. Writing 0x30
(Linux default) fixed it: reads back 0x30, BUSY clears instantly [M:hw]. (3) **TX_BASE_PTR0 is
write-only [IP:RT2x00, M:hw].** RT5350/RT3052 datasheets document write-only DMA registers (read
returns 0 by design). Linux rt2800 driver never reads TX_BASE_PTR back. readback=0 is not an error.
The DMA engine uses the written value internally. (4) GLO_CFG BIG_ENDIAN was the real bug: with
byte-swapped DMA accesses, descriptors and TXWI were corrupted. Now clean at 0x35.
§7.1, §7.3, §7.4, §7.9 updated.

Update 2026-10-30 (WiFi bisect on real hardware): **[M:hw]** WL_CTL8 (`0x14008`) GPIO hazard.
Key findings: (1) Writing `0xFFFF` to WL_CTL8 kills the input shift register (74HC595 chain on
PA1/PA3/PA4): encoders and buttons stop responding. All other subsystems (scheduler, UI drawing,
USB) continue normally. (2) WL_CTL0 (`0x14000`) alone is sufficient for MAC/DMA bus gating:
full WiFi init sequence (rtos, rf, sdio, mac, sta, lwip, weather) and `wifi_poll()` in a
cooperative scheduler task all work without writing WL_CTL8. (3) Bisect sequence verified each
init stage independently: rtos (PASS), rf (PASS), sdio with WL_CTL8 (FAIL: input dead), sdio
without WL_CTL8 (PASS), mac (PASS), sta (PASS), lwip+weather (PASS), poll in task (PASS).
§4.2, §7.4, §7.8 updated.

Update 2026-10-31 (WDMA discovery): **[M:s]** Analysis of the vendor radio library reveals that
JieLi replaced the standard RT2800 WPDMA ring-based DMA engine with a custom "WDMA" block at MAC
offset `0xF00` (registers `0x30F00`-`0x30F1C`). The vendor's register bridge silently drops all
writes to WPDMA offsets (`0x200`-`0x29C`), and the DMA enable/disable routines are empty stubs.
All production TX/RX uses WDMA exclusively. The registers at `0x30F00`/`0x30F04` previously
identified as "RF_CFG0"/"RF_CFG1" are WDMA_CFG0/WDMA_CTL. WDMA is synchronous: one packet at a
time, no ring buffers, no descriptors. The WPDMA registers at `0x40000` base are hardware-present
[M:hw] but vendor-unused. The 2026-10-31 "write-only registers" finding remains valid for direct
MMIO (our probes bypass the vendor bridge), but the vendor never attempts WPDMA writes at all.
§4.2, §4.3, §7.1, §7.3, §7.7, §7.11 updated.

Update 2026-10-31 (WDMA RX enable): **[M:s, M:hw]** Hardware probe confirmed WDMA TX works (bit 20
fires) but RX never triggers (bit 17 stuck low). Vendor library trace reveals two missing init
steps: (1) MAC register `0x30328` (offset 808) must be written with value `2` to enable the MAC to
notify WDMA of received frames (part of the vendor's interrupt setup). (2) SoC register `0x14050` bit 0
must be set to enable the WiFi interrupt path (the vendor's interrupt unmask step). The vendor registers IRQ 52 with a
handler that clears 0x14050 bit 0, posts a semaphore, and the RX thread re-enables it after
processing. For polling mode, both registers can be set once during init.
§7.11.4, §7.11.8 updated.

Update 2026-10-31 (WiFi clock tree): **[M:s]** Root cause of WDMA RX failure: the WiFi clock tree
was severely incomplete. The vendor's clock/reset routine builds WL_CLK0 (`0x14040`) to `0x14F` (bits
0,1,2,3,6,8) in a specific sequence with inter-step delays, while our init only set bit 0 (`0x01`).
Missing bits 1,2,3,6,8 are PHY/BB/RF clock enables: without them the receiver has no clock tree.
WDMA TX worked because the WDMA engine has its own clock gate at `0x1404C` bit 24, but RF frames
cannot be received without the full clock tree. Additionally, WL_RST (`0x1405C`) must be configured
to `0x47000005` (PLL/divider config + deassert), not just `0x01` (simple deassert). The vendor also
clears SYS_CLK2 (`0x10010`) bits [15:14] before clock init to route the radio clock to WiFi (those
bits control BT/WiFi clock domain selection). §4.2, §4.5, §7.4 updated.

Update 2026-10-31 (WiFi PHY/MAC hardware init): **[M:s]** Root cause of zero-AP scan identified:
the vendor's WiFi hardware-init routine in the radio library performs extensive WiFi-specific PHY and RF
initialization AFTER the shared BLE RF bytecode (groups 1-14). Without it, the radio has correct
clocks but the baseband demodulator is not configured for 802.11 frame detection. Key findings:
(1) that routine runs PHY/MAC reset, SIFS timing, CCA register programming (~100 registers),
RX frontend (24 regs), RX 11n (72 regs), TX backend (97 regs), then RF transceiver init.
(2) The BBP write protocol at `0x3101C` is a two-phase sequence (bit 19 strobe + bit 17 kick),
not the single-write previously documented in §6.2. (3) CCA registers configure the Clear Channel
Assessment engine (STF detection, DSSS/OFDM demodulation, energy thresholds, AGC). (4) 10
calibration-dependent CCA registers load from external globals (STF thresholds, DSSS detection):
their values are unknown and must be determined by hardware probing. (5) RF transceiver init
writes to MMIO `0x11930`/`0x11900`/`0x11904`/`0x11908` with XOSC-dependent values. (6) The
full sequence is: reset, SIFS, reset, CFR calibration, PHY init, RF init, PHY init again,
CFR calibration, analog init, power/channel set.
§6.1, §6.2, §6.3, §11.2 updated.

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
| **[PUB]**        | published vendor material: the SDK's chip header `WL82.h` (register block names and bases), SDK application sources (`apps/common/net/wifi_conf.c`) and SDK headers. Added 2026-11-04 |

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

**Source search record for the 2026-11-04 reader pass.** Tier 1: the AC791N datasheet and design
guide contain no WiFi register map; the published SDK chip header `WL82.h` names the registers of
the `0x14000` block (`WL_CON0..7`, `LOFC_CON/RES`, `WL_ANL_CON0..3`, `WF_CON0..9`, `SD_CON0..2`,
`SD_CTU_CON/CNT`, `SD_CPTR/DPTR`) and places the high-speed bus at `0x40000` with the flash
controller at `0x40200` [PUB]; the published `wifi_conf.c` holds the 24 MHz RF-die LUT and the ten
CCA tunables [PUB]. Tier 2: the public RT2800 header (`rt2800.h`, `rt2800usb.h`) explains the MAC
registers, the TXWI/RXWI layouts and the USB TXINFO/RXINFO words that this integration reuses
[IP:RT2x00]; it does not cover the JieLi glue. Tier 3: twelve hardware iterations (12a-12k) left
every compared writable register equal to the vendor and the receiver dead; the emulator has no
MMIO trace of the vendor WiFi start. Tier 4 was therefore used for the ordered start sequence:
the vendor libraries `wl_rf_common.a` and `wl_wifi_sta.a` and the cpu and BT controller libraries
of the public SDK, disassembled with public LLVM tools; nothing encrypted or access-controlled was
bypassed. Facts from that pass carry [M:s] with the library named.

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
| `0x11900` | WL_A00        | [14] periodic routine bit (not a trim); [31:0] first analog word | Master analog control. Bit 14 toggled by a periodic routine (~2 ms); the RF init preserves it as found | M:t, meaning I |
| `0x11904` | WL_A04        |                                                                  | Analog configuration (constant, set during post-scan)                                                | M:t            |
| `0x11908` | WL_A08        | [7], [15], [17], [25]                                            | Trim fields from VM 187 bytes 32, 33, 36, 37                                                         | M:s            |
| `0x11910` | WL_A10        | [15:14] from VM 187 byte 42; [22:26] from T6 table               | Analog configuration with stored trims                                                               | M:s            |
| `0x11914` | WL_A14        | [17:15], [14:12], [7:5] from T6 table                            | Analog configuration                                                                                 | M:s            |
| `0x1191C` | WL_A1C        | [9:8]                                                            | Trim from VM 187 byte 24 (three RMW writes during init)                                              | M:s            |
| `0x11920` | WL_A20        | [1:0] from VM 187 byte 25; [11:10] from VM 187 byte 42           | Analog trim                                                                                          | M:s            |
| `0x11924` | WL_PA0        | [26:24]                                                          | PA trim from VM 107 byte 0. Written multiple times during init and LUT load                          | M:t, M:s       |
| `0x11928` | WL_PA1        | [2:0], [11:9], [17:15], [25:22]                                  | PA trims from VM 107 bytes 1-4 (widths: 3, 3, 3, 4 bits)                                             | M:s            |
| `0x1192C` | WL_PA2        | [2:0]                                                            | PA trim from VM 107 byte 5                                                                           | M:s            |
| `0x11930` | WL_XOSC       | [16:13] left trim, [22:19] right trim                                   | Crystal oscillator load trims from VM 106 (4 bits each; stored 0x1F lands as 0xF)                    | M:t, M:s       |
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

### 4.2 Wireless control block: `0x14000-0x140FF` (published name `JL_WL`)

**Revised 2026-11-04 (reader pass).** Register names are those of the SDK's published chip header
(`JL_WL` at `0x14000`) [PUB]; the project names used by earlier revisions and by the GPL driver are
given in parentheses. The block holds the BT/WiFi radio arbitration control, the coexistence
arbiter port, a low-power control/counter register, the RF-die SPI bridge, the WiFi clock tree,
the WiFi interrupt mask, the channel/power word and three status words. `0x14080`-`0x14098` belong
to an SD host controller that shares the address group [PUB] and are not touched by WiFi code
[M:s wl_rf_common.a, wl_wifi_sta.a].

**ERRATA (2026-11-04) to the "bus gate" description below (kept in §4.2.0):** ~~WL_CTL0 gates the
`0x30000`+ MAC register address space; writing `0xFFFF` enables MAC register access~~. On a
WiFi-only start the vendor **never writes `WL_CON0`**: it holds its reset value 0 through the clock
tree, both PHY inits, the RF init and calibration, the MAC init, the interrupt setup and the first
transmit, and the vendor reads and writes MAC/BBP registers throughout with `WL_CON0` = 0
[M:s wl_rf_common.a, wl_wifi_sta.a]. The running vendor radio reads 0 [M:hw]. The only writers are
the BT controller start, BT stop and BT recovery paths, the coexistence start that runs only when
both radios are up, and debug/RF-off helpers that are not reached on a WiFi start [M:s]. The
probe-v13 bus fault with `WL_CTL0` = 0 is therefore not explained by `WL_CON0`; the likely cause is
that the WiFi clock tree (`WF_CON0`, §4.2.1 steps 2-3) had not been built when the MAC was read
[I]. On hardware `0xFFFF` holds the block and 0 releases it [M:hw 2026-11-04]; read against the
bit semantics below, `0xFFFF` asserts both radio-arbitration sides with an unprogrammed arbiter
plus the debug output, which plausibly withholds the RF from both radios [I].

**`WL_CON0` (`0x14000`) bit semantics, from every vendor access** [M:s wl_rf_common.a, cpu and BT
controller libraries; meanings I]:

| Bits | Use by the vendor |
|------|-------------------|
| 0 | BT radio side. Set by the BT controller start after the shared front-end init; pulsed 0 then 1 (100-unit delay) by BT recovery; cleared by BT stop |
| 1 | WiFi radio side. Set only by the coexistence start (`\|= 3`, i.e. together with bit 0) and by an RF-on helper; cleared by an RF-off helper. Bits 0 and 1 both set is the vendor's test for "coexistence active" |
| 7 | BT shared RF power/clock: set by BT start and BT recovery, cleared by a BT clock-close path only when bits 0 and 1 are both clear |
| 8 | debug-signal output enable (with [27:24]) |
| [20:16] | BT RF field: 0x0C at BT start, 0x10 in BT recovery; a BT clock-close path also sets bits 20-21 |
| [27:24] | debug-signal mux: 1 = WiFi, 2 = BT, 3 = coexistence arbiter |
| 12-15 | never written by any vendor code; readable/writable on hardware (`0xFFFF` reads back `0xF103`) [M:hw] |

BT start order [M:s BT controller library]: shared WiFi front-end init first (the whole of §4.2.1
phase A runs with `WL_CON0` = 0), then `|= 0x80`, [20:16] = 0x0C, a short delay, `|= 1`; a WiFi
start afterwards adds bit 1 and programs the arbiter. BT stop: `&= ~0x1F`, delay, `= 0`, and
`SYS_CLK2` [15:14] = 3 (radio clock off). In no order is `WL_CON0` written before the clock tree or
before the PHY/RF init.

| Address   | Name (project name) | HW value core-fm1 / vendor | Bits | Function | Tag |
|-----------|---------------------|----------------------------|------|----------|-----|
| `0x14000` | WL_CON0 (WL_CTL0) | `0` at power-on; `0xF103` held; vendor `0` | see bit table above | BT/WiFi radio arbitration, BT RF field, debug mux. Assert/release on hardware: `0xFFFF` holds the block with its register files accessible, `0` releases it (CCA detector and AGC start, §7.12) [M:hw]. The vendor's WiFi start never writes it | PUB, M:s, M:hw, I |
| `0x14004` | WL_CON1 | `0` / `0` | write: `(value << 16) \| (reg << 4) \| 1`; read: write `(reg << 4) \| 2`, then read [31:16]; table access: reg 6 = data, reg 7 = `(port << 2) \| (index << 5) \| 1` to write, `\| 2` to read | Coexistence arbiter indirect port. Programmed (registers 0-22, five priority tables of 46 rows, 41 weights) only when both radios run; a WiFi-only start never touches it | PUB, M:s |
| `0x14008` | WL_CON2 (WL_CTL8) | `0` / `0` | vendor `\|= 0x103` (bits 0, 1, 8) | External-PA option: written only when the board's external-PA option is configured, together with PA1/PA2 as outputs. Not configured on the FM-1 vendor build (reads 0). **ERRATA:** earlier text said the stock vendor library writes it; it does not on this board. Writing `0xFFFF` breaks the input shift register [M:hw]: do not write | PUB, M:s, M:hw |
| `0x1400C` | WL_CON3 (WL_ID) | `0x5B000000` then `0x7B000000` / `0x7B000000` | b0 set before sleep; b1 = 1 only with BT EDR and coexistence active; b2 set before sleep and after wake; b16 latch request; b17 busy (vendor waits for 0, about 10^6 polls, then resets the system); [31:18] latched 14-bit low-power counter (the vendor converts it at count/48 µs) | Low-power control and counter. **ERRATA:** not a chip/block ID. Bit 29 is bit 11 of the latched counter field [M:s cpu library, wl_rf_common.a]; only the sleep path re-latches it, so on a WiFi-only build it keeps whatever was latched last, which fits the sticky behaviour measured since 12i [M:hw, I]. Not written on the WiFi start; not an indicator | PUB, M:s, M:hw, I |
| `0x14010`-`0x1401C` | WL_CON4..7 | `0` | | Not accessed by WiFi code | PUB, M:s, M:hw |
| `0x14020`, `0x14024` | LOFC_CON, LOFC_RES | `0` | | LO frequency counter (name); not accessed by WiFi code. The SDK interrupt header lists a separate vector for this block | PUB, M:s |
| `0x14028` | WL_ANL_CON0 (RFSPI_CTL) | `0x00010000` | [22:20] busy (poll until 0 before every transaction, no bound); b16 enable; [15:8] address / LUT entry; [3:0] opcode: 0xE LUT word 0 write, 0xD LUT word 1 write, 0xF RF-die register write, 0x6 RF-die register read, 0 null; b4 strobe (five writes setting it, five clearing it) | RF-die SPI bridge control (§5). **ERRATA:** opcode 0x6 is an RF-die register read, not a LUT read; the vendor never reads the LUT back and never uses opcodes 0xF/0x6 | PUB, M:s, M:hw |
| `0x1402C` | WL_ANL_CON1 (RFSPI_DATA) | `0` | [31:0] | SPI data word, written before the opcode; holds read data after opcode 0x6 | PUB, M:s |
| `0x14030` | WL_ANL_CON2 | `0` | | Only ever written 0 (bridge reset and sync) | PUB, M:s |
| `0x14034` | WL_ANL_CON3 (RFSPI_WAIT) | `0` | | Written 0 in every iteration of the busy poll; meaning (busy acknowledge) inferred | PUB, M:s, I |
| `0x14040` | WF_CON0 (WL_CLK0) | `0x14F` / `0x14F` | [0], [1], [2], [3], [6], [8] built in the order of §4.2.1; bits 7 and 11 are set only in the SDIO-slave configuration (not used) | WiFi clock tree; bit meanings remain inferred | PUB, M:s, M:hw, I |
| `0x14044`, `0x14048` | WF_CON1, WF_CON2 | `0` | | Written 0 at clock init, never again | PUB, M:s, M:hw |
| `0x1404C` | WF_CON3 (WL_CLK3) | `0x01000000` / `0x01000000` | b24 WDMA clock; [20:18] cleared only by an uncalled debug helper | Written 0 at clock init, then b24 set before the WDMA registers | PUB, M:s, M:hw |
| `0x14050` | WF_CON4 (WL_CLK4) | `0x00480001` / `0x00C80001` | b0 WiFi interrupt unmask; b23 read-only status; b22 and b19 read 1 on both units | Written 0 at clock init (interrupt masked). **ERRATA:** b0 is the WiFi interrupt unmask (interrupt 52 is delivered while it is set), not a bus clock enable: the vendor's handler clears it, and it is set again when a receive buffer is posted or a received frame is dropped (§7.11.9). b23 is never read, polled, set or cleared by any vendor code [M:s]; it refuses writes on a held and on a released block [M:hw 12e, 12h, 12k] | PUB, M:s, M:hw |
| `0x14054` | WF_CON5 | `0x82FF82FF` both | two 16-bit halves | Accessed only by an RF-off/RF-on save/restore pair that nothing on the WiFi start calls; meaning unknown | PUB, M:s, M:hw |
| `0x14058` | WF_CON6 | `0x00FF82FF` / `0x009F827D` | [23:16] 0xFF vs 0x9F, [7:0] 0xFF vs 0x7D (the differing bits are 22, 21, 7, 1) | Read-only in practice [M:hw]. Never read or written on the start path (only the uncalled save/zero/restore pair) [M:s]. Reading: two live 8-bit codes driven by the RF die once it runs (current gain/AGC index or synthesizer codes); 0xFF = idle/maximum [I] | PUB, M:s, M:hw, I |
| `0x1405C` | WF_CON7 (WL_RST) | `0x4706006B` / `0x4706006B` | [7:0] channel code; [15:8] 0; [23:16] TX power level; [26:24] and b30 enable group; b31 0; [29:27] never written | **ERRATA:** not "PLL divider / PLL multiplier / PLL enable / PLL bypass". [7:0] is the channel code that indexes the RF-die LUT (§5): 5 during the clock bring-up, then channel + 101 (channel 1 = 102 ... channel 14 = 115) after init and on every channel switch; this single write is the vendor's entire per-channel RF step. [23:16] is the TX power level 0..6 (requests above 6 are clamped to 6). [26:24] = 7 and b30 = 1 are set at clock init and set again right after auto control is handed to the RF die; b31 is cleared both times [M:s wl_rf_common.a; meaning of [26:24]/b30 I]. Zeroed before PMU sleep; on wake [26:24] is restored first, then the saved word | PUB, M:s, M:hw, I |
| `0x14060` | WF_CON8 | `0x00480000` / `0x00C80000` | b23 read-only status | Not referenced by any code in any vendor library [M:s]; refuses writes [M:hw] | PUB, M:s, M:hw |
| `0x14064` | WF_CON9 | `0` | signed value | Fine frequency offset, in units of 24000/2^20 kHz (about 23 Hz), clamped to ±6553; used only by an RF test path | PUB, M:s |
| `0x14068`-`0x1407C` | reserved | `0` | | | PUB, M:hw |
| `0x14080`-`0x14098` | SD_CON0..2, SD_CTU_CON, SD_CTU_CNT (`0x14090`), SD_CPTR, SD_DPTR | `0x800`, `0xE`, `0x1FF`, -, `0x23CC` | | SD/SDIO host controller registers, **not WiFi** [PUB]; never touched by WiFi code [M:s]. The `0x14090` bit-4 "mirror" of SYS_CLK1 bit 12 (§4.5) is a property of this controller [I] | PUB, M:s, M:hw |

**Read-only indicators [revised 2026-11-04].** Four bit groups differ between the running vendor
radio and core-fm1 and refuse writes on core-fm1, held or released [M:hw 12e, 12h, 12k]:
`WF_CON4` (`0x14050`) b23 (vendor 1), `WF_CON8` (`0x14060`) b23 (vendor 1), `WF_CON6`
(`0x14058`) bits 22, 21, 7, 1 (vendor 0), `WDMA_CTL` (`0x30F04`) b16 (vendor 1). `WL_CON3`
(`0x1400C`) b29 is dropped from the list (low-power counter bit, see its row). **No vendor code
reads, polls, waits on, sets or clears any of them** [M:s wl_rf_common.a, wl_wifi_sta.a, cpu and
BT controller libraries: every constant-address access to `0x14050`, `0x14058`, `0x14060` and
`0x30F04` and every MAC accessor call was checked; `WF_CON8` is not referenced at all]. They are
therefore not enables that software sets; they are hardware outputs of states the vendor reaches.
What asserts them is not determinable from the binaries. Readings [I]: `WF_CON4` b23 and
`WF_CON8` b23 move together (`0x48..` vs `0xC8..`), consistent with one ready/lock flag of the RF-die
sequencer or synthesizer seen in two registers; the two `WF_CON6` codes leave 0xFF only on a
running radio, consistent with gain or tuning codes of the RF die acting on a real signal;
`WDMA_CTL` b16 sits next to b17 ("received frame available") and is consistent with "MAC
TX/RX datapath running". The vendor behaviours that the GPL driver lacks and that could plausibly
raise them are listed in §14 in order of likelihood.

What the vendor does wait on, by contrast (all without a bound unless stated) [M:s]: BBP port busy
(`0x3101C` b17 = 0) before every BBP write; RF-die SPI busy (`0x14028` [22:20] = 0) before every
transaction; the RC-tune done bit and the estimator done field during a full calibration; WDMA TX
done (`0x30F04` b20) and RX done (b21); `WL_CON3` b17 = 0 in the sleep path (bounded, then
system reset). None of these is one of the four indicators.

#### 4.2.0 Superseded text (2026-10-28 .. 2026-11-04 12i), kept as errata

The paragraphs and the table below are the previous revision of §4.2, unchanged. Where they
disagree with the revised text above, the revised text wins; the main errors are the "bus gate"
reading of `WL_CTL0`, the "chip/block ID" reading of `0x1400C`, the "PLL" reading of `0x1405C`,
the "bus clock enable" reading of `0x14050` bit 0, the "read LUT entry" opcode 0x6 and the
statement that the stock vendor library writes `WL_CTL8`.


System-level wireless control: bus gating, clock gating, reset, and the RF-die SPI interface [M:t, M:hw].

**Bus gate discovery [M:hw]:** WL_CTL0 (`0x14000`) gates the `0x30000`+ MAC register address space.
When WL_CTL0 is zero (default after reset), any read to `0x30000`+ causes a bus fault (confirmed:
probe v13 crashed on `0x31004` read with WL_CTL0=0). Writing `0xFFFF` to WL_CTL0 (reads back
`0xF103`) enables MAC register access (confirmed: probe v18 read `0x31004`=`0x0C` after gate).
WL_CTL0 alone is sufficient for MAC/DMA bus gating [M:hw confirmed 2026-10-30].

**WL_CTL8 hazard [M:hw confirmed 2026-10-30]:** Writing `0xFFFF` to WL_CTL8 (`0x14008`) kills the
input shift register (74HC595 chain on PA1/PA3/PA4). All 16 bits stick (unlike WL_CTL0 which masks
to `0xF103`), indicating WL_CTL8 enables individual hardware blocks or remaps GPIO pins to wireless
functions. Bisect on real hardware confirmed: WL_CTL0=0xFFFF alone enables MAC/DMA access; writing
WL_CTL8 is not required and must be avoided. The stock firmware's vendor library writes both, but
the stock firmware does not use a cooperative scheduler or scan input concurrently with WiFi init.

Full hardware scan of `0x14000`-`0x140A0` (probes v17, v18). Registers reading non-zero after WiFi
clock/reset init are marked; zero-reading addresses between non-zero ones may be reserved or
mode-dependent [M:hw].

| Address   | Name       | HW value     | Bits                                                                     | Function                                                                                      | Tag           |
|-----------|------------|--------------|--------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------|---------------|
| `0x14000` | WL_CTL0    | `0x00000000` | [15:0] writable; write `0xFFFF` reads `0xF103`; write `0xBEEF` reads `0xB003` | **Assert/release control for the wireless block, not a level gate** [M:hw 2026-11-04]. At power-on (0, never asserted) MAC reads fault [M:hw v13]; after `0xFFFF` they succeed; writing `0` afterwards (the running vendor radio's steady state) keeps the MAC readable, and within the next 100 ms dwell the baseband starts counting false CCA (`RX_STA_CNT1[15:0]` 201-240 per 100 ms, always 0 while held at `0xF103`) and direct BBP R60 becomes non-zero. The five read-only indicators and TX queueing did not change when the release came 15 s after init, nor when it came before the MAC reset with the whole init on a released block [M:hw 2026-11-04 12j]; the released block shows more live AGC registers (R57, R60, R62, R63) and up to 1172 false CCA per 100 ms with zero PLCP/CRC errors. Bit assignment unknown; whether the enables that refused writes on a held block accept them once released is the open question (12k) | M:t, M:hw |
| `0x14004` |            | `0x00000000` |                                                                          | Unknown (reads zero) [M:hw]                                                                   | M:hw          |
| `0x14008` | WL_CTL8    | `0x00000000` | [15:0] fully writable; write `0xFFFF` reads `0xFFFF`                     | **HAZARD: do not write.** Remaps GPIO pins to wireless functions, kills input shift register (PA1/PA3/PA4). Not required for MAC/DMA bus gating. Stock vendor library writes it, but stock firmware does not scan input concurrently | M:s, M:hw     |
| `0x1400C` | WL_ID      | `0x5B000000` | [29] read-only, write refused [M:hw 2026-11-03]. Reads 0 until the block has been released once (WL_CTL0 = 0, §4.2); afterwards 1 from the first sample of every later boot, through warm reboots and the power switch, even while the block is held again [M:hw 2026-11-04] | Chip/block ID in the upper bits (`0x5B`/`0x7B`); bit 29 is a sticky "released at least once" latch, not a live running flag | M:hw, I       |
| `0x14010`-`0x14024` |  | `0x00000000` |                                                                          | Zero (7 registers) [M:hw]                                                                     | M:hw          |
| `0x14028` | RFSPI_CTL  | `0x00010000` | [22:20] busy; [16] start; [15:8] address; [7:0] command; [4] kick toggle | RF-die SPI control port. Reads `0x10000` (start bit set) at rest. Commands: 0xE = LUT word 0 write, 0xD = LUT word 1 write, 0x6 = read | M:t, M:s, M:hw |
| `0x1402C` | RFSPI_DATA | `0x00000000` | [31:0]                                                                   | RF-die SPI data (written before CTL)                                                          | M:t, M:hw     |
| `0x14030`-`0x1403C` |  | `0x00000000` |                                                                          | Zero (4 registers, includes RFSPI_WAIT at `0x14034`) [M:hw]                                   | M:hw          |
| `0x14040` | WL_CLK0    | `0x0000014F` | [0] base clock, [1] MAC/BB, [2] PHY, [3] RF RX, [6] RF TX, [8] PLL ref  | **WiFi clock tree** (see §4.2.1 init sequence). Must be built up to `0x14F` in specific order with delays. Setting only bit 0 enables WDMA but not the radio receiver | M:t, M:s, M:hw |
| `0x14044` | WL_CLK1    | `0x00000000` |                                                                          | WiFi clock control                                                                            | M:t, M:s, M:hw |
| `0x14048` | WL_CLK2    | `0x00000000` |                                                                          | WiFi clock control                                                                            | M:t, M:s, M:hw |
| `0x1404C` | WL_CLK3    | `0x01000000` | [24] WDMA clock enable (§7.11)                                           | WiFi clock control. Bit 24 gates the WDMA engine; must be set before WDMA register access     | M:t, M:s, M:hw |
| `0x14050` | WL_CLK4    | `0x00480001` | [0] SoC WiFi IRQ/bus enable (writable); [23] read-only status: `1` on a running vendor radio (`0x00C80001`), `0` on core-fm1, write refused in RMW and direct form [M:hw 2026-11-02, 2026-11-03] | WiFi clock control (bit 0: SDIO/bus clock enable) | M:t, M:s, M:hw |
| `0x14054` | WL_CFG54   | `0x82FF82FF` |                                                                          | Unknown. Symmetric 16-bit halves; possibly calibration mask or filter config                   | M:hw, I       |
| `0x14058` | WL_CFG58   | `0x00FF82FF` | Read-only: a full write of the vendor value `0x009F827D` leaves it at `0x00FF82FF` [M:hw 2026-11-03]. The running vendor radio shows bits 22, 21, 7, 1 **clear** | Unknown status word. Similar pattern to `0x14054`; the four differing bits are candidates for per-sub-block "not ready / held" flags | M:hw, I       |
| `0x1405C` | WL_RST     | `0x47000005` | [2:0] PLL divider (5), [15:8] zero, [23:16] zero, [26:24] PLL mul (7), [30] PLL enable, [31] PLL bypass=0 | **WiFi PLL + reset** (see §4.2.1). Final value `0x47000005`. Not a simple reset toggle: contains PLL configuration. The old "write 0, delay, write 1" sequence leaves PLL unconfigured | M:t, M:s, M:hw |
| `0x14060` | WL_CFG60   | `0x00480000` | [23] read-only status: `1` on a running vendor radio (`0x00C80000`), `0` on core-fm1, write refused [M:hw 2026-11-03] | Unknown. Upper bits match WL_CLK4 pattern; bit 23 behaves like WL_CLK4 bit 23 | M:hw, I       |
| `0x14064`-`0x1407C` |  | `0x00000000` |                                                                          | Zero (7 registers) [M:hw]                                                                     | M:hw          |
| `0x14080` | WL_CFG80   | `0x00000800` |                                                                          | Unknown                                                                                       | M:hw          |
| `0x14084` | WL_CFG84   | `0x0000000E` |                                                                          | Unknown                                                                                       | M:hw          |
| `0x14088` | WL_CFG88   | `0x000001FF` |                                                                          | Unknown. 9 low bits set; possibly a mask or channel bitmap                                    | M:hw, I       |
| `0x1408C` |            | `0x00000000` |                                                                          | Zero [M:hw]                                                                                   | M:hw          |
| `0x14090` | WL_CFG90   | `0x0000234C` | [4] mirrors SYS_CLK1 (`0x10008`) bit 12: `0x23DC` with it set, `0x23CC` with it clear [M:hw 2026-11-03]; [7] set on the running vendor radio and on core-fm1 once SYS_CLK2 bit 10 is selected [M:hw 2026-11-02] | Unknown. Vendor steady state `0x000023CC`, now matched | M:hw          |
| `0x14094`-`0x140A0` |  | `0x00000000` |                                                                          | Zero (4 registers) [M:hw]. End of scanned range; registers beyond `0x140A0` untested          | M:hw          |

**Read-only indicators of a running MAC/BBP [M:hw 2026-11-03]:** with every writable register of
this block and of `0x10008`/`0x10010` at the vendor's steady-state value, five bits still differ
between the running vendor radio and core-fm1, and all five refuse writes on core-fm1: `0x1400C`
bit 29 (vendor 1), `0x14050` bit 23 (vendor 1), `0x14058` bits 22, 21, 7, 1 (vendor 0),
`0x14060` bit 23 (vendor 1), and `WDMA_CTL` (`0x30F04`) bit 16 (vendor 1, §7.11.3). They refuse writes on a held block (12e, 12h) and on a released block, 1 ms and 100 s after the release (12k) [M:hw 2026-11-04]. While they
are clear/set as on core-fm1, the MAC queues no frame (`TXRXQ_PCNT` 0), reports no TX status, and
the baseband counts no false CCA and leaves direct BBP R32-R63 at zero. They are therefore status
outputs of a block that something else starts; that trigger is not a steady-state value of any
register compared so far. Update 2026-11-04: WL_CTL0 (`0x14000`) written to 0 after init releases
the CCA detector (false-CCA counts appear, BBP R60 populates) without moving these five bits; the
release-then-init order is under test (12j). TODO: requires binary analysis for the vendor's ordered
writes between clock-tree completion and first TX (pulses, uncompared addresses, indirect steps).
**Resolved 2026-11-04:** the ordered sequence is §4.2.1, the indicator semantics are in the revised
text above, the candidates are in §14.

#### 4.2.1 Complete ordered bring-up, clock tree to first transmitted frame [M:s, revised 2026-11-04]

Vendor WiFi-only station start on the FM-1 (24 MHz crystal, no external PA, interrupt-driven
receive), in program order [M:s wl_rf_common.a for phase A and B13; wl_wifi_sta.a for B2-B12 and
phase C; cpu library for A0]. Every step of phases A and B runs synchronously in one task; no step
is completed by an interrupt, a thread or a timer, and there is no mailbox, MCU command or firmware
download (the MCU command path is a stub that returns success without touching hardware) [M:s
wl_wifi_sta.a]. Phase C runs in the scan context after a scan request. "MAC+x" = `0x30000 + x`.
BBP writes use the port protocol of §6.2 (busy poll before each write). Window entries are written
as §6.2 describes (entry index, data, commit 1).

Column **GPL**: what the GPL driver in this tree currently does for that step, read from its
source and its replayed stock-BLE capture on 2026-11-04 [I for the comparison]: **same**; **diff
value**; **diff order**; **missing**; **extra** (GPL writes something the vendor does not);
**replay** (only the captured stock-BLE bytecode does it, before the GPL driver's own re-inits).

**Phase 0: boot clock setup (not radio specific)**

| # | Register | Value / operation | Delay / poll | Condition | GPL |
|---|----------|-------------------|--------------|-----------|-----|
| A0 | `0x10010` SYS_CLK2 | [11:10] = 01 (UART clock), bit 16 cleared, [3:2] cleared. No other SYS_CLK1/SYS_CLK2 bit is written by radio code; the vendor unit's bit 16 = 1 is set outside the analysed libraries | | boot | extra: GPL also sets SYS_CLK2 b8 then b10 and SYS_CLK1 b12, later cleared |

**Phase A: shared radio front end (once per power-up)**

| # | Register | Value / operation | Delay / poll | Condition | GPL |
|---|----------|-------------------|--------------|-----------|-----|
| A1 | `0x10010` SYS_CLK2 | [15:14] = 0: radio clock source = PLL (3 = disabled, used only by BT stop) | | first front-end init | same (replay) |
| A2 | `0x14040`, `0x14044`, `0x14048`, `0x1404C`, `0x14050` | = 0 each, in that order (WiFi interrupt masked) | | | same (replay) |
| A3 | `0x14040` WF_CON0 | `\|= 0x100`; `\|= 0x40`; `\|= 0x01`; 1 µs; `\|= 0x04`; 1 µs; `\|= 0x08`; 1 µs; `\|= 0x02`; 50 µs. Final `0x14F` | 1, 1, 1, 50 µs | | same final value (replay) |
| A4 | `0x1405C` WF_CON7 | six read-modify-writes: [7:0] = 5; [15:8] = 0; [23:16] = 0; `\|= 0x07000000`; `\|= 0x40000000`; bit 31 cleared. Result `0x47000005` | none | | same (replay, and a later direct write of `0x47000005`) |
| A5 | `0x1404C` WF_CON3 | `\|=` bit 24 (WDMA clock) | | | same |
| A6 | `0x30F04` WDMA_CTL | nine separate read-modify-writes in this order: `\|= 0x800`, `\|= 0x400`, `\|= 0x200`, `\|= 0x20`, `\|= 0x10`, `&= ~0x8`, `\|= 0x4`, `\|= 0x2`, `\|= 0x1`. Bit 16 is never written | none | | diff value and diff order: one write of `0x00010E37` (also sets read-only bit 16) after the GPL's own MAC reset |
| A7 | `0x30F00` WDMA_CFG0 | nine read-modify-writes: `\|= 0x400`, `&= ~0x200`, `&= ~0x100`, [7:5] = 3, `&= ~0x10`, `\|= 0x8`, `\|= 0x4`, `\|= 0x2`, `\|= 0x1` (written `0x46F`, reads `0x467`) | none | | same final value, diff order (after the GPL MAC reset) |
| A8 | PA1/PA2 direction and output, `0x14008` `\|= 0x103` | external-PA option | | board option; **not configured on the FM-1** | same (not written) |
| A9 | MAC+`0x1004` MAC_SYS_CTRL; MAC+`0x30C` | MAC_SYS_CTRL = 3; MAC+`0x30C` = `0x1E`; MAC+`0x30C` = 0 (reset pulse of bits [4:1]) | none | | replay; GPL later adds MAC_SYS_CTRL `\|= 3`, 10 µs, **= 0** (extra: the vendor never writes 0 on the start path) |
| A10 | MAC_SYS_CTRL; BBP R22, R21, R21, R25 | MAC_SYS_CTRL = `0xC`; R22 = 0; R21 = `0x31`; R21 = `0x30` (BBP reset pulse on bit 0); R25 = 0 | BBP busy poll | | same |
| A11 | MAC+`0x1104`, `0x1100`, `0x1330`, `0x1334`, `0x1338`, `0x1348` | `0x00140314`, `0x33A40C0B`, `0x00040804`, `0x000C0808`, `0x000C0104`, `0x000F1290` (in that order) | | | same |
| A12 | MAC+`0x308` | `0x9407` | | | same |
| A13 | repeat A9, A10 | | | | replay |
| A14 | window D (TX back end), mode-0 set | D[`0x4D`] = 1, D[`0x4E`] = 1, D[`0x4F`] = 0, then 15 coefficient writes in D[`0x29`..`0x4B`]; the PHY init (A15) overwrites every coefficient, only the three tap counts survive | | | diff value: GPL writes D[`0x29`..`0x2C`] = `6C 01 02 02`, D[`0x31`] = 3, D[`0x4D`] = D[`0x4E`] = `0x0F`, D[`0x4F`] = 0 *after* its PHY init, so mode-0 coefficients and tap counts 15 remain in its final state |
| A15 | PHY init #1 | the sequence of §6.6 (resets, timing registers, MAC+`0x308`, AGC RAM, windows C, D', C', D, fix-ups, R106 = 4, R84 = 2, final PHY reset) | BBP busy poll only | | replay; GPL's own single PHY init runs later (same tables; R84/R106 after the final reset: diff order, values persist) |
| A16 | RF transceiver init and calibration | crystal word `0x11930` (type [27:23] = `0x0B` for 24 MHz, caps from VM 106); `0x11950` = 0; constant analog block; RF mode off; PLL restart; PLL bank scan and 48-entry bank-limit table (§5.2); PLL for 2442 MHz with 24 MHz values (N = 101, fraction `0xC00000`, `0x11954` = `0x02822520`); PLL restart; loop pulse; calibration: quick path from VM 187 when its CRC is valid, else full trim (§10.1); RF mode RX; PA fields from VM 107 | PLL restart 5/5/100 µs; loop pulse 80 µs | 24 MHz crystal (`PLL_CON0` [27:26] != 2) | **diff value**: GPL switches to 40 MHz crystal type (`0x1A`) and 40 MHz PLL words (N = 40, `0x11954` = `0x000A2500`) for its calibration, always runs a full trim, never builds the bank-limit table (own bisection), then forces 15 RF registers to the vendor read-backs |
| A17 | PHY init #2 | byte-identical to A15 | | | **diff order**: GPL's only own PHY init runs after A20/A21 |
| A18 | window D (TX back end), mode-1 set | 57 writes: D[`0x4D`] = D[`0x4E`] = 15, D[`0x4F`] = 0; coefficient groups in D[`0x26`..`0x4C`]; per-rate gains D[`0x0B`, `0x0D`, `0x0F`, `0x11`, `0x13`] = `0x20`, D[`0x15`] = `0x2B`, D[`0x17`] = `0x26`, D[`0x19`] = D[`0x1B`] = `0x20`, D[`0x1D`] = `0x2B`, D[`0x1F`] = `0x26`, D[`0x21`] = `0x20`; D[`0x23`] = `0x23`, D[`0x24`] = `0x32`, D[`0x25`] = 0 | | | **missing** (only in the replay, then overwritten) |
| A19 | RAPP (TX soft compression, window D) | mode off; 128-entry curve load; thresholds and powers; mode 2 (§6.9) | | | **diff value**: GPL loads a rising 0, 2 .. 254 ramp and writes the thresholds to D[`0x56`..`0x59`] |
| A20 | RF-die LUT load | bridge reset and sync; 256 entries x 2 words with patches: FEQ entries 0-127 word 1 `\|=` bank << 9 from the bank-limit table; AGC entries 128-223 word 0 RX DC; APC entries 224-255 PA fields; then six writes of 0 and one of `0x10000` to `0x14028` (§5.1) | SPI busy poll per word | | **diff value**: FEQ bank field left 0 in all 128 entries; AGC DC from the GPL's own (40 MHz) trim; **diff order**: before the GPL's MAC/PHY reset and PHY init |
| A21 | RF auto control | `0x11950` built bit by bit to `0x57BFFFDC`; `0x11938` b26 = b27 = 1; `0x11924` b5..b8 = 1; `0x11964` [31:29] = 1 then [28:24] = `0x1B`; `0x1405C` `\|= 0x07000000`, `\|= 0x40000000`, bit 31 cleared (§11.2.11) | none | | same final values (forced), **diff order**: followed in the GPL by a MAC/PHY reset pulse and a PHY init; the vendor performs no reset after this step |
| A22 | `0x1405C` [23:16] | = 6 (TX power level) | | | replay only |
| A23 | `0x1405C` [7:0] | = 6 + 101 = `0x6B` (channel 6) → `0x4706006B` | | | replay only; never rewritten by the GPL after its resets and channel switches |
| A24 | window D per-rate gains | defaults of A18, then every non-zero byte of VM 108 overrides its entry | | VM 108 present | same |
| A25 | (software snapshot of `0x1191C`, `0x11920`) | no register write | | | n/a |

No delay follows A21-A24 [M:s]. Phase A writes nothing to `0x14000`, `0x14004`, `0x14054`,
`0x14058`, `0x14060`, MAC+`0x1200`, MAC+`0x1328` or MAC+`0x132C` [M:s].

**Phase B: driver probe and interface up (same task, directly after phase A)**

| # | Register | Value / operation | Delay / poll | Condition | GPL |
|---|----------|-------------------|--------------|-----------|-----|
| B1 | `0x14000` `\|= 3`, arbiter through `0x14004` | coexistence start | | **only if BT already did the front end**; not on a WiFi-only start | extra: GPL writes `0x14000` = `0xFFFF` (holds the block) and later 0 |
| B2 | MAC configuration table, 22 writes in this order | MAC+`0x42C` = `0xF8F0E8E0`, `0x430` = `0x6F77D0C8`, `0x1408` = `0x13F`, `0x140C` = `0x8003`, `0x1400` = `0x17F97`, `0x1350` = `0x1020`, `0x1018` = `0x1F00`, `0x40C` = `0x1F3FBF9F`, `0x134C` = `0x47D01F0F`, `0x1404` = `0x13`, `0x1364` = `0x05740003`, `0x1368` = `0x05740003`, `0x408` = `0x00F40006`, `0x1370` = `0x03F44084`, `0x1374` = `0x01744004`, `0x1378` = `0x03F44084`, `0x136C` = `0x01744004`, `0x1340` = `0x583F`, `0x1344` = `0x00092B20`, `0x1380` = `0x002400CA`, `0x1608` = 2, `0x1204` = 3 | | | diff value in several (GPL uses post-association values for `0x1350`, the protection registers and `0x1018`; `0x1408` = `0x15F`); extra: GPL writes MAC+`0x1200` = `0x00030601` and MAC+`0x137C` |
| B3 | MAC+`0x1800` + 8i, i = 0..253 (WCID table) | each entry bytes `FF FF FF FF FF FF FF 00` (words `0xFFFFFFFF`, `0x00FFFFFF`) | | | **missing** |
| B4 | MAC+`0x1700`..`0x1714` | read (read-to-clear counters) | | | same |
| B5 | MAC+`0x7000`..`0x700C`; MAC+`0x6800` + 4i, i = 0..255 | 0 each; 1 each (shared-key mode; WCID attributes) | | hard-reset path (taken at first start) | **missing** |
| B6 | MAC+`0x1114` BCN_TIME_CFG | `&= ~0x001F0000` (TSF/beacon sync off) | | | **missing** |
| B7 | MAC+`0x1700`..`0x1714`; MAC+`0x1340` | read again; `0x583F` | | | same |
| B8 | MAC+`0x1008`, `0x100C` | MAC address; `0x100C` [23:16] = `0xFF` | | | same |
| B9 | MAC+`0x1300`..`0x130C` EDCA; MAC+`0x132C` bit 0 | AC1 = `0x000A4200`; AC0/AC2/AC3 are computed from default parameters (`0xA4200`, `0xA4260`, `0xA4230` per the reader's notes, not re-verified [I]); bit 0 only for 40 MHz operation | | | diff value (GPL uses association-time values; effect on scanning not expected [I]) |
| B10 | delay | 10 ms | 10 ms | | n/a |
| B11 | first receive buffer posted → `0x14050` `\|= 1` | WiFi interrupt unmasked | | | same final state (GPL sets b0 once) |
| B12 | MAC+`0x1400` RX_FILTER_CFG | `0x17F97` | | station mode | diff value (GPL `0x1BF93`: DROP_UC_NOME and DROP_BA clear, DROP_BAR set) |
| B13 | MAC+`0x328`; interrupt 52 | MAC+`0x328` = 0; interrupt 52 registered (priority argument 2, CPU argument 1, i.e. core 1 [I]); MAC+`0x328` = 2 | | interrupt-driven receive | same register values; the GPL's handler also reads and writes MAC+`0x328`, the vendor's handler never touches the MAC |
| B14 | MAC_SYS_CTRL | **no write**: stays `0xC` from the final PHY reset of A17 | | | extra: GPL writes 0, then 4, 50 µs, then `0xC` |

The order B11 before B13 means the interrupt mask is opened before interrupt 52 is registered
[M:s]; harmless because nothing is pending [I].

**Phase C: scan, per channel, then the first transmit**

| # | Register | Value / operation | Delay / poll | Condition | GPL |
|---|----------|-------------------|--------------|-----------|-----|
| C1 | `0x1405C` [7:0] | = channel + 101, one read-modify-write; no PLL, LUT or BBP access, no delay | | channel in the driver's channel list | **missing**; instead GPL writes BBP R94 = 17, `0x1193C` [12:5] = f − 2320, pulses `0x11934` b24, writes **BBP R106 = channel** and R94 = 26 (all extra; the vendor never writes R94 and keeps R106 = 4) |
| C2 | MAC+`0x132C` TX_BAND_CFG | `(v & ~0x6) \| 0x4` for channels below 15 (`\| 0x2` otherwise) | | | **missing** |
| C3 | MAC+`0x1328` TX_PIN_CFG | `0x00050302` (from the board's 1T1R antenna configuration). The first write of this register happens here, before the first probe request | | | **missing** (never written, never compared on hardware) |
| C4 | (AGC initial value through the RT2800 BBP path) | dropped by the vendor's own register filter: no hardware write | | | n/a |
| C5 | dwell timer, then probe request built | software | 30-140 ms dwell | | n/a |
| C6 | TX readiness gate | read MAC+`0x438` byte 2 (pages in TX queue 2) and require it below the limit (byte 1 of the value written to MAC+`0x40C`, i.e. `0xBF`) with (limit − pages) x 128 > MPDU length; read MAC+`0x434`, require bit 18 = 0; read MAC+`0x43C`, require [7:5] != 0. Otherwise wait (10 ms or yield) and retry, no bound | polled retry | | **missing** (`0x434`/`0x43C` never read) |
| C7 | TX staging | software buffer: length word (MPDU + 22, not transferred), queue word (bit 0 = WIV, [2:1] = queue 2), TXWI, frame (§7.11.5) | | | diff value (different first DMA word) |
| C8 | WDMA TX | `WDMA_CTL` `\|= 0x400`; barrier; `WDMA_TX_ADR` = address of the queue word; `WDMA_TX_LEN` = MPDU + 26 (bit 31 = 0); barrier; poll `WDMA_CTL` b20; `WDMA_CTL` `\|= 0x400` | b20 polled without bound | | **diff value**: GPL starts at the length word with length + 8 (12d) or at the TXWI (12c) |

The GPL driver's actual order, for reference: replay of the stock-BLE capture (phase A
equivalents A1-A24 with its own VCO search), 40 MHz-mode full calibration, LUT reload without
banks, auto control, forced RF read-backs; then `WL_CON0` = `0xFFFF`, a MAC reset ending in
MAC_SYS_CTRL = 0, WDMA single writes, PHY init (with resets), RAPP ramp, calibration values into
windows D/D', interrupt setup, MAC configuration (MAC_SYS_CTRL 0, 4, `0xC`); scan per channel with
the C1 alternative; TX from the length word [I, from the GPL source].

#### 4.2.1a Superseded: clock tree init sequence (2026-10-31), kept as errata

The WiFi clock tree must be initialized in a specific order with inter-step delays. This sequence
is performed by the vendor's clock/reset routine before any MAC or RF access. Failure to complete
the full sequence leaves the radio receiver without clocks (WDMA TX still works because the WDMA
engine has a separate clock gate at `0x1404C` bit 24).

**Step 0: System clock routing** (`0x10010` = SYS_CLK2)
- Read-modify-write: clear bits [15:14]. These bits control BT/WiFi radio clock domain selection.
  Clearing them routes the radio clock to the WiFi subsystem [M:s].

**Step 1: Zero all WiFi clock/IRQ registers** (clean start)
- `0x14040` (WL_CLK0) = 0
- `0x14044` (WL_CLK1) = 0
- `0x14048` (WL_CLK2) = 0
- `0x1404C` (WL_CLK3) = 0
- `0x14050` (WL_CLK4) = 0

**Step 2: Build up WL_CLK0** (`0x14040`) with specific bit ordering [M:s]

Each substep is a read-modify-write OR. Delays between substeps allow each clock domain to lock.

| Substep | Operation       | Cumulative value | Delay after | Purpose (inferred)     |
|---------|-----------------|------------------|-------------|------------------------|
| 2a      | OR bit 8        | `0x100`          | none        | PLL reference clock    |
| 2b      | OR bit 6        | `0x140`          | none        | RF TX clock            |
| 2c      | OR bit 0        | `0x141`          | 1 us        | Base WiFi clock        |
| 2d      | OR bit 2        | `0x145`          | 1 us        | PHY clock              |
| 2e      | OR bit 3        | `0x14D`          | 1 us        | RF RX clock            |
| 2f      | OR bit 1        | `0x14F`          | **50 us**   | MAC/BB clock (final)   |

The 50 us delay after the final bit allows the entire clock tree to stabilize before reset
de-assertion.

**Step 3: Configure WL_RST** (`0x1405C`) with PLL settings [M:s]

**ERRATA (2026-11-04):** the values and the write order of this step are right, the meanings in
the table below are wrong. [7:0] = 5 is the RF-die LUT entry selected during the clock bring-up
(the channel code field), [23:16] is the TX power level, [26:24] and bit 30 are an enable group
(meaning inferred), see §4.2 `WF_CON7`.

WL_RST is not a simple reset toggle. It contains PLL multiplier and divider configuration:

| Bits    | Value | Meaning (inferred)          |
|---------|-------|-----------------------------|
| [7:0]   | 0x05  | PLL divider / mode select   |
| [15:8]  | 0x00  | Reserved (cleared)          |
| [23:16] | 0x00  | Reserved (cleared)          |
| [26:24] | 0x07  | PLL multiplier              |
| [30]    | 1     | PLL enable                  |
| [31]    | 0     | PLL bypass (off)            |

Final WL_RST value: `0x47000005`. The vendor configures this via sequential read-modify-write
operations (clear field, OR new value) but the end result is a single configuration word [M:s].

**Bit purpose names are inferred** from clock tree ordering and position. The vendor library does
not name these bits. The ordering (PLL ref first, then TX, base, PHY, RX, MAC/BB) and the delays
between enable steps are observed facts [M:s].

### 4.3 Wi-Fi MAC / BBP window: `0x30000-0x31FFF`

The Wi-Fi MAC and baseband processor (BBP) register space [M:t].

| Address   | Name     | Function                                                                     | Tag      |
|-----------|----------|------------------------------------------------------------------------------|----------|
| `0x30308` | MAC_308  | MAC configuration (part of first BBP load). Written `0x9407` three times: once before the second MAC/PHY reset and at the start of each PHY init; meaning unknown | M:t, M:s |
| `0x3030C` | MAC_30C  | Reset pulse: written `0x1E` then 0 (bits [4:1]) inside every MAC/PHY reset, after MAC_SYS_CTRL = 3 (2026-11-04) | M:t, M:s |
| `0x30324` | MAC_324  | Written 2 before every receive DMA (2026-11-04); meaning unknown | M:s |
| `0x30328` | MAC_328  | Receive-notify source: 0, interrupt registration, 2; 0 again at shutdown; never read (§7.11.4) | M:s, M:hw |
| `0x30408` | PBF_CFG  | `0x00F40006`; watchdog flush pulse (§7.11.9) | M:s, IP:RT2x00 |
| `0x3040C` | PBF_MAX_PCNT | `0x1F3FBF9F` written; byte 1 is the vendor's software TX-page limit for queue 2 (§7.11.5) | M:s, IP:RT2x00 |
| `0x30434` | (unnamed) | Read before every TX; bit (8q + 2) must be 0 for queue q (§7.11.5); never captured on hardware | M:s |
| `0x30438` | TXRXQ_PCNT | Byte q = pages in queue q (TX0Q, TX1Q, TX2Q, RX0Q); vendor idle `0x00020000` | M:s, M:hw, IP:RT2x00 |
| `0x3043C` | PBF_DBG  | Read before every TX; [7:5] must be non-zero (§7.11.5); never captured on hardware | M:s, IP:RT2x00 |
| `0x30F00` | WDMA_CFG0  | **WDMA configuration register** (§7.11). Init value: `0x46F`               | M:s      |
| `0x30F04` | WDMA_CTL   | **WDMA control/status register** (§7.11). TX/RX control + completion status| M:s      |
| `0x30F10` | WDMA_TX_ADR| WDMA TX source address (physical, 4-byte aligned)                          | M:s      |
| `0x30F14` | WDMA_TX_LEN| WDMA TX transfer length. **Writing triggers DMA transfer**                 | M:s      |
| `0x30F18` | WDMA_RX_ADR| WDMA RX destination address (physical, 4-byte aligned)                     | M:s      |
| `0x30F1C` | WDMA_RX_LEN| WDMA RX transfer length. **Writing triggers DMA transfer**                 | M:s      |
| `0x31004` | MAC_SYS_CTRL | MAC system control. HW probe: `0x0000000C` (bits 2+3 = TX_EN + RX_EN per RT2800). ~~Requires WL_CTL0 bus gate~~ (**ERRATA 2026-11-04**, §4.2). Vendor writes only 3 (reset) and `0xC` on the start path, never 0 (§4.2.1) | M:t, M:hw, M:s, IP:RT2x00 |
| `0x31328` | TX_PIN_CFG | `0x00050302` at every channel switch, first before the first probe request; `0x00050000` at halt (§4.2.1 C3, §14) | M:s, IP:RT2x00 |
| `0x3132C` | TX_BAND_CFG | `(v & ~6) \| 4` at every channel switch below channel 15 (§4.2.1 C2) | M:s, IP:RT2x00 |
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

**ERRATA / scope note (2026-11-04):** these values were observed on the BLE side. The vendor's WiFi
code never writes BBP register 94 (`0x5E`) or 96 (`0x60`) and has no per-scan priority step; BT/WiFi
arbitration on a combined build goes through `WL_CON0` bits 0-1 and the arbiter behind `WL_CON1`
(§4.2), programmed only when both radios run [M:s wl_rf_common.a, cpu library]. Writing BBP R94 from
the WiFi driver therefore has no vendor counterpart (§6.4).

### 4.5 Clock and system registers

| Address   | Name     | Function                                       | Tag      |
|-----------|----------|------------------------------------------------|----------|
| `0x10008` | SYS_CLK1 | System clock configuration. Bit 12: set by core-fm1's boot path, **clear** on the running vendor radio (`0x00010200`); writable, clearing it also clears `0x14090` bit 4 but does not start the MAC/BBP [M:hw 2026-11-03] | M:t, M:hw |
| `0x10010` | SYS_CLK2 | System clock configuration. Bits [15:14]: BT/WiFi clock domain select (clear for WiFi). Bit [8]: WiFi RF clock enable (set by group 1 init; not written by the WiFi libraries, which write only [15:14] here [M:s, 2026-11-04]). The running vendor radio reads `0x00010400` (bit 10 set, bit 8 clear) [M:hw 2026-11-02]; selecting bit 10 on core-fm1 sticks but does not by itself start the MAC/BBP | M:t, M:s, M:hw |

---

## 5. RF-die LUT (via SPI at `0x14028`/`0x1402C`)

256-entry lookup table on the RF die, programmed via SPI [M:t]. Two words per entry [M:s].

| Command | Function                   | Tag |
|---------|----------------------------|-----|
| `0xE`   | Write LUT word 0 for entry | M:s |
| `0xD`   | Write LUT word 1 for entry | M:s |
| `0x6`   | ~~Read LUT entry~~ RF-die register read (**ERRATA 2026-11-04**; unused by the vendor, which also never uses `0xF` register write and never reads the LUT back) | M:s |

Protocol: write data to `0x1402C`, then `0x14028` = `0x10000 | entry<<8 | cmd`, then toggle bit 4
five times high, five times low (the "kick") [M:t, I].

LUT patches at runtime [M:t, M:s]:

- Word 0, entries `0xE0-0xFF`: patched from VM 107 (PA calibration)
- Word 1, entries `0x00-0x7F`: patched from VCO scan results
- Word 1, entries `0xE0-0xFF`: patched from VM 107
- Word 0, entries `0x80-0xDF`: patched from VM 187 bytes 48-63

### 5.1 WiFi RF-die RAM LUT loading [M:s]

After the BLE RF bytecode loads a BLE-mode LUT (§11 group 11), switching to WiFi mode (§11.2.9)
requires a **separate WiFi-specific LUT** to be loaded into the RF die's RAM. Without this table,
the RF die has no WiFi frequency equalization plan, no WiFi-mode AGC configuration, and no WiFi
TX power amplifier curve. This is step A20 of §4.2.1 (step 11 of the hardware-init order in
§11.2.1) [M:s wl_rf_common.a].

**Table structure:** 256 entries, each with two 32-bit words (word 0 and word 1). Three functional
sections [M:s]:

| Section | Entries | Word 0 | Word 1 |
|---------|---------|--------|--------|
| FEQ (Frequency Equalization) | 0-127 | Frequency plan coefficients | PLL band settings |
| AGC (Automatic Gain Control) | 128-223 | Gain control (0x7A or 0x0A base) | AGC step configuration |
| APC (Automatic Power Control) | 224-255 | PA control (0x78 base) | PA curve / power levels |

**Table data source:** ~~the base table values are in the JieLi AC79 SDK's constant source file, licensed
Apache-2.0~~ (**ERRATA 2026-11-04**). The table used on a 24 MHz-crystal unit such as the FM-1 is
published in the SDK application source `apps/common/net/wifi_conf.c` (a 256 x 2 table starting
near line 1331, columns in the order word 0, word 1) [PUB]; a 40 MHz-crystal variant lives inside
the library and is selected only when `PLL_CON0` (`0x119A0`) [27:26] = 2 [M:s wl_rf_common.a,
cpu library].

**Loading protocol** (same SPI interface as §5) [M:s]:

Pre-load reset:
1. Write `0` to RFSPI_CTL (`0x14028`), RFSPI_DATA (`0x1402C`), RFSPI_REG2 (`0x14030`)
2. Write `0x10000` to RFSPI_CTL (set start bit)
3. SPI sync: poll RFSPI_CTL bits [22:20] until 0, clear busy via `0x14034`=0, reset DAT=0,
   REG2=0, CTL=`0x10000`, kick SPI

Per-entry load (for each i = 0..255):
1. Poll RFSPI_CTL bits [22:20] until 0 (busy clear)
2. Write word 0 to RFSPI_DATA (`0x1402C`)
3. Write `(i << 8) | 0x1000E` to RFSPI_CTL (`0x14028`) -- cmd 0xE = LUT word 0
4. Kick SPI (toggle bit 4: five times high, five times low)
5. Poll RFSPI_CTL bits [22:20] until 0
6. Write word 1 to RFSPI_DATA
7. Write `(i << 8) | 0x1000D` to RFSPI_CTL -- cmd 0xD = LUT word 1
8. Kick SPI

Post-load reset:
1. Write `0` to RFSPI_CTL six times (flush pipeline)
2. Write `0x10000` to RFSPI_CTL (restore start bit)

**Optional calibration patches** (not required for basic operation) [M:s]:

When calibration data is available, some entries are patched before loading:

- **AGC word 0 (entries 128-223):** base value (0x7A or 0x0A) in low 12 bits, RX DC offset
  values from the RX DC calibration (four gain groups, sign-magnitude, bit 9 = non-negative,
  [8:0] magnitude) patched into bits [31:12]: I into [21:12] (masked to 10 bits), Q shifted into
  [31:22]. Entries 128-132 use group 0, 133-179 group 1, 180-185 group 2, 186-223 group 3.

- **APC word 0 (entries 224-255):** PA configuration bytes (the seven bytes of VM 107, here
  numbered 0-6) patched into low bits: byte 5 in bits [2:0], byte 6 in bits [5:3].

- **FEQ word 1 (entries 0-127):** the VCO bank for the entry's feedback divider is OR'd in at
  bit 9 (`word1 |= bank << 9`, no clear first); the divider is word 1 [28:21], the bank comes
  from the bank-limit table of §5.2. **ERRATA 2026-11-04:** the field is [15:9] (zero in the base
  table; banks are at most 63, so only [14:9] change), not [16:9]: bit 16 is set in the base table
  and is not part of the field.

- **APC word 1 (entries 224-255):** PA configuration bytes patched at: byte 0 [11:9], byte 1
  [17:15], byte 2 [20:18], byte 3 [25:23], byte 4 [29:26], after the word is masked with
  `0xC06071FF` [M:s wl_rf_common.a]. (**ERRATA 2026-11-04:** the previous positions [16:9] and
  [28:26] and the mask description were wrong.)

~~For initial WiFi bring-up, loading the raw table values without calibration patches is
sufficient. Patches are a quality-of-signal improvement, not a hard requirement for RX [I].~~
**ERRATA 2026-11-04:** with auto control on (`0x11950` = `0x57BFFFDC`, §11.2.11) the RF die tunes
from the LUT entry selected by `WF_CON7` [7:0], and the vendor writes no PLL register on a channel
change (§4.2.1 C1). The FEQ bank patch is therefore likely what sets the VCO band in operation;
without it every FEQ entry carries bank 0 [I]. Listed in §14.

**FEQ entry meaning** [PUB table, M:s for the index rule, I for the grid]: word 1 [28:21] is the
feedback divider (fbds = f − 2321 MHz on the 24 MHz table). Entries 0-85 form a 1 MHz grid (fbds
80..165, 2401-2486 MHz, the BLE/BT grid); entries 100-117 are a 5 MHz series; entries 102-115 are
the 802.11 channels 1-14 (entry = channel + 101, channel 14 at fbds 163 = 2484 MHz). Entry 5
(2406 MHz) is the one selected during the clock bring-up.

### 5.2 VCO bank scan and bank-limit table [M:s wl_rf_common.a, revised 2026-11-04]

The FEQ bank patch needs a 48-entry table of feedback-divider limits, built once during the RF
init (§4.2.1 A16). Registers: VCO bank (capacitor code) `0x11938` [25:19]; feedback divider
`0x1193C` [12:5]; VCO voltage detector enable `0x11940` b24; comparator read through the analog
status port (`0x11968` [31:28] = 1 selects it; write `0x11978` = 1 eight times, then 0, then read
`0x11978`): bit 18 set = code 1 ("above"), bit 17 set = code 2 ("below"). Each measurement is a
PLL restart (§11.2.9: `0x11938` b16 = 0, b18 = 0, `0x11934` b24 = 0; 5 µs; b16 = 1; 5 µs;
`0x11934` b24 = 1, `0x11938` b18 = 1; 100 µs) followed by one comparator read.

Procedure (inputs: none; output: 48 limits and the bank for one divider):

1. Detector on. Set the divider to 73.
2. Mid-bank search: for candidate banks c = 7, 23, 39, 55, 71, 87, 103, 119 in turn, measure at
   banks c, c − 7 and c + 8. At the first candidate whose three codes are not all equal, the top
   bank is c with its low four bits set (c | 15). If no candidate disagrees, the top bank is the
   last code read with its low four bits set (a degenerate 15).
3. Store the top bank as limit[47]. Start with "previous low" = 73 and "previous high" = 73.
4. For i = 0 .. 46: set bank = top − i; sweep the divider upward from the previous low, one step
   per measurement. A code-2 reading moves "low" to the current divider; the first code-1 reading
   sets "high" to the current divider and ends the sweep; past 171 the sweep ends with high = 171
   (and low = 171 if the last reading was code 2). Store limit[i] = (previous high − 3 + low) / 2
   (integer division). The new high and low become the previous values for i + 1.
5. Bank lookup for a divider f: find the first j in 1 .. 47 with limit[j] > f; the bank is
   limit[47] − (j − 1). If none, the bank is limit[47] − 47.
6. End of scan: divider = 121 (2442 MHz), bank = lookup(121), detector off; no restart.

Measured for comparison: the vendor radio holds bank `0x35` at divider 121 in `0x11938` [M:hw];
the GPL driver's own bisection found a single band and never builds the table [I, GPL source].
The 2026-10 emulator description "binary search over 64 bands" (§11.1) does not describe this
procedure [M:t vs M:s]; the emulator models only the comparator.

---

## 6. BBP programming (via `0x3101C`)

The BBP (baseband processor) is programmed indirectly through four register windows, each accessed
via the BBP_PORT at `0x3101C` [M:t, M:s]. Each window has up to four BBP registers: commit, entry
address, data, and optionally read-back.

### 6.1 Window registers

| Window | Role | Commit | Entry addr | Data   | Read-back | Entry range | Boot transactions | Tag      |
|--------|------|--------|------------|--------|-----------|-------------|-------------------|----------|
| C      | CCA         | `0xC8` | `0xC9`     | `0xCA` | `0xCB`    | 0x00-0x7A   | 893       | M:t, M:s |
| C'     | RX 11n      | `0xCC` | `0xCD`     | `0xCE` | `0xCF` (**ERRATA 2026-11-04**: exists; earlier "--") | 0x00-0x47   | 73        | M:t, M:s |
| D      | TX backend  | `0xD0` | `0xD1`     | `0xD2` | `0xD3`    | 0x00-0x5F   | 101       | M:t, M:s |
| D'     | RX frontend | `0xD4` | `0xD5`     | `0xD6` | `0xD7`    | 0x00-0x1C   | 27        | M:t, M:s |

Window roles (names assigned by the reader from how the vendor uses each window) [M:s, I]: C =
CCA/detection, C' = RX 11n, D = TX back end, D' = RX front end. The CCA window configures the Clear Channel
Assessment engine (802.11 signal detection and demodulation). RX 11n configures 802.11n-specific
receive parameters. TX backend configures transmit signal processing. RX frontend configures
the analog-to-digital receive path.

### 6.2 Write protocol

**ERRATA (2026-10-31):** the original protocol described a single MMIO write per BBP access
with bit 17 as the control bit. Analysis of the vendor binary [M:s] reveals a **two-phase write**
using bit 19 (strobe) and bit 17 (kick). The corrected protocol is below.

BBP_PORT register at `0x3101C` bit fields [M:s]:

| Bits    | Name     | Description                                          |
|---------|----------|------------------------------------------------------|
| [7:0]   | DATA     | BBP register data (read or write)                    |
| [15:8]  | REG_NUM  | BBP register index                                   |
| [16]    | READ_CTL | 1 = read mode, 0 = write mode                       |
| [17]    | BUSY     | Hardware: set while busy. Software: set to kick/exec |
| [19]    | STROBE   | Enable/strobe bit (JieLi extension over RT2800)      |

To write a direct BBP register (not through a window) [M:s]:

1. **Wait** for bit 17 (BUSY) to clear: poll `0x3101C` until `(val & 0x20000) == 0`
2. **Load**: write `0x3101C` = `(1 << 19) | (reg << 8) | data` (= `0x80000 | (reg << 8) | data`)
3. **Kick**: write `0x3101C` = `(1 << 19) | (1 << 17) | (reg << 8) | data` (= `0xA0000 | (reg << 8) | data`)

Each direct BBP write is TWO MMIO writes to `0x3101C` plus a busy-wait [M:s].

To write a BBP register through a window (e.g. CCA), three direct BBP writes are needed, in this
order [M:s]:

1. direct write of the window's entry-address register with the entry index
2. direct write of the window's data register with the value
3. direct write of the window's commit register with 1

Each window write is therefore 6 MMIO writes to `0x3101C` (3 direct writes x 2 MMIO writes each)
[M:s].

Previously documented protocol [M:t]:

1. Write the entry address to the window's entry-address BBP register via `0x3101C`:
   set `0x3101C` = `(1 << 17) | (entry_addr_reg << 8) | value` [M:t]
2. Write the data value to the window's data BBP register via `0x3101C`:
   set `0x3101C` = `(1 << 17) | (data_reg << 8) | value` [M:t]
3. Write the commit register with value 1 via `0x3101C`:
   set `0x3101C` = `(1 << 17) | (commit_reg << 8) | 1` [M:t]

Each BBP write is therefore three MMIO writes to `0x3101C` [M:t].

### 6.3 Read protocol

To read a direct BBP register [M:s]:

1. **Wait** for bit 17 (BUSY) to clear
2. **Load**: write `0x3101C` = `(1 << 19) | (1 << 16) | (reg << 8)` (= `0x90000 | (reg << 8)`)
3. **Kick**: write `0x3101C` = `(1 << 19) | (1 << 17) | (1 << 16) | (reg << 8)` (= `0xB0000 | (reg << 8)`)
4. **Read**: read `0x3101C`, extract bits [7:0] for the result

To read through a window [M:s] (**ERRATA 2026-11-04:** all four windows have a read-back
register: C `0xCB`, C' `0xCF`, D `0xD3`, D' `0xD7`; not "C and D only". The vendor reads C, D and
D' and never reads C'):

1. direct write of the entry-address register with the index
2. direct write of the commit register with 2 (read mode, not 1)
3. direct read of the read-back register via the protocol above

The vendor's direct read performs no busy poll: it writes the two command words and reads
`0x3101C` immediately [M:s wl_rf_common.a]. A faster host should poll bit 17 before reading [I].
The vendor's read-modify-write of window entries assumes a window read returns the last value
written [I].

### 6.4 Direct BBP registers [revised 2026-11-04]

Earlier text (kept): "Certain BBP registers are accessed directly (not through windows): `0x15`,
`0x16`, `0x19`, `0x54`, `0x6A` [M:t]." The list is confirmed and completed below.

**Every direct BBP write in the vendor libraries** [M:s wl_rf_common.a, wl_wifi_sta.a; every
constant register index of the BBP write path checked, no computed index exists]:

| Register | Value | When, in the sequence of §4.2.1 |
|----------|-------|---------------------------------|
| R22 (`0x16`) | 0 | every PHY reset (A10, A13, start and end of each PHY init, and the PHY resets inside a full calibration) |
| R21 (`0x15`) | `0x31` then `0x30` | idem, directly after R22: BBP reset pulse on bit 0 |
| R25 (`0x19`) | 0 | idem, directly after R21 |
| R106 (`0x6A`) | **4** | once per PHY init, after the window fix-ups and before that init's final PHY reset (A15, A17). **Never written per channel** |
| R84 (`0x54`) | 2 | directly after R106 |
| R25 (`0x19`) | `0x18` | full calibration only (TX-to-RX loopback), restored to 0 by the next PHY reset |
| R4 (`0x04`) | read-modify-write clearing bits 4:3 | full calibration only; never restored (no other writer). The vendor radio reads R4 = 0 [M:hw] |
| R22 (`0x16`) | `0x80` | TX test mode only; not on any start path |
| R200-R215 | window ports | §6.1-§6.3 |

Nothing else is written directly: not R94 (`0x5E`), not R96, not R0-R20 (except R4), not R23, not
R31-R63, not R89-R104 [M:s]. The RT2800-layer BBP path (AGC initial value per channel, BBP tuning)
reaches the MAC's BBP port through the vendor's own register filter, which drops every write to
`0x3101C`; in this build those routines are also empty [M:s wl_wifi_sta.a]. No window commit
mirrors into the direct register space as far as the binaries show; R32-R63 are hardware state [I].

**Measured vendor values** [M:hw 2026-11-02, 2026-11-03]: R0 `0x60`, R2 `0x42`, R3 `0x10`, R4 0,
R5 `0x09`, R13 `0x70`/`0xF0`, R20 `0x44`, R21 `0x30`, R23 `0x66`, R31 `0x06`, R32-R46 stable
non-zero, R48-R63 moving, R84 2, R89 `0x80`, R98-R104 non-zero, R106 4. The R0-R31 differences to
core-fm1 (R2, R5, R6, R7, R10, R13, R14, R15, R16, R20) are therefore outcomes of a running
baseband, not missing writes [I]. The core-fm1 R106 value 1 measured in 12h is the GPL's per-channel
write (§4.2.1 C1) [M:hw, I].

**GPL extras** (from the GPL source, 2026-11-04) [I]: R94 = 17 / 26 and R106 = channel at every
channel switch; neither exists in the vendor.

### 6.5 Boot transaction counts

The full RF init (§10) performs approximately 34,274 total register writes, of which the majority
are BBP transactions [M:t]:

| Phase                    | Window       | Writes        | Read-backs     | Notes                                   | Tag |
|--------------------------|--------------|---------------|----------------|-----------------------------------------|-----|
| Group 4 (first BBP load) | C, C', D, D' | ~3,300        | 0              | Constant table data                     | M:t |
| Group 10 (second phase)  | C, D         | ~4,600 writes | 539 read-backs | D read-backs are DC/IQ trim calibration | M:t |


### 6.6 PHY init: register order and window load order [M:s wl_rf_common.a, 2026-11-04]

The PHY init has no arguments, no branches and no reads; both runs (§4.2.1 A15 and A17) issue the
identical sequence [M:s]. In order:

1. PHY reset (MAC_SYS_CTRL = `0xC`; R22 = 0; R21 = `0x31`; R21 = `0x30`; R25 = 0), the six MAC
   timing registers of §11.2.3, MAC+`0x308` = `0x9407`.
2. AGC RAM through window C, 128 entries (rule in §6.7), then the finals C[`0x2F`] = 0,
   C[`0x54`] = `0xC0`, C[`0x55`] = 1, C[`0x4E`] = `0x42`, C[`0x2B`] = `0x42`, C[`0x2C`] = `0x42`,
   C[`0x79`] = 0, C[`0x7A`] = `0x20`.
3. Window C table, 114 writes, ascending index `0x00`..`0x7A` without `0x2B`-`0x2F`, `0x4A`,
   `0x4D`, `0x4E`, `0x74` (values in §11.2.6). Step 3 overwrites four of the step-2 finals:
   C[`0x54`] = 0, C[`0x55`] = 1, C[`0x79`] = 4, C[`0x7A`] = `0xD0`.
   Ten entries come from published SDK tunables (the active "stronger RX anti-interference" set of
   `apps/common/net/wifi_conf.c`) [PUB, M:s]: C[`0x1B`] = 190 (`0xBE`), C[`0x36`] = 12 (`0x0C`,
   masked to 7 bits), C[`0x37`] = 4 (7 bits), C[`0x48`] = 190 (`0xBE`), C[`0x49`] = 150 (`0x96`),
   C[`0x4F`] = 140 (`0x8C`), C[`0x65`] = 80 (`0x50`), C[`0x66`] = 80 (`0x50`, 7 bits),
   C[`0x69`] = 100 (`0x64`), C[`0x6A`] = 4 with bit 7 set (`0x84`). This resolves the "values
   unknown" remark of the 2026-10-31 update in the header (the values are listed in §11.2.6).
4. Window D' (RX front end), 24 writes in the order D'[0], [4], [5], [6], [7], [3], [8], [`0x0D`]
   .. [`0x1C`], then D'[`0x18`] again (values in §11.2.7). D'[1], D'[2] and D'[9]-D'[`0x0C`] (the
   RX IQ calibration results) are not written here.
5. Window C' (RX 11n), 72 writes, ascending `0x00`..`0x47` (values in §11.2.8).
6. Window D (TX back end), 83 writes: D[0] = 2; D[`0x0B`]..D[`0x22`] (odd = `0x20`, even = 0);
   D[`0x23`..`0x25`] = 3, `0x32`, 0; D[`0x26`]..D[`0x4C`] (39 coefficient bytes); D[`0x50`]..D[`0x5D`];
   D[`0x5F`] = `0x40`; D[0] = 2 again (values in §11.2.8b).
7. Fix-ups, in this order: C[`0x2A`] = `0x94`, C[`0x57`] = `0x27`, D'[`0x19`] = `0x39`,
   C[`0x37`] = 4 (the tunable again), D'[`0x0F`] = `0xC4`, D'[`0x1B`] = `0x38`, C'[`0x3C`] = 1 (the
   table wrote `0xFF`; this is the only fix-up that changes a value).
8. R106 = 4, R84 = 2, PHY reset.

Hardware read-back of window C matches the vendor entry for entry [M:hw 2026-11-03]; windows C'
and D' have read-back registers (§6.3) but have not been compared on hardware yet.

### 6.7 AGC: table load and runtime gain control [M:s wl_rf_common.a, 2026-11-04]

**AGC RAM rule.** Entry i (0..127) is written as five window-C writes plus a strobe: C[`0x2F`] = i;
C[`0x2B`] = i − 20 clamped to −64..+63 (two's complement byte); C[`0x4E`] = i; C[`0x54`] and
C[`0x55`] = low and high byte of the 16-bit two's complement value 8i − 80; C[`0x2F`] = i | `0x80`
(write strobe). This rule reproduces every entry of the 128-word table of §11.2.5a [M:s].

**Runtime AGC maximum gain** (in the receive path, only after a scan or connect has named a target
access point): for each received frame whose transmitter address equals the target, the RSSI0
byte of the RXWI selects a value for C[`0x2C`]: RSSI0 > −36: 34; > −46: 43; > −60: 51; > −76: 57;
otherwise 66. The entry is written only when the value changes. An idle period (no frame within
the receive wake interval: 11 OS ticks, about 110 ms with a 10 ms tick [I]) applies the "−76 or below" value 66, which equals the PHY-init
final value [M:s]. Nothing is written before a target is named, so no runtime AGC write happens
before the first scan result.

### 6.8 Per-channel behaviour [M:s wl_rf_common.a, wl_wifi_sta.a, 2026-11-04]

A channel change writes **no BBP register**, direct or windowed: R106 stays 4, R94 is never written,
and the RT2800-layer per-channel AGC write is an empty routine whose BBP path the vendor's filter
would drop anyway (§6.4). The RF side of a channel change is the single `WF_CON7` [7:0] write of
§4.2.1 C1 (no PLL, LUT or analog register access, no delay); the MAC side is TX_BAND_CFG and
TX_PIN_CFG (C2, C3). A temperature-drift retune that reloads only the FEQ half of the LUT exists
but is disabled in the SDK configuration [M:s].

### 6.9 Window D final state: CFR and RAPP [M:s wl_rf_common.a, 2026-11-04]

**CFR (TX filter) mode 1**, written after PHY init #2 (§4.2.1 A18), 57 writes: D[`0x4D`] = 15,
D[`0x4E`] = 15, D[`0x4F`] = 0; coefficient groups in D[`0x26`]..D[`0x4C`]; per-rate gains at the
odd entries D[`0x0B`]..D[`0x21`] = `20 20 20 20 20 2B 26 20 20 2B 26 20`; D[`0x23`] = `0x23`,
D[`0x24`] = `0x32`, D[`0x25`] = 0. Mode 0 (before PHY init #1) writes D[`0x4D`] = 1, D[`0x4E`] = 1,
D[`0x4F`] = 0 and 15 coefficients in D[`0x29`]..D[`0x4B`]; only the three tap counts survive the
PHY init. The coefficient lists are short constant tables in the library (listed by count and
index range here, not transcribed).

**RAPP (TX soft compression)** register map in window D:

| Entry | Bits | Content |
|-------|------|---------|
| D[`0x51`] | [1:0] | mode: 0 while the curve is loaded, 2 = enabled |
| D[`0x51`] | [3:2], [5:4], [7:6] | bits [17:16] of the 11g/n, 11b and BPSK power words |
| D[`0x52`], D[`0x53`] | | 11g/n power word [7:0], [15:8] |
| D[`0x54`], D[`0x55`] | | 11b power word [7:0], [15:8] |
| D[`0x56`], D[`0x57`] | | BPSK power word [7:0], [15:8] |
| D[`0x58`] | [2:0], [5:3], [7:6] | 11g/n threshold [10:8], 11b threshold [10:8], BPSK threshold [9:8] |
| D[`0x59`], D[`0x5A`], D[`0x5B`] | | 11g/n, 11b, BPSK threshold [7:0] |
| D[`0x5C`] | b0 | BPSK threshold bit 10 |
| D[`0x5C`] | b4 | curve RAM write disable (set before each entry, cleared after) |
| D[`0x5C`] | b5 | curve data bit 8 |
| D[`0x5D`] | [6:0] | curve RAM address |
| D[`0x5E`] | | curve data [7:0] |

Load sequence: mode = 0; for i = 0..127: set D[`0x5C`] b4, D[`0x5D`] = i, D[`0x5E`] = data[7:0]
with D[`0x5C`] b5 = data bit 8, clear D[`0x5C`] b4; then thresholds 11g/n = 160, 11b = 160, BPSK =
400; powers 11g/n = `0x32D64`, 11b = `0x32D64`, BPSK = `0x23FD6`; mode = 2. The curve is 128 9-bit
values falling monotonically from 511 to 271 (a soft-compression curve, a constant table in the
library). Resulting D[`0x51`]..D[`0x5B`] = `BE 64 2D 64 2D D6 3F 40 A0 A0 90` [M:s; computed from
the field writes, assuming a window read returns the last written value, I].

**Per-rate digital gain from VM 108** (§4.2.1 A24): defaults as in mode 1; each non-zero VM 108
byte replaces one odd entry (bytes 0, 1, 2, 3, 5, 7, 9, 11, 12, 14, 16, 19 map to D[`0x0B`],
D[`0x0D`], D[`0x0F`], D[`0x11`], D[`0x1B`], D[`0x1D`], D[`0x1F`], D[`0x21`], D[`0x13`], D[`0x15`],
D[`0x17`], D[`0x19`]); all-zero VM 108 leaves the mode-1 values [M:s].

**Calibration results in the windows**: D[4]-D[9] (TX I gain, I phase, Q gain, Q phase, I DC, Q DC
high bytes), D[`0x61`], D[`0x62`] (their 2-bit low fields), D'[9]-D'[`0x0C`] (RX IQ). The quick
path writes them from VM 187. A full calibration additionally leaves R4 bits 4:3 cleared and
D[1]-D[3] = `0x14`, 0, `0x11` (tone generator), which the second PHY init does not restore [M:s].

---

## 7. CPU-to-WiFi MAC register interface

**ERRATA (2026-11-04, reader pass) [PUB WL82.h, M:s all analysed libraries]:** (a) the block at
`0x40000`-`0x40FFF` is the SoC high-speed peripheral bus (`hsfr_base`); the flash controller's
registers start at `0x40200`. It is **not** an RT2800 WPDMA/interrupt block and no WiFi code reads
or writes it; the only users in the libraries are flash, cache, SDRAM, SPI, clock, delay, power and
AES code. The values read at `0x40200`-`0x40210` in probes v21-v23c are flash-controller state,
and the v23b bus fault came from writing into it. §7.1, §7.3, §7.4 steps 3-5, 7 and 9, §7.5 and
§7.5c (WPDMA descriptors and their addressing), the WPDMA path of §7.7, §7.6, §7.8 and §7.9 are
superseded wherever they rely on WPDMA at `0x40000`; do not write that block.
(b) `WL_CTL0` is not the MAC bus gate (§4.2). (c) The production data path is the WDMA engine of
§7.11 and the complete start order is §4.2.1.

**ERRATA (2026-10-28):** the previous version of this section described an SDIO controller at
`0x50000`. That was incorrect: `0x50000` / `0x50008` / `0x5000C` are **GPIO Port A** registers
(`PA_OUT`, `PA_DIR`, `PA_DIE`) per `fm1_gpio.h` [M:t, M:hw]. The AC791N does not use an SDIO bus
for CPU-to-WiFi communication. The vendor's "SDIO" naming is a software abstraction borrowed
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

**Superseded (2026-11-04):** see the erratum at the top of §7. The table below documents
flash-controller registers read under a wrong hypothesis; kept for the record only.

**CONFIRMED [M:hw v21-v23c].** The DMA/interrupt engine uses RT2800 register offsets from base
`0x40000` (not `0x0000` as in standard RT2800, and not `0x0200` as seen in vendor binary [M:s]).

**WDMA DISCOVERY (2026-10-31) [M:s]:** these WPDMA registers are hardware-present [M:hw] but
**not used by the vendor's production WiFi stack**. The vendor's register bridge drops all writes
to offsets below `0x400` (covering all WPDMA offsets `0x200`-`0x29C`) [M:s], and the DMA
enable/disable routines are empty stubs [M:s]. All production TX/RX uses a JieLi-custom "WDMA"
engine at MAC offset `0xF00` instead (§7.11). Our direct MMIO probes bypass the bridge and
confirmed the WPDMA registers are live hardware. However, no successful data transfer has been
achieved via WPDMA on AC791N. The WDMA engine (§7.11) is the vendor-proven path.

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
| `0x4020C` | `0x020C`      | WPDMA_RST_IDX       | `0x00004000`        | DMA ring index reset. Bit fields: [5:0] RST_DTX_IDX0-5 (reset TX queue DMA indices), [16] RST_DRX_IDX0 (reset RX queue DMA index). Linux writes `0x1003f` (all TX + RX0). Previous crash was from writing `0xFFFFFFFF` (undefined upper bits) | M:hw, IP:RT2x00        |
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
| `0x40408` | `0x0408`      | PBF_SYS_CTRL        | (unprobed)          | **Packet Buffer Framework control [IP:RT2x00].** PBF sits between DMA engine and MAC: DMA fetches descriptors, PBF buffers frames, MAC transmits. Linux resets PBF during init: write `0x0e1f` (assert reset bits [4:0]), then `0x0e00` (clear reset, keep enable bits [11:8]). Without PBF reset, DMA register writes may not latch. Address hypothesized at DMA base; untested on AC791N | IP:RT2x00, I           |

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
| `0x30F00` | `0x0F00`      | WDMA_CFG0 (JieLi; earlier "RF config") | `0x467` | M:t, M:s, M:hw |

**Address mapping rule:** AC791N MAC address = `0x30000` + RT2800 offset [M:hw, IP:RT2x00].

### 7.3 Address region summary

| Region            | Address range         | Base       | RT2800 equivalent        | HW status                              | Tag                    |
|-------------------|-----------------------|------------|--------------------------|----------------------------------------|------------------------|
| ~~WPDMA / interrupts~~ high-speed bus (flash controller at `0x40200`) | `0x40200`-`0x4029C`  | `0x40000`  | none (**ERRATA 2026-11-04**) | not WiFi; do not write | PUB, M:s, M:hw |
| **WDMA (production)** | `0x30F00`-`0x30F1C` | `0x30000` | (JieLi custom)          | Vendor production DMA [M:s]. Not yet HW-probed | M:s                    |
| DMA (legacy)      | `0x0200`-`0x0298`    | raw `0`    | (vendor binary refs)     | **Open bus** [M:hw v15-v16]            | M:s, IP:RT2x00         |
| ~~Bus gate~~ radio arbitration (`WL_CON0`) | `0x14000` only       | (JieLi)    | (no RT2800 equivalent)   | assert/release on hardware; never written by the vendor's WiFi start (§4.2). `0x14008` hazard: kills GPIO input | M:t, M:s, M:hw        |
| MAC / BBP control | `0x30000`-`0x31FFF`  | `0x30000`  | MAC_SYS_CTRL, BBP_CSR    | **Confirmed** (after bus gate) [M:hw]  | M:hw, IP:RT2x00       |
| RF analog         | `0x11900`-`0x11978`  | (JieLi)    | (no RT2800 equivalent)   | Untested on HW                         | M:t                    |
| WL control        | `0x14000`-`0x140A0`  | (JieLi)    | (no RT2800 equivalent)   | **Confirmed** [M:hw v17-v18]           | M:t, M:s, M:hw        |
| Clock / reset     | `0x14040`-`0x1405C`  | (JieLi)    | (no RT2800 equivalent)   | **Confirmed** [M:hw v15-v17]           | M:t, M:s, M:hw        |
| BBP indirect      | via `0x3101C`        | `0x30000`  | BBP_CSR_CFG (0x101C)     | Read confirmed [M:hw v23c]             | M:hw, IP:RT2x00       |

**Base address splitting:** ~~JieLi split the RT2800 IP block across two MMIO bases: MAC/BBP at
`0x30000`, DMA/interrupts at `0x40000`.~~ (**ERRATA 2026-11-04:** there is one base, `0x30000`;
`0x40000` is the high-speed bus.) Both use RT2800 register offsets. This is a common SoC
integration pattern for licensed IP [I]. Additionally, JieLi added a custom WDMA engine at
`0x30F00`-`0x30F1C` (within the MAC address space) as a simpler replacement for WPDMA (§7.11).

### 7.4 Initialization sequence (revised)

**Superseded (2026-11-04):** the vendor order is §4.2.1. Steps 2 (bus gate), 3-5, 7 and 9 below
do not exist in the vendor sequence; step 6 (MAC/BBP reset) is §4.2.1 A9-A10, which never ends
with MAC_SYS_CTRL = 0.

The WiFi MAC initialization follows this order [M:s, M:hw, IP:RT2x00]:

1. Clock and reset init via `0x14040`-`0x1405C` (WiFi clock gates and reset) [M:t, M:hw]
2. **Bus gate**: write WL_CTL0 (`0x14000`) non-zero to enable `0x30000`+ and `0x40000`+ access [M:hw]. **Do not write WL_CTL8 (`0x14008`)**: it remaps GPIO pins and kills input [M:hw confirmed 2026-10-30]. **Without WL_CTL0, MAC/DMA reads will bus fault** [M:hw]
3. **Wait WPDMA ready**: poll WPDMA_GLO_CFG (`0x40208`) until TX_DMA_BUSY and RX_DMA_BUSY clear. Disable TX_DMA_EN and RX_DMA_EN [IP:RT2x00]
4. **Reset DMA indices**: write WPDMA_RST_IDX (`0x4020C`) = `0x1003f` to reset all TX (bits 0-5) and RX (bit 16) DMA index counters. Note: writing `0xFFFFFFFF` crashes AC791N; use only defined bits [M:hw, IP:RT2x00]
5. **Reset PBF**: write PBF_SYS_CTRL (`0x40408`?) = `0x0e1f` (assert reset), then `0x0e00` (release reset, keep PBF enabled). The Packet Buffer Framework sits between DMA and MAC; without resetting it, DMA register writes may not latch [IP:RT2x00]
6. **Reset MAC/BBP**: read MAC_SYS_CTRL (`0x31004`), set bits 0-1 (RESET_CSR + RESET_BBP), write back, delay, clear bits 0-3 (reset + TX_EN + RX_EN), write back. This puts the MAC and BBP into a known state before configuration [IP:RT2x00]
7. DMA init: program TX/RX ring base pointers and counts at `0x40230`+/`0x40290`+, set CPU indices to 0, configure WPDMA_GLO_CFG to enable DMA [M:hw, IP:RT2x00]
8. MAC init: configure MAC_SYS_CTRL to enable TX_EN + RX_EN, write MAC address [M:t, M:hw, IP:RT2x00]
9. Interrupt setup: configure INT_MASK_CSR (`0x40204`) to enable desired interrupts [M:hw, IP:RT2x00]
10. RF common init (full RF analog bring-up, §11) [M:t]

**WDMA alternative (2026-10-31):** for WDMA-based operation (§7.11), steps 3-7 and 9 above
(WPDMA-specific) are replaced by the simpler WDMA initialization (§7.11.4): enable WDMA clock
gate, configure WDMA_CTL and WDMA_CFG0. Steps 1-2 (clock/reset/bus gate), 6 (MAC reset), 8
(MAC config), and 10 (RF init) remain the same regardless of DMA engine [I].

**Critical gap (identified 2026-10-31):** steps 3-6 were missing from our implementation. The Linux
rt2800mmio driver (`rt2800_init_registers()` in `rt2800lib.c`, GPL-2.0+) performs these steps before
programming DMA rings. Without PBF reset, the DMA engine may not properly connect to the MAC, and
register writes to DMA ring pointers may not latch (observed: TX_BASE_PTR0 reads back 0 after write
[M:hw v23c]). See §7.9 for the full Linux reference sequence.

### 7.5 DMA descriptor format (WPDMA only)

**Note (2026-10-31):** this section describes the RT2800 WPDMA descriptor-ring format. The WDMA
engine (§7.11) does **not** use descriptors: it transfers data via direct address + length
registers, one packet at a time. This section is retained for completeness and in case WPDMA is
used in the future.

The RT2800 uses scatter-gather DMA with ring buffers [IP:RT2x00]. Each descriptor is
4 x 32-bit words (16 bytes), 16-byte aligned. The RT2800 MMIO format is documented in the
Linux GPL-2.0+ driver (`rt2800mmio.h`). Not yet confirmed on AC791N hardware [IP:RT2x00, I].

**TX descriptor (TXD) — 4 words [IP:RT2x00]:**

| Word | Bits | Field | Description |
|------|------|-------|-------------|
| 0 | [31:0] | SD_PTR0 | Physical address of first scatter buffer (TXWI + frame, or TXWI alone) |
| 1 | [13:0] | SD_LEN1 | Length of second scatter buffer (0 if single-scatter) |
| 1 | [14] | LAST_SEC1 | 1 = SD_PTR1 is the last segment |
| 1 | [15] | BURST | Burst mode |
| 1 | [29:16] | SD_LEN0 | Length of first scatter buffer |
| 1 | [30] | LAST_SEC0 | 1 = SD_PTR0 is the last segment (set 1 for single-scatter) |
| 1 | **[31]** | **DMA_DONE** | **Set by DMA engine when transfer completes. CPU clears before submit.** |
| 2 | [31:0] | SD_PTR1 | Physical address of second scatter buffer (frame data if two-scatter) |
| 3 | [24] | WIV | Wireless IV control (0 = HW inserts IV from IVEIV register) |
| 3 | [26:25] | QSEL | Queue select (2 for normal TX) |
| 3 | [29] | TCO | TX checksum offload |
| 3 | [30] | UCO | UDP checksum offload |
| 3 | [31] | ICO | IP checksum offload |

**ERRATA (2026-10-31):** the previous version of §7.5 listed `DMA_DONE` at word 3 bit 17. This
was incorrect. The RT2800 MMIO driver (`rt2800mmio.h`, GPL-2.0+) defines `TXD_W1_DMA_DONE` as
`FIELD32(0x80000000)` = **bit 31 of word 1**. The previous field layout for word 1 was also
reversed: SD_LEN1 occupies the low 14 bits, SD_LEN0 occupies bits [29:16]. This matches the
two-scatter design where SD_PTR0/LEN0 holds the TXWI and SD_PTR1/LEN1 holds the 802.11 frame.

**RX descriptor (RXD) — 4 words [IP:RT2x00]:**

| Word | Bits | Field | Description |
|------|------|-------|-------------|
| 0 | [31:0] | SDP0 | Physical address of RX buffer |
| 1 | [13:0] | SDL1 | (unused in single-buffer mode) |
| 1 | [29:16] | SDL0 | RX buffer size (must be pre-set by CPU) |
| 1 | [30] | LS0 | Last segment |
| 1 | **[31]** | **DMA_DONE** | **Set by DMA engine when frame received. CPU clears when re-posting.** |
| 2 | [31:0] | SDP1 | Second scatter pointer (unused) |
| 3 | [0] | BA | Block ACK |
| 3 | [1] | DATA | Data frame |
| 3 | [2] | NULLDATA | Null data frame |
| 3 | [3] | FRAG | Fragment |
| 3 | [4] | UNICAST_TO_ME | Destination is our MAC |
| 3 | [5] | MULTICAST | Multicast frame |
| 3 | [6] | BROADCAST | Broadcast frame |
| 3 | [7] | MY_BSS | From associated BSS |
| 3 | [8] | CRC_ERROR | FCS check failed |
| 3 | [10:9] | CIPHER_ERROR | Decryption error status |
| 3 | [14] | L2PAD | L2 padding inserted |
| 3 | [16] | DECRYPTED | Hardware decrypted |

**TX buffer layout (single-scatter) [IP:RT2x00]:**

The DMA buffer pointed to by SD_PTR0 must contain a TXWI header (§7.5a) followed immediately
by the 802.11 frame. SD_LEN0 = sizeof(TXWI) + frame_length. Set LAST_SEC0 = 1, SD_LEN1 = 0.

**TX buffer layout (two-scatter) [IP:RT2x00]:**

SD_PTR0/SD_LEN0 points to the TXWI header. SD_PTR1/SD_LEN1 points to the 802.11 frame.
Set LAST_SEC0 = 0, LAST_SEC1 = 1. The Linux MMIO driver uses this mode.

**RX buffer layout [IP:RT2x00]:**

The MAC writes into the RX buffer: RXWI header (§7.5b) followed by the 802.11 frame.
The actual received frame length is in RXWI word 0 MPDU_TOTAL_BYTE_COUNT, not in the RXD.

### 7.5a TXWI format (TX Wireless Info)

Every TX frame must be preceded by a TXWI header in the DMA buffer. The MAC reads this to
determine PHY rate, power, and framing parameters. Without a valid TXWI, the MAC cannot
transmit. The TXWI is 4 words (16 bytes) for the RT2800 MMIO variant [IP:RT2x00].

| Word | Bits | Field | Description |
|------|------|-------|-------------|
| 0 | [0] | FRAG | More fragments follow |
| 0 | [1] | MIMO_PS | MIMO power save |
| 0 | [2] | CF_ACK | CF-ACK |
| 0 | [3] | TS | Ask for TX timestamp |
| 0 | [4] | AMPDU | A-MPDU aggregation |
| 0 | [7:5] | MPDU_DENSITY | MPDU density (for A-MPDU) |
| 0 | [9:8] | TX_OP | TX operation (0 = normal) |
| 0 | [22:16] | MCS | MCS index (rate-dependent, see below) |
| 0 | [23] | BW | Bandwidth (0 = 20 MHz, 1 = 40 MHz) |
| 0 | [24] | SHORT_GI | Short guard interval |
| 0 | [26:25] | STBC | Space-time block coding |
| 0 | [27] | IFS | Interframe spacing |
| 0 | [31:30] | PHYMODE | PHY mode (see table below) |
| 1 | [0] | ACK | Request ACK from receiver |
| 1 | [1] | NSEQ | Hardware generates sequence number |
| 1 | [7:2] | BW_WIN_SIZE | Block ACK window size |
| 1 | [15:8] | WCID | Wireless client ID (0xFF for broadcast/unassociated) |
| 1 | [27:16] | MPDU_TOTAL_BYTE_COUNT | 802.11 frame length in bytes (excluding TXWI) |
| 1 | [31:28] | PACKETID | Packet ID for TX status reporting |
| 2 | [31:0] | IV | Crypto IV (set to 0 when WIV=0 in TXD) |
| 3 | [31:0] | EIV | Crypto extended IV (set to 0 when WIV=0 in TXD) |

**PHYMODE values [IP:RT2x00]:**

| Value | Mode | MCS field meaning |
|-------|------|-------------------|
| 0 | CCK (802.11b) | 0=1Mbps, 1=2Mbps, 2=5.5Mbps, 3=11Mbps; bit 3=short preamble |
| 1 | OFDM (802.11g) | 0=6M, 1=9M, 2=12M, 3=18M, 4=24M, 5=36M, 6=48M, 7=54M |
| 2 | HT Mixed (802.11n) | MCS index 0-7 (single stream) |
| 3 | HT Greenfield | MCS index 0-7 |

**Minimal TXWI for probe request at 1 Mbps [IP:RT2x00, I]:**

For a basic management frame at 1 Mbps CCK (most compatible for scanning):
- Word 0: PHYMODE=0 (CCK), MCS=0 (1 Mbps), all other bits 0 → `0x00000000`
- Word 1: ACK=0 (broadcast), NSEQ=1 (HW generates seq), WCID=0xFF,
  MPDU_TOTAL_BYTE_COUNT=frame_len → `(frame_len << 16) | 0xFF02`
  (Note: for broadcast probe requests, NSEQ=1 lets the MAC handle sequencing,
  and WCID=0xFF indicates no specific client association)
- Word 2: `0x00000000` (no crypto)
- Word 3: `0x00000000` (no crypto)

### 7.5b RXWI format (RX Wireless Info)

Received frames in the RX DMA buffer are preceded by an RXWI header (4 words, 16 bytes).
The MAC writes this with reception metadata. Software must skip past it to reach the
802.11 frame [IP:RT2x00].

| Word | Bits | Field | Description |
|------|------|-------|-------------|
| 0 | [7:0] | WCID | Wireless client ID |
| 0 | [9:8] | KEY_INDEX | Crypto key index used |
| 0 | [12:10] | BSSID | BSS index |
| 0 | [15:13] | UDF | User-defined field (cipher type) |
| 0 | [27:16] | MPDU_TOTAL_BYTE_COUNT | Received 802.11 frame length |
| 0 | [31:28] | TID | Traffic identifier |
| 1 | [3:0] | FRAG | Fragment number |
| 1 | [15:4] | SEQUENCE | Sequence number |
| 1 | [22:16] | MCS | MCS index |
| 1 | [23] | BW | Bandwidth |
| 1 | [24] | SHORT_GI | Short guard interval |
| 1 | [26:25] | STBC | STBC |
| 1 | [31:30] | PHYMODE | PHY mode (same values as TXWI) |
| 2 | [7:0] | RSSI0 | RSSI chain 0 (AGC value, needs conversion) |
| 2 | [15:8] | RSSI1 | RSSI chain 1 (0 for single-antenna) |
| 2 | [23:16] | RSSI2 | RSSI chain 2 (0 for single-antenna) |
| 3 | [7:0] | SNR0 | SNR chain 0 |
| 3 | [15:8] | SNR1 | SNR chain 1 |

### 7.5c DMA buffer addressing

**Hypothesis [IP:RT2x00, I]:** the WiFi DMA engine at `0x40000` is a bus master on the same
internal bus as the CPU. It accesses memory using the same physical addresses as the CPU:
SRAM at `0x01C00000`+, flash XIP at `0x02000000`+. This is the standard pattern for
SoC-integrated RT2800 derivatives (see `rt2800soc.c` in the Linux driver, which uses
`dma_alloc_coherent()` returning CPU-addressable memory).

Evidence:
- Writing a CPU SRAM address to TX_BASE_PTR0 (`0x40230`) does not crash [M:hw v23c].
  (Writing garbage value DID crash [M:hw v23b], proving the register is live.)
- The linker places .bss at `0x01C08000` and .pool at `0x01C20000` [M:d from linker map].
  DMA ring descriptors in .bss are at approximately `0x01C11860`.

**Unconfirmed:** whether DMA actually fetches data from these addresses. The TX timeout
(DMA_DONE never set despite DMA enabled) could indicate an address mapping issue, or could
be caused by the missing TXWI header (§7.5a) or incorrect descriptor format (§7.5 ERRATA).
Confirm by fixing the TXWI and descriptor format first; if DMA still fails, the buffer
addressing hypothesis needs hardware probing.

**Cache coherency [I]:** the AC791N SRAM may or may not have cache coherency between CPU
writes and DMA reads. If DMA buffers are in cached SRAM, a cache flush may be needed after
writing the TXWI+frame and before kicking DMA. The linker script defines a CACHE region at
`0x01F28000` (28 KiB) which is separate from the main RAM/POOL regions, suggesting the main
SRAM is not cached. This needs confirmation.

### 7.6 IRQ handling

~~The MAC signals events via INT_SOURCE_CSR (`0x40200`) [M:hw, IP:RT2x00]. The vendor registers an
interrupt handler that reads this register, ACKs by writing back the active bits (W1C), and defers
processing to a background task [M:s].~~ **ERRATA (2026-11-04):** the vendor's WiFi interrupt is
line 52; its handler only clears `WF_CON4` (`0x14050`) bit 0 and wakes the receive task. It reads
and acknowledges no MAC or `0x40000` register. See §7.11.9 [M:s wl_rf_common.a]. The interrupt mask at `0x40204` controls which events
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

**WPDMA path** (ring-based, §7.1). Based on the RT2800 DMA ring architecture [IP:RT2x00]:

- **TX**: CPU writes frame into a TX descriptor buffer, updates TX_CTX_IDX to advance the ring.
  MAC DMA fetches the descriptor, transmits, advances TX_DTX_IDX, and raises TX-done interrupt.
- **RX**: MAC DMA writes received frames into pre-posted RX descriptor buffers, advances
  RX_DRX_IDX (read-only by CPU), and raises RX-done interrupt. CPU reads frames up to RX_CRX_IDX.

**WDMA path** (synchronous, §7.11). The vendor production path [M:s]:

- **TX**: CPU writes TXWI + frame into a contiguous buffer, programs WDMA_TX_ADR and WDMA_TX_LEN.
  Hardware DMA's the buffer to the MAC, sets completion bit. One packet at a time.
- **RX**: CPU polls WDMA_CTL bit 17 for data available, then performs a two-phase DMA read (4-byte
  length word, then RXWI + frame). One frame at a time; re-check bit 17 for more.

**Rate control**: per-packet TX rate selection via the TXWI PHYMODE/MCS fields (§7.5a) [M:s, IP:RT2x00].

### 7.8 What needs hardware probes

**Confirmed by probes v14-v23c [M:hw]:**

| Fact | Result | Probe | Tag |
|------|--------|-------|-----|
| ~~**DMA at `0x40000` base**~~ | **ERRATA 2026-11-04:** flash-controller / high-speed bus, not WiFi (§7 header) | v21-v23c | M:hw, PUB |
| MAC_SYS_CTRL (`0x31004`) readable | Yes, returns `0x0C` (TX_EN + RX_EN per RT2800) | v18, v23c | M:hw, IP:RT2x00 |
| CHIP_VERSION (`0x31000`) | `0x00300100` | v19, v23c | M:hw |
| BBP_CSR_CFG (`0x3101C`) readable | Yes, returns `0x0008D000` | v19, v23c | M:hw, IP:RT2x00 |
| MAC_ADDR_DW0 (`0x30300`) | `0x00007001` | v23c | M:hw |
| XIFS_TIME_CFG (`0x31100`) | `0x33A40C0B` | v23c | M:hw, IP:RT2x00 |
| BKOFF_SLOT_CFG (`0x31104`) | `0x00140314` | v23c | M:hw, IP:RT2x00 |
| TX_SW_CFG0 (`0x31330`) | `0x00040804` | v23c | M:hw, IP:RT2x00 |
| ~~WL_CTL0 (`0x14000`) is bus gate~~ | ~~Yes, `0x30000`+/`0x40000`+ fault without it~~ **ERRATA 2026-11-04:** assert/release control; the vendor never writes it (§4.2) | v13 vs v18 | M:hw, M:s |
| WL_CTL8 (`0x14008`) kills input | Writing `0xFFFF` remaps GPIO, kills 74HC595 shift register on PA1/PA3/PA4. Not needed for bus gating | v30 bisect | M:hw |
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
| WL_CTL0 minimal gate bits         | [M:hw]            | Which bit(s) enable MAC/DMA access? (0xFFFF works; reads back 0xF103, so only some bits matter) |
| TX queues 2-5                     | [IP:RT2x00]       | Expected at `0x40260`-`0x40288`, untested |
| PBF_SYS_CTRL address              | [IP:RT2x00]       | Hypothesized at `0x40408` (DMA base + 0x408). Could be `0x30408` (MAC base). Probe both |
| PBF_SYS_CTRL effect               | [IP:RT2x00]       | Does PBF reset cause TX_BASE_PTR0 writes to latch? Key test for DMA bring-up |

### 7.9 RT2800 init_registers reference [IP:RT2x00]

The Linux `rt2800_init_registers()` function (in `drivers/net/wireless/ralink/rt2x00/rt2800lib.c`,
GPL-2.0+) performs a hardware reset sequence that our driver was missing. The sequence relevant to
DMA bring-up (extracted from `rt2800mmio_enable_radio()` call chain):

**Phase 1: Queue init (`rt2800mmio_init_queues`)**
1. For each TX queue: write `TX_BASE_PTRn`, `TX_MAX_CNTn`, `TX_CTX_IDXn` = 0
2. For RX queue: write `RX_BASE_PTR`, `RX_MAX_CNT`, `RX_CRX_IDX` = max_count - 1
3. Call `rt2800_disable_wpdma()`: clear TX_DMA_EN and RX_DMA_EN in WPDMA_GLO_CFG

**Phase 2: Register init (`rt2800_init_registers`)**
1. `rt2800_wait_wpdma_ready()`: poll until DMA not busy
2. Disable DMA: clear TX/RX DMA enable bits in WPDMA_GLO_CFG
3. **`WPDMA_RST_IDX = 0x1003f`**: reset all TX DMA indices (bits 0-5) and RX index (bit 16)
4. Many MAC/BBP register configuration writes (XIFS, backoff, TX power, etc.)
5. **`PBF_SYS_CTRL = 0x0e1f`** then **`PBF_SYS_CTRL = 0x0e00`**: reset Packet Buffer Framework
6. **`MAC_SYS_CTRL |= 0x03`** then **`MAC_SYS_CTRL &= ~0x03`**: reset MAC CSR and BBP
7. More register configuration

**Phase 3: Enable radio**
1. `rt2800_init_bbp()`: BBP calibration and configuration
2. `rt2800_init_rfcsr()`: RF calibration
3. Enable DMA: set TX_DMA_EN and RX_DMA_EN in WPDMA_GLO_CFG

**Key insight:** Linux writes ring base pointers (phase 1) then resets PBF and MAC/BBP (phase 2)
before enabling DMA (phase 3). The PBF reset is critical because the Packet Buffer Framework
connects the DMA engine to the MAC: without it in a known state, DMA descriptor fetches may fail
silently. The MAC_SYS_CTRL register value `0x0C` (TX_EN + RX_EN) observed on hardware [M:hw v23c]
indicates the MAC was left enabled from a previous boot or vendor init; resetting it ensures a
clean state.

**Our gap (pre-2026-10-31):** we jumped from bus gate directly to programming DMA rings and enabling
DMA, skipping the PBF reset, MAC reset, and DMA index reset entirely.

### 7.10 MAC configuration registers [IP:RT2x00, RT5350 §3.20.3.5]

After DMA and MAC reset, the Linux `rt2800_init_registers()` configures several MAC
registers before enabling TX/RX. core-fm1 was missing all of these until 2026-10-31.

**RX_FILTER_CFG (`0x31400`)** -- the most critical gap. This register controls which
received frame types the MAC hardware accepts vs. silently drops before DMA. Each bit
is `1=drop, 0=accept`. Hardware default has `DROP_NOT_MYBSS=1` (bit 3), which drops
ALL frames from BSS IDs not matching the configured BSSID. During scanning, no BSSID
is set, so every beacon and probe response is dropped. This was the root cause of
zero scan results despite successful probe request transmission.

| Bit | Name | Default | Linux init | Purpose |
|-----|------|---------|------------|---------|
| 0 | DROP_CRC_ERR | 1 | 1 | Drop CRC errors |
| 1 | DROP_PHY_ERR | 1 | 1 | Drop PHY errors |
| 2 | DROP_UC_NOME | 1 | 1 | Drop unicast not-to-me |
| 3 | DROP_NOT_MYBSS | 1 | **0** | Drop frames from other BSS |
| 4 | DROP_VER_ERR | 1 | 1 | Drop 802.11 version errors |
| 5 | DROP_MC | 0 | 0 | Drop multicast |
| 6 | DROP_BC | 0 | 0 | Drop broadcast |
| 7 | DROP_DUPL | 1 | 1 | Drop duplicates |
| 8 | DROP_CFACK | 1 | 1 | Drop CF-ACK |
| 9 | DROP_CFEND | 1 | 1 | Drop CF-END |
| 10 | DROP_ACK | 1 | 1 | Drop ACK |
| 11 | DROP_CTS | 1 | 1 | Drop CTS |
| 12 | DROP_RTS | 1 | 1 | Drop RTS |
| 13 | DROP_PSPOLL | 0 | 1 | Drop PS-Poll |
| 14 | DROP_BA | 1 | 1 | Drop Block ACK |
| 15 | DROP_BAR | 0 | 0 | Drop Block ACK Request |
| 16 | DROP_CTRL_RSV | 1 | 1 | Drop reserved control subtypes |

Linux initial value: `0x17F97`. Our value: same for scanning, `|= 0x08` after association.

**Other MAC config registers set during init:**

| Register | Offset | AC791N addr | Linux value | Purpose |
|----------|--------|-------------|-------------|---------|
| AUTO_RSP_CFG | 0x1404 | 0x31404 | 0x13 | Enable auto CTS/ACK (bit0=enable, bit1=BA policy, bit4=CCK short) |
| LEGACY_BASIC_RATE | 0x1408 | 0x31408 | 0x015F | Basic rate bitmap: 1,2,5.5,11,6,12,24 Mbps |
| HT_BASIC_RATE | 0x140C | 0x3140C | 0x00FF | HT basic rates: MCS 0-7 |
| MAX_LEN_CFG | 0x1018 | 0x31018 | 0x2FFF | Max MPDU length (bits [13:0]) |
| RX_STA_CNT0-2 | 0x1700+ | 0x31700+ | read-to-clear | Error counters (PHY/CRC/PLCP/CCA/overflow) |
| TX_STA_CNT0-2 | 0x170C+ | 0x3170C+ | read-to-clear | TX counters (beacon/fail/retry/success) |

**Full MAC configuration table** (22 entries, written in this order at interface up, §4.2.1 B2)
[M:s wl_wifi_sta.a; names IP:RT2x00]. **ERRATA (2026-11-04):** the RT2800 names of eleven rows
were wrong in the previous revision (old names in the last column).

| AC791N addr | Value        | RT2800 name (`rt2800.h`) | Previous name in this sheet |
|-------------|--------------|--------------------------|-----------------------------|
| 0x3042C     | 0xF8F0E8E0   | BCN_OFFSET0              | LEGACY_PROT_CFG (wrong)     |
| 0x30430     | 0x6F77D0C8   | BCN_OFFSET1              | HT_PROT_CFG (wrong)         |
| 0x31408     | 0x013F       | LEGACY_BASIC_RATE        | same                        |
| 0x3140C     | 0x8003       | HT_BASIC_RATE            | same                        |
| 0x31400     | 0x17F97      | RX_FILTER_CFG            | same                        |
| 0x31350     | 0x1020       | TX_LINK_CFG              | same                        |
| 0x31018     | 0x1F00       | MAX_LEN_CFG              | same                        |
| 0x3040C     | 0x1F3FBF9F   | PBF_MAX_PCNT (bytes = per-queue page limits; byte 1 is the limit the vendor's TX gate uses for queue 2, §7.11.5) | CCK_PROT_CFG (wrong) |
| 0x3134C     | 0x47D01F0F   | TX_RTY_CFG               | TX_RTS_CFG (wrong)          |
| 0x31404     | 0x13         | AUTO_RSP_CFG             | same                        |
| 0x31364     | 0x05740003   | CCK_PROT_CFG             | OFDM_PROT_CFG (wrong)       |
| 0x31368     | 0x05740003   | OFDM_PROT_CFG            | MM20_PROT_CFG (wrong)       |
| 0x30408     | 0x00F40006   | PBF_CFG                  | unknown                     |
| 0x31370     | 0x03F44084   | MM40_PROT_CFG            | same                        |
| 0x31374     | 0x01744004   | GF20_PROT_CFG            | same                        |
| 0x31378     | 0x03F44084   | GF40_PROT_CFG            | same                        |
| 0x3136C     | 0x01744004   | MM20_PROT_CFG            | unknown (20 MHz alias)      |
| 0x31340     | 0x583F       | TXOP_CTRL_CFG            | EDCA_TID_AC_MAP (wrong)     |
| 0x31344     | 0x092B20     | TX_RTS_CFG               | TX_TIMEOUT_CFG (wrong)      |
| 0x31380     | 0x002400CA   | EXP_ACK_TIME             | same                        |
| 0x31608     | 0x02         | TXOP_HLDR_ET             | unknown                     |
| 0x31204     | 0x03         | PWR_PIN_CFG              | unknown                     |

The values are those of the table; the vendor radio's steady state differs in several of them
(e.g. `0x31350` = `0x007F1020`, `0x3040C` = `0x1F3F7F9F`, protection and EDCA registers), because
the RT2800 layer rewrites them at association time or from its periodic task [M:hw, M:s]. Writes to
MAC offsets below `0x400`, `0x500`-`0x1007` (MAC_SYS_CTRL included), `0x101C`-`0x101F` (BBP port),
`0x102C`-`0x102F`, `0x1100`-`0x1107`, `0x1314`-`0x1327`, `0x1330`-`0x133B` and `0x2000`-`0x2FFF`
coming from the RT2800 layer are dropped by the vendor's register filter; the registers in those
ranges that matter (MAC_SYS_CTRL, timing, BBP port, `0x308`, `0x30C`, `0x324`, `0x328`) are written
by the vendor's own low-level code instead (§4.2.1) [M:s wl_rf_common.a].

~~TX_LINK_CFG, EDCA_TID_AC_MAP, and TX_TIMEOUT_CFG are critical for TX scheduling:
without them the MAC hardware cannot dispatch DMA'd frames to the air interface.~~ (**ERRATA
2026-11-04:** two of these three names were wrong, see the table; the claim was an inference.)
EDCA_AC0-AC3 (0x31300-0x3130C) are NOT in the static table; they are written dynamically when the
MLME state machine starts [M:s].

### 7.11 JieLi WDMA engine (MAC offset 0xF00)

**Discovery date: 2026-10-31 [M:s].** The vendor radio library reveals that JieLi replaced
the standard RT2800 WPDMA ring-based DMA engine with a custom "WDMA" (WiFi DMA) block at MAC
offset `0xF00` [M:s]. The standard WPDMA registers documented in §7.1 (`0x40200`-`0x4029C`) are
**hardware-present** [M:hw v21-v23c] but **silently ignored by the vendor's register bridge**:
all writes to offsets below `0x400` (which includes all WPDMA offsets `0x200`-`0x29C`) are
dropped [M:s]. The RT2800 DMA enable and disable routines are empty stubs [M:s]. All production
TX/RX uses WDMA exclusively [M:s].

**Architecture: synchronous, single-packet DMA.** Unlike RT2800 WPDMA which uses ring buffers
of scatter-gather descriptors (§7.5), WDMA operates on one packet at a time: software writes a
buffer address and length, the hardware transfers the data, and software polls for completion
[M:s]. There are no descriptor rings, no index registers, and no scatter-gather [M:s].

#### 7.11.1 WDMA register map

All WDMA data registers reside within the MAC address space at base `0x30000` [M:s]. The clock
gate is at a SoC-level address.

| Address   | Offset | Name        | R/W | Function                                                           | Tag |
|-----------|--------|-------------|-----|--------------------------------------------------------------------|-----|
| `0x1404C` | (SoC)  | CLK_GATE    | R/W | Bit 24: WDMA clock enable. Must be set before any WDMA access     | M:s |
| `0x30F00` | 0xF00  | WDMA_CFG0   | R/W | DMA configuration (§7.11.2)                                       | M:s |
| `0x30F04` | 0xF04  | WDMA_CTL    | R/W | DMA control and status (§7.11.3)                                  | M:s |
| `0x30F10` | 0xF10  | WDMA_TX_ADR | W   | TX DMA source address (physical, 4-byte aligned)                  | M:s |
| `0x30F14` | 0xF14  | WDMA_TX_LEN | W   | TX transfer length in bytes. **Writing triggers DMA**              | M:s |
| `0x30F18` | 0xF18  | WDMA_RX_ADR | W   | RX DMA destination address (physical, 4-byte aligned)             | M:s |
| `0x30F1C` | 0xF1C  | WDMA_RX_LEN | W   | RX transfer length in bytes. **Writing triggers DMA**              | M:s |

**Clock gate dependency:** bit 24 of `0x1404C` (WL_CLK3 in §4.2) must be set before any WDMA
register access [M:s]. Hardware value after WiFi clock init is `0x01000000` (bit 24 already set)
[M:hw].

#### 7.11.2 WDMA_CFG0 (0x30F00) bit fields

Configured once during WDMA initialization [M:s].

| Bits  | Init value      | Purpose        | Tag |
|-------|-----------------|----------------|-----|
| 3:0   | `0xF` (all set) | Enable bits    | M:s |
| 4     | `0` (clear)     | Unknown        | M:s |
| 7:5   | `011` (value 3) | Mode select    | M:s |
| 9:8   | `00` (clear)    | Unknown        | M:s |
| 10    | `1` (set)       | Enable         | M:s |
| 31:11 | unknown         | Not documented | --  |

**Init value: `0x46F`** (bits 0-3, 5-6, 10 set) [M:s].

#### 7.11.3 WDMA_CTL (0x30F04) bit fields [revised 2026-11-04]

| Bit   | R/W | Function                                                                    | Tag |
|-------|-----|-----------------------------------------------------------------------------|-----|
| 0, 1, 2, 4, 5, 9 | W | Set during init (separate read-modify-writes, order in §4.2.1 A6); meanings unknown | M:s |
| 3     | W   | Cleared during init                                                         | M:s |
| 6-8   |     | Unknown; never written                                                      | M:s |
| 10    | W   | **TX acknowledge/clear.** Written 1 before every TX start and again after TX done is seen. Reads back 0 (self-clearing) [M:hw] | M:s, M:hw |
| 11    | W   | **RX acknowledge/clear.** Written 1 before RX phase 1 and after each RX phase completes. Reads back 0 [M:hw] | M:s, M:hw |
| 12-15 |     | Unknown; never written or read                                              | M:s |
| 16    | R   | **Read-only status.** 1 on the running vendor radio (`0x00010037`), 0 on core-fm1 (`0x37`); refuses writes held and released [M:hw]. **Never read, polled, written or masked by any vendor code** (no access to `0x30F04` uses this bit, and no accessor call addresses `0xF04`) [M:s wl_rf_common.a, wl_wifi_sta.a]. Meaning unknown; next to bit 17, consistent with "MAC datapath running" [I] | M:s, M:hw, I |
| 17    | R   | **Received frame available.** The first test of the receive path; when clear, the receive task does nothing (and does not re-open the interrupt mask) | M:s |
| 18-19 |     | Unknown; never accessed                                                     | M:s |
| 20    | R   | **TX DMA complete.** Polled without bound after a TX start; cleared by the bit-10 acknowledge [I for the clearing] | M:s |
| 21    | R   | **RX DMA complete.** Polled without bound after each RX phase; cleared by the bit-11 acknowledge [I for the clearing] | M:s |
| 31:22 |     | Unknown; never accessed                                                     | M:s |

The vendor reaches `0x00010037` at steady state: bits 0, 1, 2, 4, 5 from the init, bits 9-11 read
back 0 (9 is written 1 at init and reads 0 on hardware, so it also behaves as a strobe [M:hw, I]),
bit 16 set by hardware.

#### 7.11.4 WDMA initialization sequence [revised 2026-11-04]

**ERRATA (2026-11-04):** the vendor runs the WDMA init directly after the clock tree, **before**
any MAC/BBP reset and before the PHY/RF init (§4.2.1 A5-A7), not "after clock/reset and bus gate";
there is no bus gate step. Steps 4-5 come at the very end of the start (B11, B13). Order [M:s
wl_rf_common.a]:

1. **Enable WDMA clock**: set bit 24 of `0x1404C` (A5).
2. **Configure WDMA_CTL** (`0x30F04`): nine read-modify-writes, `|= 0x800`, `|= 0x400`, `|= 0x200`,
   `|= 0x20`, `|= 0x10`, `&= ~0x8`, `|= 0x4`, `|= 0x2`, `|= 0x1` (A6). Bit 16 is never written.
3. **Configure WDMA_CFG0** (`0x30F00`): nine read-modify-writes ending in `0x46F` (A7).
4. **MAC receive-notify source**: MAC+`0x328` = 0, register interrupt 52, MAC+`0x328` = 2 (B13).
   Without the final 2, `WDMA_CTL` bit 17 never asserts [M:hw 2026-10-31]. The vendor never reads
   `0x30328` and its interrupt handler never touches it [M:s].
5. **WiFi interrupt unmask** (`0x14050` bit 0): set when the first receive buffer is posted (B11),
   which happens before step 4. In the interrupt-driven model the handler clears it and the
   receive path sets it again (§7.11.9).

No delays between steps 1-3 [M:s].

#### 7.11.5 TX behavioral contract [revised 2026-11-04]

WDMA TX is synchronous: one packet at a time, poll for completion [M:s].

**Transfer image (what the WDMA moves to the MAC)** [M:s wl_rf_common.a; consistent with the
2026-11-02 buffer samples, M:hw]. **ERRATA:** the transfer does not start at the TXWI and its
length is not "TXWI + frame"; the first word the MAC receives is a 4-byte queue word:

| Offset in the transfer | Size | Content |
|------------------------|------|---------|
| 0 | 4 | **Queue word**: bit 0 = WIV (the "wireless info valid" flag of the RT2800 USB TXINFO word), bits [2:1] = queue select QSEL; bits [31:3] = 0 in practice. The vendor uses QSEL = 2 for every frame: management frames (probe request, QoS null) carry 5 (WIV 1), data frames 4 (WIV 0) |
| 4 | 16 | TXWI (§7.5a) |
| 20 | MPDU | 802.11 frame, **not padded**; MPDU = TXWI word 1 [27:16] |
| 20 + MPDU | 2 | two bytes copied after the frame (zero padding of the RT2800 layer) |
| 22 + MPDU | 4 | four further bytes, whatever the staging buffer holds (0 after boot) |

Transfer length = MPDU + 26 bytes, written to `WDMA_TX_LEN` with bit 31 = 0. The field
correspondence with RT2800 [IP:RT2x00]: the queue word is the USB TXINFO word (WIV = bit 24, QSEL
= bits [26:25] there) reduced to its low three bits; the length field of TXINFO is not sent. The
vendor keeps a software length word (MPDU + 22) 4 bytes in front of the queue word in its staging
buffer; that word is **not** transferred. It explains the measured buffer samples: 104 for MPDU 82
and 48 for MPDU 26 [M:hw 2026-11-02] (the earlier formula "16 + round_up_4(frame) + 4" matched
those two samples only because both lengths are 2 mod 4).

**TX readiness gate (before every transfer)** [M:s wl_rf_common.a]. With q = the frame's QSEL (2):

1. Read TXRXQ_PCNT (`0x30438`), byte q (pages in TX queue q). It must be below the queue's page
   limit (byte 1 of the value written to PBF_MAX_PCNT `0x3040C`, i.e. `0xBF` from the table), and
   (limit − pages) x 128 must exceed the MPDU length.
2. Read `0x30434`; bit (8q + 2) = bit 18 must be 0 (queue busy/full flag, inferred).
3. Read PBF_DBG `0x3043C`; bits [7:5] must be non-zero. If this is the only failing test the vendor
   prints a marker character, i.e. it is an expected transient.
4. If any test fails: release the bus lock, wait (10 ms, or yield) and retry from 1, without bound;
   every 10 s the vendor prints `0x30438`, `0x3040C`, `0x30434`, `0x3043C`, `0x31500`, `0x31504`.

`0x30434` is not named in the public RT2800 header. Captured on core-fm1 before a probe request, block
released, front end enabled [M:hw 2026-11-04 12l]: `0x3043C` = `0x8400A0FE` ([7:5] = 7, ready), `0x30434` =
`0x22020602` (bit 18 clear), `TXRXQ_PCNT` = 0 before and `0x00010000` right after the transfer (one frame in
queue 2), `TX_STA_CNT1` [15:0] incremented, `MAC_STATUS_CFG` bit 0 set during the transmit. The transfer
image above (queue word first, MPDU + 26) is confirmed: it is the first layout that the MAC accepted.
`WDMA_TX_ADR` reads back queue word + 64 for MPDU 35, so the engine rounds the transfer up to a
multiple of 4 [M:hw 12l].

**TX sequence** [M:s wl_rf_common.a]:

1. Set WDMA_CTL bit 10 (acknowledge/clear).
2. Barrier (`csync`).
3. `WDMA_TX_ADR` = address of the queue word (4-byte aligned).
4. `WDMA_TX_LEN` = MPDU + 26 (bit 31 = 0). **This write starts the transfer.**
5. Barrier.
6. Poll WDMA_CTL bit 20 until set (no bound in the vendor; a timeout is advisable [I]).
7. Set WDMA_CTL bit 10.
8. Optional fixed delay (configuration value; 0 by default).

Completion is reported to the network layer synchronously; TX status (TX_STA_FIFO `0x31718`) is
read only by the periodic statistics task and nothing waits for it [M:s wl_wifi_sta.a].

**TXWI as the vendor builds it** [M:s wl_wifi_sta.a; layout IP:RT2x00]:

| Field | Management (probe request) | Data |
|-------|----------------------------|------|
| W0 TXOP [9:8] | **3** (backoff) | 0 |
| W0 MPDU density [7:5] | 0 | per station |
| W0 MCS / PHYMODE | management rate (CCK 1 Mbps during a 2.4 GHz scan [I]) | rate control |
| W1 ACK [0] | per frame (0 for broadcast) | per frame |
| W1 NSEQ [1] | 0 (software sequence numbers) | 0 |
| W1 BA window [7:2] | min(configured, 7) | per BA |
| W1 WCID [15:8] | `0xFF` | station entry |
| W1 MPDU [27:16] | frame length, unpadded | frame length |
| W2, W3 | 0 | IV / EIV |

Measured vendor QoS-null and data TXWIs carry TXOP 0 [M:hw 2026-11-02], so the hardware accepts
TXOP 0 at least on that path.

**Measured buffer samples (2026-11-02), kept with corrections:** the table below is the previous
text. Offsets 0 and 4 are the software length word and the queue word (not "purpose unknown");
the TXWI at offset 8 is the second transferred word group, not the first; "total length = 16 +
round_up_4(frame) + 4" is superseded by MPDU + 22.

**Previous text (2026-10-31 .. 2026-11-02), kept as errata:**


WDMA TX is synchronous: one packet at a time, poll for completion [M:s].

**Buffer layout:** the TX buffer must be contiguous [M:s]:

| Offset | Size     | Content                                    | DMA'd? |
|--------|----------|--------------------------------------------|--------|
| 0      | 4 bytes  | Total length (TXWI + frame), software only | No     |
| 4      | 16 bytes | TXWI (§7.5a)                               | Yes (DMA source starts here) |
| 20     | variable | 802.11 frame                               | Yes    |

**TX sequence (behavioral description):**

1. Acknowledge/clear pending TX: set WDMA_CTL bit 10 [M:s]
2. Memory barrier (`csync` instruction on pi32v2) [M:s]
3. Write TX source address to WDMA_TX_ADR: the physical address of the TXWI (buffer + 4,
   skipping the 4-byte software header) [M:s]
4. Write the total transfer length (TXWI size + frame size) to WDMA_TX_LEN. **This write
   triggers the DMA transfer** [M:s]
5. Memory barrier (`csync`) [M:s]
6. Poll WDMA_CTL bit 20 until set (TX DMA complete) [M:s]. A practical timeout of ~10 ms
   is recommended to avoid hanging the system [I]
7. Acknowledge completion: set WDMA_CTL bit 10 [M:s]

**Alignment:** TX source address must be 4-byte aligned [I, inferred from 32-bit address register].

**Buffer sizing:** 4-byte header + 16-byte TXWI + 1600-byte max frame = 1620 bytes total [I].

**Measured TX buffer layout [M:hw 2026-11-02]:** sampled on a running vendor build via its HTTP
debug endpoint (`WDMA_TX_ADR` read back after each frame plus the bytes around it), six frames:

| Offset from length word | Size | Content |
|-------------------------|------|---------|
| 0 | 4 | total length = 16 + round_up_4(frame) + 4 (observed 104 for an 84-byte frame, 48 for a 26-byte QoS null) |
| 4 | 4 | small integer: 4 for a data frame, 5 for a QoS null (purpose unknown) |
| 8 | 16 | TXWI |
| 24 | frame | 802.11 frame, padded with zeros to a 4-byte boundary |
| 24 + pad4(frame) | 4 | zero word |

- `WDMA_TX_ADR` reads back as TXWI start + total length, i.e. the DMA **end** address, in every
  sample [M:hw]. The register is therefore readable, and the end pointer alone cannot show
  whether the transfer began at the TXWI (step 3 above) or at the length word with length + 8.
- The vendor TXWI uses software sequence numbers (NSEQ=0), BW_WIN_SIZE=7, MPDU_DENSITY=5,
  ACK=1 and WCID=1 for unicast data to the associated AP; MPDU_TOTAL_BYTE_COUNT is the unpadded
  frame length [M:hw].
- The hardware-set bit 16 of `WDMA_CTL` and `MAC_STATUS_CFG` (`0x31200`, read-only per
  [IP:RT2x00]) stay as documented; see §7.11.3.

#### 7.11.6 RX behavioral contract

WDMA RX is two-phase: first read a 4-byte length word, then read the frame [M:s].

**RX availability:** check WDMA_CTL bit 17. When set, the MAC has a received frame ready [M:s].

**Phase 1: read the 4-byte RX length word**

1. Acknowledge/clear pending RX: set WDMA_CTL bit 11 [M:s]
2. Memory barrier (`csync`) [M:s]
3. Write RX destination address to WDMA_RX_ADR (start of RX buffer) [M:s]
4. Write `4` to WDMA_RX_LEN (triggers DMA of 4 bytes) [M:s]
5. Memory barrier (`csync`) [M:s]
6. Poll WDMA_CTL bit 21 until set (RX DMA complete) [M:s]
7. Acknowledge: set WDMA_CTL bit 11 [M:s]
8. Parse length: byte-swap the 32-bit value (big-endian wire format from MAC) [M:s].
   Valid range: 1-4088 bytes. Zero or >= 4089 indicates error [M:s]

**Phase 2: read the RXWI + frame**

1. Write destination address to WDMA_RX_ADR (buffer + 4, past the length word) [M:s]
2. Write `rx_len + 4` to WDMA_RX_LEN (triggers DMA) [M:s]
3. Memory barrier (`csync`) [M:s]
4. Poll WDMA_CTL bit 21 until set [M:s]
5. Acknowledge: set WDMA_CTL bit 11 [M:s]
6. Buffer at offset 4 now contains RXWI (16 bytes, §7.5b) followed by the 802.11 frame [M:s]

**Byte-swap:** the RX length word arrives in big-endian format [M:s]. On pi32v2 (little-endian),
byte-swap is required. If garbage lengths are observed, try without byte-swap: the DMA engine
may already perform byte-swapping on some configurations [I].

**Pre-RX MAC write:** the vendor writes value `2` to MAC register offset `0x324` (address
`0x30324`) before each RX DMA read [M:s]. Purpose unknown. Include for compatibility; remove
if it causes bus faults [I].

**Confirmed on core-fm1 2026-11-04 (12o) [M:hw]:** with the block released, the front end enabled
(§4.2.1 C2-C3), the channel code path and the LUT bank patch (§4.2.1 C1, §5.2) in place, the two-phase
read above delivers frames: 38 frames in one 13-channel scan, beacons of 224-377 bytes and 24-byte
control frames, 16 BSSIDs with correct SSIDs and channels, `RX_STA_CNT0` counting CRC and PHY errors
per dwell. Delivery was by polling `WDMA_CTL` bit 17; interrupt 52 did not fire during the scan
(`0x14050` bit 0 set, line registered). A handler that masked nothing and wrote MAC+0x328, combined
with an unbounded drain, stalled the CPU until the watchdog reset on the first reception (12n).

**Receive path as the vendor runs it (revised 2026-11-04)** [M:s wl_rf_common.a; trailer layout
IP:RT2x00 `rt2800usb.h` RXD word]:

1. Woken by interrupt 52 (handler: `WF_CON4` bit 0 cleared, receive task woken) or by a timeout of
   11 OS ticks (about 110 ms [I]). On a timeout the runtime-AGC idle step of §6.7 runs.
2. Test `WDMA_CTL` bit 17. Clear: return (every 110 ms the AGC idle step again); the interrupt
   mask stays closed until a later wake finds a frame or a buffer is re-posted.
3. If no receive buffer is free, return; the frame stays in the hardware and the mask stays
   closed.
4. MAC+`0x324` = 2; phase 1 and phase 2 as above (length word byte-swapped, valid 1..4088; outside
   that range the vendor prints a diagnostic and resets the system).
5. Buffer layout after phase 2: [0] length n | [4] RXWI (16 bytes, §7.5b) | [20] frame | [4 + n]
   a 4-byte trailer (byte-swapped like the length) whose bits follow the RT2800 USB receive
   descriptor word: CRC error bit 8, cipher error bits [10:9], L2 padding bit 14, decrypted bit 16
   [IP:RT2x00]. Frames with length 0, a length not a multiple of 4, an RXWI MPDU count larger than
   the length, or a CRC error are dropped by the RT2800 layer [M:s wl_wifi_sta.a].
6. Runtime AGC update from the frame's RSSI0 (§6.7).
7. A software filter may drop the frame: then `WF_CON4` bit 0 is set again at once and the buffer
   is kept. Otherwise the frame is handed up; `WF_CON4` bit 0 is set again when the network layer
   re-posts the (single, 4096-byte) receive buffer.

**Multiple frames:** after processing one frame, re-check WDMA_CTL bit 17. If set, repeat
the two-phase read for the next frame [M:s].

**Timeouts:** poll loops should use timeouts. Recommended: 1 ms for RX phase 1/2 (single-frame
DMA completes in microseconds), 10 ms for TX [I].

**Buffer sizing:** 4-byte length + max 1600-byte frame = 1604 bytes minimum [I].

#### 7.11.7 WDMA vs WPDMA summary

| Property     | WPDMA (§7.1)                           | WDMA (§7.11)                                  |
|--------------|----------------------------------------|-----------------------------------------------|
| Registers    | `0x40000` + RT2800 offset              | `0x30000` + `0xF00`                           |
| Architecture | Ring-based scatter-gather              | Synchronous single-packet                     |
| Descriptors  | 16-byte TXD/RXD rings (§7.5)          | None (direct address + length)                |
| TX flow      | Fill descriptor, kick index, poll done | Write address + length, poll status bit       |
| RX flow      | Pre-post descriptors, reap on done     | Check available bit, two-phase read           |
| Throughput   | Multiple outstanding transfers         | One transfer at a time                        |
| HW presence  | ~~Confirmed [M:hw v21-v23c]~~ not WiFi (§7 header) | Within MAC space [M:s]; probed: TX transfers complete, CFG0/CTL read back [M:hw] |
| Vendor usage | Writes NOP'd by bridge [M:s]          | All production TX/RX [M:s]                    |

**Implementation note [I]:** WDMA is simpler and vendor-proven. WPDMA is RT2800-compatible with
potential throughput advantages from ring buffers, but the vendor's bridge layer drops all WPDMA
writes, and no successful data transfer has been achieved via WPDMA on AC791N. For a first
implementation, WDMA is the recommended path.

#### 7.11.8 What needs hardware probes (WDMA-specific)

| Fact | Hypothesis source | Status | Notes |
|------|-------------------|--------|-------|
| WDMA_CFG0 (`0x30F00`) readable | [M:s] | **Confirmed** [M:hw] | Reads `0x467` (bit 3 read-only zero; `0x46F` written, `0x467` read back) |
| WDMA_CTL (`0x30F04`) readable | [M:s] | **Confirmed** [M:hw] | Reads `0x37` after init (ack bits 10/11 auto-clear on write) |
| WDMA TX transfer | [M:s] | **Confirmed** [M:hw] | Bit 20 fires on every probe TX. 13 channels, zero timeouts |
| WDMA RX bit 17 | [M:s] | **Confirmed** [M:hw] | Bit 17 stays low without `0x30328=2`. Root cause: MAC RX notification not enabled (§7.11.4 step 4) |
| MAC RX notify enable (`0x30328`) | [M:s] | **Confirmed** [M:hw] | Must write `2` for bit 17 to assert. The vendor's interrupt setup does this (0, then 2) |
| SoC WiFi IRQ enable (`0x14050` bit 0) | [M:s] | Set on core-fm1 [M:hw] | Interrupt unmask only (§7.11.9); not required for bit 17 [I] |
| RX byte-swap | [M:s] | Untested | Pending RX enable fix |
| Pre-RX MAC write (`0x30324`) | [M:s] | Untested | Vendor writes `2` before each RX read |


#### 7.11.9 Interrupt-driven and asynchronous steps [M:s wl_rf_common.a, wl_wifi_sta.a, 2026-11-04]

- **No initialization step depends on an interrupt, a thread, a timer or a mailbox.** Every register
  write of §4.2.1 phases A and B completes synchronously in the starting task; the MCU-command
  path is a stub that returns success, and the firmware-download path is not called [M:s].
- **One interrupt line, 52**, registered with a priority argument of 2 on CPU core 1 [M:s; argument
  meanings I]. Event: "received frame available" (the handler chain's first test is `WDMA_CTL`
  bit 17). The handler clears `WF_CON4` bit 0 (masking further interrupts) and wakes the receive
  task; it reads or acknowledges no MAC register. The mask is re-opened by the receive path
  (§7.11.6 step 7).
- The interrupt is unmasked for the first time when the first receive buffer is posted (§4.2.1 B11),
  before line 52 is registered (B13).
- **TX completion is polled** (`WDMA_CTL` bit 20); TX has no interrupt.
- Asynchronous items before the first TX: a periodic task (nominally every 100 ms; reads the
  statistics counters; on every second run [timing I] a watchdog pulses PBF_CFG to `0x00F40012`/`0x00F4000A` and back to `0x00F40006`
  only if TX queue 0 or 1 is stuck non-empty) and, after a full calibration only, a one-shot
  timer that saves VM 187 one second later [M:s]. Neither is needed for the first TX [I].
- Polling mode (configuration option, off by default): a busy receive task replaces the interrupt;
  register writes are the same [M:s].

---

### 7.12 Why no frame is received: RX path status [M:hw 2026-11-04]

Stage-by-stage state of the receive path on core-fm1 against the running vendor radio on the same
FM-1 unit (fm1-base `/status` oracle), after iteration 12i. "Equal" means read back identical on
both sides. Observation files: `.scratch/debug/observations/2026-11-0[2-4]*`.

| # | Stage | core-fm1 evidence | Vendor | State |
|---|-------|-------------------|--------|-------|
| 1 | RF analog: LNA, mixer, PLL, channel, LUT | 22 registers equal, LUT loaded, calibration converges within 1 LSB [M:hw] | same values | configured identically. No direct measurement of RX gain or LNA state yet (no RSSI readout until a frame arrives) |
| 2 | Block hold: WL_CTL0 `0x14000` | held at `0xF103` through the whole init; released only by the 12i probe, 15 s after init [M:hw] | `0` at steady state | **the root difference found so far** (§4.2). Release-before-init under test (12j) |
| 3 | Baseband energy / CCA detector | false CCA (`RX_STA_CNT1[15:0]`) 0 per dwell while held; 201-240 per 100 ms after the release [M:hw] | 795 per interval | runs once released |
| 4 | Baseband AGC / gain state | direct R48-R63 zero while held, R60 live after the release; R32-R46 zero in every capture [M:hw] | R32-R46 stable non-zero, R48-R63 moving between captures | unknown whether R32-R46 are hardware-populated once the block runs or software-written through a path we do not have (§6.4 lists only R21, R22, R25, R84, R106 as direct writes). Analyst question |
| 5 | Baseband demodulation (PLCP, PSDU) | `RX_STA_CNT0` PHY-error and CRC-error counts 0 in every capture, including the scan after the release [M:hw] | CRC and PHY errors non-zero every interval | **where the receive path stops today**: nothing is ever demodulated, so the MAC never sees a frame, good or bad |
| 6 | BBP tables without read-back: window C' (RX 11n), window D' (RX frontend) | loaded from the vendor tables [M:s]; no read-back path (§6.3) | | unverifiable by measurement; a wrong entry here would look exactly like stage 5 |
| 7 | BBP direct R0-R31 | R2, R5, R6, R7, R10, R13, R14, R15, R16, R20 differ (first compared 2026-11-03) [M:hw] | | semantics unknown; neither side writes them directly per §6.4 |
| 8 | MAC RX: filter, auto-response, RX_EN, RX notify `0x30328` = 2 | equal [M:hw] | equal | configured identically; idle until stage 5 delivers a frame |
| 9 | WDMA RX and interrupt | `WDMA_CTL` bit 17 never set, WiFi ISR never fired (`isr_n` 0) [M:hw] | works | downstream of stage 5 |
| 10 | Five read-only "running" indicators (§4.2) | all clear, refuse writes, did not follow the late release [M:hw] | set / cleared | expected to follow the early release if that is the enable; otherwise an analyst question |

Reading: stages 1, 8 and 9 are configured identically and are waiting. Stage 2 was the hold.
Stage 3 proves the release reaches the baseband. Stage 5 is where the receive path still stops:
the detector hears energy but never demodulates a frame. Candidates for stage 5, in order of
likelihood: (a) the rest of the init has to run on a released block (12j); (b) AGC/gain state
(stage 4) that the vendor's running baseband holds and ours never acquires; (c) a wrong entry in
the two write-only windows (stage 6); (d) an RX-frontend or LNA enable on the RF die outside the
22 compared registers. TX shares stages 2 and 10 and additionally never queues a frame
(`TXRXQ_PCNT` 0, §7.10).

Open items for RX, prioritized:
1. 12j result: indicators and counters after release-before-init.
2. Analyst (`[M:s]`): vendor writes to `0x14000` and the surrounding block in order from clock-tree
   completion to first transmit; what fills BBP R32-R46; contents and position of the window C'
   and D' loads; R106 per channel.
3. Sweep diff of `0x10000`-`0x100FF`, `0x14000`-`0x140FF`, `0x30000`-`0x31FFF`, `0x40000`-`0x40FFF`.
4. RSSI and AGC readout from RXWI word 2 as soon as any frame arrives.

**Reader answers (2026-11-04)** [M:s wl_rf_common.a, wl_wifi_sta.a]: stage 4: no vendor code writes
BBP R32-R63; they are hardware state of a running baseband (§6.4). Stage 6: windows C' and D' do
have read-back registers (`0xCF`, `0xD7`, §6.3), so their contents can be compared on hardware;
their write order is in §6.6. Stage 7: the R0-R31 differences are not vendor writes (§6.4). Stage
10: no vendor code reads, polls or writes the indicators (§4.2). Open item 2 is answered by §4.2.1
(no write to `0x14000`; the steps the GPL driver lacks are TX_PIN_CFG, TX_BAND_CFG, the channel
code in `WF_CON7`, the FEQ bank patch and the end-of-init order) and by §6.8 (R106 constant 4).
The candidates for stages 5 and 10 are ranked in §14. Also note that releasing `WL_CON0` (stage 2)
only undoes a write the vendor never makes.

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
| 106   | 2 B  | left, right crystal load trims                 | `0x11930` bits [16:13], [22:19]                                                                    | M:t, M:s |
| 107   | 7 B  | PA_C_I values (7 calibration bytes)            | `0x11924` [26:24], `0x11928` [2:0]/[11:9]/[17:15]/[25:22], `0x1192C` [2:0], plus 32 LUT words each | M:t, M:s |
| 108   | 20 B | WiFi PA per-rate digital gain                  | 12 BBP window-D entries                                                                            | M:s      |
| 187   | 68 B | Full calibration cache (64 B payload + CRC-16) | Window-D/D' entries, `0x11908`, `0x1191C`, `0x11920`, `0x1195C`, `0x11960`, LUT word patches       | M:t, M:s |

Record format: 4-byte header (CRC-16/XMODEM low byte, ID, length) + data [M:s].
VM 187 has an inner CRC-16 at bytes 64-65 over the 64-byte payload [M:s].

See BLE-HW-FACTS.md §14 and §16.4 for the complete byte-to-register mapping.

### 10.1 Boot-time calibration when VM 187 is absent [M:s, OS:fm1-base]

When no VM 187 record exists in flash (`have & 0x08 == 0`), the BLE RF bytecode interpreter writes
zeros for every calibration-dependent field: RC filter bandwidth codes, TX LO leakage trims, RX DC
offset compensation, and RF-die LUT bank/RX-DC patches. **The receiver is completely deaf with
uncalibrated defaults.** On bare metal without MMU, addresses 0-67 are valid SRAM, so the zero-fill
does not crash, it silently corrupts all trim fields.

**Requirement:** when VM 187 is absent, the driver must perform live RF calibration at boot to
measure and apply the radio's actual trim values. This is confirmed by the fm1-base project
(Apache-2.0, `bsp/chip/fm1_rf_cal.h`) which performs identical calibration when `fm1_rf_cal_on=1`.

**Calibration procedure** (runs after WiFi mode switch, ~15 ms total):

1. **RC filter tune** (`0x11968` tune engine, 3 runs for 6/10/20 MHz):
   - Set `0x11904` [11:8] = 8, set `0x11950` bit 27
   - For each bandwidth code (0x1FC, 0x122, 0x0A0): write `0x11968` [9:1], clear [17:11] to 15,
     clear [22:18], clear bit 23, set bit 0 (start)
   - Poll the analog status port (selector 1, §5.2) bit 5 for completion (timeout 5 ms)
   - Read the result from the same port, bits [15:8]
   - Clear bit 0 (stop)
   - Clear `0x11950` bit 27
   - **Apply codes:** `0x11920` [17:10], `0x11910` [21:14], `0x1195C` bytes 1-3, `0x11960` bytes 1-3

2. **TX LO leakage search** (loopback self-test):
   - Set RF enables for TX cal mode (29 enable bits, `fm1_rfc_en_set(2)`)
   - Configure loopback attenuation (`0x11920` bits 20-23, `0x11924` [30:28], `0x11928` [14:13,28:27])
   - PHY reset, CCA-for-trim setup, loopback test mode (`bbp[25]=0x18`)
   - Generate test tone via window D entries 0x00, 0x5F, 0x01-0x03
   - Configure estimator via window D' entries 0x00, 0x13, 0x14
   - Set TX neutral (gains 1023, phases/DC 0), TX LO I/Q to 64
   - Auto-tune VGA gain to ADC peak target > 47
   - **I search:** sweep `0x1191C` [14:8] values 0-127, pick lowest I²+Q² metric
   - **Q search:** sweep `0x11920` [6:0] values 0-127, pick lowest I²+Q² metric
   - On timeout: restore defaults (I=64, Q=64)

3. **RX DC successive approximation** (4 front-end gain steps):
   - Switch to RX mode (`fm1_rfc_en_set(0)`), clear `0x11904` bit 15
   - PHY reset, CCA-for-trim, loopback
   - Configure estimator for DC mean readout
   - For each of 4 gain configs (`0x1190C` [7:6], `0x11910` [11:8,6:0]):
     - Zero analog RX DC trims in `0x11908` (I [15:7]+bit16, Q [25:17]+bit26)
     - 12 rounds: step starts at 256, halves to 1
     - Read I mean (estimator sel=1): adjust I trim ±step, clamp ±510
     - Read Q mean (estimator sel=2): adjust Q trim ±step, clamp ±510
   - Result: encoded I/Q trims per gain step (4 x 2 values)

4. **Record build** (68 bytes, §10 format):
   - Bytes 0-23: TX IQ values (6 × 4-byte LE: I gain, I phase, Q gain, Q phase, I DC, Q DC)
   - Byte 24: TX LO I (`0x1191C` [14:8])
   - Byte 25: TX LO Q (`0x11920` [6:0])
   - Bytes 26-27: RX I/Q gain (255 = neutral, not measured without full IQ cal)
   - Bytes 28-31: zero (RX I/Q phase, not measured)
   - Bytes 32-35: analog RX DC I (signed, from `0x11908`)
   - Bytes 36-39: analog RX DC Q (signed, from `0x11908`)
   - Bytes 40-43: filter cal register `0x1195C`
   - Bytes 44-47: filter cal register `0x11960`
   - Bytes 48-63: RX DC results, 4 gain groups x (I, Q) (8 × 2-byte LE)
   - Bytes 64-65: CRC-16/XMODEM of bytes 0-63

**Estimator protocol** (via window D'):
- Write D'[0x15]=1, D'[0x16]=1, D'[0x15]=3, D'[0x16]=3
- Poll D'[0x8D] bits [1:0] until clear (timeout 5 ms)
- Read result (sel-dependent): sel=0/3: I²+Q² from D'[0x81:0x82,0x84:0x85];
  sel=1: 24-bit I mean from D'[0x81-0x83]; sel=2: 24-bit Q mean from D'[0x8A-0x8C]

**RF enable table** (RF mode select, 29 entries):
Each entry: register offset from `0x11900`, bit number, flag class. Six classes:
A (rx|rxcal|txcal|rxdc), RX (receive), R (rx|rxcal|rxdc), T (tx|rxcal|txcal),
TXC (txcal), RXC (rxcal). Mode selects which classes are active. See fm1-base
`fm1_rfc_en_tab[29]` for the complete table [OS:fm1-base, Apache-2.0].

**VGA gain table** (42 steps, 4 fields each):
Per step: `0x11910` [24:22], [26:25]; `0x11914` [17:15]+[14:12]; `0x11914` [7:5].
See fm1-base `fm1_rfc_vga[42][4]` [OS:fm1-base, Apache-2.0].

Evidence: fm1-base `bsp/chip/fm1_rf_cal.h` (Apache-2.0) contains a complete C rewrite of
`wl_rf_common.a`'s calibration, independently confirms all register addresses and bit fields
above. Comments in that file document where the library's code diverges from the spec and which
bugs it contains.

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

### 11.2 WiFi PHY/MAC hardware init [M:s]

The shared RF init (groups 1-14 above, executed by `fm1_ble_rf_run()` bytecode) configures the
analog radio hardware. After it completes, the vendor's WiFi hardware-init step must run to
configure the digital baseband for 802.11 frame processing. **Without this init, the radio has
correct clocks and can transmit raw frames via WDMA, but the receiver cannot detect or demodulate
any 802.11 signal.** (2026-11-04: the complete ordered start, including this step, is §4.2.1
phase A.)

#### 11.2.1 Hardware-init call sequence [M:s, names removed 2026-11-04]

1. MAC/PHY reset (§11.2.2)
2. MAC timing registers (§11.2.3)
3. MAC+`0x0308` = `0x9407` (§11.2.4)
4. MAC/PHY reset again (§11.2.2)
5. Window D TX filter, mode 0 (§11.2.10, §6.9)
6. PHY init (§11.2.5, §6.6)
7. RF transceiver init and calibration (§11.2.9)
8. PHY init again (identical)
9. Window D TX filter, mode 1 (§11.2.10, §6.9)
10. RAPP (TX soft compression, §6.9)
11. RF-die LUT load (§5.1)
12. RF auto control (§11.2.11)
13. TX power level 6 (`0x1405C` [23:16])
14. Channel 6 (`0x1405C` [7:0] = `0x6B`)
15. Per-rate digital gain from VM 108 (§6.9)
16. Software snapshot of the TX LO registers `0x1191C`/`0x11920` (§11.2.11)

~~Steps 5-9 are the critical path. Steps 10-16 are optimization/calibration that may be
deferrable.~~ **ERRATA (2026-11-04):** steps 11-14 are not optional: the RF die tunes from the LUT
entry that step 14 selects once step 12 hands it control, and the vendor writes no PLL register
per channel afterwards (§4.2.1 C1, §5.1) [M:s, I]. The PHY init runs twice (steps 6 and 8),
byte-identical.

#### 11.2.2 PHY/MAC reset sequences [M:s]

**MAC/PHY reset**:

| Step | Register           | Value  | Hex    | Notes                    |
|------|--------------------|--------|--------|--------------------------|
| 1    | MAC[0x1004]        | 3      | 0x03   | RESET_CSR + RESET_BBP    |
| 2    | MAC[0x030C]        | 30     | 0x1E   | Control register set     |
| 3    | MAC[0x030C]        | 0      | 0x00   | Control register clear   |
| 4    | PHY reset (below)  |        |        | PHY reset sub-sequence   |

**PHY reset**:

| Step | Register           | Value  | Hex    | Notes                    |
|------|--------------------|--------|--------|--------------------------|
| 1    | MAC[0x1004]        | 12     | 0x0C   | TX_EN + RX_EN; the last MAC_SYS_CTRL value before the first TX |
| 2    | BBP[22]            | 0      | 0x00   |                          |
| 3    | BBP[21]            | 49     | 0x31   | Toggle bit 0             |
| 4    | BBP[21]            | 48     | 0x30   | Toggle bit 0 back        |
| 5    | BBP[25]            | 0      | 0x00   |                          |

MAC register addresses in this section use the raw offset (e.g. `0x1004`). The MMIO address is
`0x30000 + offset` (e.g. `0x31004`). See §7.2.

#### 11.2.3 SIFS timing registers [M:s]

These 6 MAC register writes configure 802.11 SIFS (Short Interframe Space) timing:

| MAC Offset | MMIO Addr   | Value        | Hex          | Notes                  |
|------------|-------------|--------------|--------------|------------------------|
| 0x1104     | 0x31104     | 1,311,508    | 0x00140314   | IFS timing register 1  |
| 0x1100     | 0x31100     | 866,389,003  | 0x33A40C0B   | IFS timing register 0  |
| 0x1330     | 0x31330     | 264,196      | 0x00040804   | IFS config register 0  |
| 0x1334     | 0x31334     | 788,488      | 0x000C0808   | IFS config register 1  |
| 0x1338     | 0x31338     | 786,692      | 0x000C0104   | IFS config register 2  |
| 0x1348     | 0x31348     | 987,792      | 0x000F1290   | IFS config register 3  |

#### 11.2.4 MAC mode register [M:s]

After the first SIFS reset the hardware-init step writes MAC offset 0x0308 (MMIO 0x30308) with
value `0x9407`. This is written before the second PHY/MAC reset. The same write appears at the
start of each PHY init (three writes in total). Purpose inferred: MAC operating mode
configuration [I].

#### 11.2.5 PHY init [M:s]

The main PHY init. Run twice (before and after RF init). Sequence (detail in §6.6):

1. PHY reset (§11.2.2)
2. MAC timing registers (§11.2.3)
3. MAC[0x0308] = 0x9407 (§11.2.4)
4. AGC table load (§11.2.5a)
5. CCA register table (§11.2.6): 114 entries
6. RX frontend register table (§11.2.7): 24 entries
7. RX 11n register table (§11.2.8): 72 entries
8. TX backend register table (§11.2.8b): 83 writes
9. Post-init fixups: CCA[42], CCA[87], RX frontend[25], CCA[55], RX frontend[15,27]
10. RX 11n entry 60 = 1 (override from the table's 0xFF)
11. direct BBP R106 = 4, then R84 = 2
12. PHY reset

#### 11.2.5a AGC table load [M:s]

Automatic Gain Control RAM load. Configures 128 gain entries in the CCA window's AGC RAM.
Without this, the receiver cannot auto-adjust gain and is effectively deaf.

**Protocol** (all writes through window C):

For each entry i = 0..127:
1. `CCA[47] = i` (select AGC RAM index)
2. Take the 32-bit table word for entry i (rule below)
3. Extract byte 0 (bits [7:0]), clamp signed to [-64, +63]
4. `CCA[43]` = the clamped byte 0 (gain value)
5. `CCA[78] = byte1` (bits [15:8])
6. `CCA[84] = byte2` (bits [23:16])
7. `CCA[85] = byte3` (bits [31:24])
8. `CCA[47] = i | 0x80` (commit: bit 7 = write strobe)

After loop, set final defaults:
- `CCA[47] = 0`
- `CCA[84] = 0xC0`
- `CCA[85] = 1`
- `CCA[78] = 66 (0x42)`
- `CCA[43] = 66 (0x42)`
- `CCA[44] = 66 (0x42)`
- `CCA[121] = 0`
- `CCA[122] = 32 (0x20)`

**AGC RAM table** (128 x 32-bit words). **Changed 2026-11-04:** the verbatim transcription of the
table that stood here was removed (vendor tables are described, not transcribed). The table is
generated exactly by this rule [M:s wl_rf_common.a]: for entry i, byte 0 = i − 20 (two's
complement), byte 1 = i, bits [31:16] = 8i − 80 (16-bit two's complement). Entry 0 is
`0xFFB000EC`, entry 127 is `0x03A87F6B`. The previous revision's 128-word list equals this rule.

#### 11.2.6 CCA register table (BBP window C) [M:s]

Clear Channel Assessment configuration. These registers configure 802.11 signal detection
(STF preamble, DSSS, OFDM), correlation thresholds, AGC levels, and energy detection.
Written through BBP window C (§6.1).

| Index | Hex  | Index | Hex  | Index | Hex  | Index | Hex  |
|-------|------|-------|------|-------|------|-------|------|
| 0     | 0x8A | 1     | 0x20 | 2     | 0x04 | 3     | 0x08 |
| 4     | 0x0C | 5     | 0x10 | 6     | 0xB0 | 7     | 0xA0 |
| 8     | 0x70 | 9     | 0x32 | 10    | 0x0C | 11    | 0xE0 |
| 12    | 0x01 | 13    | 0x00 | 14    | 0xB5 | 15    | 0x91 |
| 16    | 0x44 | 17    | 0x43 | 18    | 0x44 | 19    | 0x04 |
| 20    | 0x7F | 21    | 0x0F | 22    | 0x00 | 23    | 0x05 |
| 24    | 0x77 | 25    | 0x80 | 26    | 0x12 | 28    | 0x35 |
| 29    | 0xEF | 30    | 0x0F | 31    | 0x0F | 32    | 0x4F |
| 33    | 0x05 | 34    | 0x05 | 35    | 0x93 | 36    | 0x04 |
| 37    | 0x0D | 38    | 0x03 | 39    | 0x84 | 40    | 0x1E |
| 41    | 0x0A | 42    | 0x94 | 48    | 0x20 | 49    | 0x06 |
| 50    | 0x3F | 51    | 0x0C | 52    | 0x0C | 53    | 0x04 |
| 56    | 0x10 | 57    | 0x10 | 58    | 0x20 | 59    | 0xC0 |
| 60    | 0xF0 | 61    | 0xD0 | 62    | 0x80 | 63    | 0xD0 |
| 64    | 0xE7 | 65    | 0x87 | 66    | 0x18 | 67    | 0xE8 |
| 68    | 0x30 | 69    | 0x30 | 70    | 0x07 | 71    | 0x00 |
| 75    | 0x70 | 76    | 0x50 | 80    | 0x80 | 81    | 0x90 |
| 82    | 0xFF | 83    | 0x03 | 84    | 0x00 | 85    | 0x01 |
| 86    | 0x01 | 87    | 0x27 | 88    | 0x03 | 89    | 0x28 |
| 90    | 0xD8 | 91    | 0x0C | 92    | 0x01 | 93    | 0x05 |
| 94    | 0x50 | 95    | 0x80 | 96    | 0x80 | 97    | 0xDD |
| 98    | 0x20 | 99    | 0xFE | 100   | 0x50 | 103   | 0x00 |
| 104   | 0x00 | 107   | 0xDF | 108   | 0x47 | 109   | 0x40 |
| 110   | 0x2C | 111   | 0x70 | 112   | 0x50 | 113   | 0xE0 |
| 114   | 0x01 | 115   | 0x9C | 117   | 0x00 | 118   | 0x03 |
| 119   | 0x49 | 120   | 0x00 | 121   | 0x04 | 122   | 0xD0 |

Gaps: indices 43-47, 74, 77-78, 116 are not written. Indices 27, 54, 55, 72, 73, 79, 101, 102,
105, 106 are loaded from external calibration globals (see below).

**Calibration-dependent CCA registers** [M:s]:

| CCA Reg | Published SDK constant [PUB] | Mask | Notes                   |
|---------|-------------------------|-----------|------------------------------|
| 27      | STF_END_CNT_THR_VAL     | [7:0]     | STF end count threshold      |
| 54      | STF_DET_CNT_THR_VAL     | [6:0]     | STF detection count, AND 0x7F|
| 55      | DS_DET_CNT_THR0_VAL     | [6:0]     | DSSS detection count, AND 0x7F|
| 72      | STF_DET_THR1_VAL        | [7:0]     | STF detection threshold 1    |
| 73      | STF_DET_THR2_VAL        | [7:0]     | STF detection threshold 2    |
| 79      | STF_DET_THR3_VAL        | [7:0]     | STF detection threshold 3    |
| 101     | DSSS_XCDET_THR1_VAL     | [7:0]     | DSSS cross-detect threshold 1|
| 102     | DS_VLD_CNT_THR_VAL      | [6:0]     | DSSS valid count, AND 0x7F   |
| 105     | DSSS_XCDET_THR2_VAL     | [7:0]     | DSSS cross-detect threshold 2|
| 106     | DS_DET_CNT_THR1_VAL     | [7:0]     | OR 0x80 (bit 7 forced set)   |

These values are constants of the published SDK source `apps/common/net/wifi_conf.c` (the active
"stronger RX anti-interference" set; **ERRATA 2026-11-04:** not a library-internal file) [PUB, M:s]:

| CCA Reg | SDK Default | Hex  | Notes                            |
|---------|-------------|------|----------------------------------|
| 27      | 190         | 0xBE | STF end count threshold          |
| 54      | 12          | 0x0C | STF detection count (& 0x7F)     |
| 55      | 4           | 0x04 | DSSS detection count (& 0x7F)    |
| 72      | 190         | 0xBE | STF detection threshold 1        |
| 73      | 150         | 0x96 | STF detection threshold 2        |
| 79      | 140         | 0x8C | STF detection threshold 3        |
| 101     | 80          | 0x50 | DSSS cross-detect threshold 1    |
| 102     | 80          | 0x50 | DSSS valid count (& 0x7F)        |
| 105     | 100         | 0x64 | DSSS cross-detect threshold 2    |
| 106     | 132         | 0x84 | DSSS detection count (4 \| 0x80)  |

These thresholds control WiFi preamble/frame detection sensitivity. STF thresholds must be
high enough for OFDM Short Training Field correlation; DSSS thresholds must match 802.11b
preamble timing. Using conservative defaults (0x10/0x40) makes the receiver deaf to all
WiFi signals.

#### 11.2.7 RX frontend register table (BBP window D') [M:s]

Written through BBP window D' (§6.1).

| Index | Hex  | Index | Hex  | Index | Hex  | Index | Hex  |
|-------|------|-------|------|-------|------|-------|------|
| 0     | 0x3A | 3     | 0x15 | 4     | 0x6A | 5     | 0x00 |
| 6     | 0x00 | 7     | 0xB5 | 8     | 0xC1 | 13    | 0x2B |
| 14    | 0xA0 | 15    | 0xC4 | 16    | 0x04 | 17    | 0xA0 |
| 18    | 0x2B | 19    | 0x00 | 20    | 0x00 | 21    | 0x00 |
| 22    | 0x00 | 23    | 0x00 | 24    | 0x00 | 25    | 0x39 |
| 26    | 0x6A | 27    | 0x38 | 28    | 0x00 |       |      |

Note: index 24 is written twice (both 0x00). Indices 1, 2, 9-12 are not written.

#### 11.2.8 RX 11n register table (BBP window C') [M:s]

Written through BBP window C' (§6.1).

| Index | Hex  | Index | Hex  | Index | Hex  | Index | Hex  |
|-------|------|-------|------|-------|------|-------|------|
| 0     | 0x62 | 1     | 0x58 | 2     | 0x45 | 3     | 0xE5 |
| 4     | 0x00 | 5     | 0x0A | 6     | 0x12 | 7     | 0x04 |
| 8     | 0x00 | 9     | 0x70 | 10    | 0x34 | 11    | 0x00 |
| 12    | 0x00 | 13    | 0x9A | 14    | 0xA6 | 15    | 0x33 |
| 16    | 0x9A | 17    | 0xA6 | 18    | 0x33 | 19    | 0x9A |
| 20    | 0xA6 | 21    | 0x33 | 22    | 0x9A | 23    | 0xA6 |
| 24    | 0x33 | 25    | 0x9A | 26    | 0xA6 | 27    | 0x33 |
| 28    | 0x08 | 29    | 0x0F | 30    | 0x10 | 31    | 0x10 |
| 32    | 0x10 | 33    | 0x24 | 34    | 0x25 | 35    | 0x24 |
| 36    | 0x25 | 37    | 0x25 | 38    | 0x25 | 39    | 0x25 |
| 40    | 0x25 | 41    | 0x25 | 42    | 0x25 | 43    | 0x25 |
| 44    | 0x25 | 45    | 0x1D | 46    | 0x25 | 47    | 0x1C |
| 48    | 0x25 | 49    | 0x0A | 50    | 0x0A | 51    | 0x0A |
| 52    | 0x0A | 53    | 0x0A | 54    | 0x0A | 55    | 0x0A |
| 56    | 0x07 | 57    | 0x2E | 58    | 0x08 | 59    | 0x20 |
| 60    | 0xFF | 61    | 0x08 | 62    | 0x00 | 63    | 0x04 |
| 64    | 0x00 | 65    | 0x60 | 66    | 0x0F | 67    | 0x00 |
| 68    | 0x04 | 69    | 0x00 | 70    | 0x00 | 71    | 0x00 |

Registers 13-27 contain a repeating pattern `{0x9A, 0xA6, 0x33}` (5 repetitions): these are
likely per-subcarrier or per-rate configuration for 802.11n.

Note: at the end of the PHY init, register 60 is overwritten from 0xFF to 0x01.

#### 11.2.8b TX backend register table (BBP window D) [M:s]

Written through BBP window D (§6.1).

**Part 1: per-rate gain registers (indices 11-34)**

Odd indices 11-33 are set to 0x20 (gain high byte), even indices 12-34 are set to 0x00 (gain
low byte). This programs 12 rate-indexed gain entries (pairs of high/low bytes).

**Part 2: TX signal processing (indices 35-76)**

| Index | Hex  | Index | Hex  | Index | Hex  | Index | Hex  |
|-------|------|-------|------|-------|------|-------|------|
| 35    | 0x03 | 36    | 0x32 | 37    | 0x00 | 38    | 0x3F |
| 39    | 0xE5 | 40    | 0x00 | 41    | 0xCC | 42    | 0x01 |
| 43    | 0x8A | 44    | 0x02 | 45    | 0xCA | 46    | 0x01 |
| 47    | 0x88 | 48    | 0x02 | 49    | 0x06 | 50    | 0x04 |
| 51    | 0x19 | 52    | 0xB6 | 53    | 0x00 | 54    | 0x99 |
| 55    | 0x01 | 56    | 0x42 | 57    | 0x02 | 58    | 0xE6 |
| 59    | 0x01 | 60    | 0xAF | 61    | 0x02 | 62    | 0x04 |
| 63    | 0x04 | 64    | 0x44 | 65    | 0x5B | 66    | 0x00 |
| 67    | 0xCC | 68    | 0x01 | 69    | 0x8A | 70    | 0x02 |
| 71    | 0x99 | 72    | 0x01 | 73    | 0x43 | 74    | 0x02 |
| 75    | 0x06 | 76    | 0x05 |       |      |       |      |

**Part 3: TX filter/calibration (indices 80-95)**

| Index | Hex  | Index | Hex  | Index | Hex  | Index | Hex  |
|-------|------|-------|------|-------|------|-------|------|
| 80    | 0x00 | 81    | 0x4F | 82    | 0xA4 | 83    | 0x90 |
| 84    | 0xED | 85    | 0xDA | 86    | 0x54 | 87    | 0x6B |
| 88    | 0x00 | 89    | 0x10 | 90    | 0xD6 | 91    | 0x90 |
| 92    | 0xC0 | 93    | 0x00 | 95    | 0x40 |       |      |

Register 0 is written as 0x02 at both the start and end of the table.

#### 11.2.9 RF transceiver init [M:s]

The RF init configures the analog transceiver for WiFi operation. It writes to MMIO registers
in the RF analog block:

| MMIO Address | Description                                       |
|--------------|---------------------------------------------------|
| 0x11930      | RF control 0: XOSC cap, PLL, clock source         |
| 0x11900      | RF control 1: LNA, PA, mixer, VCO enables         |
| 0x11904      | RF control 2: bias, filter, calibration            |
| 0x11908      | RF control 3: power, gain                          |
| 0x11950      | RF control 4: zeroed during init                   |

Key behavioral details [M:s]:

1. Determines the crystal frequency: 40 MHz when `PLL_CON0` (`0x119A0`) [27:26] = 2, else 24 MHz
   [M:s cpu library]. **The FM-1 has a 24 MHz crystal** (vendor read-back `0x11930` [27:23] =
   `0x0B`, `0x11954` = `0x02822520`) [M:hw, M:s].
2. Reads the two crystal load trims from VM 106 (left, right)
3. Programs `0x11930` bits [16:13] = left trim [3:0], bits [22:19] = right trim [3:0]
4. Clock-dependent: `0x11930` [27:23] = `0x1A` (40 MHz) or `0x0B` (24 MHz) (as previously
   written: bits [27:25] `0x0D` / `0x05`, the same values seen three bits higher)
5. `0x11900` and `0x11904` are programmed bit-by-bit using read-modify-write
6. `0x11950` is zeroed

The full ~200 RMW operations were simulated against the hardware-captured BLE bytecode residue
(CON0=`0xEA9E0005`, CON1=`0x01C40480`, CON2=`0x9C010010`, ANL_CON2=`0x0580065F`) with 40MHz
crystal and xosc=0,0 [M:s]. Registers CON3-CON11 and PLL start from zero (BLE bytecode values
unknown; the simulation builds them up entirely from RMW operations on zero).

**ERRATA (2026-11-04):** the simulation below assumed a 40 MHz crystal. The FM-1 has a 24 MHz
crystal, so the PLL words (`0x11934`, `0x11938`, `0x1193C`, `0x11940`, `0x11954`) and the crystal
word in this table do **not** apply to this board; the 24 MHz end values are the vendor read-backs
(`0x11938` = `0x8DAFD065`: N = 101, bank `0x35`; `0x1193C` = `0x12640F32`: fbds 121; `0x11940` =
`0x08B41411`; `0x11954` = `0x02822520`; `0x11930` = `0x05D9625F`) [M:hw]. For 24 MHz the PLL is
set for 2442 MHz with N = 101, fraction `0xC00000`, `0x11940` b0 = 1 and [7:6] = 0, `0x1193C`
[21:18] = 9 and [29:27] = 2 [M:s wl_rf_common.a]. Calibrating with the 40 MHz words on a 24 MHz
unit is listed in §14 (row 8).

**Target register values after the RF init completes (40MHz crystal, not this board) [M:s]:**

| Addr      | Name      | BLE residue  | WiFi target  | Delta        | Notes                           |
|-----------|-----------|-------------|-------------|-------------|----------------------------------|
| `0x11900` | WL_CON0   | `0xEA9E0005` | `0xEA220005` | `0x00BC0000` | Clear 23,20,19,18; set 21. LNA/mixer BLE->WiFi |
| `0x11904` | WL_CON1   | `0x01C40480` | `0x01C34880` | `0x00074C00` | Clear 18,10; set 17,16,14,11. Filter/bias |
| `0x11908` | WL_CON2   | `0x9C010010` | `0x9C010010` | `0x00000000` | No net change |
| `0x1190C` | WL_CON3   | unknown     | `0x672B9349` | --          | Filter/VGA/mixer |
| `0x11910` | WL_CON4   | unknown     | `0x0508098E` | --          | Analog config |
| `0x11914` | WL_CON5   | unknown     | `0x4078C319` | --          | Analog config |
| `0x11918` | WL_CON6   | unknown     | `0x00076A24` | --          | Analog config |
| `0x1191C` | WL_CON7   | unknown     | `0x2915C066` | --          | Analog config |
| `0x11920` | WL_CON8   | unknown     | `0x00A641C0` | --          | Analog config |
| `0x11924` | WL_CON9   | unknown     | `0x65B80057` | --          | PA trims |
| `0x11928` | WL_CON10  | unknown     | `0x1AFAE7E7` | --          | PA config |
| `0x1192C` | WL_CON11  | unknown     | `0x0052A070` | --          | Analog config |
| `0x11930` | WL_ANL2   | `0x0580065F` | `0x0D00025F` | `0x08800400` | Set 27 (40MHz); clear 23,10 |
| `0x11934` | PLL_CON0  | unknown     | `0x01B33333` | --          | PLL frac (WiFi 2.4GHz) |
| `0x11938` | PLL_CON1  | unknown     | `0x819FD028` | --          | PLL int=40, config |
| `0x1193C` | PLL_CON2  | unknown     | `0x12600EF2` | --          | PLL config |
| `0x11940` | PLL_CON3  | unknown     | `0x08B42451` | --          | PLL config (40MHz path) |
| `0x11944` | PLL_CON4  | unknown     | `0x00000A80` | --          | PLL config |
| `0x11950` | ANA_CON0  | --          | `0x00000000` | --          | Zeroed explicitly |
| `0x11954` | ANA_CON1  | --          | `0x000A2500` | --          | Analog frontend (40MHz) |
| `0x11964` | ANA_CON5  | --          | `0x00010000` | --          | Analog config |

**Bit-level delta for known-starting-value registers:**

WL_CON0 (`0x11900`): switches LNA/mixer from BLE to WiFi receive path.
- Clear bits 23, 20, 19, 18 (BLE path enables)
- Set bit 21 (WiFi path enable)

WL_CON1 (`0x11904`): adjusts filter bandwidth and bias for 20MHz WiFi channels.
- Clear bits 18, 10 (BLE narrow filter)
- Set bits 17, 16, 14, 11 (WiFi wide filter + bias)

WL_ANL_CON2 (`0x11930`): configures PLL clock source for 40MHz crystal.
- Set bit 27 (40MHz mode select)
- Clear bits 23, 10

**Caveats [M:s]:**

1. CON3-CON11 starting values from BLE bytecode are unknown. The simulation built targets
   from zero using the same RMW sequence the vendor uses. If the BLE bytecode left different
   values, bits NOT touched by any RF-init RMW operation would differ. Full overwrite
   recommended over incremental RMW for these registers.
2. Several sub-steps (RF mode select, PLL restart, VCO bank scan, calibration, PA configuration,
   TX IQ keep, bandwidth save) modify registers beyond the RMW list; the PLL restart and the bank
   scan are now in §5.2, the RF modes and calibration in §10.1. These are calibration/optimization routines.
3. PLL bank scan is a runtime calibration; the simulated PLL values may need VCO re-lock.

#### 11.2.10 Window D TX filter (CFR) [M:s, completed 2026-11-04]

Run twice: mode 0 before the first PHY init and mode 1 after the second. ~~Mode 0 likely performs
initial calibration; mode 1 applies post-RF corrections. TODO: trace the implementation.~~
**Resolved 2026-11-04:** neither mode calibrates; both write constant TX-filter coefficients, tap
counts and per-rate gains into window D. Mode 0 is overwritten by the PHY init except its tap
counts; mode 1 is the final state. Contents in §6.9 [M:s wl_rf_common.a].

#### 11.2.11 Post-init steps [M:s]

| Step (§11.2.1) | Description | Priority (revised 2026-11-04) |
|----------------|-------------|-------------------------------|
| 10 RAPP | TX soft-compression curve and thresholds in window D (§6.9). ~~Unknown (likely receive amplifier/power)~~ | TX quality |
| 11 RF-die LUT load | §5.1, with the FEQ bank patch | **required** |
| 12 RF auto control | below | **required** |
| 13 TX power level 6 | `0x1405C` [23:16] = 6 | High |
| 14 Channel 6 | `0x1405C` [7:0] = `0x6B` | **required** (and repeated per channel, §4.2.1 C1) |
| 15 Per-rate digital gain | window D odd entries from VM 108 (§6.9) | Medium |
| 16 TX LO snapshot | software copy of `0x1191C`/`0x11920` | none for WiFi |

**TX LO snapshot** [M:s]: after all init and calibration, two registers are copied to RAM and can
be written back later.

| Register | MMIO Addr | Name (inferred) | Content                          |
|----------|-----------|-----------------|----------------------------------|
| CON7     | 0x1191C   | WLA_CON7        | TX LO leakage trim, I ([14:8])   |
| CON8     | 0x11920   | WLA_CON8        | TX LO leakage trim, Q ([6:0])    |

**ERRATA (2026-11-04):** these are the TX LO trims (calibration result of the TX LO search, §10.1
step 2), not RX DC offsets; and the restore is used by the **BT controller** (it reloads the WiFi TX
LO values), not by the WiFi channel switch, which writes only `0x1405C` [7:0] [M:s wl_rf_common.a,
BT controller library].

**RF auto control register sequence** [M:s, completed 2026-11-04]: `0x11950` (WLA_CON20) is built by
single-bit read-modify-writes, interleaved with the other registers, in this order: clear b0, clear
b1, set b2, set b3; `0x11938` set b26, set b27; `0x11950` set b23, clear b29, set b28; `0x1405C`
`|= 0x07000000`, `|= 0x40000000`; `0x11950` set b10, b11, b12, b13, b18, b19, b20, b21, clear b22;
`0x11924` set b5, b6, b7, b7 again, b8; `0x11950` set b30, clear b31; `0x11964` [31:29] = 1;
`0x1405C` clear b31; `0x11950` set b4, clear b5, set b6, b7, b8, b9, b14, b15, b16, b17, b24, b25,
b26, clear b27; `0x11964` [28:24] = `0x1B`. Final `0x11950` = **`0x57BFFFDC`** (all 32 bits
determined; the vendor radio reads the same [M:hw]). No delays. Reading: hand gain, frequency and
T/R control to the RF die, which then works from the LUT entry selected by `WF_CON7` [7:0] [I].

**Previous description (kept):**

Register sequence as previously listed [M:s]:

Configures automatic gain control and frequency tracking loops in the RF analog section.
Without these, the receiver cannot adapt to varying signal levels from different APs.

Register `0x11950` (CON20, RF analog auto-control):
- Clear bits 0, 1 (disable manual AGC/frequency control)
- Set bits 2, 3 (enable auto AGC/frequency control)
- Set bit 23, clear bit 29, set bit 28
- Set bits 10, 11, 12, 13 (fine-grained AGC loops)
- Set bits 18, 19, 20, 21 (fine-grained frequency loops)
- Clear bit 22

Register `0x11938` (CON14, PLL config):
- Set bits 26, 27

Register `0x1405C` (WL_RST, PLL/clock area):
- OR bits 26:24 with 0x07
- Set bit 30 (PLL enable)

#### 11.2.12 Minimum viable PHY init [I]

~~For initial bring-up (getting any RX to work), the following subset is recommended: MAC/PHY
reset, timing registers, MAC[0x0308], the four window tables, PHY reset. Skip the TX filter, the RF
init (already done by bytecode), and post-init functions initially.~~

**Withdrawn (2026-11-04):** the post-init steps 11, 12 and 14 (LUT with banks, auto control,
channel code) are what tunes the RF die, and the vendor's order ends with them, after both PHY
inits and with no reset afterwards (§4.2.1 A15-A23, §14 rows 5-7) [M:s, I]. Use the full order of
§4.2.1 phase A instead of a subset.

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

---

## 14. Facts awaiting hardware confirmation (2026-11-04)

Each row is a vendor behaviour the GPL driver currently lacks or does differently (§4.2.1 GPL
column), stated as register, value, position in the sequence of §4.2.1 and the observable effect
expected if the behaviour matters. Rows are ordered by how likely each explains the present state
(TX: the MAC never queues a frame, `TXRXQ_PCNT` 0, no TX status; RX: the CCA detector counts energy
but no frame is demodulated, PHY/CRC error counters 0; the four read-only indicators of §4.2
unchanged). The ranking is an inference [I]; the register facts carry their own tags. "Read on
both" means: read the register on the running vendor radio and on core-fm1 before changing
anything; none of these registers has been compared so far unless stated.

| # | Register | Value | Position | Expected observable effect | Tag |
|---|----------|-------|----------|----------------------------|-----|
| 1 | **Confirmed 2026-11-04 12l [M:hw]: frame queued and transmitted.** WDMA TX image: first transferred word | queue word (bit 0 = WIV, bits [2:1] = QSEL = 2; 5 for management, 4 for data), then the 16-byte TXWI, the unpadded frame and 6 more bytes; `WDMA_TX_ADR` = address of the queue word, `WDMA_TX_LEN` = MPDU + 26 | every TX (§4.2.1 C7-C8, §7.11.5) | `TXRXQ_PCNT` byte 2 rises during the transfer and falls after; `TX_STA_CNT0` success or failure counts move; `TX_STA_FIFO` (`0x31718`) valid bit set; the probe request appears on a sniffer. The GPL's first word today is the software length (bits [2:1] of 56 = queue 0) and its TXWI is shifted by 4 bytes | M:s wl_rf_common.a, M:hw (buffer samples), I |
| 2 | `0x3043C` [7:5], `0x30434` bit 18, `0x30438` byte 2 | vendor requires [7:5] != 0, bit 18 = 0 and byte 2 below the queue-2 limit before every TX | read immediately before each TX (C6) | Cheapest discriminating read for TX: if `0x3043C` [7:5] reads 0 on core-fm1 the packet buffer is not ready and no framing change can queue a frame; if it is non-zero the blocker is downstream (row 1). Read on both | M:s wl_rf_common.a, I |
| 3 | **Confirmed [M:hw 12l-12o]: with rows 1, 3-7 applied the receiver decodes frames on every channel.** MAC+`0x1328` TX_PIN_CFG | `0x00050302` (RT2800 bits: PA_PE_G0_EN 1, LNA_PE_A0_EN 8, LNA_PE_G0_EN 9, RFTR_EN 16, TRSW_EN 18) | at every channel switch, after the channel code (C1) and TX_BAND_CFG (C2), before the first probe request; never written earlier | RX: PLCP/CRC error counts in `RX_STA_CNT0` become non-zero, beacons and probe responses arrive (bit 17 of `WDMA_CTL`, interrupt 52). TX: PA enabled on air. Read on both first (expected vendor `0x00050302`, core-fm1 the reset value) | M:s wl_wifi_sta.a, IP:RT2x00 for bit names, I |
| 4 | MAC+`0x132C` TX_BAND_CFG | read-modify-write: bits [2:1] cleared, bit 2 set (2.4 GHz band) | every channel switch, directly before TX_PIN_CFG (C2) | band select for TX and RX paths [IP:RT2x00]; same effects as row 3 if the reset value selects the other band. Read on both first | M:s wl_wifi_sta.a, IP:RT2x00, I |
| 5 | `0x1405C` [7:0] | channel + 101 (one read-modify-write, no delay); with no BBP R94 write, no BBP R106 write (R106 stays 4), no `0x1193C`/`0x11934` write | every channel switch (C1), first after the scan starts | BBP R106 reads 4 throughout a scan (core-fm1 reads the channel number today); the RF die retunes from the LUT entry for the channel; demodulation on channels other than 6; the two `WF_CON6` codes change with the channel [I] | M:s wl_rf_common.a, M:hw (R106), I |
| 6 | RF-die LUT word 1, entries 0-127 | `\|=` bank << 9, bank = lookup(word 1 [28:21]) in the 48-entry limit table built by the bank scan of §5.2 | LUT load (A20), after PHY init #2, CFR mode 1 and RAPP, before auto control | With auto control on, the synthesizer runs in the right VCO band for the selected channel; candidates for `WF_CON4` b23 / `WF_CON8` b23 (lock or ready) asserting and for the `WF_CON6` codes leaving `0xFF` [I] | M:s wl_rf_common.a, I |
| 7 | End of the front-end init | order PHY init #2 → CFR mode 1 → RAPP → LUT load → auto control → power → channel, and **no** MAC/BBP reset (MAC_SYS_CTRL 3, MAC+`0x30C` pulse, R21 pulse) or PHY init after the LUT load and auto control | A17-A23 | Read the four indicators after each of A20, A21 and A23: if one asserts, the step that raised it is the missing condition. A reset after auto control may stop the RF-die sequencer [I] | M:s wl_rf_common.a, I |
| 8 | RF calibration settings | 24 MHz crystal type (`0x11930` [27:23] = `0x0B`), N = 101, fraction `0xC00000`, `0x11954` = `0x02822520` during calibration; or the quick path from VM 187 when its CRC-16 is valid | A16 | RX DC values in the LUT AGC words and TX IQ/DC values in window D taken under the board's real reference clock; RX sensitivity and AGC behaviour closer to the vendor (R32-R46 values) | M:s wl_rf_common.a, M:hw (crystal type), I |
| 9 | MAC+`0x1800` + 8i (i = 0..253); MAC+`0x6800` + 4i (i = 0..255); MAC+`0x7000`..`0x700C`; MAC+`0x1114` | `0xFFFFFFFF`, `0x00FFFFFF`; 1; 0; `&= ~0x001F0000` | interface up (B3, B5, B6), before the first TX | The broadcast/unassociated WCID (`0xFF`) used by the probe request has a defined table entry; TX status for WCID `0xFF` frames [IP:RT2x00, I]; TSF/beacon sync off during the scan | M:s wl_wifi_sta.a, IP:RT2x00, I |
| 10 | `0x14000` WL_CON0 | never written: no `0xFFFF`, no later 0; clock tree (A2-A3) built before the first MAC access | whole start | MAC/BBP readable from power-on with `WL_CON0` = 0; results identical to the released case of 12j/12k. Confirms that the v13 fault came from a missing clock tree, and removes the BT/WiFi arbitration side effects of `0xFFFF` | M:s all libraries, M:hw (vendor 0), I |
| 11 | MAC_SYS_CTRL (MAC+`0x1004`) | only 3 (with the `0x30C` pulse) and `0xC`; never 0 or 4 after the last PHY reset | A9, A10, A13, end of A15/A17; nothing in phase B | No transient TX/RX disable after init; effect unknown | M:s wl_rf_common.a, I |
| 12 | MAC configuration values | table of §7.10 (e.g. `0x31408` = `0x13F`, `0x31400` = `0x17F97` from B12, `0x31350` = `0x1020`), no write to MAC+`0x1200` or MAC+`0x137C` | B2, B12 | Unlikely to block a scan; RX filter differences (DROP_UC_NOME, DROP_BA, DROP_BAR) do not affect beacons [IP:RT2x00, I] | M:s wl_wifi_sta.a |
| 13 | Window D CFR mode 1 and RAPP | §6.9 values (tap counts 15/15/0, per-rate gains, RAPP curve 511 → 271, thresholds 160/160/400, mode 2) | A18, A19 | TX spectral quality and power; not expected to change "never queued" [I] | M:s wl_rf_common.a |
| 14 | Management TXWI W0 TXOP | 3 | every management TX | Unlikely to matter: the vendor's QoS-null and data TXWIs use TXOP 0 and are sent [M:hw] | M:s wl_wifi_sta.a, M:hw |
| 15 | WDMA init | nine `WDMA_CTL` and nine `WDMA_CFG0` read-modify-writes before any MAC reset; `WDMA_CTL` bit 16 never written | A5-A7 | Unlikely to matter (final values equal; bit 16 is read-only) | M:s wl_rf_common.a, M:hw |
| 16 | Interrupt handler | clears `WF_CON4` bit 0 and wakes the receive task only; no access to MAC+`0x328` | after B13 | None until a frame arrives; avoids disturbing the receive-notify source | M:s wl_rf_common.a |

**Not worth trying** (no vendor counterpart) [M:s]: writing the four indicator bits, writing
`0x14054`, `0x14058` or `0x14060`, any write to `0x40000`-`0x40FFF`, any write to BBP R32-R63 or
R94, an MCU command or firmware download, polling an indicator as a start condition.
