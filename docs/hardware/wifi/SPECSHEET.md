# Wi-Fi hardware facts for the AC791N / WL82

Written 2026-10-25 by the clean-room reader for the GPL-3.0 Wi-Fi driver on the JieLi AC791N (WL82)
radio. **The implementers must not read vendor code, vendor IR or vendor disassembly.** They work from
this sheet, the IEEE 802.11 standard, and the permitted references of §2 only; `CLEAN_ROOM_METHODOLOGY.md`
states the barrier and the licence record.

This sheet contains facts only: addresses, bit fields, values, sequences, timing, interrupts, bus
protocols and observed behaviour. It contains no vendor code or pseudo-code. Constant tables follow the
policy of §0 ("Tables"): published tables are cited, tables that a measured rule reproduces are
given as the rule, register tables are recorded in full with their tag, and nothing is transcribed
that a higher-tier source supplies.

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

Update 2026-11-05 (reader pass 3, Phase D; evidence cutoff: the implementer's observation file of
2026-11-05 06:50 with build dev-20261105T064012Z, its device ring and its register sweep of 06:53;
the uncommitted driver tree read on 2026-11-05 for the GPL column; verification: every row below was
re-read in the vendor libraries by the reader, and the join-step, link-up, link-down and key-table
claims were checked a second time by hand): **[M:s wl_wifi_sta.a, hostapd_and_wpasupplicant.a,
wl_rf_common.a; IP:RT2x00 (rt2800.h, rt2800lib.c); PUB assign_macaddr.c, wifi_conf.c, cpu.h,
cache.h, linker scripts, IEEE OUI registry; M:hw vendor captures 2026-11-02 and 2026-11-04, GPL
observations 2026-11-05]**. New: §4.2.1 Phase D (join, authentication, association, link-up,
EAPOL and key install, link-down: rows D1-D32 with the GPL column and the timers); §7.11.5 TXWI rows
for unicast management frames and station EAPOL frames; §7.13 (station slot table, key tables,
address handling, the acknowledgement path, the hand-off contract between the 802.11 state
machine, the supplicant and the key tables, WDMA buffer placement); §14 rows 17-19; §15 T19-T26;
§16 U6 and U14 narrowed, U15-U21 added. **Errata** (old text kept where it stood):
(22) The on-air transmitter address: nothing in the hardware or the libraries substitutes the
transmitter address of a frame; addr2 is the frame bytes (§7.13.1). The address the access point
answered on 2026-11-05 (`D8:C8:0C:F7:36:E2`) is registered to Tuya Smart Inc.: a third station on
the same channel, whose responses the GPL driver accepted because its RX filter has bit 2 (drop
unicast not to me) clear and its parser did not check addr1.
(23) §4.2.1 B5 "1 each (shared-key mode; WCID attributes)": the attribute value 1 selects the
pairwise-key table with no cipher (bit 0 = key-table select); SHARED_KEY_MODE is the `0x7000`
block (§7.13.3).
(24) §7.10 "`|= 0x08` after association": the vendor never sets RX_FILTER_CFG bit 3 in station
mode; the running vendor radio reads `0x17F97` while associated [M:hw 2026-11-02].
(25) §7.11.5 "CCK 1 Mbps only after a rate update the initial scan does not run": that update is
the join step (D3); every management frame after the first join, including the authentication and
association requests, goes out at CCK 1 Mbps with a 304 µs duration field.
(26) The vendor's station slot 1 and the BSSID registers are written at join, **before** the
authentication request (D4-D5), not at link-up only; §4.2.1 B3's "WCID `0xFF` is never used as a
table index" stands, and the vendor's acknowledged unicast management frames all carry WCID `0xFF`.

Update 2026-11-04 (reader pass 2; evidence cutoff: the observation record through iteration 12p
and the driver at commit 76da132d; supported profile: FM-1, 24 MHz crystal, WiFi-only cold start,
station mode; verification: every claim of the private evidence record re-checked in the vendor
libraries, and the vendor unit's window D read-back of 2026-11-03 used as a cross-check): **[M:s
wl_rf_common.a, wl_wifi_sta.a, cpu and BT controller libraries; PUB SDK application sources; M:hw
12l-12o]**. The driver written from this sheet transmits and receives on the FM-1 since iteration
12o (2026-11-04 09:56): a 13-channel scan decodes beacons on channels 3-11 and lists 16 networks
[M:hw]. New: §10.1 rewritten so the full RF calibration can be implemented from this sheet alone
(RF mode table, gain-step rule, measurement engine, trim prologue, attenuation settings, RC tune,
TX LO/IQ and RX DC/IQ searches, bounds and failure contract); new §10.2 (record layout and
validity, quick path, when the record is written); §6.9 completed (both TX-filter coefficient
sets, the compression curve, the final window D state, confirmed against the vendor unit's
read-back); §11.2.9 completed (the 24 MHz constant analog block as an ordered field-write list,
the PLL field map, the crystal word, and the vendor unit's steady state split into constant and
unit-specific parts); §4.2.1 GPL column refreshed to commit 76da132d; §14 reconciled with the
observation record; new §15 (hardware tests for the implementer) and §16 (unspecified steps,
unknowns, housekeeping). **Errata** (old text kept where it stood):
(11) The vendor's receive path handles **one frame per wake** and never loops on `WDMA_CTL` bit 17
(§7.11.6); "re-check bit 17 for more frames" was wrong.
(12) In polling mode (a build option, not the default) the vendor writes neither MAC+`0x328` nor
`0x14050` bit 0 (§7.11.9); "register writes are the same" was wrong.
(13) The runtime AGC idle rule is "no target frame for 2 x 110 ms" (configurable), not 11 ticks
(§6.7).
(14) Record 187 stores its CRC-16 as a **32-bit word at bytes 64-67**, a stored 0 is invalid, and
the record is written only after a full trim that ran because the stored record was invalid (§10,
§10.2).
(15) The vendor's first-scan probe request goes out at **OFDM 6 Mbps**, not CCK 1 Mbps, unless a
profile step the sheet cannot resolve changes the management rate (§7.11.5); the MAC accepts the
GPL's CCK 1 Mbps probe [M:hw 12l].
(16) At interface up the EDCA registers receive the WMM default set (`0x64300`, `0xA4700`,
`0x43538`, `0x3212F`), not the constant set (`0xA4200` ...), which is written only at link-down or
at association (§4.2.1 B9, §7.10).
(17) `PBF_MAX_PCNT` is rewritten to `0x1F3F7F9F` at association (TX burst off); the table value
`0x1F3FBF9F` is replaced by software, not dropped by hardware (§7.10).
(18) The names `PLL_CON0..4` / `ANA_CON0..5` / `WL_CON0..11` that §11.2.9 used for
`0x11900`-`0x11964` clash with the published names `WL_CONn` (`0x14000` block) and `PLL_CON0/1`
(`0x119A0`/`0x119A4`); §11.2.9 now uses the published `WLA_CONn` names.
(19) The vendor identifier earlier text used for the feedback-divider code is replaced by
"divider code"; "PA_C_I" by "PA trim bytes".
(20) The bank-limit scan's degenerate case writes banks 15..0 then 127..97 (8-bit wrap, §5.2); the
GPL's bank table differs from the vendor's (43 vs 53 at divider 121 [M:hw 12n]) and works.
(21) `0x1193C` and `0x11938` do not track the channel on either unit: the MMIO PLL words stay at
divider 121 / the init bank while the RF die tunes from the LUT (§6.8, §11.2.9) [M:hw, M:s].

Update 2026-11-04 (iteration 12i, WL_CTL0 release): **[M:hw]** WL_CTL0 (`0x14000`) is an
assert/release control, not a bus gate: writing 0 after init keeps the MAC readable and starts the
baseband CCA detector (false CCA 201-240 per 100 ms, BBP R60 live) while TX and the five indicators
stay unchanged. Every init so far ran on a held block. §4.2 row updated; new §7.12 is the
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

**Tables (transcription policy, 2026-11-04).** A constant table that the hardware consumes is a
hardware fact; how it reaches this sheet depends on its source, in this order: (1) a published SDK
source is cited by path and line and not copied (the RF-die LUT, the CCA tunables, the default
trim bytes, the record ids); (2) a table that a measured generating rule reproduces exactly is
given as the rule with check values (the AGC RAM, §6.7; the receive gain steps, §10.1.2); (3) a
register table (entry index, bit position, value, write order) is recorded in full with its tag
(the BBP window tables of §11.2.6-§11.2.8b, the TX-filter coefficient sets and the compression
curve of §6.9, the RF mode table of §10.1.1); (4) a value read back from the running vendor unit
is tagged [M:hw] when it has been; (5) nothing is transcribed that a higher-tier source supplies.
There is no numeric size threshold; each table says which route it took. Unit-specific calibration
results (RX DC, TX LO, TX/RX IQ, RC codes, VCO bank, crystal caps, PA trims) are never to be
hard-coded from any unit's read-back; §11.2.9 marks them.

**Published-source record [PUB].** The SDK sources cited in this sheet come from JieLi's public
repository `fw-AC79_AIoT_SDK` (gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK; repository licence
Apache-2.0, `LICENSE` = the Apache License 2.0 text; default branch `release/AC79NN_SDK_V1.2.0`,
head commit `0617f4cc` of 2026-10-27 at the time of writing). They were read from the release
package `AC79NN_SDK_V1.2.14_2026-10-13`, whose copies of every cited file are byte-identical to
the repository's. Files cited: `apps/common/net/wifi_conf.c`, `apps/common/config/user_cfg.c`,
`apps/demo/demo_wifi/board/wl82/board.c`, `include_lib/net/wifi/wifi_connect.h`,
`include_lib/utils/syscfg/syscfg_id.h`, `include_lib/driver/cpu/wl82/asm/WL82.h`,
`include_lib/driver/cpu/wl82/asm/hwi.h`. The vendor's pre-compiled libraries in the same package
are not published source; they were analysed under the interoperability exemption (`CLEAN_ROOM_METHODOLOGY.md`).

**Source search record for the 2026-11-04 reader pass 2.** Tier 1: the published `user_cfg.c`,
`board.c` and `wifi_connect.h` supply the default crystal caps, PA trim bytes and per-rate gains
and the record ids (cited in §10, §6.9, §11.2.9); the published profile string in `wifi_conf.c`
supplies the wireless mode, TX-burst, HT-bandwidth and scan-dwell settings that decide several
MAC values (§4.2.1 B9, C5, §7.10, §7.11.5); no published document describes the RF calibration,
the BBP window contents or the WDMA engine. Tier 2: the RT2800 public header explains the
TXWI/RXWI/TXINFO/RXINFO layouts, the EDCA packing and the association-time MAC writes
[IP:RT2x00]. Tier 3: the vendor unit's window D read-back of 2026-11-03 confirms the TX-filter
and compression finals (§6.9); iterations 12l-12o confirm the TX image, the gate, the front-end
registers, the channel code, the bank patch and the interrupt handling (§14). Tier 4: the
calibration, TX-filter, PHY-init, RF-init, RT-layer and transport routines of `wl_rf_common.a`
and `wl_wifi_sta.a` and the clock, delay, CRC and power routines of the cpu library were re-read
for the claims listed in the private evidence record; every fact added in this pass carries
[M:s] with the library named, or a higher tag.

**Source search record for the 2026-11-05 reader pass 3 (Phase D).** Tier 1: the published
`apps/common/net/assign_macaddr.c` (two variants of the address generator, L273-L310 and
L314-L339), the published profile string `MacAddress=00:00:00:00:00:00` in `wifi_conf.c` (L396,
L530), `include_lib/net/wifi/wifi_connect.h` (events L71-L113, API L170-L397),
`include_lib/net/wireless_ext/wpa_supplicant_event.h` (L5-L12) and `wifi_ext.h` (L10, L16),
`include_lib/driver/cpu/wl82/asm/cpu.h` (address map L45-L53, cache wait L65-L66), `cache.h`
(L22-L32), `csfr.h` (L41, L105-L115), the linker scripts `cpu/wl82/sdk_ld_sfc.c` and
`sdk_ld_sdram.c`; the IEEE OUI registry (lookup of `D8:C8:0C` through macvendors.com on
2026-11-05: Tuya Smart Inc.). Tier 2: `rt2800.h` (station, key, IV/EIV, attribute and shared-key
tables at `0x1800`/`0x4000`/`0x6000`/`0x6800`/`0x6C00`/`0x7000` with their field masks, TX_RTY_CFG,
TX_STA_FIFO, BCN_TIME_CFG, EDCA, protection, fallback, MAX_LEN_CFG, RX_PARSER_CFG) and
`rt2800lib.c` (the station-table, key-table, interface-address, TX-status and ERP routines) as the
public meaning of the same registers. Tier 3: the vendor captures of 2026-11-02 (`0x31008`/`0x3100C`,
`0x31408`, `0x31718`, `0x31400` while associated), the V1 capture of 2026-11-04, the oracle build's
symbol table (buffer addresses), and the implementer's 2026-11-05 observations (ring, sweep, the
response timing). Tier 4: the station library's control, synchronisation, authentication,
association, link-up/link-down, ASIC-helper, management-transmit, counter, periodic-task,
control-interface and key routines, the supplicant archive's driver glue, 4-way-handshake and
key-install routines, and the transport library's WDMA and staging routines were read for the
claims listed in the private evidence record; no published document describes the station-table
and key-table usage, the join-time writes or the hand-off, so the exemption was used for those.
Every fact carries [M:s] with the library named, or a higher tag.

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

**No GPL-compatible Wi-Fi driver existed for the AC791N before this project.** Status
2026-11-04: the GPL-3.0 driver written from this sheet transmits and receives on the FM-1 (a
13-channel scan lists 16 networks) [M:hw 12o]; on 2026-11-05 it reached the authentication step with no unicast frame acknowledged yet (§7.13.4); the paragraphs below describe the starting point.

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

