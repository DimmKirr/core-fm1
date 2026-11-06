# WiFi Implementation Plan (core-fm1)

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.
> Hardware-in-the-loop tasks (flash, observe) are performed by Dmitry; Claude prepares the builds,
> the readback code, and the observation write-ups.

**Goal:** a GPL-3.0 clean-room 802.11 STA in core-fm1 that scans, joins a WPA2-PSK AP, gets a DHCP
lease, and serves the Weather page over HTTP, with no vendor binaries in the shipped firmware.

**Architecture:** keep the existing clean-room driver (`firmware/src/wifi/`, `CONFIG_WIFI`) as the
product. Use the fm1-base vendor build on the same FM-1 unit only as a black-box hardware oracle
(register, counter and memory snapshots over its HTTP debug endpoint). Route every vendor-specific
fact through `docs/hardware/wifi/SPECSHEET.md` via the analyst (`cleanroom-analyze`); the
implementer codes from the SPECSHEET plus the public RT2800 Linux driver, lwIP and wpa_supplicant.

**Tech Stack:** JieLi AC791N / WL82 (RT2800-lineage MAC+BBP, JieLi WDMA, JieLi RF analog),
pi32v2 clang 4.0.1, unity build, cooperative scheduler, DAP log over USB SysEx (`task debug:*`),
fm1-base HTTP debug endpoint (`curl http://<fm1-ip>:8080/status`), Python unit tests, Playwright E2E.

---

## 0. Where we are (2026-11-02)

### What exists

| Layer | File(s) | State |
|-------|---------|-------|
| RF analog init, VCO, channel, LUT, auto-control | `wifi_rf.c`, `wifi_rf_cal.c`, `wifi_rf_lut_data.c` | Runs. All 22 RF registers read back equal to vendor, but 15 of them only because of hard-coded vendor overrides (`wifi_rf.c:824-844`). |
| PHY/MAC init, BBP tables, WDMA, TX/RX DMA | `wifi_sdio.c` | Runs. WDMA TX completes (bit 20), WDMA RX never asserts (bit 17). |
| MAC registers, frame builders, BSS table | `wifi_mac.c` | Runs. All compared MAC config registers equal to vendor. |
| STA state machine, scan loop | `wifi_sta.c` | Runs the 13-channel scan; finds nothing. |
| WPA2-PSK, CCMP | `wifi_wpa.c` (976 lines) | Compiled, never exercised on air. |
| lwIP (NO_SYS=1), DHCP, HTTP, weather | `wifi_lwip*.c`, `wifi_http.c`, `wifi_weather.c` | Compiled; weather page E2E-tested in the emulator only. |
| Vendor-binary backend | `firmware/src/wifi/nonfree/`, `configs/wifi_nonfree_defconfig`, `firmware/app_wifi.ld` | Scaffold only. Seven integration gaps, 7-14 days estimated (task note in the reader's private store). |
| Oracle | the vendor-firmware copy in the reader's private store (its WiFi example and shim) | Works on this unit: associates, DHCP, HTTP `/status` dumps 80 MMIO + 13 BBP values. |

### Hard evidence from 11 debug iterations

Observations: the observation store, 2026-11-01T11:20 through 2026-11-02T09:08.

- `TX_STA_CNT0/1` (`0x3170C/0x31710`) stay 0: the MAC never completes or fails a transmission.
- `RX_STA_CNT0/1` (`0x31700/0x31704`) stay 0: the PHY never reports a CRC error, PHY error,
  false CCA or PLCP error. A live receiver on a 2.4 GHz band with an AP at RSSI -65 would.
- `isr_n = 0`, WDMA bit 17 never set: nothing reaches the DMA.
- RF analog, MAC config, EDCA, protection, SIFS, rates: all match the vendor (iteration 11).
- BBP direct registers R66 and R82 read back 0 after being written 0x38/0x62 (iterations 5-6);
  never resolved. R1, R4, R31 survive.

### The three residual register diffs, re-read against the public RT2800 header

| Register | Observation | Public RT2800 meaning (`rt2800.h`) | Consequence |
|----------|-------------|-------------------------------------|-------------|
| `0x31200` "BASIC_RATE" | write ignored, reads 0x30F00 vs vendor 0x30601 | `MAC_STATUS_CFG` (line 1029): read-only status, bits [1:0] = BBP/RF busy TX/RX | Not a knob. The diff is a symptom of a different live MAC state. |
| `0x30F04` WDMA_CTL bit 16 | refuses writes in RMW and direct form | Spec marks bits 17/20/21 as hardware-set; bit 16 behaves the same way | Most likely a hardware status flag, not a configuration bit. Needs analyst semantics, not more write attempts. |
| `0x14050` bit 23 | one-sided, vendor has it set | JieLi, undocumented | Analyst question. |

Two more labelling errors in the comparison docs and code (fix in Phase 3):

- `0x30408` was labelled "TX_STA_CNT0 (TX works!)" in the vendor dump. It is `PBF_CFG`
  (`rt2800.h` line 582), a constant. The vendor dump has never shown real TX counters.
- `0x30300/0x30304` are labelled `MAC_ADDR_DW0/DW1` in `wifi_sdio.c:42-43` and SPECSHEET §7.2.
  The RT2800 MAC address is at `0x1008/0x100C` (header lines 742/756), which the vendor dump
  confirms (`0x31008 = 0x020B7F72` = MAC `72:7f:0b:02:…`). `wifi_mac.c:608` already writes
  the correct registers; only the names, the "OTP read" and the spec are wrong.

### What has never been compared

1. **The baseband (BBP).** 128 direct registers and ~350 window entries control CCA, AGC,
   demodulation and modulation. Both sides have read helpers, but the vendor-side reads raced the
   vendor driver and were discarded as unreliable. R66/R82 = 0 on core-fm1 is a known, open diff.
2. **The TX buffer the vendor actually hands to WDMA.** The spec says DMA starts at buffer+4
   (TXWI) and the 4-byte header is software-only [M:s]. The RT2870 USB lineage this MAC comes
   from expects a 4-byte TXINFO word (length, QSEL=EDCA, WIV) *in front of* the TXWI
   (`rt2800usb.h` lines 46-51). The two readings are indistinguishable from the static
   description and distinguishable in one hardware read.
3. **Behavioural MAC counters.** `TXRXQ_PCNT` (`0x30438`: packets queued per TX queue),
   `CH_IDLE_STA/CH_BUSY_STA` (`0x31130/0x31134`: CCA idle/busy time), `TX_STA_FIFO` (`0x31718`).
   Nobody has read them on either side. Together they separate "frame never entered the MAC"
   from "frame queued but CCA stuck busy".

---

## 1. Strategy

### Approaches considered

**A. Keep matching static register state.** Diminishing returns: 62 registers compared, 3 left,
two of them status bits. Not recommended as the main line.

**B. Differential behavioural debugging against the oracle (recommended).** Extend both debug
endpoints with the same counters, snapshot the BBP on both sides in a quiescent state, capture the
vendor's real TX buffer bytes and DMA start address, and send the open semantic questions to the
analyst. Each item is a day or less and each produces a decision, not a hunch. Time-box: 5 working
days with hardware access.

**C. Finish the vendor-binary port inside core-fm1 first.** 7-14 days before it yields any new
information, and fm1-base already runs on the same unit, so its unique value (vendor stack under
core-fm1's scheduler and audio load) is small until the open driver is at least transmitting.
Keep as the fallback, not the plan.

### Decision

B, with C parked behind an explicit trigger (Phase 1 exit criteria not met). The clean-room driver
is the only shippable artifact under GPL-3.0; everything else is instrumentation.

---

## 2. Phase 1: break the TX/RX deadlock (time-box 5 days)

Every task below ends in a new observation file in the observation store using the
existing format (build, result, tables, analysis, next steps) and a line in
the behavioural-diff task note (reader's private store).

### Task 1: Group and commit the current working tree (Dmitry)

**Why first:** 28 modified files, 3,144 insertions, mixing WiFi debugging, UI/demo work, tests and
docs on branch `feat/weather-page`. Debug iterations need clean diffs.

**Step 1:** run `/git-group-commit`. Expected groups: (a) WiFi RF overrides + MAC/WDMA fixes +
readbacks, (b) SPECSHEET and wifi.kconfig, (c) nonfree scaffold (`firmware/src/wifi/nonfree/`,
`configs/wifi_nonfree_defconfig`, `firmware/app_wifi.ld`, `tools/build.py`), (d) UI/demo/keyboard,
(e) tests, (f) Taskfile/AGENTS.

**Step 2:** do not commit `work/panel.ppm` (3 MB scratch) or the private stores.

### Task 2: Behavioural counters on the core-fm1 side

**Files:**
- Modify: `firmware/src/wifi/wifi_sdio.c:60-66` (defines), `:669-676` (inside `wifi_sdio_tx`)
- Modify: `firmware/src/wifi/wifi_sta.c:187-195` (probe block), `:579-628` (scan block)

**Step 1:** add the RT2800 diagnostic registers next to the counters in `wifi_sdio.c`:

```c
/* RT2800 MAC diagnostics [IP:RT2x00, rt2800.h lines 613, 1012, 1017, 1022, 1029, 1931] */
#define WIFI_TXRXQ_PCNT  (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x0438u)) /* [7:0] TX0Q [15:8] TX1Q [23:16] TX2Q [31:24] RX0Q */
#define WIFI_CH_IDLE_STA (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1130u))
#define WIFI_CH_BUSY_STA (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1134u))
#define WIFI_CH_BUSY_SEC (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1138u))
#define WIFI_MAC_STATUS  (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1200u)) /* [0] BBP/RF busy TX, [1] busy RX */
#define WIFI_TX_STA_FIFO (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1718u)) /* [0] valid, [5] success, [15:8] WCID */
```

**Step 2:** in `wifi_sdio_tx`, right after the completion poll loop and before the ack
(line 673), read the queue count while the frame can still be in the MAC queue:

```c
    if (tx_dbg < 3u)
        cfm1_log_int("wifi: qpcnt=", (int32_t)WIFI_TXRXQ_PCNT); /* >0 = frame reached a MAC TX queue */
```

**Step 3:** in `wifi_sta_send_probe_req` (inside the `scan_ch <= 2` block) add:

```c
        cfm1_log_int("wifi: mstat=", (int32_t)WIFI_MAC_STATUS);
        cfm1_log_int("wifi: tsfifo=", (int32_t)WIFI_TX_STA_FIFO);
```

**Step 4:** in the scan-dwell block (`scan_ch <= 2`), after the `mac_rx1=` line, add the CCA
busy pair. The dwell is 100 ms (`WIFI_STA_DWELL_MS`), so one read per dwell gives the ratio:

```c
                cfm1_log_int("wifi: ch_idle=", (int32_t)WIFI_CH_IDLE_STA);
                cfm1_log_int("wifi: ch_busy=", (int32_t)WIFI_CH_BUSY_STA);
                cfm1_log_int("wifi: ch_bsec=", (int32_t)WIFI_CH_BUSY_SEC);
```

**Step 5:** `task build` then `task flash`, `task debug:dump`. Record `qpcnt`, `mstat`, `tsfifo`,
`ch_idle`, `ch_busy` for channels 1-2.

**Decision table:**

| `qpcnt` TX0Q | `ch_busy / (idle+busy)` | Reading |
|--------------|-------------------------|---------|
| 0 after every TX | any | Frame never entered the MAC queue: WDMA to MAC handoff or header format (Task 4, Task 5 H1) |
| > 0 | ≈ 100 % | Frame queued, CCA stuck busy: baseband/AGC problem (Task 3) |
| > 0 | < 50 % and `tsfifo` valid | MAC transmitted; counters were misread. Move to RX side. |

### Task 3: Full BBP snapshot on both sides, in a quiescent state

**Files:**
- Modify: `firmware/src/wifi/wifi_sdio.c` (new `wifi_bbp_snapshot`), `firmware/src/wifi/wifi_sta.c:579` (call once at `scan_ch == 1`)
- Modify (reader side): the oracle copy's shim register list and its debug page

**Step 1 (core-fm1):** compact hex rows so 22 log lines cover everything readable
(direct R0-R127, window C 0x00-0x7A, window D 0x00-0x5F; windows C' and D' have no read-back):

```c
/* BBP snapshot for 1:1 comparison with the vendor dump: 16 values per line. */
static void wifi_bbp_snapshot(void)
{
    uint8_t row[16];
    uint32_t i, j;
    cfm1_log("wifi: bbp snapshot");
    for (i = 0; i < 128u; i += 16u) {
        for (j = 0; j < 16u; j++) row[j] = (uint8_t)wifi_bbp_read(i + j);
        cfm1_log_hex("bbpR:", row, 16);
    }
    for (i = 0; i < 0x80u; i += 16u) {              /* window C (CCA) */
        for (j = 0; j < 16u; j++) row[j] = (uint8_t)wifi_bbp_win_read(0, i + j);
        cfm1_log_hex("bbpC:", row, 16);
    }
    for (i = 0; i < 0x60u; i += 16u) {              /* window D (TX backend) */
        for (j = 0; j < 16u; j++) row[j] = (uint8_t)wifi_bbp_win_read(2, i + j);
        cfm1_log_hex("bbpD:", row, 16);
    }
}
```

Call it once from the scan-dwell block when `wifi_sta.scan_ch == 1`, before the register
readbacks. Capture with `task debug:follow` from power-on (the DAP ring wraps).

**Step 2 (fm1-base):** dump the same set with interrupts held so the vendor driver cannot
interleave BBP port transactions. Replace the two tables in `wifi_shim.c:616-630` and
`dbg_bbp_dump` with:

```c
#define DBG_BBP_DIRECT_N 128u
#define DBG_BBP_C_N      0x80u
#define DBG_BBP_D_N      0x60u
uint32_t dbg_bbp_count(void) { return DBG_BBP_DIRECT_N + DBG_BBP_C_N + DBG_BBP_D_N; }
void dbg_bbp_dump(uint32_t *ids, uint32_t *vals, uint32_t count)
{
    uint32_t i, f = shim_irq_save();                  /* no vendor BBP access while we read */
    volatile uint32_t *port = (volatile uint32_t *)0x3101Cu;
    for (i = 0; i < 2000u && (*port & (1u << 17)); i++) ;   /* let an in-flight op finish */
    for (i = 0; i < count && i < DBG_BBP_DIRECT_N; i++) { ids[i] = i; vals[i] = dbg_bbp_read(i); }
    for (; i < count && i < DBG_BBP_DIRECT_N + DBG_BBP_C_N; i++) {
        uint32_t e = i - DBG_BBP_DIRECT_N; ids[i] = 0x100u | e; vals[i] = dbg_bbp_win_read(0, e);
    }
    for (; i < count; i++) {
        uint32_t e = i - DBG_BBP_DIRECT_N - DBG_BBP_C_N; ids[i] = 0x300u | e; vals[i] = dbg_bbp_win_read(2, e);
    }
    shim_irq_restore(f);
}
```

Window ids use `(w + 1) << 8 | entry` so they cannot collide with direct register numbers.
Raise `MAX_REGS` to 400 and `DBG_BUF` to 12288 in `debug_http.c`; check the vendor heap
headroom printed on `/status` (`heap_used` was 95 KiB of 278 KiB in iteration 4).

**Step 3:** collect both, write a Python diff (reader-side tooling)
(parse `bbpR:/bbpC:/bbpD:` rows from `dump.log` and `bbp[...]` rows from the curl output, print
a three-column table). Add the table to the observation file.

**Pass criterion:** every direct register and window C/D entry has a value from both sides.
Diff rows become the first suspects, R66/R82 expected among them.

### Task 4: Capture the vendor's real TX buffer and DMA start address

**Files:**
- Modify (reader side): the oracle copy's shim (a TX buffer sampler) and its status page

**Step 1:** the WDMA address register reads back on this SoC (core-fm1 reads
`txadr=0x01C17750`). After any vendor TX it still points at the vendor's static TX buffer:

```c
/* Last WDMA TX: registers plus the 64 bytes starting 16 bytes before the DMA start. */
void dbg_txbuf(uint32_t *adr, uint32_t *len, uint8_t *bytes, uint32_t n)
{
    uint32_t i, a = *(volatile uint32_t *)0x30F10u;
    *adr = a;
    *len = *(volatile uint32_t *)0x30F14u;
    if (a < 0x01C00000u || a >= 0x01C80000u) n = 0;   /* SRAM only */
    for (i = 0; i < n; i++)
        bytes[i] = *(volatile uint8_t *)(a - 16u + i);
}
```

In `status_page` print a `=== TXBUF ===` section: `adr`, `len`, then the 64 bytes as hex.

**Step 2:** on the FM-1 running fm1-base, trigger a scan from the app (SELECT on the WIFI page
calls `plat_wifi_scan()`), then `curl /status` within a few seconds so the last TX is a probe
request. Repeat once while idle (last TX is a data/null frame).

**Step 3:** decode. Bytes `[0..15]` precede the DMA start, `[16..]` are what the MAC sees.

| Finding | Meaning | Action |
|---------|---------|--------|
| `adr` = buffer+4, bytes[12..15] = plain length, bytes[16..31] look like TXWI (word1 bits [27:16] = frame length) | Spec is right; TXWI content is the suspect | Diff TXWI word0/word1 against `wifi_build_txwi` (PHYMODE, MCS, WCID, PACKETID, ACK) |
| `adr` = buffer+0 and bytes[16..19] decode as TXINFO (`[15:0]` = length rounded to 4, bits 25-26 = 2, bit 24 = WIV) | DMA includes a TXINFO word; our TX starts at the wrong offset, so the MAC reads a zero length and drops it silently | Implement H1 (Task 6) |
| `len` differs from TXWI+frame (for example +4 or +8) | Trailing pad word expected | Match it |

This is black-box observation of a running device, the same category as the register dumps
already tagged [M:hw]. Record the result in the SPECSHEET as [M:hw] with the date.

### Task 5: Analyst pass (separate session, `cleanroom-analyze`)

**Output:** new or corrected SPECSHEET sections, tagged [M:s]. The implementer session does not
read the IR.

Questions, in priority order:

1. §7.11.5: what exactly is DMA'd on TX: from buffer+0 or buffer+4? What does the vendor write
   into the first 4 bytes (length only, or length | queue select | WIV flags)? Any trailing pad?
2. §7.11.3: WDMA_CTL bit 16. Does any vendor code read it, poll it, or set it? Under what
   condition is it set (after MAC enable, after first RX, after IRQ init)?
3. New §6.6: direct BBP registers the vendor writes after reset (register, value, call site,
   and whether per-channel), especially R66, R82, R25, R27, R62-R86. This closes the gap noted in
   the BBP direct-registers task note (reader's private store).
4. §7.10: MAC_STATUS_CFG (`0x1200`) bits 8-11 as used by the vendor, if at all.
5. §4.2: `0x14050` bit 23 and bits 22-21 (vendor value `0x00C80001`).
6. §7.11.4 step order: where in `wfhw_init`/`wfhw_iqr_init` the vendor enables TX relative to
   WDMA init, MAC_SYS_CTRL and IRQ enable.

### Task 6: Implement the outcome of Tasks 2-5

Only one of these branches is expected to apply. Each is a single commit with the observation
that justified it in the message.

**H1, TXINFO header (if Task 4 shows DMA from buffer+0):** in `wifi_sdio_tx`:

```c
    /* TXINFO word [IP:RT2x00, rt2800usb.h lines 46-51]: [15:0] length of TXWI+frame rounded
     * to 4, bit 24 WIV (hardware inserts no IV), bits 25-26 QSEL = 2 (EDCA). */
    pkt = (total + 3u) & ~3u;
    *(uint32_t *)&wifi_tx_buf[0] = (pkt & 0xFFFFu) | (1u << 24) | (2u << 25);
    WDMA_TX_ADR = (uint32_t)&wifi_tx_buf[0];
    WDMA_TX_LEN = pkt + 4u;          /* +4 for TXINFO; add a zero pad word if Task 4 says so */
```

Pass: `qpcnt` TX0Q > 0 right after DMA, then `tx_cnt1` [15:0] success > 0 on the next probe.

**H2, baseband (if Task 2 shows CCA stuck or Task 3 shows BBP diffs):** apply the differing
direct registers from SPECSHEET §6.6 (analyst) or `rt2800lib.c` values, after `wifi_phy_rst()`,
and re-write R66 on every channel switch as Linux does (`rt2800_config_channel`). Pass: `ch_busy`
falls below 50 % on a quiet channel, `rx_cnt1` false-CCA or `rx_cnt0` CRC errors become non-zero
(the receiver hears something), then `scan rx` > 0.

**H3, WDMA to MAC handoff (if `qpcnt` stays 0 with correct header):** follow the analyst's
answer on bit 16 and init order; re-order `wdma_init`, MAC enable and IRQ enable accordingly.

### Task 7: Phase 1 exit review

**2026-11-04: pass.** `TX_STA_CNT1` success counted (12l) and `scan done n=8` with real SSIDs on the
correct channels (12o). The reader pass of 2026-11-04 (SPECSHEET §4.2.1,
§14) supplied the missing steps; see the observation of 2026-11-04T09:45.


**Pass (go to Phase 2):** `TX_STA_CNT1` success > 0 on at least one channel and
`scan done n > 0` with a real SSID in the BSS table.

**Fail after 5 days:** run the fallback ladder in order, one day each:

1. Environment isolation: build `configs/wifi_debug_defconfig` with the stub engine, no effects,
   no USB host, and compare the same counters. Same result means the environment is not the cause.
2. Decide on the vendor-binary port (Phase 4, parked task) as a last resort for observing the
   vendor stack under core-fm1's runtime. Re-estimate before starting; the earlier 7-14 day figure
   stands.

---

## 3. Phase 2: scan to weather over the air (estimate 1-2 weeks after Phase 1)

Each milestone has a hardware acceptance check logged over DAP; tasks get expanded into
bite-sized steps once Phase 1 reveals which layers still need work.

| Milestone | Files | Acceptance on hardware |
|-----------|-------|------------------------|
| M2.1 Scan results | `wifi_sta.c` (beacon/probe-response parse, RSSI from RXWI word 2), `wifi_mac.c` BSS table | `scan done n >= 1`, SSID and channel match a known AP, RSSI within -30..-90 |
| M2.2 Open-AP association | `wifi_sta.c` auth/assoc, `wifi_mac.c` BSSID + `RX_FILTER_ASSOC` | `sta state=` reaches CONNECTED on an open test AP; deauth and reconnect work |
| M2.3 WPA2-PSK | `wifi_wpa.c`, `wifi_data.c` CCMP TX/RX | 4-way handshake completes; encrypted data frame round trip (ARP reply) |
| M2.4 DHCP + DNS | `wifi_lwip.c` | `wifi_get_ip()` non-zero within 10 s; DNS resolves the weather host |
| M2.5 Weather page | `wifi_http.c`, `wifi_weather.c`, `page_weather.c` | Weather page renders live data on the device; `CONFIG_WIFI_PASSWORD` path works |

Keep the existing per-layer unit tests (`tests/wifi/`) green at every milestone and add one test
per new parser (beacon IEs, RXWI RSSI, EAPOL key descriptor) with captured byte strings.

---

## 4. Phase 3: make it shippable (estimate 1-2 weeks)

1. **Real calibration instead of overrides.** Remove the 15 vendor constants in
   `wifi_rf.c:824-844`. Fix convergence of TX LO, RX DC, filter bandwidth and PLL bit 27 so
   per-boot values land within the drift the vendor shows (±1 LSB, iteration 10). This is required
   because the constants belong to one unit's calibration.
2. **Register naming errata.** Rename `WIFI_MAC_ADDR_DW0/DW1` (`wifi_sdio.c:42-43`) and drop
   the fake "OTP MAC" read in `wifi_mac.c:566-580`; correct SPECSHEET §7.2 (`0x30300` is not
   MAC_ADDR_DW0), §7.10 (`0x1200` = MAC_STATUS_CFG, `0x0408` = PBF_CFG, `0x040C` = PBF_MAX_PCNT).
3. **Debug hygiene.** Move the readback blocks in `wifi_sta.c:579-628` and `wifi_rf.c` behind
   `CONFIG_DEBUG_WIFI` (new Kconfig symbol under `kconf/wifi.kconfig`); keep one compact
   `wifi: stat` line per scan.
4. **Robustness.** AP loss and reconnect, scan while audio plays (check `sched` stats and the
   audio ISR diagnostics), WiFi off/on, timeouts in every state.
5. **Budgets.** `task flash:size-check` after each milestone; record flash/RAM/pool deltas in the
   plan close-out.
6. **Tests and CI.** Keep `task test:unit` and `task test:e2e` green; add a hardware checklist
   doc (`docs/hardware/wifi/HW_CHECKLIST.md`) mirroring the acceptance table above.
7. **Docs.** Update `docs/COMPONENTS.md` (`task docs:module-diagram`), `AGENTS.md` WiFi section,
   SPECSHEET with every new [M:hw] fact, and close the WiFi task notes.

---

## 5. Phase 4: parked and later

| Item | Status | Trigger to resume |
|------|--------|-------------------|
| Vendor-binary backend (`CONFIG_WIFI_NONFREE`) | Parked | Phase 1 fails its exit criteria, or a need to A/B the vendor stack under core-fm1's runtime |
| `CONFIG_WIFI_MIDI` (rtpMIDI/OSC) | Kconfig stub only | After Phase 3 |
| OTA over WiFi | Out of MVP scope (the MVP scope note in the reader's private store) | After Phase 3 |
| BLE/WiFi coexistence (RFPRIO) | `CONFIG_WIFI` depends on `!BLE` | Separate design |
| Power save | Not started | After Phase 3 |

---

## 6. Decisions needed from Dmitry

1. **Park the vendor-binary port** until Phase 1 fails (recommended), or run it in parallel.
2. **Phase 1 time-box**: 5 working days with the FM-1 and the test AP available (recommended).
3. **Oracle memory reads**: Task 4 reads the vendor TX buffer bytes from the running fm1-base
   build. It is black-box observation like the existing register dumps, documented as [M:hw]. Confirm
   this fits the clean-room policy in `docs/hardware/wifi/CLEAN_ROOM_METHODOLOGY.md`.

---

## 7. Risks

| Risk | Mitigation |
|------|------------|
| Both TX and RX are dead for different reasons, so a single fix shows no change | Tasks 2-4 measure TX and RX independently (`qpcnt`/`tx_cnt1` versus `ch_busy`/`rx_cnt0-1`) |
| Vendor BBP dump still unreliable despite the IRQ hold | Dump twice, keep only rows that agree; otherwise dump after `wifi_off()` with the clocks left on |
| DAP ring wraps before the snapshot is read | Use `task debug:follow` from power-on; snapshots are 22 lines |
| Calibration never converges without vendor constants | Phase 3 item 1 has its own observations; ship behind a "calibration failed" fallback that keeps WiFi off rather than radiating with bad trims |
| Audio dropouts during scan | Measured in Phase 3 item 4; WDMA TX is synchronous and holds IRQs off for up to 10 ms, so the TX timeout may need to shrink or move the poll out of the IRQ-off region |