The implementers may use the following references (revised 2026-11-04; `CLEAN_ROOM_METHODOLOGY.md` keeps the
same list). Nothing else that touches this hardware is permitted: not the vendor's pre-compiled
libraries, not any reader-side material (the reader's private store), not the stale copy
`docs/hardware/WIFI.md`, and not third-party code written from the vendor libraries (see
`CLEAN_ROOM_METHODOLOGY.md`, "fm1-base").

| Reference | Licence | Use for | Tag |
|-----------|---------|---------|-----|
| Linux `drivers/net/wireless/ralink/rt2x00/` (`rt2800.h`, `rt2800usb.h`, `rt2800lib.c`, `rt2800mmio.h`), pinned by kernel version where cited | GPL-2.0+ | MAC register names and bit layouts, TXWI/RXWI/TXINFO/RXINFO, EDCA packing, STA state machine patterns | IP:RT2x00 |
| Ralink RT5350 datasheet §3.20 (public) | published | MAC register semantics of the RT2800 ancestor | IP:RT2x00 |
| wpa_supplicant / hostapd (w1.fi) | BSD-3-Clause | WPA2-PSK, 4-way handshake, CCMP, EAPOL | OSS |
| lwIP 2.x (savannah.nongnu.org) | BSD-3-Clause | TCP/IP stack, netif driver pattern | OSS |
| ESP-IDF WiFi (github.com/espressif) | Apache-2.0 | STA state machine patterns, API surface | OSS |
| JieLi SDK published sources listed in §0 ("Published-source record") | Apache-2.0 | RF-die LUT data, CCA tunables, default trim bytes, record ids, register block names, IRQ index, profile defaults | PUB |
| Optimist `firmware/upstream/optimist/docs/BLE-HW-FACTS.md` and this repository's `firmware/hal/fm1_ble_rf.h` with its capture tables | GPL-3.0 / MIT | the shared RF analog bring-up replay and the BLE-side facts | OSS |
| IEEE 802.11-2020, IEEE 802.11i, FIPS 197, FIPS 180-4, RFC 2104, RFC 2898 | standards | frame formats, crypto | standard |

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

Column **GPL**: what the GPL driver at commit 76da132d (2026-11-04, iteration 12p) does for that
step, read from its source and its replayed stock-BLE capture [I for the comparison; refreshed in
reader pass 2]: **same**; **diff
value**; **diff order**; **missing**; **extra** (GPL writes something the vendor does not);
**replay** (only the captured stock-BLE bytecode does it, before the GPL driver's own re-inits).

**Phase 0: boot clock setup (not radio specific)**

| # | Register | Value / operation | Delay / poll | Condition | GPL |
|---|----------|-------------------|--------------|-----------|-----|
| A0 | `0x10010` SYS_CLK2 | [11:10] = 01 (UART clock), bit 16 cleared, [3:2] cleared. No other SYS_CLK1/SYS_CLK2 bit is written by radio code; the vendor unit's bit 16 = 1 is set outside the analysed libraries | | boot | extra: GPL sets SYS_CLK2 b8 (then clears it and sets b10) and SYS_CLK1 b12 (later cleared); bit 16 and [3:2] are not cleared |

**Phase A: shared radio front end (once per power-up)**

| # | Register | Value / operation | Delay / poll | Condition | GPL |
|---|----------|-------------------|--------------|-----------|-----|
| A1 | `0x10010` SYS_CLK2 | [15:14] = 0: radio clock source = PLL (3 = disabled, used only by BT stop) | | first front-end init | replay only (the GPL's own write is unreachable code) |
| A2 | `0x14040`, `0x14044`, `0x14048`, `0x1404C`, `0x14050` | = 0 each, in that order (WiFi interrupt masked) | | | replay only |
| A3 | `0x14040` WF_CON0 | `\|= 0x100`; `\|= 0x40`; `\|= 0x01`; 1 µs; `\|= 0x04`; 1 µs; `\|= 0x08`; 1 µs; `\|= 0x02`; 50 µs. Final `0x14F` | 1, 1, 1, 50 µs | | replay only |
| A4 | `0x1405C` WF_CON7 | six read-modify-writes: [7:0] = 5; [15:8] = 0; [23:16] = 0; `\|= 0x07000000`; `\|= 0x40000000`; bit 31 cleared. Result `0x47000005` | none | | replay only (the direct write of `0x47000005` is unreachable code) |
| A5 | `0x1404C` WF_CON3 | `\|=` bit 24 (WDMA clock) | | | same |
| A6 | `0x30F04` WDMA_CTL | nine separate read-modify-writes in this order: `\|= 0x800`, `\|= 0x400`, `\|= 0x200`, `\|= 0x20`, `\|= 0x10`, `&= ~0x8`, `\|= 0x4`, `\|= 0x2`, `\|= 0x1`. Bit 16 is never written | none | | diff value and diff order: one write of `0x00010E37` (sets read-only bit 16) after the GPL's own MAC reset |
| A7 | `0x30F00` WDMA_CFG0 | nine read-modify-writes: `\|= 0x400`, `&= ~0x200`, `&= ~0x100`, [7:5] = 3, `&= ~0x10`, `\|= 0x8`, `\|= 0x4`, `\|= 0x2`, `\|= 0x1` (written `0x46F`, reads `0x467`) | none | | same final value, diff order: one write of `0x46F` after the GPL MAC reset |
| A8 | PA1/PA2 direction and output, `0x14008` `\|= 0x103` | external-PA option | | board option; **not configured on the FM-1** | same (not written) |
| A9 | MAC+`0x1004` MAC_SYS_CTRL; MAC+`0x30C` | MAC_SYS_CTRL = 3; MAC+`0x30C` = `0x1E`; MAC+`0x30C` = 0 (reset pulse of bits [4:1]) | none | | same inside its PHY init; **extra**: a separate reset `\|= 3`, 10 µs, `= 0` before it, and MAC_SYS_CTRL = `0xC` repeatedly during calibration |
| A10 | MAC_SYS_CTRL; BBP R22, R21, R21, R25 | MAC_SYS_CTRL = `0xC`; R22 = 0; R21 = `0x31`; R21 = `0x30` (BBP reset pulse on bit 0); R25 = 0 | BBP busy poll | | same (the calibration also writes R25 = `0x18`) |
| A11 | MAC+`0x1104`, `0x1100`, `0x1330`, `0x1334`, `0x1338`, `0x1348` | `0x00140314`, `0x33A40C0B`, `0x00040804`, `0x000C0808`, `0x000C0104`, `0x000F1290` (in that order) | | | same |
| A12 | MAC+`0x308` | `0x9407` | | | same |
| A13 | repeat A9, A10 | | | | replay only |
| A14 | window D (TX back end), mode-0 set | D[`0x4D`] = 1, D[`0x4E`] = 1, D[`0x4F`] = 0, then 15 coefficient writes in D[`0x29`..`0x4B`]; the PHY init (A15) overwrites every coefficient, only the three tap counts survive | | | diff value and position: D[`0x29`..`0x2C`] = `6C 01 02 02`, D[`0x31`] = 3, D[`0x4D`] = D[`0x4E`] = `0x0F`, D[`0x4F`] = 0 inside its PHY init; no mode-0 set |
| A15 | PHY init #1 | the sequence of §6.6 (resets, timing registers, MAC+`0x308`, AGC RAM, windows C, D', C', D, fix-ups, R106 = 4, R84 = 2, final PHY reset) | BBP busy poll only | | diff order: one own PHY init (R84/R106 after the final reset; D' ascending without the second D'[`0x18`] write; fix-ups folded into the tables) |
| A16 | RF transceiver init and calibration | crystal word `0x11930` (type [27:23] = `0x0B` for 24 MHz, caps from VM 106); `0x11950` = 0; constant analog block; RF mode off; PLL restart; PLL bank scan and 48-entry bank-limit table (§5.2); PLL for 2442 MHz with 24 MHz values (N = 101, fraction `0xC00000`, `0x11954` = `0x02822520`); PLL restart; loop pulse; calibration: quick path from VM 187 when its CRC is valid, else full trim (§10.1); RF mode RX; PA fields from VM 107 | PLL restart 5/5/100 µs; loop pulse 80 µs | 24 MHz crystal (`PLL_CON0` [27:26] != 2) | **diff value**: calibrates with the 40 MHz crystal type and PLL words, always a full trim (no quick path), then forces fifteen RF registers to the vendor read-backs (§11.2.9 marks the unit-specific ones); the bank-limit scan runs (24 MHz words, control loops off) but the forced `0x11938` word overwrites the scanned bank outside the LUT; no PA fields from record 107 (the forced values win); no loop pulse |
| A17 | PHY init #2 | byte-identical to A15 | | | diff order: the own PHY init runs after LUT load + auto control #1; the MAC init sits between the PHY init and LUT load + auto control #2 |
| A18 | window D (TX back end), mode-1 set | 57 writes: D[`0x4D`] = D[`0x4E`] = 15, D[`0x4F`] = 0; coefficient groups in D[`0x26`..`0x4C`]; per-rate gains D[`0x0B`, `0x0D`, `0x0F`, `0x11`, `0x13`] = `0x20`, D[`0x15`] = `0x2B`, D[`0x17`] = `0x26`, D[`0x19`] = D[`0x1B`] = `0x20`, D[`0x1D`] = `0x2B`, D[`0x1F`] = `0x26`, D[`0x21`] = `0x20`; D[`0x23`] = `0x23`, D[`0x24`] = `0x32`, D[`0x25`] = 0 | | | **missing** (D[`0x23`] stays 3) |
| A19 | RAPP (TX soft compression, window D) | mode off; 128-entry curve load; thresholds and powers; mode 2 (§6.9) | | | **diff value**: rising ramp 0..254, write-disable polarity inverted, thresholds written to D[`0x56`..`0x59`], no mode 0 before the load, no power words |
| A20 | RF-die LUT load | bridge reset and sync; 256 entries x 2 words with patches: FEQ entries 0-127 word 1 `\|=` bank << 9 from the bank-limit table; AGC entries 128-223 word 0 RX DC; APC entries 224-255 PA fields; then six writes of 0 and one of `0x10000` to `0x14028` (§5.1) | SPI busy poll per word | | same patches (FEQ bank from the scan, AGC DC from the own trim, APC from record 107 or the published default); runs twice, the second after all resets |
| A21 | RF auto control | `0x11950` built bit by bit to `0x57BFFFDC`; `0x11938` b26 = b27 = 1; `0x11924` b5..b8 = 1; `0x11964` [31:29] = 1 then [28:24] = `0x1B`; `0x1405C` `\|= 0x07000000`, `\|= 0x40000000`, bit 31 cleared (§11.2.11) | none | | same final values (forced); bit 31 of `0x1405C` not cleared; runs twice, the second after all resets |
| A22 | `0x1405C` [23:16] | = 6 (TX power level) | | | replay only |
| A23 | `0x1405C` [7:0] | = 6 + 101 = `0x6B` (channel 6) → `0x4706006B` | | | n/a: [7:0] is written per channel (C1) |
| A24 | window D per-rate gains | defaults of A18, then every non-zero byte of VM 108 overrides its entry | | VM 108 present | missing (the replay's values are overwritten by the own PHY init) |
| A25 | (software snapshot of `0x1191C`, `0x11920`) | no register write | | | same |

No delay follows A21-A24 [M:s]. Phase A writes nothing to `0x14000`, `0x14004`, `0x14054`,
`0x14058`, `0x14060`, MAC+`0x1200`, MAC+`0x1328` or MAC+`0x132C` [M:s].

**Phase B: driver probe and interface up (same task, directly after phase A)**

| # | Register | Value / operation | Delay / poll | Condition | GPL |
|---|----------|-------------------|--------------|-----------|-----|
| B1 | `0x14000` `\|= 3`, arbiter through `0x14004` | coexistence start | | **only if BT already did the front end**; not on a WiFi-only start | same: no write to `0x14000` on the boot path (12p); the debug probe may write 0 on host request |
| B2 | MAC configuration table, 22 writes in this order | MAC+`0x42C` = `0xF8F0E8E0`, `0x430` = `0x6F77D0C8`, `0x1408` = `0x13F`, `0x140C` = `0x8003`, `0x1400` = `0x17F97`, `0x1350` = `0x1020`, `0x1018` = `0x1F00`, `0x40C` = `0x1F3FBF9F`, `0x134C` = `0x47D01F0F`, `0x1404` = `0x13`, `0x1364` = `0x05740003`, `0x1368` = `0x05740003`, `0x408` = `0x00F40006`, `0x1370` = `0x03F44084`, `0x1374` = `0x01744004`, `0x1378` = `0x03F44084`, `0x136C` = `0x01744004`, `0x1340` = `0x583F`, `0x1344` = `0x00092B20`, `0x1380` = `0x002400CA`, `0x1608` = 2, `0x1204` = 3 | | | diff value in `0x1408` (`0x15F`), `0x1400` (`0x1BF93`), `0x1350` (`0x007F1020`), `0x1018` (`0x000A3FFF`), the protection set (association values); **extra**: `0x1200` = `0x00030601`, `0x137C` = `0x0038013A`; RX filter written first |
| B3 | MAC+`0x1800` + 8i, i = 0..253 (WCID table) | each entry bytes `FF FF FF FF FF FF FF 00` (words `0xFFFFFFFF`, `0x00FFFFFF`); entries 254 and 255 are never written, and WCID `0xFF` is never used as a table index by the vendor (only as the "no station" marker in TXWI word 1) [M:s] (§7.13.2: every management frame, including the acknowledged unicast ones, carries it) | | | **missing** |
| B4 | MAC+`0x1700`..`0x1714` | read (read-to-clear counters) | | | same (once) |
| B5 | MAC+`0x7000`..`0x700C`; MAC+`0x6800` + 4i, i = 0..255 | 0 each (SHARED_KEY_MODE); 1 each (attribute: pairwise-key table selected, no cipher; §7.13.3) | | hard-reset path (taken at first start) | **missing** |
| B6 | MAC+`0x1114` BCN_TIME_CFG | `&= ~0x001F0000` (TSF/beacon sync off) | | | **missing** |
| B7 | MAC+`0x1700`..`0x1714`; MAC+`0x1340` | read again; `0x583F` | | | diff: no second read; `0x1340` written once |
| B8 | MAC+`0x1008`, `0x100C` | MAC address; `0x100C` [23:16] = `0xFF` | | | same |
| B9 | MAC+`0x1300`..`0x130C` EDCA; MAC+`0x132C` bit 0 | the WMM default set written by the HT setup (§7.10): AC0 MAC+`0x1300` = `0x64300`, AC1 = `0xA4700`, AC2 = `0x43538`, AC3 = `0x3212F` [M:s wl_wifi_sta.a]; MAC+`0x132C` bit 0 only with the 40 MHz option (the published profile has it off: not written) | | | diff value: `0xA4300`, `0xA4700`, `0x43538`, `0x3222F` (association-time values) |
| B10 | delay | 10 ms | 10 ms | | missing (50 µs between MAC_SYS_CTRL 4 and `0xC`) |
| B11 | first receive buffer posted → `0x14050` `\|= 1` | WiFi interrupt unmasked | | | same value; set after the IRQ attach and MAC+`0x328`; re-opened after every receive drain |
| B12 | MAC+`0x1400` RX_FILTER_CFG | `0x17F97` (the station arm; the AP arm writes `0x15F97`; the operation-mode byte is set to station at module init, and B2 already wrote the same value) [M:s] | | station mode | diff value (`0x1BF93`; `0x1BF9F` after association) |
| B13 | MAC+`0x328`; interrupt 52 | MAC+`0x328` = 0; interrupt 52 registered (priority argument 2, CPU argument 1, i.e. core 1 [I]); MAC+`0x328` = 2 | | interrupt-driven receive | same values; the line is attached before MAC+`0x328` = 0/2 and without a CPU selection; handler as the vendor (12o); extra MAC+`0x324` = 2 before each RX DMA as §7.11.6 |
| B14 | MAC_SYS_CTRL | **no write**: stays `0xC` from the final PHY reset of A17 | | | extra: 0, table, 4, 50 µs, `0xC` |

The order B11 before B13 means the interrupt mask is opened before interrupt 52 is registered
[M:s]; harmless because nothing is pending [I].

**Phase C: scan, per channel, then the first transmit**

| # | Register | Value / operation | Delay / poll | Condition | GPL |
|---|----------|-------------------|--------------|-----------|-----|
| C1 | `0x1405C` [7:0] | = channel + 101, one read-modify-write; no PLL, LUT or BBP access, no delay | | channel in the driver's channel list | same (one read-modify-write; no BBP or PLL write) |
| C2 | MAC+`0x132C` TX_BAND_CFG | `(v & ~0x6) \| 0x4` for channels below 15 (`\| 0x2` otherwise) | | | same |
| C3 | MAC+`0x1328` TX_PIN_CFG | `0x00050302`: a plain write of the 2.4 GHz base value `0x00050F02` (one TX path) masked with `0x0005030A` (one RX path), from the 1T1R antenna configuration; the first write of this register happens here, before the first probe request [M:s] | | | same |
| C4 | (AGC initial value through the RT2800 BBP path) | dropped by the vendor's own register filter: no hardware write | | | n/a |
| C5 | dwell timer armed, then one probe request per channel visit (active scans only; passive channels send none) | software | 200 ms (the published tunable, infrastructure mode); 140 ms otherwise; 30 ms fast-active; 110/90 ms in dual-band modes [M:s, PUB] | | diff order: probe sent right after the switch, then a 100 ms dwell; channels 1-13 |
| C6 | TX readiness gate | read MAC+`0x438` byte 2 (pages in TX queue 2) and require it below the limit (byte 1 of the value last written to MAC+`0x40C`: `0xBF` from the table, `0x7F` after association, §7.10) with (limit − pages) x 128 > MPDU length; read MAC+`0x434`, require bit 18 = 0; read MAC+`0x43C`, require [7:5] != 0. Otherwise retry from the first read after a 10 ms sleep, a scheduler yield or at once (a published build option; yield is the default without uCOS), without bound; every 10 s the vendor reads MAC+`0x438`, `0x40C`, `0x434`, `0x43C`, `0x1500`, `0x1504` for a diagnostic | polled retry | | same reads; bounded: up to 5 x 10 ms, then transmits anyway |
| C7 | TX staging | software buffer: length word (MPDU + 22, not transferred), queue word (bit 0 = WIV, [2:1] = queue 2), TXWI, frame (§7.11.5) | | | same layout (software length word, queue word 5/4, TXWI, unpadded frame, 6 zero bytes); TXWI: TXOP 0, density 5, PacketId 0, ACK 0 and WCID `0xFF` for every frame |
| C8 | WDMA TX | `WDMA_CTL` `\|= 0x400`; barrier; `WDMA_TX_ADR` = address of the queue word; `WDMA_TX_LEN` = MPDU + 26 (bit 31 = 0); barrier; poll `WDMA_CTL` b20; `WDMA_CTL` `\|= 0x400` | b20 polled without bound | | same (bit 20 poll bounded at 10 ms) |

**Phase D: join, authentication, association, link-up, keys, link-down [M:s wl_wifi_sta.a,
hostapd_and_wpasupplicant.a; added 2026-11-05]**

The 802.11 state machine (scan, join, authentication, association) runs in the station library;
the 4-way and group handshakes and the key derivation run on the host supplicant, which drives the
station library through a control interface and receives its events; the station library forwards
every received EAPOL frame upward unchanged and transmits EAPOL frames handed to it as ordinary
data frames (§7.13.5) [M:s]. Profile assumed: infrastructure station, 2.4 GHz, 20 MHz, B/G/N
mixed, WMM access point, WPA2-PSK CCMP, TX burst off. "Slot n" = entry n of the station slot table
MAC+`0x1800` + 8n (§7.13.2). D1-D10 run before any key exists; D11-D24 are the link-up routine
(one task, directly after the association response); D25-D29 the EAPOL and key phase; D30-D32 the
link-down routine. Column **GPL**: build dev-20261105T064012Z and the uncommitted tree of
2026-11-05 [I for the comparison].

| # | Register / action | Value / operation | Delay / timer | Condition | GPL |
|---|-------------------|-------------------|---------------|-----------|-----|
| D1 | key clearing by the host before the join: slot 1 attribute MAC+`0x6804`; SHARED_KEY_MODE MAC+`0x7000` | `0x6804` = 1 (pairwise-key table selected, no cipher); nibbles [3:0], [7:4], [11:8], [15:12] of `0x7000` cleared (read-modify-write) | | the only hardware effect of the connect request before the join; no key material is handed down | missing (tables never written: all read 0) |
| D2 | join: channel switch to the target's channel (C1-C3: `0x1405C` [7:0], TX_BAND_CFG, TX_PIN_CFG) | as C1-C3 | beacon-wait timer **2000 ms**; no probe request at join | | same writes at connect; **diff order**: no beacon wait, the authentication request follows at once |
| D3 | beacon of the target BSSID received (software): management rate = CCK 1 Mbps from here on (§7.11.5); channel switch repeated; MAC+`0x132C` bit 0 cleared (20 MHz) | read-modify-write of `0x132C` | beacon-wait timer cancelled | always; bit 0 set only for a 40 MHz BSS below its control channel | n/a (rate fixed at CCK 1 Mbps; bit 0 never set) |
| D4 | MAC+`0x1010`, `0x1014` MAC_BSSID_DW0/1 | `0x1010` = BSSID bytes 0-3 (byte 0 in [7:0]); `0x1014` = bytes 4-5 in [15:0], [31:16] = 0 | | before the authentication request | same (written at connect, same packing) |
| D5 | slot 1 = the access point: MAC+`0x1808`, `0x180C` | `0x1808` = BSSID bytes 0-3; `0x180C` = bytes 4-5 in [15:0], [31:16] = 0 | | before the authentication request | **missing** |
| D6 | deauthentication frame to the access point, reason 3 (unicast management TXWI, §7.11.5) | | | unless a MIC-failure block flag is set; precedes every authentication request | **missing** |
| D7 | authentication request (open system, sequence 1; header duration 304 µs; §7.11.5) | | response timer **800 ms**; on expiry a deauthentication (reason 6) is sent and the request is repeated once (open again for WPA2-PSK); after the second failure → D8 and the next entry of the scan table | | diff: one attempt, 3000 ms; TXWI diff (§7.11.5); no deauthentication on timeout |
| D8 | failed authentication or association: slot 1 rewritten from the software entry, which is zero at that point: MAC+`0x1808` = 0; MAC+`0x180C` [15:0] = 0, [31:16] kept (read-modify-write) [M:s; I for the zero] | | | after D7's second failure, after D9's failure; also at link-down (D31) | n/a |
| D9 | association request (capability from the beacon masked `0x0533`, listen interval, RSN element from the host supplicant; §7.11.5) | | response timer **300 ms**; one attempt per BSS; on timeout or a non-zero status → D8, next entry | | diff: resent every 500 ms within 3000 ms; TXWI diff |
| D10 | association response status 0: software station entry (WCID 1, AID from the response); no hardware write | | | | n/a |
| D11 | MAC+`0x1010`, `0x1014` | BSSID again (as D4) | | infrastructure | same |
| D12 | EDCA MAC+`0x1300`-`0x130C` | from the beacon's WMM parameter set: [7:0] TXOP (32 µs units), [11:8] AIFSN, [15:12] CWmin exponent, [19:16] CWmax exponent; AC2 (`0x1308`): AIFSN forced to 5, TXOP x 6/10; no WMM: `0xA4200`, `0xA4200`, `0xA4260`, `0xA4230` (TX burst off) | | | diff order: one access point's association-time values written at init (`0xA4300`, `0xA4700`, `0x43538`, `0x3222F`) |
| D13 | TX_RTS_CFG MAC+`0x1344`; CCK_PROT_CFG MAC+`0x1364`; OFDM_PROT_CFG MAC+`0x1368` | `0x1344` [23:8] = RTS threshold 2347 (`0x92B`; `0x1000` when A-MSDU or aggregation is on), [7:0] kept; `0x1364` = `0x1368` = `0x07F40000` (no protection, NAV short, all TXOP allowed, RTS threshold enable) | | always at link-up; later, when the beacon's ERP element asks for protection, `0x1368` = `0x07F60000` (CTS-to-self at CCK 1 Mbps) | diff order (init), same values |
| D14 | HT protection MAC+`0x136C`, `0x1370`, `0x1374`, `0x1378` (MM20, MM40, GF20, GF40) | by the beacon's HT operation mode: mode 0 → `0x01744004`, `0x03F44084`, `0x01744004` (`0x01754004` with a non-greenfield station), `0x03F44084`; modes 1 and 3 → `0x01754004`, `0x03F54084`, `0x01754004`, `0x03F54084` (`0x01750003`/`0x03F50003` when BG protection is on); mode 2 → `0x01744004`, `0x03F54084`, `0x01744004` (`0x01754004` non-GF), `0x03F54084`; a TX block-ack session later rewrites them as mode 8 (the mode-1 values) | | HT access point | diff order and value: `0x01754004`, `0x03F54084`, `0x01754004`, `0x03F54084` at init (the mode-1 values) |
| D15 | AUTO_RSP_CFG MAC+`0x1404` | bit 4 = 1 (short-preamble auto responses; stays `0x13`) | | access point advertises short preamble and the configured preamble is not "long" | same (`0x13` at init) |
| D16 | BCN_TIME_CFG MAC+`0x1114` | [15:0] = beacon interval x 16 (TU/16 units), [20:16] = `0x0B` (TSF ticking, TSF sync mode 1 = station, TBTT on, beacon generation off), [31:21] kept → `0x000B0640` for 100 TU | | | **missing** (no beacon sync; TSF not ticking) |
| D17 | LEGACY_BASIC_RATE MAC+`0x1408` | bits of the access point's basic rates (b0 1, b1 2, b2 5.5, b3 11, b4 6, b5 9, b6 12, b7 18, b8 24, b9 36, b10 48, b11 54 Mbps) with b4, b6, b8 set whenever 6/12/24 Mbps are supported at all → `0x15F` for basic 1/2/5.5/11 [M:hw 2026-11-02]; HT_BASIC_RATE unchanged (`0x8003`); the management rate stays CCK 1 Mbps | | | same value, diff order (init) |
| D18 | fallback tables MAC+`0x1354`, `0x1358`, `0x135C`, `0x1360` | `0x65432100`, `0xEDCBA980`, `0xEDCBA988`, `0x00002100` (the one-stream rate tables leave the defaults) | | | **missing** |
| D19 | MAX_LEN_CFG MAC+`0x1018` | `0x000A0FFF` with [13:12] (max PSDU size) = the access point's A-MPDU factor 0..3 → `0xA3FFF` on the running vendor [M:hw] | | HT access point | `0xA3FFF` at init (diff order) |
| D20 | EDCA_AC0 MAC+`0x1300` [7:0]; PBF_MAX_PCNT MAC+`0x40C` | TXOP byte = 0 and `0x40C` = `0x1F3F7F9F` (TX burst off); `0x60` and `0x1F3FBF9F` with TX burst | | | **diff value**: `0x1F3FBF9F` stays (queue-2 page limit `0xBF`, §7.11.5) |
| D21 | RX_PARSER_CFG MAC+`0x1418` ("set NAV for all received frames" [IP:RT2x00]) | 1 when the link is infrastructure with WMM in use and the pairwise cipher is TKIP (the library's encryption-status code 4 [M:s; I for the TKIP reading of that code]), else 0: 0 for a CCMP link | | | **missing** (reset value unknown, §16 U17) |
| D22 | TX_LINK_CFG MAC+`0x1350` bits 12 (CF-ACK piggyback) and 11 (RDG) with EDCA_AC0 TXOP = `0x80` | only when the access point's Ralink vendor element advertises them; otherwise untouched | | not on an ordinary access point | n/a (`0x007F1020` at init; [23:16] on the running vendor is a hardware-updated feedback field [I]) |
| D23 | slot time: **no register write**; BKOFF_SLOT_CFG stays `0x00140314` (20 µs); short slot is a software flag only | | | | same |
| D24 | media connected (software); the 802.1X port is "not secured": non-EAPOL data is dropped both ways in software until the pairwise key is installed | | a 20 s handshake timer exists only for the library's internal supplicant, which this build does not use | WPA modes | same (software) |
| D25 | EAPOL frames from the host supplicant (messages 2/4 and 4/4, group 2/2): data path, WCID 1, queue word 4 (WIV 0), CCK 1 Mbps, no per-frame "do not encrypt" marker; they leave in the clear because slot 1's attribute has no cipher (D1) | | | | diff: WCID `0xFF` (no station entry); otherwise the same path |
| D26 | pairwise key install, **after** message 4/4 has been handed to the MAC (no wait for its TX status): shared-key table MAC+`0x6C00` slot (BSS 0, key 0); SHARED_KEY_MODE MAC+`0x7000`; IV/EIV MAC+`0x6008`, `0x600C`; slot 1 attribute MAC+`0x6804` | `0x6C00` + 0..15 = TK, + 16..23 = TX MIC key, + 24..31 = RX MIC key (TKIP; for CCMP the two MIC fields are written as supplied, unused), as little-endian words (§7.13.2); `0x7000` [3:0] = cipher code (4 AES, 3 TKIP); `0x6008` = `0x20000001` (extended IV, key id 0, packet number 1), `0x600C` = 0; `0x6804` = (cipher & 7) << 1 \| (cipher & 8) << 7 \| BSS 0 → `0x8` for AES, `0x6` for TKIP (bit 0 = 0: shared-key table). The pairwise-key table MAC+`0x4000` is not used | | WPA2-PSK; received sequence counters are not loaded | **missing** (software CCMP only; group key not processed) |
| D27 | group key install: shared-key table slot (BSS 0, key id from the GTK element, 1 or 2) MAC+`0x6C00` + 32 x id; SHARED_KEY_MODE nibble [4 id + 3 : 4 id] = cipher code | no attribute, no IV/EIV write | | | **missing** |
| D28 | port secured (software) at the pairwise install (WPA2 family); application link-up and DHCP follow; no hardware write | | | | same |
| D29 | rekey: the same slots are overwritten in place (packet number restarts at 1); the previous group slot stays | | | | n/a |
| D30 | station-initiated disconnect: disassociation (reason 8) then a deauthentication to the access point (unicast management TXWI) | | | | not read |
| D31 | link-down writes, in this order: AUTO_WAKEUP_CFG MAC+`0x1208` = 0; software entry deleted and slot 1 rewritten (D8); EDCA = the no-WMM set of D12; BCN_TIME_CFG [20:16] cleared; `0x6804` = 1 and the four BSS-0 nibbles of `0x7000` cleared; `0x1350` bits 12 and 11 cleared, `0x1300` [7:0] = 0; protection `0x1364` = `0x1368` = `0x07F40000`, `0x136C`.. = the mode-0 values of D14; MAX_LEN_CFG = `0x1FFF`; then the host clears the keys again (D1) | | | every link-down (deauthentication received, beacon loss, user request) | diff: the GPL clears `0x1010`/`0x1014` and rewrites RX_FILTER_CFG, which the vendor never does at link-down |
| D32 | RX_FILTER_CFG MAC+`0x1400`: **never written in Phase D**; stays `0x17F97` while associated [M:hw 2026-11-02] | | | | **diff value**: `0x1BF93` during the connect, `0x1BF9F` after the handshake (bit 2 "drop unicast not to me" clear throughout; bit 3 set after the handshake) |

Timers of Phase D [M:s]: join beacon wait 2000 ms (D2); authentication 800 ms, two attempts (D7);
association 300 ms, one attempt per BSS (D9); disassociation 500 ms; the 100 ms periodic task
(§7.13.4); no timer waits for a TX status. Nothing in Phase D writes `0x14000`-`0x140FF`,
`0x11900`-`0x1197C`, the BBP, MAC_SYS_CTRL, MAC+`0x1200` or the timing registers
`0x1100`-`0x1107` and `0x1330`-`0x1338` [M:s].

The GPL driver's actual order at commit 76da132d, for reference: record read, boot clocks, replay
of the stock-BLE capture (phase A equivalents A1-A24 with its own VCO search), 40 MHz-mode full
trim, forced RF read-backs, bank-limit scan (24 MHz words, control loops off), LUT load with the
bank patch, auto control #1 and the forced words again; then system clocks, a MAC reset ending in
MAC_SYS_CTRL = 0, WDMA single writes, one PHY init (with resets), its own TX-filter bytes and
compression ramp, calibration values into windows D/D', interrupt setup, MAC configuration
(MAC_SYS_CTRL 0, 4, `0xC`); then LUT load and auto control #2 after all resets; scan per channel
with the channel code, TX_BAND_CFG and TX_PIN_CFG; TX from the queue word [I, from the GPL
source]. No write to `0x14000` on the boot path (12p).

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

**Calibration patches** [M:s] (**ERRATA 2026-11-04**: not optional; the FEQ bank patch is what
tunes the VCO band per channel (§14 row 6), and the AGC patch carries the RX DC results of
§10.1.10 or §10.2):

Entries are patched before loading:

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
feedback divider code (= f_MHz − 2321 on the 24 MHz table; the same convention as the analog
register `0x1193C` [12:5]: the bank scan starts at code 73 = 2394 MHz and init parks at 121 =
2442 MHz [M:s]). Entries 0-85 form a 1 MHz grid (codes 80..165, 2401-2486 MHz, the BLE/BT grid);
entries 100-117 are a 5 MHz series; entries 102-115 are the 802.11 channels 1-14 (entry = channel
+ 101, channel 14 at code 163 = 2484 MHz; the library's 40 MHz table has 156 there, and differs
from the published table in word 0 of entries 0-127 and word 1 of entries 115 and 180-255 [M:s]).
Entry 5 (2406 MHz) is the one selected during the clock bring-up.

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

Arithmetic [M:s]: banks are 8-bit values masked to 7 bits when written to `0x11938` [25:19], so
bank = (top − i) mod 128; in the degenerate case (top = 15) the sweep writes banks 15..0 and then
127..97; limit[i] is truncated to 8 bits. The comparator read is one status-port read per probe:
`0x11968` [31:28] = 1, eight writes of 1 to `0x11978`, one write of 0, one read (no poll, no delay).

Measured [M:hw]: the vendor radio holds bank 53 (`0x35`) or 52 at divider 121, varying by one
between boots (this oracle runs the full scan at every start). The GPL driver's scan
(12n: run after its calibration, in the 24 MHz PLL state, with `0x11950` = 0 and `0x11938` bits
26/27 clear) found top 47, lookup(121) = 43 and limits rising 71..169; with the 40 MHz PLL words or
with auto control armed the comparator reads flat (every reading "below", limits at the sweep
ceiling, top 15) [M:hw 12m]. With the 12n table in the LUT the receiver decodes on every channel
[M:hw 12o]. The 10-bank difference to the vendor is unexplained (the vendor scans before its
calibration in RF mode 5 with every path enable off; §16 U11) [I]. The 2026-10 emulator
description "binary search over 64 bands" (§11.1) does not describe this procedure [M:t vs M:s];
the emulator models only the comparator.

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

**GPL extras** (from the GPL source, 2026-11-04 morning) [I]: R94 = 17 / 26 and R106 = channel at
every channel switch; neither exists in the vendor. Removed at commit 76da132d (12l): the GPL now
writes no BBP register on a channel change [I, GPL source].

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
otherwise 66. The entry is written only when the value changes. The five bucket values were
re-verified in pass 2 [M:s]. An idle rule applies the "−76 or below" value 66 (the PHY-init final
value) when no frame from the target has arrived for interval x 110 ms (interval 2 by default,
settable as max(1, ms / 100)), or on every interval-th empty receive wake; the adjustment is on by
default and the first comparison value is 66, so nothing is written until a frame changes it
[M:s; **ERRATA 2026-11-04**: not "11 ticks"]. Nothing is written before a target is named, so no runtime AGC write happens
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
PHY init. The complete write lists follow (policy route 3 of §0). Every write is a plain window write
(entry index, value, commit 1); none is a read-modify-write [M:s]. Mode 0 ("TX filter" with
argument 0, before PHY init #1) is 18 writes in this order:

| # | Entry | Value | # | Entry | Value | # | Entry | Value |
|---|-------|-------|---|-------|-------|---|-------|-------|
| 1 | `0x4D` | `0x01` | 7 | `0x2C` | `0x02` | 13 | `0x3E` | `0x02` |
| 2 | `0x4E` | `0x01` | 8 | `0x31` | `0x03` | 14 | `0x43` | `0x6C` |
| 3 | `0x4F` | `0x00` | 9 | `0x36` | `0x6B` | 15 | `0x44` | `0x01` |
| 4 | `0x29` | `0x6C` | 10 | `0x37` | `0x01` | 16 | `0x45` | `0x02` |
| 5 | `0x2A` | `0x01` | 11 | `0x38` | `0x02` | 17 | `0x46` | `0x02` |
| 6 | `0x2B` | `0x02` | 12 | `0x39` | `0x02` | 18 | `0x4B` | `0x03` |

(three groups of five at bases `0x29`, `0x36`, `0x43`, each in the order base, +1, +2, +3, +8).
Mode 1 (argument 1, after PHY init #2) is 57 writes in this order:

| # | Entry | Value | # | Entry | Value | # | Entry | Value | # | Entry | Value |
|---|-------|-------|---|-------|-------|---|-------|-------|---|-------|-------|
| 1 | `0x4D` | `0x0F` | 16 | `0x32` | `0x00` | 31 | `0x41` | `0x01` | 46 | `0x11` | `0x20` |
| 2 | `0x4E` | `0x0F` | 17 | `0x33` | `0x38` | 32 | `0x42` | `0x01` | 47 | `0x13` | `0x20` |
| 3 | `0x4F` | `0x00` | 18 | `0x34` | `0x01` | 33 | `0x43` | `0x6B` | 48 | `0x15` | `0x2B` |
| 4 | `0x26` | `0x37` | 19 | `0x35` | `0x01` | 34 | `0x44` | `0x01` | 49 | `0x17` | `0x26` |
| 5 | `0x27` | `0x01` | 20 | `0x36` | `0xEC` | 35 | `0x45` | `0x02` | 50 | `0x19` | `0x20` |
| 6 | `0x28` | `0x02` | 21 | `0x37` | `0x01` | 36 | `0x46` | `0x02` | 51 | `0x1B` | `0x20` |
| 7 | `0x29` | `0x6C` | 22 | `0x38` | `0xB7` | 37 | `0x4B` | `0x02` | 52 | `0x1D` | `0x2B` |
| 8 | `0x2A` | `0x01` | 23 | `0x39` | `0x02` | 38 | `0x47` | `0x6A` | 53 | `0x1F` | `0x26` |
| 9 | `0x2B` | `0x02` | 24 | `0x3E` | `0x02` | 39 | `0x48` | `0x01` | 54 | `0x21` | `0x20` |
| 10 | `0x2C` | `0x02` | 25 | `0x3A` | `0xEB` | 40 | `0x49` | `0x01` | 55 | `0x23` | `0x23` |
| 11 | `0x31` | `0x03` | 26 | `0x3B` | `0x01` | 41 | `0x4A` | `0x02` | 56 | `0x24` | `0x32` |
| 12 | `0x2D` | `0x6A` | 27 | `0x3C` | `0xB6` | 42 | `0x4C` | `0x00` | 57 | `0x25` | `0x00` |
| 13 | `0x2E` | `0x01` | 28 | `0x3D` | `0x02` | 43 | `0x0B` | `0x20` | | | |
| 14 | `0x2F` | `0x01` | 29 | `0x3F` | `0x01` | 44 | `0x0D` | `0x20` | | | |
| 15 | `0x30` | `0x02` | 30 | `0x40` | `0x38` | 45 | `0x0F` | `0x20` | | | |

Mode 1 writes each of the 39 entries `0x26`..`0x4C` exactly once (in each 13-entry group
`0x26`-`0x32`, `0x33`-`0x3F`, `0x40`-`0x4C` the +11 entry comes before +7..+10), no even entry of
`0x0C`-`0x22`, and nothing in `0x50`-`0x5F`. **Hardware cross-check [M:hw 2026-11-03]:** the
vendor unit's window D read-back equals the mode-1 value for every entry `0x26`..`0x4C`, D[`0x23`]
= `0x23`, and D[`0x4D`..`0x4F`] are not readable in that capture; the GPL's window D (which holds
the PHY-init values plus its own four coefficients) differs from it in exactly the entries where
mode 1 differs from the PHY-init table. The per-rate gain entries are rewritten to the same values
by the per-rate gain step (A24) in the order `0x0B`, `0x0D`, `0x0F`, `0x11`, `0x1B`, `0x1D`,
`0x1F`, `0x21`, `0x13`, `0x15`, `0x17`, `0x19`.

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
400; powers 11g/n = `0x32D64`, 11b = `0x32D64`, BPSK = `0x23FD6`; mode = 2. The curve is the 128 9-bit values below (policy route 3 of §0: a RAM content the hardware
consumes; no generating rule reproduces it, and the vendor never reads the RAM back) [M:s]; it
falls from 511 to 271, never rises between entries, and every value is ≥ 256 (bit 8 = 1 for all
128 entries). Entry i is written as D[`0x5E`] = value [7:0] and D[`0x5C`] bit 5 = value bit 8:

| i | values i .. i+15 |
|---|------------------|
| 0 | 511 511 510 509 508 507 506 505 504 503 502 501 500 498 497 496 |
| 16 | 495 493 492 491 489 488 487 485 484 482 481 479 477 476 474 473 |
| 32 | 471 469 467 466 464 462 460 458 457 455 453 451 449 447 445 443 |
| 48 | 441 439 437 435 433 431 428 426 424 422 420 418 416 413 411 409 |
| 64 | 407 404 402 400 398 396 393 391 389 386 384 382 380 377 375 373 |
| 80 | 371 368 366 364 362 359 357 355 353 350 348 346 344 341 339 337 |
| 96 | 335 333 330 328 326 324 322 320 318 315 313 311 309 307 305 303 |
| 112 | 301 299 297 295 293 291 289 287 285 283 281 279 277 275 273 271 |

Write detail per entry [M:s]: D[`0x5C`] read, written with bit 4 set; D[`0x5D`] read, written with
[6:0] = i (bit 7 kept); D[`0x5C`] read, then D[`0x5E`] = value [7:0], then D[`0x5C`] written with
bit 5 = value bit 8 (bit 4 still set); D[`0x5C`] read, written with bit 4 cleared (the address is
set before the data; the low byte before bit 8; bit 4 is cleared last, which is presumably the
write strobe [I]). The thresholds and powers are then written as: read D[`0x58`], D[`0x59`] =
`0xA0`, D[`0x58`] [2:0] = 0; read D[`0x51`], D[`0x52`] = `0x64`, D[`0x53`] = `0x2D`, D[`0x51`]
[3:2] = 3; read D[`0x58`], D[`0x5A`] = `0xA0`, D[`0x58`] [5:3] = 0; read D[`0x51`], D[`0x54`] =
`0x64`, D[`0x55`] = `0x2D`, D[`0x51`] [5:4] = 3; read D[`0x58`] and D[`0x5C`], D[`0x5B`] = `0x90`,
D[`0x58`] [7:6] = 1, D[`0x5C`] bit 0 = 0; read D[`0x51`], D[`0x56`] = `0xD6`, D[`0x57`] = `0x3F`,
D[`0x51`] [7:6] = 2; read D[`0x51`], D[`0x51`] [1:0] = 2. The read-modify-writes touch only
D[`0x51`], D[`0x58`], D[`0x5C`] and D[`0x5D`]; the address clamps at 127 and the data at 511.

Resulting D[`0x51`..`0x5E`] = `BE 64 2D 64 2D D6 3F 40 A0 A0 90 E0 7F 0F` (D[`0x5C`] = `0xE0`: the
PHY-init `0xC0` with bit 5 = 1 from the last entry; D[`0x5D`] = `0x7F`; D[`0x5E`] = `0x0F` = 271
[7:0]). **Confirmed on the vendor unit's read-back for every one of these fourteen entries
[M:hw 2026-11-03]**, which also shows that a window read returns the last written value for the
read-modify-written entries D[`0x51`], D[`0x58`], D[`0x5C`] (an open question of the 2026-11-04
plan) and that D[`0x5E`] reads back the last data byte written; whether the curve RAM itself can
be read back is unknown (§16).

**Per-rate digital gain from VM 108** (§4.2.1 A24): defaults as in mode 1; each non-zero VM 108
byte replaces one odd entry (bytes 0, 1, 2, 3, 5, 7, 9, 11, 12, 14, 16, 19 map to D[`0x0B`],
D[`0x0D`], D[`0x0F`], D[`0x11`], D[`0x1B`], D[`0x1D`], D[`0x1F`], D[`0x21`], D[`0x13`], D[`0x15`],
D[`0x17`], D[`0x19`]); all-zero VM 108 leaves the mode-1 values [M:s]. The published default for record 108 is
`50 50 50 50 72 72 85 80 64 64 62 52 72 90 80 64 64 64 50 43` [PUB
`apps/demo/demo_wifi/board/wl82/board.c` L53-74], and the vendor unit's window D carries exactly
those values in the twelve entries (D[`0x0B`] = `0x32` ... D[`0x19`] = `0x2B`) [M:hw 2026-11-03];
bytes 4, 6, 8, 10, 13, 15, 17 and 18 are unused.

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

Linux initial value: `0x17F97`. Our value: same for scanning, `|= 0x08` after association. **ERRATA 2026-11-05:** the vendor never sets bit 3 in station mode and reads `0x17F97` while associated [M:hw 2026-11-02]; the GPL's `|= 0x08` is its own choice (§4.2.1 D32).

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

**Association-time writes [M:s wl_wifi_sta.a; names IP:RT2x00].** At link-up the RT layer writes
`0x3040C` = `0x1F3F7F9F` (TX burst off, the published profile's `TxBurst=0`; `0x1F3FBF9F` with TX
burst on) and clears the AC0 TXOP byte of `0x31300`: this is the `0x1F3F7F9F` the running radio
reads (**resolves the 2026-11-04 open item**: the hardware retains bit 15; software rewrites it),
so the TX gate's queue-2 page limit becomes `0x7F` after association. Also at link-up: `0x31018`
= `0xA0FFF`..`0xA3FFF` by peer type, `0x31418` = 1 or 0, BSSID `0x31010`/`0x31014`, `0x31114`
(beacon sync), `0x31350`, `0x31300` (RDG/piggyback), `0x31404` (preamble), rate fallback
`0x31354`, `0x31358`, `0x3135C`, `0x31360` = `0x2100`, the protection set `0x31344` (bits [23:8]
from the RTS threshold, 2347 -> `0x1000`) and `0x31364`-`0x31378` (only when an HT-protection
option or the operation mode calls for it), and the WMM EDCA `0x31300`-`0x3130C` from the beacon's
parameters. Before association the only MAC writes outside §4.2.1 are the periodic task's PBF
pulse (§7.11.9).

**EDCA at interface up (ERRATA 2026-11-04) [M:s wl_wifi_sta.a, PUB profile].** The HT setup runs
at interface up with the published profile's `WirelessMode=9`, finds no access-point EDCA set and
writes the WMM defaults AIFSN 3/7/1/1, CWmin 4/4/3/2, CWmax 6/10/4/3, TXOP 0/0/94/47, packed as
TXOP [7:0], AIFSN [11:8], CWmin [15:12], CWmax [19:16], with AC2's AIFSN forced to 5 and its TXOP
scaled by 6/10: `0x31300` = `0x64300`, `0x31304` = `0xA4700`, `0x31308` = `0x43538`, `0x3130C` =
`0x3212F`. The constant set (AC0 `0xA4200`, or `0xA4220` with TX burst; AC1 `0xA4200`; AC2
`0xA4260`, or `0xA42C0` for B-only; AC3 `0xA4230`, or `0xA4260` for B-only) is written only at
link-down or at association with an invalid set. The vendor unit's steady-state EDCA (`0xA4300`,
`0xA4700`, `0x43538`, `0x3222F`) is the association-time rewrite from its access point's
parameters [M:hw, M:s]. `0x3132C` bit 0 is written by the HT setup only with the 40 MHz option
(profile `HT_BW=0`: never written at interface up); its reset value is unknown (§16 U1).
RX_FILTER_CFG is written twice on a station start (the table, then the RX/TX enable step), both
`0x17F97`; the AP arm writes `0x15F97` [M:s].

~~TX_LINK_CFG, EDCA_TID_AC_MAP, and TX_TIMEOUT_CFG are critical for TX scheduling:
without them the MAC hardware cannot dispatch DMA'd frames to the air interface.~~ (**ERRATA
2026-11-04:** two of these three names were wrong, see the table; the claim was an inference.)
EDCA_AC0-AC3 (0x31300-0x3130C) are NOT in the static table; they are written at interface up by
the HT setup (the WMM default set above) and rewritten at association from the access point's
parameters [M:s].

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
| 21    | R   | **RX DMA complete.** Polled without bound after each RX phase; cleared by the bit-11 acknowledge [I; supported by the vendor's order: RX phase 2 polls bit 21 right after the phase-1 acknowledge and a new start, which only works if the acknowledge clears it; the same reasoning applies to bits 10/20. Test T15 of §15 records it] | M:s, I |
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

**Staging buffer and what is stale [M:s wl_rf_common.a].** The vendor stages every frame in one
2560-byte, 32-byte-aligned buffer declared zero-initialised: [0] the software length word (MPDU +
22, written per frame, not transferred); [4] the queue word, of which only bits [2:0] are written
per frame (a read-modify-write: bit 0 = TXINFO bit 24, bits [2:1] = TXINFO bits [26:25]), so bits
[31:3] keep whatever was there (0 after a zeroed buffer); [8] the 16-byte TXWI, copied per frame
(an A-MPDU-disable step that clears TXWI word 0 bit 4 is overwritten by that copy, so bit 4 goes
out as the upper layer set it); [24] the frame; [24 + MPDU] two bytes copied from the source
buffer after the frame; [26 + MPDU .. 30 + MPDU] four bytes not written for this frame (leftovers
of earlier, longer frames, or 0). `WDMA_TX_ADR` = buffer + 4 is therefore only 4-byte aligned. A
frame with MPDU + 26 > 2560 halts the system in a diagnostic loop. The stale bytes and the
untouched queue-word bits are observations, not something to reproduce; an implementer may zero
them [I]. A submission may carry several frames back to back, each gated and transferred
separately (advance by TXINFO [15:0] + 4 while more than 16 bytes remain) [M:s].

**TX readiness gate (before every transfer)** [M:s wl_rf_common.a]. With q = the frame's QSEL (2):

1. Read TXRXQ_PCNT (`0x30438`), byte q (pages in TX queue q). It must be below the queue's page
   limit (byte (3 − q) = byte 1 of the value last written to PBF_MAX_PCNT `0x3040C`: `0xBF` from the
   table, `0x7F` after association, §7.10), and
   (limit − pages) x 128 must exceed the MPDU length.
2. Read `0x30434`; bit (8q + 2) = bit 18 must be 0 (queue busy/full flag, inferred).
3. Read PBF_DBG `0x3043C`; bits [7:5] must be non-zero. If this is the only failing test the vendor
   prints a marker character, i.e. it is an expected transient.
4. If any test fails: retry from 1 after a 10 ms sleep, a scheduler yield, or at once (a published
   build option; the SDK default is the yield except with uCOS), **without bound**; every 10 s the
   vendor reads `0x30438`, `0x3040C`, `0x30434`, `0x3043C`, `0x31500`, `0x31504` for a diagnostic;
   only a driver shutdown ends the retry, and then the frame is dropped untransferred [M:s].

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
8. Optional fixed delay (a configuration value, 0 by default; no library path sets it, so by
   default nothing waits after TX) [M:s].

Completion is reported to the network layer synchronously; TX status (TX_STA_FIFO `0x31718`) is
read only by the periodic statistics task and nothing waits for it [M:s wl_wifi_sta.a].

**Concurrency [M:s wl_rf_common.a].** The vendor does not serialise TX transfers against RX
transfers: both may be in flight at once, both read-modify-write `WDMA_CTL` (bit 10 on the TX
side, bit 11 on the RX side, each writing back whatever other bits read as 1), and the gate reads
of the TX side can interleave with the RX side's MAC+`0x324` write and an RX transfer. An
implementer that runs TX and RX from different contexts must serialise `WDMA_CTL`
read-modify-writes or establish that the bit-10 and bit-11 writes are independent [I].

**TXWI as the vendor builds it** [M:s wl_wifi_sta.a; layout IP:RT2x00]:

| Field | Management (probe request) | Data |
|-------|----------------------------|------|
| W0 TXOP [9:8] | **3** (backoff) | 0 |
| W0 MPDU density [7:5] | 0 (a station entry is consulted only for WCID < 2) | from the station entry; 7 when the peer is flagged and encryption is on |
| W0 MCS / PHYMODE / BW / SGI / STBC | the management transmit setting: at the first scan after bring-up PHYMODE 1 (OFDM), MCS 0 (6 Mbps), BW 0, SGI 0, STBC 0 [M:s]; CCK 1 Mbps (PHYMODE 0, MCS 0) only after a rate update that the initial scan does not run, unless the profile reader (not analysed) triggers one [I]. The MAC accepts a CCK 1 Mbps probe [M:hw 12l]. BW is set only for HT modes with the HT 40 MHz option | rate control |
| W1 ACK [0] | per frame (0 for broadcast) | per frame |
| W1 NSEQ [1] | 0 (software sequence numbers) | 0 |
| W1 BA window [7:2] | min(global BA size, 7) = 7 with the default; the per-frame argument is ignored | same |
| W1 WCID [15:8] | `0xFF` | station entry (1 for the access point) |
| W1 MPDU [27:16] | frame length, unpadded | frame length |
| W1 PacketId [31:28] | MCS[3:0] (0 for CCK 1 Mbps and OFDM 6 Mbps); the "PID" argument of the management path is not stored | MCS[3:0] |
| W2, W3 | 0 | IV / EIV |

Measured vendor QoS-null and data TXWIs carry TXOP 0 [M:hw 2026-11-02], so the hardware accepts
TXOP 0 at least on that path.

**TXWI for unicast management frames and for station EAPOL frames [M:s wl_wifi_sta.a; layout
IP:RT2x00; added 2026-11-05].** Authentication request, association request, deauthentication and
disassociation all take the management path with one constant TXWI; only ACK, the length and the
rate state differ from the probe request. The station-side EAPOL-Key frames (messages 2/4, 4/4 and
the group 2/2) take the data path.

| Field | Auth request / assoc request / deauth / disassoc | EAPOL-Key from the station |
|-------|---------------------------------------------------|----------------------------|
| W0 FRAG, MIMO_PS, CF_ACK, TS, AMPDU [4:0] | 0 | 0 (CF_ACK 1 only with a Ralink piggyback-capable peer) |
| W0 MPDU density [7:5] | 0 | the access point's density from its HT capabilities (7 with the "bad peer + WEP" case) |
| W0 TXOP [9:8] | **3** (backoff) | 0 |
| W0 MCS [22:16], BW, SGI, STBC | 0, 0, 0, 0 | 0, 0, 0, 0 |
| W0 PHYMODE [31:30] | 0 (CCK 1 Mbps) after the join step (§4.2.1 D3); the first scan's probe requests go at OFDM 6 Mbps (PHYMODE 1) | 0 (CCK 1 Mbps; the low-rate class for EAPOL) |
| W1 ACK [0] | 1 | 1 |
| W1 NSEQ [1] | 0 (software sequence number: a 12-bit counter shared by all non-QoS frames, incremented per frame, wrapping at 4095) | 0 |
| W1 BA window [7:2] | 7 | per block-ack state (7 default) |
| W1 WCID [15:8] | `0xFF` (the station table is not consulted for these frames, whether or not slot 1 holds the access point) | 1 (the access point's entry; exists only after the association response) |
| W1 MPDU [27:16] | frame length: 30 (open auth), 26 (deauth, disassoc), 28 + elements (assoc) | frame length |
| W1 PacketId [31:28] | MCS & 0xF = 0 | 0 |
| W2, W3 | 0 | 0 |
| Queue word | 5 (WIV 1, QSEL 2) | 4 (WIV 0, QSEL 2) |
| Header duration | **304 µs** (a 14-byte ACK at 1 Mbps with long preamble: 192 + 112; 50 µs would be used at OFDM 6 Mbps) | data path |
| Header addr2 / addr3 | own address / BSSID (auth, requested deauth); own address / addr1 (assoc, disassoc, the deauth that precedes the auth) | data path |
| Retry, more data, power management, protected bits | 0 (power management set only while a scan runs; protected only for shared-key sequence 3, encrypted in software) | data path |

Related [M:s]: a deauthentication (reason 3) to the access point precedes every authentication
request; a disassociation is followed by a deauthentication; no software wait for TX completion
and no TX status read on this path, only the state-machine timers of §4.2.1 Phase D. The running
vendor radio's TX status word read on 2026-11-02 (`0x800701EF`: valid, packet id 7, success,
aggregated, ACK required, WCID 1, MCS 7, HT mixed) shows the status FIFO reporting a data frame to
slot 1 [M:hw]. The GPL driver at build dev-20261105T064012Z sends every frame with TXOP 0, density
5, duration 0, WCID `0xFF` (EAPOL included), ACK for unicast addr1, CCK 1 Mbps [I from its source].

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
   that range the vendor resets the system or prints a diagnostic and continues, by a build
   option; the default build continues).
5. Buffer layout after phase 2: [0] length n | [4] RXWI (16 bytes, §7.5b) | [20] frame | [4 + n]
   a 4-byte trailer (byte-swapped like the length) whose bits follow the RT2800 USB receive
   descriptor word: CRC error bit 8, cipher error bits [10:9], L2 padding bit 14, decrypted bit 16
   [IP:RT2x00]. The RT2800 layer reads n as the 16-bit little-endian value of bytes 0-1 (after the
   transport's swap) and rejects the buffer without consuming it when fewer than 24 bytes remain,
   n = 0, n is not a multiple of 4, n + 8 exceeds the bytes remaining, or the RXWI MPDU count
   [27:16] exceeds n (n covers RXWI + frame); a frame is then dropped when trailer bit 8 (CRC
   error) is set, when the frame-control ToDS bit is set, when the MPDU count exceeds 3840, or
   when the trailer's cipher-error field [10:9] is non-zero on a decrypted frame (bit 16 set),
   except the AES group-key fragment case; trailer bit 7 (my BSS) is consulted for MIC reports and
   bit 14 (L2 pad) later as a pad flag [M:s wl_wifi_sta.a].
6. Runtime AGC update from the frame's RSSI0 (§6.7).
7. A software filter may drop the frame (the default filter never drops): then `WF_CON4` bit 0 is
   set again at once and the buffer is kept. Otherwise the frame is handed up; `WF_CON4` bit 0 is
   set again when the network layer re-posts the (single, 4096-byte) receive buffer.

**Buffer, alignment and barriers [M:s wl_rf_common.a].** The receive buffer must be 4-byte
aligned (a misaligned buffer is rejected with a reset or a diagnostic); the delivered length is
n + 8 (length word, frame, trailer); the trailer is stored as one little-endian 32-bit word after
byte-swapping. The vendor issues a core-sync barrier after the acknowledge write, after the length
write and after the done poll of phase 1, and after the address and length writes of phase 2; the TX
path issues them after its acknowledge and after its length write, none after done. No cache
maintenance is performed anywhere on the vendor's path; whether its buffers are uncached or the
SoC is coherent is not determinable (§16 U6). **Polling mode** (a build option, off by default):
the vendor then writes neither MAC+`0x328` nor `0x14050` bit 0 and polls bit 17 continuously
(§7.11.9).

**Multiple frames (ERRATA 2026-11-04):** the vendor handles exactly one frame per wake and does
not loop on bit 17; further frames wait for the next interrupt or the next timeout poll [M:s]. A
bounded drain (the GPL uses 8 per poll) is the implementer's choice [I]; an unbounded drain, with
a handler that never masked its source, stalled the CPU until the watchdog reset on the first
reception [M:hw 12n].

**Timeouts:** poll loops should use timeouts. Recommended: 1 ms for RX phase 1/2 (single-frame
DMA completes in microseconds), 10 ms for TX [I].

**Buffer sizing:** the vendor posts one 4096-byte buffer (n ≤ 4088) [M:s]; 4 + 16 + 1600 + 4
bytes suffice for the frames of this profile [I].

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
| RX byte-swap | [M:s] | **Confirmed** [M:hw 12o] | frames decode with the swap |
| Pre-RX MAC write (`0x30324`) | [M:s] | **Applied** [M:hw 12o] | written 2 before each read; not separately ablated |


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
- Asynchronous items before the first TX [M:s wl_wifi_sta.a]: a periodic task armed with a 100 ms
  period during the MLME init step, before the MAC configuration table is written (whether its
  first tick lands before or after the table write is not determinable). Every tick reads
  TX_STA_FIFO (`0x31718`) until empty; every second tick (about 200 ms) reads `0x31700`-`0x31714`
  and `0x30438`, and if the TX0 (bits [7:0]) or TX1 (bits [15:8]) page count is non-zero at that
  instant writes PBF_CFG `0x30408` = `0x00F40012` (TX0) or `0x00F4000A` (TX1), polls `0x30438` up
  to 10 times at 1 µs, then writes `0x00F40006`; it also reads MAC+`0x10F4` (a hang test whose
  MAC_SYS_CTRL writes of 1 and `0xC` are dropped by the vendor's filter and never reach hardware).
  Rate switching, EDCA retuning and protection rewrites need association state. After a full trim
  that ran because the stored record was invalid, a one-shot timer writes record 187 one second
  later (§10.2). Neither is needed for the first TX [I].
- Polling mode (a build option, off by default; **ERRATA 2026-11-04**): a busy receive task polls
  `WDMA_CTL` bit 17 continuously; MAC+`0x328` is not written at all, interrupt 52 is not registered,
  and `0x14050` bit 0 is neither set nor cleared (it stays at the 0 of the clock init). The
  register writes are therefore **not** the same as in interrupt mode [M:s].
- No sleep or clock-gating register is written between init and the first TX on a WiFi-only
  start: the driver's sleep veto holds until 802.11 power save is requested [M:s]. The sleep path,
  not on the start path, would write `0x1400C` bits 0, 1, 2, 16 (latch request) and 17 (busy poll),
  save and zero `0x1405C` (restored on wake), and MAC+`0x1180` = `0x300C`/`0x300F`, MAC+`0x1184` =
  `0x1000000`/`0x2000000`/`0x10000`/`0x20000`/`0x80000`, MAC+`0x1188`, MAC_SYS_CTRL and MAC+`0x1608`
  [M:s].
- Delays [M:s cpu library]: the vendor's microsecond delay is a NOP countdown of (system clock /
  D) x µs iterations, D = 4,000,000 when the flash controller's control register (`0x40200`) bit 0
  is clear and 8,000,000 when set (about 4 or 8 cycles per iteration); below 4 or 8 MHz the count
  truncates to 0. The nominal 1/1/1/50 µs (clock tree), 5/5/100 µs (PLL restart), 80 µs (loop
  pulse) and 1000 µs (ADC peak) are therefore approximate on the vendor as well; treat them as
  minimums [I] (§16 U5).
- On core-fm1 interrupt 52 has not fired during a scan although `0x14050` bit 0 is set and the
  line is registered; polling `WDMA_CTL` bit 17 carries reception [M:hw 12o]. The vendor registers
  the line after the probe returns, with priority argument 2 and CPU argument 1, between
  MAC+`0x328` = 0 and = 2; the GPL attaches it before writing MAC+`0x328` and selects no CPU [I for
  the cause; §16 U4].

---

### 7.12 Why no frame is received: RX path status [M:hw 2026-11-04]

**Resolved 2026-11-04 (12o):** the receive path delivers frames (§7.11.6); the table below is the
state after 12i, kept as the record of the diagnosis. The chain that closed it: `WL_CON0` never
written (§4.2), the TX image from the queue word and the front-end registers (§7.11.5, §4.2.1
C2-C3), the channel code with the bank-patched LUT measured in the 24 MHz PLL state and no reset
after auto control (§5.2, §14 rows 5-7), a masking interrupt handler and a bounded drain
(§7.11.9) [M:hw 12l-12o].

Stage-by-stage state of the receive path on core-fm1 against the running vendor radio on the same
FM-1 unit (fm1-base `/status` oracle), after iteration 12i. "Equal" means read back identical on
both sides. Observation files: the implementer's observation store, 2026-11-02 to 2026-11-04.

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

### 7.13 Station table, key tables, address handling and the acknowledgement path [M:s wl_wifi_sta.a, hostapd_and_wpasupplicant.a, wl_rf_common.a; IP:RT2x00; PUB; M:hw; added 2026-11-05]

#### 7.13.1 Addresses: what determines addr2 on the air, and where the vendor's address comes from

- The transmitter address of a frame is the frame bytes. Nothing in the station library, the
  transport library or the MAC substitutes it: the vendor copies its own address into addr2 of
  every management header in software, the TXWI carries no address, and the only address
  registers the vendor writes are MAC_ADDR_DW0/1 (B8: own address, DW1 [23:16] = `0xFF` unicast
  mask), MAC_BSSID_DW0/1 (D4) and the station slots (§7.13.2) [M:s]. MAC_ADDR_DW0/1 is the receive
  match for "unicast to me" and the source address of the MAC's own responses (ACK, CTS, block ack)
  [IP:RT2x00]. No vendor code reads an address from any register: MAC+`0x300`/`0x304` read
  `0x7001`/0 on both units and are not an address [M:hw, M:s].
- The vendor libraries have no built-in address. The station library's default address hook
  returns failure; the published profile string `MacAddress=00:00:00:00:00:00` makes the library
  call the application's generator for its address [M:s; PUB wifi_conf.c L396, L530]. The published
  generator (`apps/common/net/assign_macaddr.c` L314-L339; the L273-L310 variant adds a server
  path) takes a stored address from flash if one exists, else: 16-byte NOR flash unique id →
  CRC32 and CRC16 of it, each XOR the hardware random generator → 6 bytes; byte 0 bits 0 and 1
  cleared (unicast, globally-administered form); the result is stored in flash so it stays the same
  across boots [PUB]. The oracle build on this unit replaces that generator with a fold of the
  flash unique id into 6 bytes with the locally-administered bit set, which gives
  `72:7F:0B:02:72:6E` (`0x31008` = `0x020B7F72`, `0x3100C` = `0x00006E72`, every capture) [M:hw].
  Which of the two the stock FM-1 firmware uses is not determinable from the analysed libraries
  (§16 U20). An implementer may derive a stable address from the flash unique id by any rule; the
  published rule above is citable.
- The 2026-11-05 observation: `D8:C8:0C` is registered to Tuya Smart Inc. [PUB IEEE OUI registry,
  2026-11-05]. The frames the GPL driver accepted as "its" authentication responses were addressed
  to a Tuya station on the same channel; the GPL driver received them because RX_FILTER_CFG bit 2
  (drop unicast not to me) is clear in `0x1BF93`, and its parser matched the sender only. The 0.15 s
  to 2.2 s intervals between the GPL's request and those frames are not an access point's response
  time (the vendor waits 800 ms at most, D7). After the override, the GPL's own registers matched
  that address, so the same frames passed the addr1 check [I]. Consequence: the override is not a
  fix for anything; the GPL's own address is the transmitter address of its frames, and the
  question is why no station ever answers it (§7.13.4).

#### 7.13.2 Station slot table (MAC+`0x1800` + 8 x slot, 256 x 8 bytes) [M:s, IP:RT2x00]

| Item | Fact |
|------|------|
| Entry layout | word 0 = address bytes 0-3 (byte 0 in [7:0]); word 1 [15:0] = bytes 4-5, [31:16] = 0 when written by the vendor (the init pattern leaves `0x00FF` there) |
| Init | slots 0..253 = `FF FF FF FF FF FF FF 00` (B3); slots 254 and 255 never written |
| Slot 1 | the access point: written at join, before the authentication request (D5); rewritten with zeros on a failed authentication or association and at link-down (D8, D31); software entry (WCID 1, AID) created only after the association response (D10) |
| Slot 0 / multicast | not written in station mode |
| WCID `0xFF` | a TXWI literal for every frame sent without a station entry: probe requests and all unicast management frames (§7.11.5); the vendor's unicast management frames with WCID `0xFF` are acknowledged on the air (the vendor associates). Slot 255 (`0x1FF8`/`0x1FFC`) is never written; attribute 255 (`0x6BFC`) receives the init value 1 with all others |
| Byte-array writes | every table written from a byte array goes in 32-bit words, little-endian (byte 0 in [7:0]), at offset, offset + 4, ...; the key tables are written as 16 + 8 + 8 bytes |

#### 7.13.3 Key tables [M:s, IP:RT2x00]

| Table | Base (MAC+) | Entry | Vendor use |
|-------|-------------|-------|------------|
| Pairwise key table | `0x4000` | 32 bytes x 256: key 16, TX MIC 8, RX MIC 8 | not used by the station library for WPA (the PTK goes to the shared table) |
| IV/EIV table | `0x6000` | 8 bytes x 256: IV word, EIV word | slot 1: IV = `0x20000001` (bit 29 extended IV, [31:30] key id 0, [15:0] packet number 1), EIV = 0 at every pairwise install; nothing for the group key; nothing at init |
| Attribute table | `0x6800` | 4 bytes x 256: bit 0 key-table select (0 shared-key table, 1 pairwise table); [3:1] cipher code bits 2:0, bit 10 cipher bit 3; [6:4] BSS index bits 2:0, bit 11 BSS bit 3; bits [9:7] (the cipher copy for the receive descriptor in the Linux driver) are never written by the vendor | init 1 for all 256; slot 1 = `0x8` (AES) or `0x6` (TKIP) at the pairwise install; 1 on removal, at link-down and at the host's pre-join clearing |
| Shared key table | `0x6C00` | 32 bytes x 16: index 4 x BSS + key id; key 16, TX MIC 8, RX MIC 8 | (0, 0) = the pairwise key after message 4/4; (0, id) = the group key, id from the GTK element (1 or 2) |
| SHARED_KEY_MODE | `0x7000` | 4 words; word = BSS / 2; nibble [4 k + 3 : 4 k] for BSS even, [4 k + 19 : 4 k + 16] for BSS odd, k = key id; value = cipher code | 0 x 4 at init (B5); (0, 0) and (0, id) set at the installs; BSS 0 nibbles cleared at link-down |
| Cipher codes | 0 none, 1 WEP-64, 2 WEP-128, 3 TKIP, 4 AES-CCMP [IP:RT2x00 (the Linux driver's hardware cipher enumeration); M:s for 3 and 4 on the install path] | | |

Order of a pairwise install (D26): key bytes (16, 8, 8), SHARED_KEY_MODE read-modify-write, IV/EIV
(two words), attribute word; the frame carrying message 4/4 has been handed to the MAC before the
first of these writes, without a wait for its TX status (§16 U18). Received sequence counters from
the handshake are not loaded (replay detection is the host's).

#### 7.13.4 The acknowledgement path: what the MAC needs to count a unicast frame as acknowledged [M:s, IP:RT2x00, M:hw]

| Register / item | Fact |
|-----------------|------|
| TX_RTY_CFG MAC+`0x134C` = `0x47D01F0F` | [7:0] short retry limit 15; [15:8] long retry limit 31; [27:16] long-retry threshold 2000 bytes; bits 28/29 retry modes 0 (expire by retry count); bit 30 auto fallback 1 [IP:RT2x00 fields, M:s value]. A 30-byte frame uses the short limit: one transmission + 15 retries, then "given up". The GPL's counters after its authentication request (TX_STA_CNT0 [15:0] = 1, TX_STA_CNT1 = `0x000F0000`) are exactly "limit exhausted, no ACK recognised" [M:hw 2026-11-05]. The vendor changes the limits only while associated with a 100 % error rate (both to 0 for up to 330 ms, then restored) |
| TX_STA_FIFO MAC+`0x1718` | bit 0 valid; [4:1] packet id; bit 5 success; bit 6 aggregated; bit 7 ACK required; [15:8] WCID; [22:16] MCS; bit 23 BW; bit 24 SGI; [31:30] PHY mode [IP:RT2x00]. The vendor pops it every 100 ms until bit 0 reads 0 (at most 16 counted entries), for statistics only, and discards entries with bit 7 = 0 or WCID > 1, i.e. every management frame; nothing waits on it [M:s]. Packet id = the MCS low nibble, so 1 Mbps frames carry packet id 0; whether the hardware reports packet id 0 is not determinable (the Linux driver uses 1..3 only) (§16 U15). The 2026-11-02 vendor read `0x800701EF` is a slot-1 HT data frame entry [M:hw] |
| TX_STA_CNT0/1/2 MAC+`0x170C`-`0x1714` | [15:0] TX fail / [31:16] beacons; [15:0] success / [31:16] retransmissions; [15:0] zero-length / [31:16] underflow; read-to-clear; read by the periodic task every 200 ms [IP:RT2x00, M:s] |
| AUTO_RSP_CFG MAC+`0x1404` | `0x13` from init to link-up (bit 0 auto responder, bit 1 block-ack policy, bit 4 short-preamble responses); `0x03` only if a later beacon signals Barker preamble; bits 2/3 never set. It governs the responses the station sends, not the recognition of the access point's ACK [IP:RT2x00, M:s] |
| RX_FILTER_CFG MAC+`0x1400` | the vendor runs with bit 10 (drop ACK) and bit 2 (drop unicast not to me) set and still counts acknowledgements; the Linux driver does the same. The filter decides what reaches the host; the transmit state machine's ACK match is separate [IP:RT2x00, I] |
| EXP_ACK_TIME MAC+`0x1380` = `0x002400CA` | [15:0] = 202 (CCK), [31:16] = 36 (OFDM): SIFS + PLCP preamble/header time until the response's payload, not the whole ACK (the Linux driver writes the same constant for every rate) [I]; never recomputed [M:s] |
| TX_TIMEOUT_CFG MAC+`0x1348` = `0x000F1290` | [7:4] MPDU lifetime 9; [15:8] RX ACK timeout 18 (unit per the Linux field name "slot", not verified, §16 U19); [23:16] TXOP timeout 15 [IP:RT2x00]; never recomputed [M:s] |
| XIFS_TIME_CFG `0x33A40C0B`, BKOFF_SLOT_CFG `0x00140314`, TX_SW_CFG0-2 | CCK SIFS 11 µs, OFDM SIFS 12 µs, OFDM XIFS 4, EIFS 314 µs, BB RX-end enable; slot 20 µs, CC delay 3; written once by the PHY init (A11) and never again; the GPL's values are identical [M:s, M:hw] |
| Per-transmit writes | none: the transmit path reads the PBF gate registers and drives the WDMA only; TX_PIN_CFG `0x00050302` (PA/LNA/TR-switch enables) and TX_BAND_CFG are written per channel switch (C2-C3) and nowhere else; no BBP, RF-word or `0x1405C` access per transmit or in the periodic task [M:s] |
| MAC_STATUS_CFG MAC+`0x1200` | never read or written by any vendor code [M:s]; the Linux driver reads bits [1:0] (BBP/RF busy TX/RX) before BBP access [IP:RT2x00]. The GPL's write of `0x00030601` reads back `0x00030F00` [M:hw 2026-11-05]: a live status register; the write is an error with no vendor counterpart (§16 U16) |
| Basic-rate interplay | the ACK to a 1 Mbps CCK frame comes at 1 Mbps (bit 0 of `0x13F` and `0x15F`); to an OFDM 6 Mbps frame at 6 Mbps (bit 4); the vendor's expected-ACK-rate table is software, derived from the same bitmap [M:s] |
| Periodic task (100 ms base) | 100 ms: TX_STA_FIFO drain; 200 ms: RX_STA_CNT0-2 and TX_STA_CNT0-2 read (clear), TXRXQ_PCNT read and, if TX queue 0 or 1 holds pages, PBF_CFG `0x408` = `0x00F40012` / `0x00F4000A` then `0x00F40006` (queue 2, the management queue, is never flushed), MAC+`0x10F4` hang test (its recovery writes are dropped by the filter); 500 ms (associated): TX counters for the retry-limit rule above; associated and HT: EDCA AC0/AC2 traffic-dependent tweaks and protection updates [M:s] |

**What the 2026-11-05 observations show [M:hw, I].** Over every scan since iteration 12l (13
channels, 16 networks listed) and over every connect attempt, no frame addressed to the GPL
driver's own address has ever been received: no probe response to its probe requests, no
authentication response (the ones it logged were the Tuya station's), no ACK counted. Every
register that takes part in acknowledging a frame holds the vendor's value on the GPL side
(timing, retry, response, address registers; the slot and key tables differ but are not consulted
for unencrypted management frames). The remaining differences are on the transmit side: the TXWI
(TXOP 0 instead of 3, density 5 instead of 0), the header duration (0 instead of 304 µs), no
deauthentication before the authentication, no slot 1, and the TX back end (§4.2.1 A18/A19 and
§6.9: whether build dev-20261105T064012Z carried the window D mode-1 set and the compression
curve is not in the observation record). The cheapest discriminator is §15 T19 (a probe response
to a directed probe request proves the transmissions are decodable); T21 then tests the TXWI and
header differences one at a time.

#### 7.13.5 Hand-off contract: 802.11 state machine, supplicant, key tables [M:s; PUB wifi_connect.h, wpa_supplicant_event.h]

Behavioural contract, not an API: (1) connect request = SSID, optional BSSID, the RSN element for
the association request and the auth/cipher/key-management selections; no key material; the only
hardware effect before the join is the key clearing of D1. (2) The state machine joins,
authenticates and associates by itself (D2-D10) and reports "associated" with the association
request's elements and the BSSID; the response's elements are not reported. (3) The 802.1X port is
"not secured" from association to the pairwise install: non-EAPOL data is discarded both ways in
software; EAPOL passes both ways (received EAPOL frames go up as 802.3 frames, type `0x888E`;
transmitted ones go down the data path to slot 1, D25). (4) The host supplicant runs the 4-way and
group handshakes, sends message 4/4, then installs the pairwise key (D26) and the group key (D27).
(5) "Port secured" at the pairwise install (WPA2) raises the network interface and starts DHCP;
no hardware write. (6) Link-down (deauthentication received, beacon loss, user request) runs D31
and then the host's key clearing. The published event enumeration and API that correspond to these
events are `wifi_connect.h` L71-L113 and L170-L397 and `wpa_supplicant_event.h` L5-L12 [PUB].

#### 7.13.6 WDMA buffer placement and cache (closes §16 U6) [M:s wl_rf_common.a, wl_wifi_sta.a; PUB; M:hw]

- TX staging buffer: 2560 bytes, 32-byte aligned, zero-initialised, in the internal-RAM section
  (`.sram`, collected into the `ram0` region at `0x01C00000` by both published linker scripts);
  WDMA_TX_ADR = buffer + 4 (4-byte aligned source). RX buffer: 4096 bytes, 4-byte aligned, ordinary
  static RAM (`.bss`), which is internal RAM on the FM-1 (no SDRAM). The running vendor's
  read-backs (`0x01C26EB8`, `0x01C1EF40`) fall inside these buffers [M:hw oracle symbol table].
- No cache maintenance exists anywhere in the WiFi, lwIP or supplicant code; the only barrier is
  `csync` (TX: set WDMA_CTL bit 10, `csync`, address, length, `csync`, poll bit 20, set bit 10; RX:
  as §7.11.6 with `csync` after each phase-1 step and none after phase 2) [M:s].
- Published cache model: 32-byte lines; SDRAM at `0x04000000` is cached and has an uncached alias
  at +`0x04000000`; addresses below `0x04000000` (internal RAM, the flash window) have no alias
  [PUB cpu.h L45-L53, cache.h L22]. The vendor's other DMA users flush only SDRAM-resident buffers
  (display) and keep descriptors in internal RAM with `csync` only (memory-to-memory DMA) [M:s].
- Conclusion: a WDMA buffer in internal RAM needs ordering (`csync`) only; an SDRAM-resident
  buffer would need line flush/invalidate [I]. The GPL's buffers are internal RAM (`0x01C0xxxx`).

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
`0x14000` (WL_CTL0), `0x10010` (SYS_CLK2) [M:s]. Detail [M:s BT controller library, 2026-11-04]:
its trim reload writes back the full 32-bit words `0x1191C` and `0x11920` from the snapshot that
the WiFi init takes at its very end (§11.2.11); its PLL-reset helper (test paths only) clears or
sets `0x11924` bits 2, 0, 1 and writes `0x11950` [3:0] = 0 or `|= 0xE`; its single-carrier test
forces `0x11920` [6:0] = `0x7F` with bit 7 and `0x1191C` [14:8] = `0x7F` with bit 15. A BT start
before WiFi is outside the supported profile (§16 U9).

---

## 10. Stored calibration (VM records)

Factory calibration data stored in the SDK's VM (key-value store) [M:d]. On a real FM-1, the VM is
at flash `0x0E8000` [M:hw] (not `0x093000` as in the emulator [M:d]).

| VM ID | Size | Content                                        | Drives                                                                                             | Tag      |
|-------|------|------------------------------------------------|----------------------------------------------------------------------------------------------------|----------|
| 106   | 2 B  | left, right crystal load trims                 | `0x11930` bits [16:13], [22:19]                                                                    | M:t, M:s |
| 107   | 7 B  | PA trim bytes (7); published default `{1, 7, 4, 7, 11, 1, 7}` [PUB] | `0x11924` [26:24], `0x11928` [2:0]/[11:9]/[17:15]/[25:22], `0x1192C` [2:0], plus 32 LUT words each | M:t, M:s |
| 108   | 20 B | WiFi PA per-rate digital gain                  | 12 BBP window-D entries                                                                            | M:s      |
| 187   | 68 B | Calibration record (64 B payload + CRC-16 stored as a 32-bit word; §10.2) | Window-D/D' entries, `0x11908`, `0x1191C`, `0x11920`, `0x1195C`, `0x11960`, LUT word patches       | M:t, M:s |

Record format: 4-byte header (CRC-16/XMODEM low byte, ID, length) + data [M:s].
VM 187's CRC-16/XMODEM of bytes 0..63 is stored as a 32-bit word at bytes 64..67 (**ERRATA
2026-11-04**: not "at bytes 64-65"); the complete layout, the validity check and the
byte-to-register mapping are in §10.2 [M:s]. Record ids 106, 107, 108 and 187 are published
[PUB `include_lib/utils/syscfg/syscfg_id.h` L205-233]. BLE-HW-FACTS.md §14 and §16.4 describe the
same records from the BLE side.

### 10.1 Full RF calibration ("full trim") [M:s wl_rf_common.a; rewritten 2026-11-04]

Runs inside the RF init (§4.2.1 A16, §11.2.1 step 7) when the stored record is not valid (§10.2),
after the PLL programming and the bank scan (§5.2, §11.2.9) and before the second PHY init. It
needs the baseband tables of PHY init #1 in place and the RF front end at the end state of the
constant block (§11.2.9). The earlier text of this section, which named the steps and pointed to a
third-party calibration file for the tables, is withdrawn: everything below comes from the vendor
library [M:s] with the hardware cross-checks noted, and an implementer needs nothing else.
Every value this procedure measures is unit-specific and is never to be copied from another unit.

When no valid record exists and this procedure is not run, the receiver is deaf: the RF-die LUT
AGC words, the RX DC fields of `0x11908`, the TX LO fields and the RC codes all stay at their
neutral or zero values [M:t, I].

**Notation.** `WLA_CONn` = `0x11900 + 4n`. C[i], D[i], D'[i] are BBP window entries (CCA, TX back
end, RX front end) written and read through the port protocol of §6.2-§6.3; a read-modify-write of
an entry is a window read (index, commit 2, read-back register) followed by a write. R<n> is a
direct BBP register. "Status read" is the analog status port: `0x11968` [31:28] = 1, eight writes
of 1 to `0x11978`, one write of 0, then one read of `0x11978`; no poll and no delay are involved.
"PHY reset" is MAC_SYS_CTRL (`0x31004`) = `0xC`; R22 = 0; R21 = `0x31`; R21 = `0x30`; R25 = 0.

#### 10.1.1 RF path enables (mode select)

The RF front end has 29 enable bits, written as single-bit read-modify-writes in the order of the
table, each as 1 when its group's condition holds for the selected mode and 0 otherwise [M:s].

| # | Register | Bit | Group | # | Register | Bit | Group |
|---|----------|-----|-------|---|----------|-----|-------|
| 1 | `0x11900` | 23 | G1 | 16 | `0x1191C` | 4 | G4 |
| 2 | `0x11904` | 15 | G2 | 17 | `0x11920` | 9 | G4 |
| 3 | `0x1190C` | 4 | G3 | 18 | `0x11924` | 5 | G4 |
| 4 | `0x1190C` | 5 | G3 | 19 | `0x11924` | 7 | G4 |
| 5 | `0x1190C` | 20 | G3 | 20 | `0x11924` | 8 | G4 |
| 6 | `0x11910` | 12 | G1 | 21 | `0x11924` | 27 | G4 |
| 7 | `0x11910` | 27 | G1 | 22 | `0x11928` | 12 | G4 |
| 8 | `0x11914` | 11 | G1 | 23 | `0x11928` | 26 | G4 |
| 9 | `0x11914` | 18 | G1 | 24 | `0x11938` | 26 | G3 |
| 10 | `0x11918` | 0 | G1 | 25 | `0x11938` | 27 | G4 |
| 11 | `0x11918` | 1 | G1 | 26 | `0x11910` | 13 | G5 |
| 12 | `0x11914` | 26 | G1 | 27 | `0x1192C` | 14 | G5 |
| 13 | `0x11908` | 6 | G4 | 28 | `0x1192C` | 11 | G6 |
| 14 | `0x11918` | 19 | G4 | 29 | `0x11908` | 29 | G6 |
| 15 | `0x1191C` | 0 | G4 | | | | |

| Mode | Use | G1 | G2 | G3 | G4 | G5 | G6 |
|------|-----|----|----|----|----|----|----|
| 0 | receive (the operating mode; also any value outside 1..5) | 1 | 1 | 1 | 0 | 0 | 0 |
| 1 | transmit (not used by the start path) | 0 | 0 | 0 | 1 | 0 | 0 |
| 2 | TX calibration (loopback) | 1 | 0 | 0 | 1 | 1 | 0 |
| 3 | RX calibration (loopback) | 1 | 0 | 1 | 1 | 0 | 1 |
| 4 | RX DC calibration (not passed directly; reached as mode 0 then `0x11904` bit 15 = 0) | 1 | 0 | 1 | 0 | 0 | 0 |
| 5 | RF off (every enable 0) | 0 | 0 | 0 | 0 | 0 | 0 |

`0x11904` bit 15 is the receive-only flag (G2): no other mode sets it. Note that the auto-control
step (§11.2.11) sets `0x11924` bits 5-8 and `0x11938` bits 26-27 afterwards, so in the operating
state those G4 bits read 1 although mode 0 cleared them [M:s, M:hw].

Mode sequence of a WiFi start [M:s]: 5 (before the PLL programming and the bank scan; the RC tune
also runs in 5) → 2 (TX trim) → 0 then `0x11904` bit 15 = 0 (RX DC trim) → 3 (RX IQ trim) → 0. The
quick path (§10.2) goes 5 → 0.

#### 10.1.2 Receive gain steps (42 steps)

A gain step n (0..41) is written as, in this order: `0x11910` bit 22, bit 23, bit 24 = bits 0, 1, 2
of field A; `0x11910` bit 25, bit 26 = bits 0, 1 of field B; `0x11914` [17:15] = field C-low (one
masked write); `0x11914` bit 12, bit 13, bit 14 = bits 0, 1, 2 of field C-high; `0x11914` [7:5] =
field D (one masked write). The 42 x 4 table is reproduced exactly by this rule (policy route 2 of
§0; checked for all 42 entries [M:s]): with g = n div 6 and j = n mod 6,

| g | A `0x11910` [24:22] | B `0x11910` [26:25] | C-high `0x11914` [14:12] | C-low `0x11914` [17:15] | D `0x11914` [7:5] |
|---|---|---|---|---|---|
| 0 | 1 | 0 | 1 | j | 0 |
| 1 | 1 | 0 | 2 | j | 1 |
| 2 | 1 | 1 | 2 | j | 2 |
| 3 | 2 | 1 | 2 | j | 3 |
| 4 | 2 | 1 | 4 | j | 4 |
| 5 | 2 | 2 | 4 | j | 5 |
| 6 | 4 | 2 | 4 | j | 6 |

Check values: step 0 = (A 1, B 0, C-high 1, C-low 0, D 0); step 41 = (4, 2, 4, 5, 6). Use: the ADC
peak tune (§10.1.5) walks steps 0..36; the RX DC trim uses step 40; the end of the RF init writes
step 0, which is the operating value (the vendor radio reads A = 1, B = 0, C-high = 1, C-low = 0,
D = 0 [M:hw]).

#### 10.1.3 Measurement engine (window D')

One measurement: write D'[`0x15`] = 1, D'[`0x16`] = 1, D'[`0x15`] = 3, D'[`0x16`] = 3; poll
D'[`0x8D`] until bits [1:0] are both 0 (no delay between polls; the vendor's poll has **no bound**);
then read the result. The start sequence is the same for every measurement; the "selector" only
decides which entries are read [M:s]:

| Selector | Read | Result |
|----------|------|--------|
| power (used by the TX LO, TX DC, TX gain, TX phase and RX IQ searches) | I = signed 16-bit from D'[`0x81`] (high byte), D'[`0x82`] (low); Q = signed 16-bit from D'[`0x84`] (high), D'[`0x85`] (low) | I² + Q² as a signed 32-bit integer; smaller is better |
| I mean (RX DC) | D'[`0x81`] (bits 23:16), D'[`0x82`] (15:8), D'[`0x83`] (7:0) | signed 24-bit two's complement |
| Q mean (RX DC) | D'[`0x8A`] (23:16), D'[`0x8B`] (15:8), D'[`0x8C`] (7:0) | signed 24-bit two's complement |

What the hardware measures physically behind these entries is not determinable [I]; the arithmetic
above is what the vendor applies.

#### 10.1.4 Trim prologue (before each loopback step)

Each of the three loopback steps (TX trim, RX DC trim, RX IQ trim) starts with this identical
sequence [M:s]:

1. PHY reset.
2. C[`0x15`] = 0; C[`0x14`] = 0.
3. C[`0x40`]: clear bit 7 (read-modify-write); C[`0x41`]: clear bit 7.
4. C[`0x28`] = 0; C[`0x27`] = 5.
5. R4: clear bits 4:3 (read-modify-write; the only writer of R4; never restored. The vendor radio
   reads R4 = 0 [M:hw]).
6. PHY reset again; MAC_SYS_CTRL = `0xC`; R25 = `0x18` (loopback mode; returns to 0 at the next
   PHY reset).

Nothing is restored inside the trim. PHY init #2 (§6.6) rewrites C[`0x14`] = `0x7F`, C[`0x15`] =
`0x0F`, C[`0x27`] = `0x84`, C[`0x28`] = `0x1E`, C[`0x40`] = `0xE7`, C[`0x41`] = `0x87` (the §11.2.6
table values); R4 is not rewritten by anything.

#### 10.1.5 ADC peak read and receive-gain auto-tune

ADC peak read: D'[`0x17`] = `0x82`; wait 1000 µs; read D'[`0x80`] (unsigned byte = peak);
D'[`0x17`] = 2 [M:s].

Auto-tune (the variant the trim uses; the other variant stops at a peak of 16 or more and is not
used): set gain step 0; read the peak twice and keep the second; if the peak is above 47 (48 or
more) finish with step 0; otherwise increase the step by one, write it, read twice, and finish at
the first step whose peak is above 47 or at step 36 (the bound; nothing aborts). Write the final
step again at the end. Cost 2 ms per step tried; result in 0..36 [M:s].

#### 10.1.6 Loopback attenuation

Two variants, written before the TX trim ("TX") and before the RX IQ trim ("RX"); the RX DC trim
inherits the TX variant. Both first write `0x11920` bit 20 = 0, then bit 21 = 1. Then: TX variant
bit 22 = 0, bit 23 = 1; RX variant bit 22 = 1, bit 23 = 0. Both then write `0x11924` [30:28] = 5;
`0x11928` bit 13 = 1, bit 14 = 1; `0x11928` [28:27] = 3 (TX) or 1 (RX); `0x11910` [11:8] = 9 [M:s].

| Field | Constant block (§11.2.9) | During TX trim | During RX DC trim | During RX IQ trim and **after the full trim** |
|-------|---|---|---|---|
| `0x11920` b20 / b21 | 0 / 1 | 0 / 1 | 0 / 1 | 0 / 1 |
| `0x11920` b22 / b23 | 0 / 1 | 0 / 1 | 0 / 1 | **1 / 0** |
| `0x11924` [30:28] | 6 | 5 | 5 | **5** |
| `0x11928` b13 / b14 | 1 / 1 | 1 / 1 | 1 / 1 | 1 / 1 |
| `0x11928` [28:27] | 3 | 3 | 3 | **1** |
| `0x11910` [11:8] | (not set by the block) | 9 | 0, 0, 3, 9 per gain configuration | 9 |

Nothing after the trim touches these fields, so the "after" column is the operating state of a
unit that ran the full trim; the vendor radio reads exactly those values [M:hw]. A quick-path start
keeps the constant-block values instead (§10.2).

#### 10.1.7 RC filter tune

Preparation: `0x11904` [11:8] = 8 (left set); `0x11950` bit 27 = 1. For each bandwidth in the
order 6 MHz, 10 MHz, 20 MHz: `0x11968` [9:1] = `0xFE`, `0x91`, `0x50` respectively (written as a
read-modify-write of mask `0x3FE` with the raw values `0x1FC`, `0x122`, `0x0A0`); `0x11968`
[17:11] = 15; [22:18] = 0; bit 23 = 0; bit 0 = 1 (start); then status reads until bit 5 = 1 (**no
bound**, no delay); one more status read, whose bits [15:8] are the code; `0x11968` bit 0 = 0.
After the three: `0x11950` bit 27 = 0; `0x11920` [17:10] = the 10 MHz code; `0x11910` [21:14] =
the 10 MHz code; `0x1195C` = `0x11960` = ([20 MHz code] << 24) | ([10 MHz code] << 16) |
([6 MHz code] << 8) | `0xFF` [M:s]. The vendor radio reads codes 253 / 127 / 55 and
`0x1195C` = `0x11960` = `0x377FFDFF` [M:hw]; these are unit results.

#### 10.1.8 TX digital correction fields

Six 10-bit values live in window D as bits [9:2] in a main entry and bits [1:0] in a 2-bit slot of
D[`0x61`] or D[`0x62`]; the slot is updated by a read-modify-write of its entry [M:s]:

| Value | Main entry = v[9:2] | Slot = v[1:0] | Neutral | Signed |
|-------|---------------------|---------------|---------|--------|
| TX I gain | D[`0x04`] | D[`0x61`] [1:0] | 1023 (`0xFF`, slot 3) | no |
| TX I phase | D[`0x05`] | D[`0x61`] [3:2] | 0 | yes (two's complement) |
| TX Q gain | D[`0x06`] | D[`0x61`] [5:4] | 1023 | no |
| TX Q phase | D[`0x07`] | D[`0x61`] [7:6] | 0 | yes |
| TX I DC | D[`0x08`] | D[`0x62`] [1:0] | 0 | yes |
| TX Q DC | D[`0x09`] | D[`0x62`] [3:2] | 0 | yes |

TX LO leakage codes: I in `0x1191C` [14:8] (7 bits), Q in `0x11920` [6:0] (7 bits); mid-scale 64.

#### 10.1.9 TX trim (mode 2, TX attenuation)

Setup, in order: the prologue (§10.1.4); D[0] = 3, D[`0x5F`] = `0x10`, D[1] = `0x14`, D[2] = 0,
D[3] = 0; D'[0] = `0x3E`, D'[`0x13`] = `0x1E`, D'[`0x14`] = `0x38`, then D'[`0x14`] = `0x34`; the
six TX fields to neutral [M:s].

1. **LO search.** LO I = 64, LO Q = 64; auto-tune (§10.1.5); LO I = 64, measure power, best = 64;
   write LO I = 0, 1, ..., 127, measuring each; a code becomes best only when its power is strictly
   lower; write LO I = best. Then the same for LO Q (0..127) with I fixed. 258 measurements.
2. **DC search.** I DC = 0, Q DC = 0; auto-tune again; measure the baseline with I DC = 0; sweep
   I DC through the 401 values −200..+200, each reduced to an 8-bit two's-complement value before
   it is written (so the values actually written run 56..127, −128..127, −128..−56 and every
   value in −128..+127 is visited at least once); strictly-lower wins; apply the best; the same for
   Q DC with I fixed. 804 measurements.
3. **Gain search.** D'[`0x13`] = `0x3C`; gains 1023/1023; baseline; for k = −423..+423: k < 0 →
   I gain = 1023 + k, Q gain = 1023; k ≥ 0 → I gain = 1023, Q gain = 1023 − k; strictly-lower wins.
   The vendor keeps the winning k reduced to an 8-bit two's-complement value b and applies b < 0 →
   I gain = 1023 + b; b ≥ 0 → Q gain = 1023 − b, so a winner outside −128..+127 is applied aliased
   [M:s; observed vendor behaviour, not a hardware requirement]. 848 measurements.
4. **Phase search.** Both phases = 0; baseline; if b ≥ 0 sweep the Q phase, else the I phase, over
   −200..+200 (the other phase stays 0); strictly-lower wins; the winner is again reduced to 8 bits
   before it is applied. 402 measurements.

#### 10.1.10 RX DC trim (mode 0 with `0x11904` bit 15 = 0)

Preconditions: high-pass closed: `0x11914` [2:0] = 0, bit 3 = 0, bit 4 = 0. Setup: the prologue;
D[0] = 3, D[`0x5F`] = `0x10` (D[1..3] keep the TX-trim values `0x14`, 0, 0); D'[0] = `0x3E`,
D'[`0x1B`] = `0x38`, D'[`0x08`] = `0xC0`, D'[`0x13`] = 0, D'[`0x14`] = `0x38`, then D'[`0x14`] =
`0x34`; gain step 40 [M:s].

Gain configurations, written in the order `0x1190C` [7:6], `0x11910` [11:8], `0x11910` [6:0]:

| Configuration | `0x1190C` [7:6] | `0x11910` [11:8] | `0x11910` [6:0] |
|---|---|---|---|
| 0 | 0 | 0 | 127 |
| 1 | 1 | 0 | 127 |
| 2 | 1 | 3 | 69 |
| 3 | 1 | 9 | 14 |

Analog DC code encoding in `0x11908`, for a signed code v: I magnitude [15:7] = |v| (9 bits), sign
bit 16; Q magnitude [25:17] = |v|, sign bit 26; **a sign bit of 1 means non-negative, 0 means
negative**. The magnitude field is written first, the sign bit in a second write. The saved 10-bit
code is v | `0x200` for v ≥ 0 and |v| for v < 0 (bit 9 = non-negative flag) [M:s].

Procedure, for each configuration 0..3: write the configuration; write analog I = 0 then Q = 0;
step = 256, I = Q = 0; 12 rounds of: measure the I mean; if it is positive and I > −510, I −= step;
if negative and I < 510, I += step (zero leaves it); write analog I; measure the Q mean; apply the
same rule to Q and write it; step = max(step / 2, 1) (256, 128, 64, 32, 16, 8, 4, 2, 1, 1, 1, 1).
Then write the final I and Q once more and save both codes. Outputs: a 4 x 2 table of 10-bit codes;
`0x11908` is left holding the configuration-3 result (the vendor radio reads I = +90, Q = +56
[M:hw], unit results); the register state ends at `0x1190C` [7:6] = 1, `0x11910` [11:8] = 9,
[6:0] = 14. Afterwards the high-pass is reopened: `0x11914` [2:0] = 1, bit 3 = 1, bit 4 = 1.

The table is consumed by the LUT load (§5.1, A20): entries 128-132 use configuration 0, 133-179
configuration 1, 180-185 configuration 2, 186-223 configuration 3; word 0 = (base [11:0]) |
(I code << 12) | (Q code << 22) [M:s].

#### 10.1.11 RX IQ trim (mode 3, RX attenuation)

Setup: the prologue; D[0] = 3, D[`0x5F`] = `0x10`, D[1] = `0x14`, D[2] = 0, D[3] = `0x11`;
D'[`0x09`] = D'[`0x0B`] = `0xFF` (RX I/Q gain), D'[`0x0A`] = D'[`0x0C`] = 0 (RX I/Q phase, signed);
D'[0] = `0x3E`, D'[`0x14`] = `0x38`, then D'[`0x14`] = 4, then D'[`0x13`] = `0x1E`; auto-tune;
gains `0xFF` again; baseline (power selector). Gain sweep k = −105..+105: k < 0 → I gain = 255 + k;
k ≥ 0 → Q gain = 255 − k; strictly-lower wins; apply. Phases = 0; baseline; if the winning k ≥ 0
sweep the Q phase D'[`0x0C`], else the I phase D'[`0x0A`], over −50..+50 as signed 8-bit; apply the
best. 314 measurements [M:s]. D[3] = `0x11` is not restored by anything that follows; the vendor
radio reads D[1] = `0x14`, D[2] = 0, D[3] = `0x11` [M:hw 2026-11-03], i.e. a full trim has run on
that unit (in that boot, or earlier if window D survives a reset [I]).

#### 10.1.12 End of the trim and the operating state

After the RX IQ trim the shared tail of the RF init runs: mode 0; the PA fields from the seven PA
trim bytes (§10, §11.2.9); a software snapshot of the TX fields (D[4..9], D[`0x61`], D[`0x62`],
decoded as 10-bit values) and of `0x1191C`/`0x11920` (§11.2.11); the record build of §10.2. The
measured results stay in the hardware: TX fields in window D, TX LO codes, RX DC in `0x11908`, RC
codes in `0x11920`/`0x11910`/`0x1195C`/`0x11960`, RX IQ in D'[9..`0x0C`]; the LUT AGC words carry
the RX DC codes once A20 loads them. The vendor radio reads TX I gain high byte D[4] = `0xF8`,
I phase D[5] = `0xFC`, I DC D[8] = 3, Q DC D[9] = `0xFF`, Q gain D[6] = `0xFF`, Q phase D[7] = 0
[M:hw 2026-11-03]: unit results.

#### 10.1.13 Bounds, failure and timing

| Wait | Done when | Delay in loop | Bound | Vendor disposition on failure |
|------|-----------|---------------|-------|-------------------------------|
| RC tune done | status bit 5 = 1 | none | **none** | hangs |
| measurement done | D'[`0x8D`] [1:0] = 0 | none | **none** | hangs |
| ADC peak | fixed 1000 µs wait, no poll | | | |
| auto-tune | peak > 47 | 2 ms per step | step 36 | continues with 36 |
| bank-scan comparator | one status read per probe (after 5/5/100 µs) | | 24 probes + 47 sweeps of at most 99 steps | always terminates (§5.2) |
| BBP port busy (every window access) | `0x3101C` bit 17 = 0 | none | **none** | hangs |

The full trim has no failure exit: every search applies its best value, and the two unbounded
waits hang if the hardware never completes. An implementer should bound them (a few ms each is
far above the observed completion; measured latencies are not recorded) and treat a timeout as
"calibration unsupported on this unit" rather than invent a recovery [I]. The only explicit delay
inside the trim is the 1000 µs per ADC peak read: 6-222 ms in total over the three auto-tunes;
2722 measurements whose duration the hardware sets (not determinable from the vendor; measure on
hardware). An 80 µs delay precedes the trim (the PLL loop pulse, §11.2.9).

**Resulting state that a quick-path start does not have:** R4 bits 4:3 cleared, D[1..3] =
`0x14`, 0, `0x11`, the "after the full trim" attenuation column of §10.1.6, and
`0x11904` [11:8] = 8 (the quick path writes this one too).

#### 10.1.14 GPL driver calibration behaviour [M:hw 2026-11-05]

**Update 2026-11-05 23:00Z:** TX IQ calibration is now producing valid results:
D[4] = 0xF8, D[5] = 0xFC, D[6] = 0xFF, D[8] = 3, D[9] = 0xFF, TX LO I = 75, TX LO
Q = 49. All match the vendor unit's values [M:hw, observation
2026-11-05T23:00:00Z]. The implementer fixed the calibration path (A25 resolved).

**Earlier state (historical, kept for traceability):** the A24-only build produced neutral
defaults (D[4..9] = `FF 00 FF 00 00 00`) with only 354 of ~2722 expected measurements.
This was traced to the TX loopback path not producing a measurable signal for the
calibration sweeps; the RC tune (analog status port) was unaffected [M:hw, observation
2026-11-05T16:30:00Z].

**Current blocker (T25b confirmed, root cause identified 2026-11-06):** `0x11900` never
switches from RX mode (0xEAA00005) to TX mode during transmission. The PA, DAC, and TX
mixer stay off. Root cause: **ordering bug in `wifi_rf_arm()`**. The function calls
`wifi_rf_auto_control()` then `wifi_cal_rf_mode(0)`. Auto_control sets G4 enable bits in
`0x11924` (bits 5,7,8) and `0x11938` (bit 27) that the RF auto-switch hardware needs. Then
`wifi_cal_rf_mode(0)` writes all 29 enable bits for mode 0 (G4=0), clearing those bits.
The vendor runs them in the opposite order (A16 mode(0), then A20 LUT, then A21
auto_control), so auto_control's G4 bits survive. Fix: in `wifi_rf_arm()`, move
`wifi_cal_rf_mode(0)` before `wifi_rf_auto_control()`. The `0x11950` value (0x57BFFFDC)
and `0x1405C` enable group are both correct.

Analysis details: observation 2026-11-06T00:00:00Z-WIFI-reader-T25b-analysis.md.

### 10.2 Stored record 187: validity, quick path, when it is written [M:s wl_rf_common.a, cpu library; 2026-11-04]

**Record layout (68 bytes, little-endian; bytes 0..63 are covered by the CRC):**

| Offset | Size | Content | Encoding |
|--------|------|---------|----------|
| 0 | 4 | TX I gain | 10-bit unsigned in a 32-bit word (bits [9:2] → D[`0x04`], [1:0] → D[`0x61`] [1:0]) |
| 4 | 4 | TX I phase | 10-bit signed, sign-extended (→ D[`0x05`], D[`0x61`] [3:2]) |
| 8 | 4 | TX Q gain | 10-bit unsigned (→ D[`0x06`], D[`0x61`] [5:4]) |
| 12 | 4 | TX Q phase | 10-bit signed (→ D[`0x07`], D[`0x61`] [7:6]) |
| 16 | 4 | TX I DC | 10-bit signed (→ D[`0x08`], D[`0x62`] [1:0]) |
| 20 | 4 | TX Q DC | 10-bit signed (→ D[`0x09`], D[`0x62`] [3:2]) |
| 24 | 1 | TX LO I | `0x1191C` [14:8] |
| 25 | 1 | TX LO Q | `0x11920` [6:0] |
| 26 | 1 | RX I gain | D'[`0x09`] |
| 27 | 1 | RX Q gain | D'[`0x0B`] |
| 28 | 1 | RX I phase | D'[`0x0A`] (signed) |
| 29 | 1 | RX Q phase | D'[`0x0C`] (signed) |
| 30 | 2 | padding | included in the CRC as stored (0 when the vendor builds it) |
| 32 | 4 | analog RX DC I | signed 32-bit: +magnitude when `0x11908` bit 16 = 1, −magnitude when 0; magnitude = [15:7] |
| 36 | 4 | analog RX DC Q | signed 32-bit from [25:17] and bit 26, same rule |
| 40 | 4 | `0x1195C` | raw word: [7:0] = `0xFF`, [15:8] 6 MHz code, [23:16] 10 MHz code, [31:24] 20 MHz code |
| 44 | 4 | `0x11960` | raw word, same layout |
| 48 | 16 | RX DC codes | four (I, Q) pairs of 16-bit values for gain configurations 0..3 (§10.1.10), I first; each is the 10-bit code: bit 9 = non-negative, [8:0] = magnitude |
| 64 | 4 | CRC | CRC-16/XMODEM of bytes 0..63, zero-extended to 32 bits (bytes 66-67 = 0) |

CRC-16/XMODEM: polynomial `0x1021`, initial value 0, no reflection, no final XOR; check value of
"123456789" is `0x31C3`. The SoC has a CRC unit used for it outside interrupt context: write 0 to
`0x13504` (seed), write each byte zero-extended as one 32-bit write to `0x13500`, then read
`0x13504` [15:0] after a pipeline sync; the same software CRC is used otherwise, so the two are
equivalent [M:s cpu library; the hardware polynomial is inferred from that equivalence, I].

**ERRATA (2026-11-04):** earlier text said "inner CRC-16 at bytes 64-65"; the CRC is compared as a
32-bit word at 64..67, and a stored word of 0 is invalid.

**Validity check** (run at boot before the RF init, and again at the start of the quick path): the
read must return all 68 bytes; the stored CRC word must be non-zero and equal to the CRC of bytes
0..63. A short read, a missing record, a wrong CRC or a zero CRC all count as invalid [M:s]. The
record is read through the SDK's published record interface (record id 187, 68 bytes) [PUB].
Each successful validation copies bytes 48..63 into the RAM table that the LUT load consumes
(§10.1.10).

**Quick path** (taken when the published use-stored-trim option is on, its default, and the record
is valid), in this write order [M:s]:

1. `0x1191C` [14:8] = byte 24; `0x11920` [6:0] = byte 25.
2. The six TX words into their entries and slots in the order of the table (each: main entry
   write, then a read-modify-write of the slot).
3. `0x11908`: I then Q, each as a 16-bit signed value: v ≥ 0 → magnitude = v & `0x1FF`, sign bit
   set; v < 0 → magnitude = (−v) & `0x1FF`, sign bit cleared; magnitude field first, sign bit second.
4. D'[`0x09`] = byte 26, D'[`0x0B`] = byte 27, D'[`0x0A`] = byte 28, D'[`0x0C`] = byte 29.
5. `0x1195C` = word at 40; `0x11960` = word at 44 (plain stores).
6. `0x11904` [11:8] = 8.
7. `0x11920` [17:10] = `0x1195C` [23:16] (the 10 MHz code, taken from a read-back of the register);
   `0x11910` [21:14] = the same code.

The quick path contains no PHY reset, no RF mode change, no attenuation write, no high-pass
change and no measurement; it does not write the LUT (A20 does, from the RAM table). After it the
shared tail runs as after a full trim (mode 0, PA fields, snapshot). It leaves the constant-block
values of the attenuation fields (§10.1.6), R4 untouched and D[1..3] untouched.

**When the record is written [M:s]:** only when the stored-trim option is on **and** the boot-time
validity check failed. Then, after the full trim and the shared tail, the record is built at once
from the registers (the TX fields, LO codes, RX IQ entries, `0x11908`, `0x1195C`, `0x11960` and the
RX DC table of that trim), and a one-shot timer writes it one second later with the CRC filled
in. A valid record is never rewritten; a full trim that ran because the quick path's second
validation failed is **not** saved; the full trim has no failure outcome that would prevent a save.
**ERRATA (2026-11-04):** earlier text that said "saved after any full trim" and "a failed full
trim is not saved" is replaced by this rule.

**Failure contract:**

| Case | Vendor behaviour | Record written |
|------|------------------|----------------|
| record missing or short | treated as invalid; full trim | yes, one second after the trim |
| CRC wrong or stored CRC = 0 | same | yes |
| quick path fails (second validation) | full trim | **no** |
| a full-trim step "fails" | not possible: no step reports failure; the two unbounded waits hang; every search applies its best | n/a |
| bank search exhausted (§5.2) | silent; banks 15..0 then 127..97 | never (the bank table is RAM only) |

**Where the record lives:** the SDK's key-value store in flash; on the FM-1 the store is at flash
`0x0E8000` [M:hw] (§10). An implementer that cannot or does not want to read the vendor store must
run the full trim every boot (about 2722 measurements plus up to 222 ms of fixed delays) or keep
its own copy of the 68-byte image in the same format.

**State transitions of a start [M:s]:** quick path: RF mode 5 → PLL/bank scan → mode 0. Full
trim: mode 5 → PLL/bank scan → RC tune (still 5) → mode 2 → PHY reset x2 (TX trim) → high-pass
closed → mode 0 with bit 15 = 0 → PHY reset x2 (RX DC) → high-pass open → mode 3 → PHY reset x2
(RX IQ) → mode 0. Every PHY reset inside the trim ends with MAC_SYS_CTRL = `0xC`. Warm restarts,
repeated start/stop, sleep/wake and a BT start before WiFi are outside the supported profile of
this sheet (§16).

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

The running vendor radio's window C read-back of 2026-11-02 equals this (active) set entry for
entry; the alternative set that the published file keeps in a disabled block (179, 16, 3, 180,
180, 180, 120, 80, 72, 3) is not what runs on the FM-1 [M:hw, PUB].

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
(`0x11938` = `0x8DAFD065`: N = 101, bank `0x35`; `0x1193C` = `0x12640F32`: divider code 121; `0x11940` =
`0x08B41411`; `0x11954` = `0x02822520`; `0x11930` = `0x05D9625F`) [M:hw]. For 24 MHz the PLL is
set for 2442 MHz with N = 101, fraction `0xC00000`, `0x11940` b0 = 1 and [7:6] = 0, `0x1193C`
[21:18] = 9 and [29:27] = 2 [M:s wl_rf_common.a]. Calibrating with the 40 MHz words on a 24 MHz
unit is listed in §14 (row 8).

**24 MHz RF init, complete [M:s wl_rf_common.a, cpu library; 2026-11-04].** Register names below
are the published `WLA_CONn` names of the SDK chip header (`0x11900 + 4n`) [PUB]; the names
`PLL_CON0..4` and `ANA_CON0..5` that the 40 MHz table further down uses for `0x11934`-`0x11964`
were the reader's and clash with the published `PLL_CON0`/`PLL_CON1` at `0x119A0`/`0x119A4`
(**ERRATA 2026-11-04**; read that table with this map: `0x11930` WLA_CON12, `0x11934` WLA_CON13,
`0x11938` WLA_CON14, `0x1193C` WLA_CON15, `0x11940` WLA_CON16, `0x11944` WLA_CON17, `0x11950`
WLA_CON20, `0x11954` WLA_CON21, `0x11964` WLA_CON25; and `0x11900`-`0x1192C` are WLA_CON0-11, not
WL_CON0-11).

**Crystal detection.** The library asks the clock layer for the crystal frequency; that query
returns 40 MHz when `PLL_CON0` (`0x119A0`) [27:26] = 2 and 24 MHz otherwise. No library code writes
that field (boot, power and reboot code touch only `PLL_CON0` [1:0]), so it is a hardware or
boot-ROM state; the FM-1 reads 24 MHz [M:hw: `0x11930` [27:23] = `0x0B`]. The published clock
header does not define the field [PUB]. An implementer may hard-code 24 MHz for the FM-1 or test
the field.

**Crystal word `0x11930` (WLA_CON12), write order, no delays:** bit 0, then bit 1, bit 2, bit 3,
bit 4 set one at a time; [6:5] = 2; bit 7 = 0; [9:8] = 2; bit 10 = 0; bit 11 = 0; bit 12 = 0;
bit 17 = 0; bit 18 = 0; [16:13] = low nibble of the left crystal cap byte; [22:19] = low nibble of
the right cap byte; [27:23] = `0x0B` (24 MHz; `0x1A` at 40 MHz). [31:28] is never written. The
cap bytes are record 106 [PUB id]; when the record is absent the published default `0x0B`/`0x0B`
is used and written back [PUB `apps/common/config/user_cfg.c` L529-540, `apps/demo/demo_wifi/board/wl82/board.c` L50-51].
Boot clock code has already written `0x11930` before the WiFi init (bit 0 cleared, bits 7 and 17
set, [27:23] = `0x13`, bit 0 set); the WiFi init overrides all of it. The vendor radio reads
`0x05D9625F` = exactly this sequence with caps `0xB`/`0xB` [M:hw].

**Constant block** (straight-line field writes in this order after the crystal word; values at
24 MHz; "[a:b] = v" is a masked read-modify-write, "bn = v" a single-bit one):

- `0x11950` = 0 (whole word).
- `0x11900`: b0 = 1; [4:1] = 2; b5 = 0; [7:6] = 0; b8 = 0; b9 = 0; b10 = 0; [13:11] = 0; b18 = 0; [21:19] = 4.
- `0x11904`: b31 = 0.
- `0x11900`: b22 = 0; b23 = 0; [25:24] = 2; b26 = 0; b27 = 1; b28 = 0.
- `0x11904`: [7:0] = `0x80`.
- `0x11900`: b29 = 1; b30 = 1; b31 = 1.
- `0x11904`: [11:8] = 8; [14:12] = 5; b15 = 0; [17:16] = 3; [21:18] = 0; [25:22] = 7; [29:26] = 0; b30 = 0.
- `0x11908`: [2:0] = 0; [4:3] = 2; b5 = 0; b6 = 0; [15:7] = 0; b16 = 1; [25:17] = 0; b26 = 1; b27 = 1; b28 = 1; b29 = 0; [31:30] = 2.
- `0x1190C`: [1:0] = 1; [3:2] = 2; b4 = 0; b5 = 0; [7:6] = 1; [10:8] = 3; [12:11] = 2; b13 = 0; [15:14] = 2; [18:16] = 3; b19 = 1; b20 = 0; [22:21] = 1; b23 = 0; b24 = 1; [27:25] = 3.
- `0x11910`: [6:0] = `0x0E`; b7 = 1; [11:8] = 9.
- `0x1190C`: b28 = 0; [31:29] = 3.
- `0x11910`: b12 = 0; b13 = 0; [21:14] = `0x20`; b22 = 0; b23 = 0; b24 = 1; b25 = 0; b26 = 1; b27 = 0; b28 = 0; b29 = 0.
- `0x11914`: [2:0] = 1; b3 = 1; b4 = 1; [7:5] = 0; [10:8] = 3; b11 = 0; b12 = 0; b13 = 0; b14 = 1; [17:15] = 1; b18 = 0; [20:19] = 3.
- `0x11918`: b0 = 0; b1 = 0.
- `0x11914`: b21 = 1; [24:22] = 1; b25 = 0; b26 = 0; [28:27] = 0; [30:29] = 2.
- `0x11918`: b2 = 1; [5:3] = 4; b6 = 0; b7 = 0; [9:8] = 2; [12:10] = 2; b13 = 1; [15:14] = 1; [18:16] = 7; b19 = 0.
- `0x1191C`: b0 = 0; [3:1] = 3; b4 = 0; [7:5] = 3; [14:8] = `0x40`; b15 = 1; b16 = 1; [19:17] = 2; b20 = 1; [22:21] = 0; b23 = 0; b24 = 1; b25 = 0.
- `0x11920`: [6:0] = `0x40`; b7 = 1; b8 = 1.
- `0x1191C`: [28:26] = 2; b29 = 1; [31:30] = 0.
- `0x11920`: b9 = 0; [17:10] = `0x90`; b18 = 1; b19 = 0; b20 = 0; b21 = 1; b22 = 0; b23 = 1; [26:24] = 0; [29:27] = 0.
- `0x11924`: [2:0] = 7; [4:3] = 2; b5 = 0; b6 = 1; b7 .. b17 = 0; [19:18] = 2; [21:20] = 3; b22 = 0; b23 = 1; [26:24] = 5; b27 = 0; [30:28] = 6.
- `0x11928`: [2:0] = 7; b3 = 0; [5:4] = 2; [8:6] = 7; [11:9] = 3; b12 = 0; [14:13] = 3; [17:15] = 5; b18 = 0; b19 = 1; [21:20] = 3; [25:22] = `0xB`; b26 = 0; [28:27] = 3.
- `0x1192C`: [2:0] = 0; b3 = 0; b4 = 1; [6:5] = 3; [8:7] = 0; [10:9] = 0; b11 = 0; [13:12] = 2; b14 = 0; b15 = 1; [17:16] = 2; b18 = 0; [20:19] = 2; [22:21] = 2.
- `0x11934`: [23:0] = 0; b24 = 1; b25 = 0; [27:26] = 0; [29:28] = 0.
- `0x11938`: [11:0] = 51; [15:12] = `0xD`; b16 = 1; b17 = 1; b18 = 0; [25:19] = 51; b26 .. b30 = 0; b31 = 1.
- `0x1193C`: [1:0] = 2; [4:2] = 4; [12:5] = 119; [17:13] = 0; [21:18] = `0xE`; [23:22] = 1; b24 = 0; [26:25] = 1; [29:27] = 7; b30 = 0.
- `0x11940`: b0 = 0; b1 = 0; [4:2] = 4; b5 = 0; [7:6] = 2; b8 = 0; [13:9] = 10 (18 at 40 MHz); b14 = 0; [17:15] = 0; b18 = 1; [20:19] = 2; [22:21] = 1; b23 = 1; b24 .. b26 = 0; [29:27] = 1.
- `0x11944`: [1:0] = 0; [3:2] = 0; b4 .. b6 = 0; [8:7] = 1; [10:9] = 1; [12:11] = 1; [16:13] = 0; [18:17] = 0; b19 = 0.
- `0x11964`: [17:16] = 1.

End values of the block (bits the block never touches are listed; every such bit that was
captured reads 0 on hardware): `0x11900` `0xEA200005` ([17:14] untouched); `0x11904`
`0x01C35880`; `0x11908` `0x9C010010`; `0x1190C` `0x672B9349`; `0x11910` `0x0508098E` ([31:30]);
`0x11914` `0x4078C319` (b31); `0x11918` `0x00076A24` ([31:20]); `0x1191C` `0x2915C066`; `0x11920`
`0x00A641C0` ([31:30]); `0x11924` `0x65B80057` (b31); `0x11928` `0x1AFAE7E7` ([31:29]); `0x1192C`
`0x0052A070` ([31:23]); `0x11938` `0x819BD033`; `0x1193C` `0x3A780EF2` (b31); `0x11940`
`0x08B41490` ([31:30]); `0x11944` `0x00000A80` ([31:20]); `0x11950` 0. (For `0x1190C`-`0x1192C` these equal the
"WiFi target" column of the 40 MHz table below, which the two crystal paths share; that table's
`0x11900` value carries a residue bit 17 from the old simulation's starting word, and its
`0x11930`-`0x11954` rows are the 40 MHz values and do not apply.)

**PLL for 2442 MHz at 24 MHz, after the block:** mode 5 (§10.1.1); restart pulse; bank scan
(§5.2: `0x11940` b24 = 1 during the scan, 0 after; ends with `0x1193C` [12:5] = 121 and `0x11938`
[25:19] = the bank for 121); then `0x11934` b25 = 0, [27:26] = 0, [23:0] = `0xC00000`; `0x11938`
[11:0] = 101 (N; the fraction gives 4884/48 = 101.75; at 40 MHz N = 40, fraction `0xB33333`);
`0x11940` b0 = 1, [7:6] = 0 (1 at 40 MHz); `0x1193C` [21:18] = 9 (8 at 40 MHz), [29:27] = 2;
`0x11954` = `0x02822520` (`0x000A2500` at 40 MHz); restart pulse; loop pulse (`0x11938` b18 = 0,
80 µs, b18 = 1). The restart pulse is `0x11938` b16 = 0, b18 = 0, `0x11934` b24 = 0; 5 µs; b16 = 1;
5 µs; `0x11934` b24 = 1, `0x11938` b18 = 1; 100 µs. The vendor radio's `0x1193C` = `0x12640F32`
decodes to [1:0] = 2, [4:2] = 4, [12:5] = 121, [21:18] = 9, [23:22] = 1, [26:25] = 1, [29:27] = 2,
and `0x11938` = `0x8DAFD065` to N = 101, [15:12] = `0xD`, b16 = b17 = b18 = 1, bank 53, b26 = b27 =
1, b31 = 1: both exactly as predicted [M:hw, M:s].

**Vendor steady state at 24 MHz and what each part is [M:hw 2026-11-02; classification M:s].**
Starting from the block end values and applying, in order, the PLL step above, RF mode 0
(§10.1.1), the calibration writes (§10.1 or §10.2), the init tail (`0x11904` [14:12] = 4, `0x11908`
[2:0] = 0), gain step 0 (§10.1.2), the last attenuation variant (§10.1.6), the PA trim bytes and
the auto-control sequence (§11.2.11) reproduces every captured word exactly. Fields marked **unit** are this unit's results or trims and must never be
hard-coded; everything else is the same on every FM-1.

| Register | Vendor reads | Constant part | Unit-specific fields (value on this unit) |
|----------|--------------|---------------|--------------------------------------------|
| `0x11900` | `0xEAA00005` | block + mode 0 (b23) | none |
| `0x11908` | `0x9C712D10` | `0x9C010010` | **unit**: RX DC I [15:7] = 90, Q [25:17] = 56, signs b16 = b26 = 1 (§10.1.10) |
| `0x11910` | `0x085FD98E` | block, [11:8] = 9, gain step 0 ([24:22] = 1, [26:25] = 0), mode 0 (b12, b27) | **unit**: RC code [21:14] = `0x7F` |
| `0x11914` | `0x447C1B19` | block, gain step 0 ([14:12] = 1, [17:15] = 0, [7:5] = 0), mode 0 (b11, b18, b26), high-pass open | none |
| `0x1191C` | `0x2915CD66` | block | **unit**: TX LO I [14:8] = 77 |
| `0x11920` | `0x0065FDB1` | block, attenuation after trim (b22 = 1, b23 = 0) | **unit**: TX LO Q [6:0] = 49, RC code [17:10] = `0x7F` |
| `0x11924` | `0x51B801F7` | block, attenuation after trim ([30:28] = 5), auto control (b5-b8) | **trim bytes**: [26:24] = 1 |
| `0x11928` | `0x0AFBE9E7` | block, attenuation after trim ([28:27] = 1) | **trim bytes**: [2:0] = 7, [11:9] = 4, [17:15] = 7, [25:22] = 11 |
| `0x11930` | `0x05D9625F` | crystal word, type `0x0B` | **unit** (record 106): caps [16:13] = [22:19] = `0xB` (the published default) |
| `0x11938` | `0x8DAFD065` | N = 101, [15:12] = `0xD`, restart bits, auto control (b26, b27), b31 | **unit, per boot**: VCO bank [25:19] = 53 (52 in two other captures) |
| `0x1193C` | `0x12640F32` | PLL step | none |
| `0x11940` | `0x08B41411` | block + PLL step (b0 = 1, [7:6] = 0) | none |
| `0x11950` | `0x57BFFFDC` | auto control | none |
| `0x11954` | `0x02822520` | PLL step | none |
| `0x1195C`, `0x11960` | `0x377FFDFF` | layout `[20 MHz][10 MHz][6 MHz]FF` | **unit**: RC codes 55 / 127 / 253 |
| `0x11964` | `0x3B010000` | block ([17:16] = 1) + auto control ([31:29] = 1, [28:24] = `0x1B`) | none |

Captured on the vendor unit in both 2026-11-02 pages [M:hw]: `0x11904` = `0x01C3C880` (block end
`0x01C35880` plus mode 0's bit 15 and the init tail's [14:12] = 4), `0x1190C` = `0x673B9379` (block
end plus mode 0's bits 4, 5, 20; the RX DC configuration-3 field [7:6] = 1 is already the block
value), `0x11918` = `0x00076A27` (block end plus mode 0's bits 0, 1), `0x1192C` = `0x0052A071`
(block end plus trim byte 5 = 1 in [2:0]), `0x11934` = `0x01C00000` (fraction `0xC00000`, bit 24 =
1 after the restart): each equals the replay of the block and the later steps. Not captured:
`0x11944`, `0x11968`. Per-boot variation on the same unit (this oracle runs the full trim at every
start): TX LO I [14:8] read 74, 75 and 77 and the VCO bank at divider 121 read 52 and 53 across
three captures [M:hw 2026-11-02]. The attenuation "after trim" values apply to a unit that ran the
full trim; a quick-path start keeps the block values (§10.1.6).

**PA trim bytes (record 107, 7 bytes) [M:s; defaults PUB]:** byte 0 → `0x11924` [26:24]; byte 1 →
`0x11928` [2:0]; byte 2 → `0x11928` [11:9]; byte 3 → `0x11928` [17:15]; byte 4 → `0x11928` [25:22]
(4 bits); byte 5 → `0x1192C` [2:0]; byte 6 → no register (LUT APC patch only, §5.1). Published
default when the record is absent: `{1, 7, 4, 7, 11, 1, 7}` [PUB `apps/demo/demo_wifi/board/wl82/board.c` L52];
the vendor radio's fields read exactly these values [M:hw].

**Target register values after the RF init completes (40MHz crystal, not this board) [M:s]:**

| Addr      | Name      | BLE residue  | WiFi target  | Delta        | Notes                           |
|-----------|-----------|-------------|-------------|-------------|----------------------------------|
| `0x11900` | WLA_CON0  | `0xEA9E0005` | `0xEA220005` | `0x00BC0000` | Clear 23,20,19,18; set 21. LNA/mixer BLE->WiFi |
| `0x11904` | WLA_CON1  | `0x01C40480` | `0x01C34880` | `0x00074C00` | Clear 18,10; set 17,16,14,11. Filter/bias |
| `0x11908` | WLA_CON2  | `0x9C010010` | `0x9C010010` | `0x00000000` | No net change |
| `0x1190C` | WLA_CON3  | unknown     | `0x672B9349` | --          | Filter/VGA/mixer |
| `0x11910` | WLA_CON4  | unknown     | `0x0508098E` | --          | Analog config |
| `0x11914` | WLA_CON5  | unknown     | `0x4078C319` | --          | Analog config |
| `0x11918` | WLA_CON6  | unknown     | `0x00076A24` | --          | Analog config |
| `0x1191C` | WLA_CON7  | unknown     | `0x2915C066` | --          | Analog config |
| `0x11920` | WLA_CON8  | unknown     | `0x00A641C0` | --          | Analog config |
| `0x11924` | WLA_CON9  | unknown     | `0x65B80057` | --          | PA trims |
| `0x11928` | WLA_CON10 | unknown     | `0x1AFAE7E7` | --          | PA config |
| `0x1192C` | WLA_CON11 | unknown     | `0x0052A070` | --          | Analog config |
| `0x11930` | WLA_CON12 | `0x0580065F` | `0x0D00025F` | `0x08800400` | Set 27 (40MHz); clear 23,10 |
| `0x11934` | WLA_CON13 | unknown     | `0x01B33333` | --          | PLL frac (WiFi 2.4GHz) |
| `0x11938` | WLA_CON14 | unknown     | `0x819FD028` | --          | PLL int=40, config |
| `0x1193C` | WLA_CON15 | unknown     | `0x12600EF2` | --          | PLL config |
| `0x11940` | WLA_CON16 | unknown     | `0x08B42451` | --          | PLL config (40MHz path) |
| `0x11944` | WLA_CON17 | unknown     | `0x00000A80` | --          | PLL config |
| `0x11950` | WLA_CON20 | --          | `0x00000000` | --          | Zeroed explicitly |
| `0x11954` | WLA_CON21 | --          | `0x000A2500` | --          | Analog frontend (40MHz) |
| `0x11964` | WLA_CON25 | --          | `0x00010000` | --          | Analog config |

**Bit-level delta for known-starting-value registers:**

WLA_CON0 (`0x11900`): switches LNA/mixer from BLE to WiFi receive path.
- Clear bits 23, 20, 19, 18 (BLE path enables)
- Set bit 21 (WiFi path enable)

WLA_CON1 (`0x11904`): adjusts filter bandwidth and bias for 20MHz WiFi channels.
- Clear bits 18, 10 (BLE narrow filter)
- Set bits 17, 16, 14, 11 (WiFi wide filter + bias)

WLA_CON12 (`0x11930`): configures PLL clock source for 40MHz crystal.
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
BT controller library]. The snapshot is a plain read of both words taken at the very end of the
WiFi hardware init, after the per-rate gain step; the BT controller writes both words back in
full (§9.3) [M:s].

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
- JieLi SDK: `https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK` (Apache-2.0 repository: the
  published application sources and headers cited [PUB] per §0; the pre-compiled radio libraries
  in it are not published source)
- `docs/hardware/wifi/CLEAN_ROOM_METHODOLOGY.md`: methodology, barrier, permitted references, licence record
- IEEE 802.11-2020: WLAN standard (MAC, PHY)
- FIPS 197 (AES), FIPS 180-4 (SHA-1), RFC 2104 (HMAC), RFC 2898 (PBKDF2): crypto standards
- IEEE 802.11i: WPA2/CCMP security

---

## 14. Facts awaiting hardware confirmation (2026-11-04)

Each row is a vendor behaviour the GPL driver currently lacks or does differently (§4.2.1 GPL
column), stated as register, value, position in the sequence of §4.2.1 and the observable effect
expected if the behaviour matters. Rows were ordered by how likely each explained the state before 12l (no frame queued, nothing
demodulated). **Status 2026-11-04 (pass 2):** TX works since 12l and RX since 12o [M:hw]; each row
now starts with its state: collected (values read), confirmed (effect reproduced against a
baseline with one change), confirmed in combination (effect seen only together with other
changes), applied (implemented, not measured), open (not implemented), with the observation that
decided it. The original ranking was an inference [I]; the register facts carry their own tags. "Read on
both" means: read the register on the running vendor radio and on core-fm1 before changing
anything; none of these registers has been compared so far unless stated.

| # | Register | Value | Position | Expected observable effect | Tag |
|---|----------|-------|----------|----------------------------|-----|
| 1 | **Confirmed 2026-11-04 12l [M:hw]: frame queued and transmitted.** WDMA TX image: first transferred word | queue word (bit 0 = WIV, bits [2:1] = QSEL = 2; 5 for management, 4 for data), then the 16-byte TXWI, the unpadded frame and 6 more bytes; `WDMA_TX_ADR` = address of the queue word, `WDMA_TX_LEN` = MPDU + 26 | every TX (§4.2.1 C7-C8, §7.11.5) | `TXRXQ_PCNT` byte 2 rises during the transfer and falls after; `TX_STA_CNT0` success or failure counts move; `TX_STA_FIFO` (`0x31718`) valid bit set; the probe request appears on a sniffer. The GPL's first word today is the software length (bits [2:1] of 56 = queue 0) and its TXWI is shifted by 4 bytes | M:s wl_rf_common.a, M:hw (buffer samples), I |
| 2 | **Collected 12l [M:hw]: `0x3043C` = `0x8400A0FE` ([7:5] = 7), `0x30434` = `0x22020602` (bit 18 clear), queue-2 pages 0 before and 1 right after the kick; the gate passed first try.** `0x3043C` [7:5], `0x30434` bit 18, `0x30438` byte 2 | vendor requires [7:5] != 0, bit 18 = 0 and byte 2 below the queue-2 limit before every TX | read immediately before each TX (C6) | Cheapest discriminating read for TX: if `0x3043C` [7:5] reads 0 on core-fm1 the packet buffer is not ready and no framing change can queue a frame; if it is non-zero the blocker is downstream (row 1). Read on both | M:s wl_rf_common.a, I |
| 3 | **Confirmed [M:hw 12l-12o]: with rows 1, 3-7 applied the receiver decodes frames on every channel.** MAC+`0x1328` TX_PIN_CFG | `0x00050302` (RT2800 bits: PA_PE_G0_EN 1, LNA_PE_A0_EN 8, LNA_PE_G0_EN 9, RFTR_EN 16, TRSW_EN 18) | at every channel switch, after the channel code (C1) and TX_BAND_CFG (C2), before the first probe request; never written earlier | RX: PLCP/CRC error counts in `RX_STA_CNT0` become non-zero, beacons and probe responses arrive (bit 17 of `WDMA_CTL`, interrupt 52). TX: PA enabled on air. Read on both first (expected vendor `0x00050302`, core-fm1 the reset value) | M:s wl_wifi_sta.a, IP:RT2x00 for bit names, I |
| 4 | **Confirmed with row 3 [M:hw 12l-12o].** MAC+`0x132C` TX_BAND_CFG | read-modify-write: bits [2:1] cleared, bit 2 set (2.4 GHz band) | every channel switch, directly before TX_PIN_CFG (C2) | band select for TX and RX paths [IP:RT2x00]; same effects as row 3 if the reset value selects the other band. Read on both first | M:s wl_wifi_sta.a, IP:RT2x00, I |
| 5 | **Confirmed in combination [M:hw 12m-12o]: with the channel code as the only per-channel RF write, frames decode on channels 3-11; R106 stays 4; the MMIO PLL words stay at divider 121 / the init bank on both units.** `0x1405C` [7:0] | channel + 101 (one read-modify-write, no delay); with no BBP R94 write, no BBP R106 write (R106 stays 4), no `0x1193C`/`0x11934` write | every channel switch (C1), first after the scan starts | BBP R106 reads 4 throughout a scan (core-fm1 reads the channel number today); the RF die retunes from the LUT entry for the channel; demodulation on channels other than 6; the two `WF_CON6` codes change with the channel [I] | M:s wl_rf_common.a, M:hw (R106), I |
| 6 | **Confirmed in combination [M:hw 12n-12o]: a flat bank table (bank 15 everywhere, 12m) gave no reception; the measured table (12n) gave reception on every channel (12o); the necessity of the bank field alone was not isolated (§15 T3).** RF-die LUT word 1, entries 0-127 | `\|=` bank << 9, bank = lookup(word 1 [28:21]) in the 48-entry limit table built by the bank scan of §5.2 | LUT load (A20), after PHY init #2, CFR mode 1 and RAPP, before auto control | With auto control on, the synthesizer runs in the right VCO band for the selected channel; candidates for `WF_CON4` b23 / `WF_CON8` b23 (lock or ready) asserting and for the `WF_CON6` codes leaving `0xFF` [I] | M:s wl_rf_common.a, I |
| 7 | **Applied at HEAD (LUT load and auto control repeated after the MAC init, so no reset follows them); not separately measured (§15 T4).** End of the front-end init | order PHY init #2 → CFR mode 1 → RAPP → LUT load → auto control → power → channel, and **no** MAC/BBP reset (MAC_SYS_CTRL 3, MAC+`0x30C` pulse, R21 pulse) or PHY init after the LUT load and auto control | A17-A23 | Read the four indicators after each of A20, A21 and A23: if one asserts, the step that raised it is the missing condition. A reset after auto control may stop the RF-die sequencer [I] | M:s wl_rf_common.a, I |
| 8 | **Open:** the GPL calibrates with the 40 MHz words, then forces fifteen RF registers to the vendor unit's read-backs (§11.2.9 marks the unit-specific ones); no quick path (§15 T6). RF calibration settings | 24 MHz crystal type (`0x11930` [27:23] = `0x0B`), N = 101, fraction `0xC00000`, `0x11954` = `0x02822520` during calibration; or the quick path from VM 187 when its CRC-16 is valid | A16 | RX DC values in the LUT AGC words and TX IQ/DC values in window D taken under the board's real reference clock; RX sensitivity and AGC behaviour closer to the vendor (R32-R46 values) | M:s wl_rf_common.a, M:hw (crystal type), I |
| 9 | **Open (missing at HEAD; §15 T8).** MAC+`0x1800` + 8i (i = 0..253); MAC+`0x6800` + 4i (i = 0..255); MAC+`0x7000`..`0x700C`; MAC+`0x1114` | `0xFFFFFFFF`, `0x00FFFFFF`; 1; 0; `&= ~0x001F0000` | interface up (B3, B5, B6), before the first TX | The broadcast/unassociated WCID (`0xFF`) used by the probe request has a defined table entry; TX status for WCID `0xFF` frames [IP:RT2x00, I]; TSF/beacon sync off during the scan | M:s wl_wifi_sta.a, IP:RT2x00, I |
| 10 | **Implemented at HEAD 76da132d (12p): no write to `0x14000` on the boot path; the plain-boot scan result is the §15 T1 regression check, not yet in the observation record.** `0x14000` WL_CON0 | never written: no `0xFFFF`, no later 0; clock tree (A2-A3) built before the first MAC access | whole start | MAC/BBP readable from power-on with `WL_CON0` = 0; results identical to the released case of 12j/12k. Confirms that the v13 fault came from a missing clock tree, and removes the BT/WiFi arbitration side effects of `0xFFFF` | M:s all libraries, M:hw (vendor 0), I |
| 11 | **Open (the GPL writes 0 and 4; §15 T7).** MAC_SYS_CTRL (MAC+`0x1004`) | only 3 (with the `0x30C` pulse) and `0xC`; never 0 or 4 after the last PHY reset | A9, A10, A13, end of A15/A17; nothing in phase B | No transient TX/RX disable after init; effect unknown | M:s wl_rf_common.a, I |
| 12 | **Open (the GPL uses post-association values; §7.10 now gives the interface-up and association sets; §15 T9).** MAC configuration values | table of §7.10 (e.g. `0x31408` = `0x13F`, `0x31400` = `0x17F97` from B12, `0x31350` = `0x1020`), no write to MAC+`0x1200` or MAC+`0x137C` | B2, B12 | Unlikely to block a scan; RX filter differences (DROP_UC_NOME, DROP_BA, DROP_BAR) do not affect beacons [IP:RT2x00, I] | M:s wl_wifi_sta.a |
| 13 | **Open; §6.9 now gives both coefficient sets and the curve (the GPL writes a rising ramp and the thresholds at the wrong entries; §15 T10).** Window D CFR mode 1 and RAPP | §6.9 values (tap counts 15/15/0, per-rate gains, RAPP curve 511 → 271, thresholds 160/160/400, mode 2) | A18, A19 | TX spectral quality and power; not expected to change "never queued" [I] | M:s wl_rf_common.a |
| 14 | **Open (the GPL sends TXOP 0, density 5, PacketId 0; §15 T11).** Management TXWI W0 TXOP | 3 | every management TX | Unlikely to matter: the vendor's QoS-null and data TXWIs use TXOP 0 and are sent [M:hw] | M:s wl_wifi_sta.a, M:hw |
| 15 | **Open (the GPL makes one write of `0x00010E37`; §15 T12).** WDMA init | nine `WDMA_CTL` and nine `WDMA_CFG0` read-modify-writes before any MAC reset; `WDMA_CTL` bit 16 never written | A5-A7 | Unlikely to matter (final values equal; bit 16 is read-only) | M:s wl_rf_common.a, M:hw |
| 16 | **Done 12o [M:hw]: the handler masks `0x14050` bit 0 and touches no MAC register; a handler that re-armed MAC+`0x328` plus an unbounded drain stalled the CPU until the watchdog (12n).** Interrupt handler | clears `WF_CON4` bit 0 and wakes the receive task only; no access to MAC+`0x328` | after B13 | None until a frame arrives; avoids disturbing the receive-notify source | M:s wl_rf_common.a |
| 17 | **Open (missing at HEAD; §15 T20).** Station slot 1 and the BSSID registers before the authentication request | MAC+`0x1808`/`0x180C` = access point; `0x1010`/`0x1014` = BSSID | join (D4-D5) | none expected for the ACK itself [I]; required for the key install (slot 1 attribute and IV, D26) and for the hardware to decrypt unicast data | M:s wl_wifi_sta.a |
| 18 | **Open (diff at HEAD; §15 T21).** Unicast management TXWI and header | TXOP 3, density 0, PacketId 0, duration 304 µs; a deauthentication before the authentication | D6-D7 (§7.11.5) | if the access point then acknowledges (TX_STA_CNT1 [15:0] = 1), the frame format was the cause | M:s wl_wifi_sta.a |
| 19 | **Open (§15 T19).** Transmit decodability: no frame addressed to the GPL driver's own address has ever been received (no probe response, no ACK) [M:hw 2026-11-05]; the address the access point answered belongs to another station (§7.13.1) | TX back end (A18, A19, §6.9) and the §7.11.5 differences | before any Phase D work | a probe response to a directed probe request | M:hw, PUB (OUI registry), I |

**Not worth trying** (no vendor counterpart) [M:s]: writing the four indicator bits, writing
`0x14054`, `0x14058` or `0x14060`, any write to `0x40000`-`0x40FFF`, any write to BBP R32-R63 or
R94, an MCU command or firmware download, polling an indicator as a start condition.

---

## 15. Hardware tests for the implementer (2026-11-04)

Each test is a hardware experiment: prerequisites, register actions, what to record, and how to
read the result. The implementer designs the code and the logging; nothing here is a source
change recipe. Vendor-side values to compare against are in §4.2, §4.3, §6.9 and §11.2.9 [M:hw];
where a value is marked "vendor: not captured", the reader supplies it from the vendor unit (the
implementer never runs the vendor build). Record every run as an observation file with the
decision taken, the build id, and the values read; a result is "confirmed" only when the effect
reproduces against a baseline with one variable changed, "confirmed in combination" when several
changes were made at once, "collected" when only values were read.

Common to every test: sample the four indicators (`0x14050` bit 23, `0x14060` bit 23, `0x14058`
[23:16] and [7:0], `0x30F04` bit 16) at the existing init boundaries and at the boundary the test
names; scan with a known access point on channel 6 unless stated; the driver state referred to as
"HEAD" is commit 76da132d (12p).

**T0 (no flash): read the 12l-12o logs.** Already decided: `0x3043C` [7:5] = 7 and `0x30434`
bit 18 = 0 before TX (gate passes), queue 2 shows one page after the kick, `TX_STA_CNT1` counts a
frame, beacons decode on channels 3-11 [M:hw]. Remaining T0 decisions: none.

**T1 (regression, implemented at HEAD): plain boot never writes `0x14000`.** Boot with the clock
tree (§4.2.1 A1-A4) before the first MAC access and no write to `0x14000`; read `0x31000` right
after A3 (expect `0x00300100`, no fault) and `0x14000` at every boundary (expect 0); then the normal
init and a scan from the panel without any host probe. Expect the 12o readings. Decision: no
fault → record [M:hw] that the 2026-10-28 fault came from a MAC read before the clock tree and
keep this as the base of every later test; fault → report the boundary. The plain-boot scan result
is not in the observation record at the evidence cutoff: run this first.

**T2 (superseded by T3, implemented at HEAD).** Kept for the record: OR a constant bank `0x35`
into word 1 bits [15:9] of every FEQ entry and compare the indicators and `RX_STA_CNT0` during a
dwell. Not needed now that T3 is in.

**T3 (implemented at HEAD, result to record): bank-limit table.** Build the 48-entry table of §5.2
during the RF init in the 24 MHz PLL state with `0x11950` = 0 and `0x11938` bits 26/27 clear, patch
every FEQ entry, and log the top bank, the 48 limits and lookup(121). Recorded so far: top 47,
lookup(121) = 43 (vendor 53), limits 71..169 [M:hw 12n]; a scan with the 40 MHz words gives a flat
table (top 15, every limit at the ceiling) [M:hw 12m]. Open: explain the 10-bank difference
(§16 U11). A selective reversal (the 12n table against a flat one, everything else equal) would
turn "confirmed in combination" into "confirmed" for §14 row 6.

**T4 (§14 row 7): no reset after auto control.** Order the end of the init as §4.2.1 A17-A23 (PHY
init #2, TX filter mode 1, compression curve, LUT load, auto control, power, channel) with no
MAC_SYS_CTRL = 3, MAC+`0x30C` pulse, R21 pulse or PHY init after the LUT load. Sample the
indicators after A20, A21, A23 and after every later init stage. Expect an indicator to assert and
stay. Decision: asserts and stays → row 7 confirmed; asserts then drops at a later stage → that
stage is the disturbing step; never asserts → ordering is not the condition (and §16 U7 stays open).
HEAD approximates this by repeating LUT load + auto control after the MAC init; the vendor order is
the cleaner experiment.

**T5 (implemented at HEAD, per-switch reads to log): channel code only.** A channel switch is one
read-modify-write of `0x1405C` [7:0] = channel + 101, no delay, no BBP or PLL write. After each
switch read `0x1405C` (expect channel + 101), BBP R106 (expect 4), `0x1193C` and `0x11938` (expect
unchanged: 121 and the init bank), `0x14058`. Scan channels 1-13 with the access point on a channel
other than 6. Recorded: frames on channels 3-11 with no PLL write [M:hw 12o]. Decision: already
"confirmed in combination"; log the per-switch reads once to close §14 row 5 fully.

**T6 (§14 row 8): stored record and 24 MHz calibration.** Requires §10.2 (now present). Two parts
in one flash: (a) read the 68-byte record 187 from the store and log the validity check of §10.2
and the raw bytes; (b) if valid, apply the quick path of §10.2 (including D'[9..`0x0C`] and the
four RX DC pairs into the LUT AGC words) instead of a full trim; if invalid, run the full trim of
§10.1 with the 24 MHz crystal type `0x0B`, N = 101, fraction `0xC00000`, `0x11954` =
`0x02822520` (no 40 MHz words anywhere), and remove the forced vendor read-backs of the unit
results (§11.2.9 marks them). Read BBP R32-R46 and R4 after init. Expect R32-R46 near the vendor
(`0x0B 0x0E 0x1F 0x04 0x09 0x38 0x07 0xBA 0x0F 0xC4 0x01`), R4 = 0, `RX_STA_CNT0` non-zero during
a dwell, and the §10.1.6 "after trim" attenuation values when the full trim ran. Decision: valid
record → the quick path is the vendor path on this unit, keep it; invalid → the 24 MHz full trim
becomes the default; no change in reception either way → "no effect observed under these
conditions" (the forced read-backs were already this unit's results).

**T7 (§14 row 11): MAC_SYS_CTRL never 0 or 4.** Remove every write of 0 and 4; the last write is
`0xC` at the end of the PHY init. Read MAC_SYS_CTRL at every boundary (expect `0xC` from the PHY
init on). Decision: any change in `TXRXQ_PCNT` / `TX_STA_CNT1` behaviour → record; none → keep the
vendor behaviour.

**T8 (§14 row 9): WCID, attribute and key tables, beacon sync off.** At interface up write
MAC+`0x1800` + 8i = `0xFFFFFFFF` then `0x00FFFFFF` for i = 0..253, MAC+`0x6800` + 4i = 1 for i =
0..255, MAC+`0x7000`..`0x700C` = 0, MAC+`0x1114` `&= ~0x001F0000`. Read back `0x31800`/`0x31804`,
`0x31FE8`/`0x31FEC`, `0x36800`, `0x31114`. Note that the vendor never writes entries 254 and 255
and never uses WCID `0xFF` as a table index (§4.2.1 B3): keep both readings open (special "no
station" selector vs ordinary index) until measured. Decision: observe `TX_STA_FIFO` validity for
WCID `0xFF` frames and `TX_STA_CNT0`; no conclusion without a queued frame.

**T9 (§14 row 12): MAC table as the vendor writes it.** The 22 values of §4.2.1 B2, no write to
MAC+`0x1200` or MAC+`0x137C`, RX_FILTER_CFG `0x17F97`, EDCA as §7.10's interface-up set. Read back
`0x31200` (vendor `0x00020603` / `0x00030001`: hardware busy bits) and `0x31400`. Expect no change
in scan results. Decision: change → report; none → keep the vendor values.

**T10 (§14 row 13): TX filter mode 1 and the compression curve.** Requires §6.9 (now complete).
After PHY init #2: the 57 mode-1 writes, then the curve load and thresholds/powers of §6.9 with
mode 0 before and mode 2 after. Read back D[`0x4D`..`0x4F`] (expect 15, 15, 0), the odd entries
D[`0x0B`..`0x21`] (expect the mode-1 gains, or the published per-rate defaults if record 108 is
applied) and D[`0x51`..`0x5E`] (expect `BE 64 2D 64 2D D6 3F 40 A0 A0 90 E0 7F 0F`). Expect a TX
power/spectrum change only (sniffer RSSI). The vendor unit's read-back already matches these
finals [M:hw 2026-11-03]; the test confirms the GPL load reproduces them.

**T11 (§14 row 14): management TXWI as the vendor builds it.** TXOP [9:8] = 3, MPDU density 0,
BA window 7, PacketId = MCS[3:0], NSEQ 0, WCID `0xFF`, ACK 0 for broadcast (§7.11.5). Expect no
change unless TX is queued but never completes. Also test the probe rate: OFDM 6 Mbps (PHYMODE 1,
MCS 0) against CCK 1 Mbps; both should be accepted (§16 U8).

**T12 (§14 row 15): WDMA init as nine and nine read-modify-writes** directly after the clock
tree, before any MAC reset; never write bit 16. Log `WDMA_CTL` after each step; expect final `0x37`,
bit 9 reads 0 after being set, bit 16 = 0 (status). Low yield; keep.

**T13 (implemented at HEAD 12o): interrupt handler.** The handler clears `0x14050` bit 0 and
flags; no MAC+`0x328` access; the receive path re-opens bit 0 after draining. Remaining check:
read MAC+`0x328` at each boundary (expect 2 after B13) and find out why interrupt 52 never fires
on core-fm1 although the line is registered and bit 0 is set (§16 U4): try registering the line
after MAC+`0x328` = 2 as the vendor does, and compare the interrupt controller's enable/priority
registers for line 52 against the published header's semantics.

**T14 (mutating, piggyback on any flash): window C' and D' contents.** After init read all 72 C'
entries and D' entries 0..`0x1C` through their read-back registers (poll the port busy bit before
each read). Expect the §6.6 values with C'[`0x3C`] = 1 and D'[9..`0x0C`] = calibration results.
Then write one C' entry, pulse R21 bit 0, read it back (expect preserved). Mismatch → the window
write path or a reset clears windows; report entries.

**T15 (mutating, piggyback): acknowledge semantics.** After one TX: read `WDMA_CTL` bit 20
(expect 1), write bit 10, read again (expect 0); the same for bit 21 / bit 11 once a frame arrived.
Records [M:hw] whether bits 10/11 are write-1-to-clear for 20/21 (§7.11.3 infers it from the
vendor's poll order).

**T16 (piggyback): front-end register reset values.** Before the first channel switch read
MAC+`0x1328` and MAC+`0x132C` (vendor: not captured; the reset value of `0x132C` bit 0 is §16 U1);
after each switch expect `0x00050302` and bit 2 set, bits [2:1] = 10.

**T17 (settled by the reader, no flash needed): PBF_MAX_PCNT.** The running vendor reads
`0x1F3F7F9F` because the association path writes it (TX burst off); the hardware retains bit 15
(§7.10). Use byte 1 = `0x7F` as the queue-2 page limit after association and `0xBF` before.

**T18 (dropped).** The direct divider write no longer exists at HEAD.

**Phase D tests (added 2026-11-05; T17 and T18 were already taken, so these start at T19). Order:
T19 first (it decides whether any Phase D register matters yet), then T8, T20, T21, T22, T23, T24,
T25; T26 last.**

**T19 (first; no association needed): are the GPL driver's transmissions decodable?** On the
channel of a known access point, with RX_FILTER_CFG bit 2 clear, send directed probe requests
(addr1 = addr3 = the access point, its SSID in the element) once per second, alternately at CCK
1 Mbps and OFDM 6 Mbps (PHYMODE 1, MCS 0), and count, within 50 ms of each request, probe
responses whose addr1 equals the driver's own address; log beacons from the same access point as
the control, and TX_STA_CNT0/1 after each request (a directed probe request with the ACK bit set is
acknowledged by the access point). Decision: responses or an acknowledgement at either rate → the
transmissions are decodable, continue with T8/T20/T21; none at both rates with beacons arriving →
the transmit chain is the blocker: compare the window D state of the running build with §6.9
(T10) and the TX gain fields of §4.2.1 A18/A24 before any Phase D register work. Record the rate at
which responses appear (§16 U8, U21).

**T8** as given above (tables as B3/B5/B6), run before T20.

**T20 (§14 row 17): join-time writes.** At connect, before the authentication request, write slot 1
(MAC+`0x1808` = BSSID bytes 0-3, `0x180C` = bytes 4-5, [31:16] = 0) and the BSSID registers (D4-D5);
read back `0x31808`/`0x3180C` and `0x31010`/`0x31014`. Decision: no behavioural change is expected
for the ACK; the slot is needed by T24.

**T21 (§14 row 18): unicast management frames as the vendor builds them.** One variable per flash,
in this order: (a) TXWI TXOP [9:8] = 3 and density 0 for the authentication request; (b) header
duration = 304 µs for unicast management frames at CCK 1 Mbps (50 at OFDM 6 Mbps); (c) a
deauthentication (reason 3) to the access point immediately before the authentication request;
(d) authentication timeout 800 ms with one repeat, association timeout 300 ms. After each flash read
TX_STA_CNT0 [15:0] and TX_STA_CNT1 after the authentication request. Decision: TX_STA_CNT1 [15:0]
= 1 → that variable was the cause; record it in §14 row 18.

**T22 (§16 U15): TX status for packet id 0.** Send the authentication request with TXWI PacketId
[31:28] = 1 (then 0 as the baseline) and poll TX_STA_FIFO bit 0 within 20 ms. Decision: an entry
appears only with packet id 1 → the hardware suppresses reports for packet id 0 (and the vendor's
management frames are therefore never reported); entries in both cases → U15 closed.

**T23 (D11-D21): link-up set.** After the association response write D12-D21 (EDCA from the
beacon's WMM set, protection, basic rates with the 6/12/24 Mbps rule, fallback tables,
MAX_LEN_CFG from the A-MPDU factor, PBF_MAX_PCNT `0x1F3F7F9F`, BCN_TIME_CFG with the beacon
interval and `0x0B` in [20:16], RX_PARSER_CFG). Read back `0x31114`, `0x31408`, `0x31018`,
`0x3040C`, `0x31418`, `0x31354`-`0x31360`, `0x31300`-`0x3130C`; expect the §4.2.1 D values and
the running vendor's steady state (§7.10). Decision: TSF ticking (`0x3110C`/`0x31110` advance)
and beacon reception unchanged → set kept; any regression → report the register.

**T24 (D26-D27): hardware key install.** After message 4/4 is handed to the MAC: shared-key slot
(0, 0) with the TK (16 bytes, 8 + 8 MIC bytes as supplied), SHARED_KEY_MODE [3:0] = 4, IV/EIV of
slot 1 = `0x20000001`/0, attribute of slot 1 = `0x8`; the group key from the GTK element into slot
(0, id) with its nibble. Read back `0x36C00`-`0x36C1F` (expect the key bytes in little-endian words),
`0x37000`, `0x36008`/`0x3600C`, `0x36804`. Then send one unicast data frame with the TXWI of
§7.11.5 (data, WCID 1, WIV 0) and no software encryption: expect the access point to acknowledge
it and to answer a ping; expect received unicast data to arrive decrypted (the RX trailer's
decrypted/cipher-error bits of §7.11.6). Decision: both work → software CCMP retired; TX works, RX
does not → compare the attribute bits [9:7] (the Linux driver writes the cipher there, the vendor
does not); nothing → keep software CCMP and report.

**T25 (§7.13.1): the station address.** Drop the override and the random fallback; derive a
stable locally-administered address from the flash unique id (the published rule is citable). Ask
the router's client list which device carries `D8:C8:0C:F7:36:E2`. Decision: a Tuya device → §14
row 19's address part is closed; nothing → report (the registry assignment stands either way).

**T26 (D31, optional): link-down writes.** On disconnect perform D31 and read back `0x31114`,
`0x31018`, `0x36804`, `0x37000`; keep the GPL's BSSID clearing and RX filter restore as its own
choice (the vendor does neither).

**Longer observations (not one flash):** (a) an indicator timeline over a vendor-order build
(T1 + T4 + T5 + T6) to find the first boundary where each indicator asserts (§16 U7); (b) a full
register sweep (`0x11900`-`0x119AF`, `0x14000`-`0x140FF`, `0x30000`-`0x31FFF`, BBP R0-R127 and all
four windows) of the GPL build against the vendor unit's sweep, which the reader captures; (c) the
elapsed time of the clock-tree, PLL-restart and loop-pulse delays measured with the cycle counter
(§16 U5).

---

## 16. Unspecified steps, unknowns and housekeeping (2026-11-04)

**Supported profile of this revision:** FM-1 board, 24 MHz crystal, WiFi-only cold start, station
mode: scan, authenticate, associate, data. Transitions outside it are not characterised and are
excluded until measured: warm restart of the WiFi block, repeated start/stop, sleep/wake (the
sleep registers are listed in §7.11.9 as not on the start path), and a Bluetooth start before or
alongside WiFi (U9).

**16.1 Steps not yet specified.** As of this revision every step of §4.2.1 A0-C8 and D1-D32, §11.2.1 1-16,
§10.1 and §10.2 has its registers, values (or generating rule) and order in this sheet. No step is
blocked. Rows are added here, with a stable id, if a later pass finds a gap; resolved rows stay
with their resolution.

**16.2 Unknowns.** Each has a stable id; "blocking" means a feature of the supported profile
cannot be claimed until it is resolved; "optional" means the profile works without it.

| Id | Unknown | Class | Cheapest route |
|----|---------|-------|----------------|
| U1 | Reset value of MAC+`0x132C` bit 0 (HT 40 MHz select); the vendor never writes it in 20 MHz operation | optional | T16 |
| U2 | Meaning of MAC+`0x434` bit (8q + 2) and MAC+`0x43C` [7:5]; values collected (`0x22020602`, `0x8400A0FE`) [M:hw 12l] | optional | longer observation (b) |
| U3 | Whether the compression curve RAM can be read back (the vendor never reads it; D[`0x5E`] returns the last data byte written [M:hw]) | optional | T10 variant: set D[`0x5D`] to an address and read D[`0x5E`] |
| U4 | Why interrupt 52 never fires on core-fm1 (line registered, `0x14050` bit 0 set; polling carries reception) [M:hw 12o] | optional (polling works) | T13 |
| U5 | Real durations of the vendor's NOP-loop delays (§7.11.9): the nominal 1/1/1/50, 5/5/100, 80 and 1000 µs are approximate on the vendor too | optional | longer observation (c) |
| U6 | Cache coherency of the WDMA buffers: **narrowed 2026-11-05** (§7.13.6): the vendor keeps both buffers in internal RAM and does no cache maintenance; the published cache model gives SDRAM an uncached alias and internal RAM none. Closed for internal-RAM buffers (the GPL's case); an SDRAM-resident buffer would need line flush/invalidate [I] | closed for the supported profile | none |
| U7 | What asserts the four read-only indicators (§4.2) | optional | longer observation (a) |
| U8 | Whether the vendor's first probe request is sent at OFDM 6 Mbps or CCK 1 Mbps (profile-dependent, §7.11.5); the MAC accepts CCK 1 Mbps [M:hw] | optional | T11 |
| U9 | Coexistence arbiter programming (`0x14004` tables, `0x14000` bits 0-1) when BT runs too | blocking for BT + WiFi only | out of scope of this profile |
| U10 | Whether the TX IQ gain/phase fields accept the full ±423 / ±200 ranges: the vendor applies winners aliased to 8 bits (§10.1.9) | optional | T6 (full-trim run) with the winners logged |
| U11 | The GPL's bank table differs from the vendor's (43 vs 53 at divider 121) though reception works [M:hw 12n-12o] | optional | compare the 48 limits against the vendor unit's (reader capture) |
| U12 | Semantics of MAC+`0x308` = `0x9407`, MAC+`0x30C` [4:1], MAC+`0x324` = 2, MAC+`0x328` = 2 | optional | bit ablation after reception works |
| U13 | Whether the hardware drops bit 15 of `0x3040C`: no, software rewrites it (§7.10); closed 2026-11-04 | closed | |
| U14 | WCID `0xFF`: **narrowed 2026-11-05** (§7.13.2): a TXWI literal for frames sent without a station entry, including the vendor's acknowledged unicast management frames; slot 255 never written, attribute 255 = 1 with all others. Whether the hardware indexes the tables with `0xFF` is unmeasured and not needed for Phase D | optional | T8 |
| U15 | Whether TX_STA_FIFO reports frames queued with packet id 0 (the vendor's 1 Mbps management frames carry 0 and their entries, if any, are discarded; the Linux driver never uses 0) | optional | T22 |
| U16 | Meaning of MAC_STATUS_CFG `0x1200` bits [11:8] and [17:16] (the GPL reads `0x00030F00` after an erroneous write; no vendor access) | optional | none; stop writing it |
| U17 | RX_PARSER_CFG `0x1418`: reset value and effect; the vendor writes 1 only for an infrastructure WMM link with TKIP, 0 otherwise (D21), so a CCMP link gets 0 | optional | T23 read-back |
| U18 | Whether a queued EAPOL message 4/4 can still be encrypted by the MAC when the pairwise key is installed before it leaves (the vendor installs after the DMA hand-off without waiting for TX status) | optional | T24 ordering |
| U19 | Unit of TX_TIMEOUT_CFG [15:8] (RX ACK timeout 18; "slot" per the Linux field name) | optional | none needed |
| U20 | The stock FM-1 firmware's WiFi address source (a stored record, the published generator, or another rule); the two known sources are in §7.13.1 | optional | router client list; stock firmware record dump |
| U21 | Why no frame addressed to the GPL driver's own address has ever been received (no probe response over every scan, no ACK). **Root cause identified 2026-11-06** (§10.1.14, T25b): `0x11900` never switches from RX to TX mode. The RF analog TX path (PA, DAC, TX mixer) stays off during transmit. Cause: ordering bug in `wifi_rf_arm()` where `wifi_cal_rf_mode(0)` runs after `wifi_rf_auto_control()`, overwriting the G4 enable bits in `0x11924` and `0x11938` that the auto-switch hardware needs. The vendor sequence (A16→A20→A21) runs mode(0) before auto_control. Fix: swap the order in `wifi_rf_arm()`. `0x11950` = 0x57BFFFDC is correct. | **blocking** (association) | T26: apply ordering fix, verify 0x11900 changes during TX |

**16.3 Housekeeping for the implementer (barrier-clean; file:line as of HEAD 76da132d).**

Citations in the driver that do not resolve to the section they mean (every `§` citation in code
must point at a heading that supports the claim; creating §15 and §16 in this revision does not
make the old `§15`-`§18` citations valid):

| file:line | cites | should cite |
|-----------|-------|-------------|
| wifi_rf.c:59, :988 | "§17 step 27" | §11.2.11 (TX LO snapshot) |
| wifi_rf.c:661, :683, :697, :711 | "§15" (LUT patches) | §5.1 |
| wifi_rf.c:938 | "§17 steps 12-16, §18" | §11.2.1 steps 12-16, §10.2 |
| wifi_rf.c:974 | "§14.10" (RF mode 0) | §10.1.1 |
| wifi_sdio.c:626, :637, :647, :720 | "§18" (record 187) | §10, §10.2 |
| wifi_sdio.c:712 | "§17 step 19" | §11.2.1 |
| wifi_sdio.c:590, :603, :613, :619, :717 | "§7" (compression) | §6.9 |
| wifi_sdio.c:551 | "§6" (TX filter) | §6.9 |
| wifi_sdio.c:572 | "DISCOVERY §11.2.12" | §6.6 step 7 (and the DISCOVERY reference is not a public document: remove) |
| wifi_rf_cal.c:333 | "§10.1/§11.1" (bank scan) | §5.2 |
| wifi_mac.c:424 | "§3.20.3.5" | the RT5350 datasheet section, say so |

Identifiers to replace (the reader's scan; replacement names are suggestions in this sheet's
vocabulary; the reason is provenance hygiene, not function):

| file:line | replace with |
|-----------|--------------|
| wifi_rf_cal.c: every `wcal_`-prefixed function, macro, enum and variable (:9-:12, :16, :18, :43-:103, :105, :119, :138, :148, :165, :172, :181, :200-:338, :351-:393, :433, :475, :495, :536, :599-:684) | a `wifi_cal_` prefix with the §10.1 names: rf_mode (:119), gain_step (:148), highpass_open/close (:165/:172), loopback_attenuation (:181), measurement (:227), adc_peak (:261), gain_autotune (:271), tx_lo_i/q (:289/:290), rx_dc_analog (:294), divider_code (:335), vco_bank (:336), pll_restart (:338), comparator_read (:351), detector (:357), mid_bank_search (:359), bank_lookup (:384), rc_tune (:433), tx_lo_search (:475); the table at :105 → rf_mode_table (§10.1.1); the table at :138 → generate from the §10.1.2 rule; :393 is unused and can go |
| wifi_rf_cal.c:225 (comment) | describe the measurement engine of §10.1.3 without the name |
| wifi_rf_cal.c:4, :100, :136 (fm1-base references) | remove; cite §10.1 |
| wifi_rf.c:63, :284, :989-:992 | `wifi_rf_tx_lo_snapshot` (§11.2.11) |
| wifi_rf.c:208, :621, :961 and wifi_rf_lut_data.c:2 ("RFD RAM") | "RF-die LUT" (§5) |
| wifi_rf.c:643, :616, :964 | `wifi_rf_lut_load` |
| wifi_rf.c:709 (stale comment) | remove |
| wifi_rf.c:838, :842, :968 | `wifi_rf_auto_control` (§11.2.11) |
| wifi_rf.c:978 | the renamed rf_mode (0) |
| wifi_sdio.c:672, :726 (comments naming vendor routines) | describe by §4.2.1 row (A2-A4; B13) |

The terms RAPP (TX soft compression) and CFR (TX filter) are defined in §6.9 and may stay. The
published register names (`WF_CONn`, `WL_CONn`, `WLA_CONn`) are public [PUB] and may be used.

Other housekeeping: `tests/wifi/test_rf_init.py` reads vendor intermediate representation from the reader's private store (two
tests): writer-side tests must not depend on reader material; remove them. The verbatim 128-word
AGC table in wifi_sdio.c:433-466 is to be generated from the §6.7 rule. The forced vendor
read-backs in wifi_rf.c:279-296 contain this unit's results (§11.2.9 marks them) and are retired
by T6.
