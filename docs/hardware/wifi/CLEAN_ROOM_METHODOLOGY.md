# WiFi Clean-Room Methodology

This document describes the clean-room process behind the GPL-3.0 WiFi driver for the AC791N
(WL82) radio in core-fm1, the barrier between analysis and implementation, what the implementer
may and may not read, and how the clean-room claim is checked.

## Artifacts

- `docs/hardware/wifi/SPECSHEET.md`: the hardware fact sheet. The single public output of the
  analysis and the only analysis document the implementer reads. Every fact carries an evidence
  tag; §0 of the sheet explains the tags, the table policy and the published-source record.
- `docs/hardware/wifi/CLEAN_ROOM_METHODOLOGY.md`: this file.
- `firmware/NOTICES`: third-party attribution for the references the driver uses.

Everything else the analysis produced (disassembly, intermediate representation, verification
logs, per-claim evidence locations, observation conditions, agent transcripts, superseded text)
is a private part of the evidence record. It is retained by the reader with dates, source
revisions and hashes, negative results and reviewer outcomes, and it is not accessible to the
implementer. "Private" does not mean "outside the record": it is the record; only a sanitised
source-search summary and per-fact evidence tags are published in the sheet.

## Roles and the barrier

- **Reader (analyst).** Examines the hardware through public documentation, IP-ancestor
  documentation, direct observation (emulator traces, hardware probes, register read-backs of
  the running vendor radio) and, as a last resort, the vendor's pre-compiled libraries. Writes
  the fact sheet. Never writes driver code.
- **Implementer.** Writes the driver from the fact sheet, the public standards and the permitted
  references below. Never reads vendor binaries, vendor disassembly or intermediate
  representation, reader-side notes, or third-party code derived from the vendor libraries.
- **Maintainer.** Decides licence positions, runs the provenance checks that need reader-side
  material, and owns the published history.

The fact sheet is the barrier. It contains observable hardware facts only: register addresses,
bit fields, values, ordered write sequences with timing, bus protocols, calibration procedures
described as inputs, steps and outputs, measured behaviour, and identified IP ancestry. It
contains no vendor code, no pseudo-code, no vendor function or variable names, and no description
of the vendor's software architecture.

**Tables.** A constant table that the hardware consumes is a hardware fact; how it reaches the
sheet depends on its source, in this order: a published SDK source is cited and not copied; a
table that a measured generating rule reproduces exactly is given as the rule with check values;
a register table (entry index, bit position, value, write order) is recorded in full with its tag;
a value read back from the running vendor unit is tagged as measured; nothing is transcribed that
a higher-tier source supplies. There is no numeric size threshold; each table in the sheet says
which route it took. Unit-specific calibration results are marked in the sheet and are never to
be hard-coded from any unit's read-back.

**Vendor identifiers.** The sheet uses published register names (from the SDK's chip header),
RT2800 names for the MAC registers, and descriptive names for everything else. The reader scans
the driver for vendor-internal identifiers and hands the implementer file:line locations with
replacement names (SPECSHEET §16.3); the list of identifiers itself stays reader-side.

## Source tiers

1. **Published material** (tag `PUB`): the SDK's chip header, published application sources and
   headers, datasheets. Licence record: the JieLi repository `fw-AC79_AIoT_SDK`
   (gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK) carries the Apache License 2.0 (`LICENSE` file; the
   hosting service reports Apache-2.0); default branch `release/AC79NN_SDK_V1.2.0`, head commit
   `0617f4cc` of 2026-10-27 at the time of writing. The cited files were read from the release
   package `AC79NN_SDK_V1.2.14_2026-10-13`; every cited file in that package is byte-identical to
   the repository's copy. The files are listed in SPECSHEET §0 ("Published-source record"). The
   pre-compiled libraries in the same package are not published source and are not covered by
   this record.
2. **IP ancestry** (tag `IP:RT2x00`): the WiFi MAC/BBP descends from the Ralink RT2x00 family;
   the Linux rt2x00 driver (GPL-2.0+) and the RT5350 datasheet are public references for the
   inherited registers.
3. **Direct observation** (tags `M:t`, `M:d`, `M:hw`): emulator traces, memory dumps, probe
   firmware on the FM-1, and read-backs of the running vendor radio on the same unit. The
   vendor-side read-backs are captured by the reader or the maintainer with the vendor firmware;
   the implementer never runs the vendor build.
4. **Binary analysis** (tag `M:s`): the vendor's pre-compiled libraries, analysed with public
   LLVM tools, only for facts the higher tiers could not supply, with the search recorded in the
   sheet's source-search record and in the private evidence record. Before any new binary
   question is opened, the reader records the precise hardware question, the tier 1-3 attempts
   and results and the documentation-availability evidence.

## Observation methods (Tier 3)

- **Probe firmware on the FM-1.** core-fm1 builds that read and write chosen registers and report
  over USB; results are tagged `M:hw` with the iteration (12a, 12b, ...) and the observation file.
- **Read-backs of the running vendor radio.** Method: (1) clone fm1-base and build its WiFi
  example, which links the SDK's pre-compiled radio libraries taken from the public SDK package;
  (2) add read-only debug endpoints and a sampler at the application's callback boundaries,
  leaving the radio behaviour unchanged; (3) flash the instrumented build on the FM-1 the user
  owns; (4) collect the pages over HTTP, dated, with the conditions (network, channel, association
  state). The debug server returns MMIO ranges, the four BBP windows through the port protocol,
  the stored calibration records, and samples of the control-block indicators taken at the
  boundaries visible outside the libraries (their callbacks and prints, the application's start
  and scan calls). The reader runs it and keeps the raw pages in the reader's private store;
  the pages carry the libraries' print strings and never enter the implementer's workspace. Only
  the folded register values enter the sheet, tagged `M:hw` with the capture file named. The
  vendor firmware is run, not read: this is observation of lawfully obtained software on hardware
  the user owns.
- **Emulator traces.** fm1-emu with MMIO trace hooks on the stock BLE firmware (`M:t`); the model's
  limits are stated in SPECSHEET §0. No vendor WiFi firmware is run in it.
- **Differential comparison.** Register sweeps of the two builds diffed block by block
  (`tools/wifi_sweep_diff.py`), and the BBP window diffs of 2026-11-02 and 2026-11-03.

## Legal basis

- **US**: DMCA 17 U.S.C. section 1201(f) interoperability exemption: analysis of binary code is
  permitted to achieve interoperability with independently created programs.
- **EU**: Software Directive (2009/24/EC) Article 6: decompilation is permitted for
  interoperability when the information is not otherwise available from the rightsholder.

Purpose: enabling GPL-3.0-licensed firmware to operate on the AC791N hardware. No public
documentation exists for the WL82 WiFi radio register map, the RF calibration or the WDMA data
path; JieLi publishes application-level sources and headers and distributes the radio drivers as
pre-compiled libraries. What was analysed: those libraries, from the publicly available SDK, for a
device the user owns. Standard disassembly tools (the public LLVM toolchain) were used; no
encryption, DRM or technical access control was bypassed.

## Permitted references for the implementer

The same list stands in SPECSHEET §2; the sheet is the copy the implementer reads.

| Reference | Licence | Use for |
|-----------|---------|---------|
| Linux `drivers/net/wireless/ralink/rt2x00/` (`rt2800.h`, `rt2800usb.h`, `rt2800lib.c`, `rt2800mmio.h`), pinned by kernel version where cited | GPL-2.0+ | MAC register names and bit layouts, TXWI/RXWI/TXINFO/RXINFO, EDCA packing, STA state machine patterns |
| Ralink RT5350 datasheet §3.20 (public) | published | MAC register semantics of the RT2800 ancestor |
| wpa_supplicant / hostapd (w1.fi) | BSD-3-Clause | WPA2-PSK, 4-way handshake, CCMP, EAPOL |
| lwIP 2.x | BSD-3-Clause | TCP/IP stack, netif driver pattern |
| ESP-IDF WiFi | Apache-2.0 | STA state machine patterns, API surface |
| JieLi SDK published sources listed in SPECSHEET §0 | Apache-2.0 | RF-die LUT data, CCA tunables, default trim bytes, record ids, register block names, IRQ index, profile defaults |
| Optimist `firmware/upstream/optimist/docs/BLE-HW-FACTS.md`; this repository's `firmware/hal/fm1_ble_rf.h` with its capture tables | GPL-3.0 / MIT | the shared RF analog bring-up replay and the BLE-side facts |
| IEEE 802.11-2020, IEEE 802.11i, FIPS 197, FIPS 180-4, RFC 2104, RFC 2898 | standards | frame formats, crypto |

**Not permitted** (the implementer does not open these):

- the vendor's pre-compiled libraries and anything linked from them (`firmware/src/wifi/nonfree/`
  is build-isolated behind `CONFIG_WIFI_NONFREE`, mutually exclusive with the clean-room driver,
  and is not a reference for it);
- any reader-side material: the reader's private store (analysis notes, the vendor-firmware copy,
  captures, task notes), disassembly, intermediate representation, verification logs;
- `docs/hardware/WIFI.md` (a stale 2026-10-26 copy of the sheet, replaced by a pointer) and
  `docs/plans/2026-10-25-wifi-driver-design.md` (superseded);
- **fm1-base** `bsp/chip/fm1_rf_cal.h` and anything that includes it. That file states in its
  own header that it was rewritten in C from the vendor library's code and checked against the
  intermediate representation; it is a derivative of the vendor library, whatever licence its
  author applies to it, and it is the one third-party source whose vocabulary matches the
  vendor's. fm1-base's hardware fact sheet and its capture tables are observation data and are
  not affected by this exclusion.

## fm1-base ruling

The core-fm1 driver file `wifi_rf_cal.c` at commit 76da132d cites fm1-base's calibration file as
a source. Under this document's legal position (the vendor libraries are proprietary binaries
analysed under the interoperability exemption), a C rewrite of those libraries is not an
acceptable implementation source for a clean-room deliverable, and fm1-base's own position (that
the whole SDK repository, binaries included, is Apache-2.0) does not change that: accepting a
derivative is a different deliverable with its own attribution, and it cannot satisfy this
process's clean-room gate for the RF calibration layer.

Ruling recorded 2026-11-04 (reader, on the maintainer's standing position; the maintainer may
override it in writing here): **not accepted**. Consequences: SPECSHEET §10.1 and §10.2 now
supply the complete calibration procedure from the vendor libraries under the exemption, with
hardware cross-checks; the implementer rewrites `wifi_rf_cal.c` from the sheet with
project-original names (SPECSHEET §16.3) and drops the fm1-base citation; the RF calibration
layer counts as clean-room only after that rewrite. If the maintainer overrides: relabel the
layer "derived" in the table below, mark the file's licence accordingly, add JieLi and fm1-base
to NOTICES for that layer, and state here that the clean-room claim does not cover it.

## Layers and their sources

| Layer | Clean-room from the fact sheet? | Implementation source |
|-------|---------------------------------|------------------------|
| RF analog bring-up, PLL, bank scan | Yes | SPECSHEET §4.2.1, §5.2, §11.2.9 |
| RF calibration (full trim, stored record, quick path) | Yes from SPECSHEET §10.1-§10.2 as of 2026-11-04; the code at commit 76da132d predates this and is derived (see the ruling): rewrite required | SPECSHEET §10.1, §10.2 |
| RF-die LUT | Yes | SPECSHEET §5; table data from the published SDK source (Apache-2.0) |
| Clock tree, control block, WDMA transport | Yes | SPECSHEET §4.2, §7.11 |
| BBP programming (windows, tables, TX filter, compression) | Yes | SPECSHEET §6, §11.2 |
| MAC / 802.11 | No (public standard + GPL code) | IEEE 802.11, Ralink RT2x00 (GPL-2.0+), SPECSHEET §7.10 for the values |
| STA state machine | No (public standard + GPL code) | IEEE 802.11, Ralink RT2x00 |
| WPA2-PSK | No (public standards + BSD code) | IEEE 802.11i, FIPS 197, wpa_supplicant |
| lwIP integration | No (BSD library) | lwIP 2.x |
| RTOS shim | Partial (behavioural contract from the sheet) | SPECSHEET §8, bare-metal adaptation |
| WiFi API | No (original) | own design |

## History

- 2026-10-25: first fact sheet written from emulator traces and the first binary pass.
- 2026-10-26: a prototype driver was written directly from the intermediate representation
  (commit `6fa6dac7`; seven files, including register tables "extracted from decompiled IR").
  It was discarded the next day. Its blobs are no longer in any file at HEAD, but they remain in
  the published history until the purge below.
- 2026-10-27: the driver was restarted from the fact sheet (commit `313ca7ca`).
- 2026-10-28 to 2026-11-04: hardware probes and twelve bring-up iterations; reader passes on
  2026-10-31 and 2026-11-04.
- 2026-11-01: fm1-base's calibration file was used as a reference for `wifi_rf_cal.c` (the
  contamination the ruling above addresses).
- 2026-11-04: the driver written from the sheet transmits and receives (iteration 12o); the
  clean-room audit of commit 76da132d listed the open provenance items; reader pass 2 supplied
  the missing calibration facts, the tests (SPECSHEET §15) and the housekeeping list (§16).
- 2026-11-05: reader pass 3 added Phase D (join, authentication, association, link-up,
  keys, link-down) to SPECSHEET §4.2.1, §7.11.5, §7.13 and §14-§16 from the implementer's
  2026-11-05 observations and the station, supplicant and transport libraries; two of the nine
  reader-side analysis packages were stopped by a safety classifier before writing and were
  redone by the reader by hand (private record §5).
- Pending (maintainer): purge of the seven 2026-10-26 blobs from the published history and
  force-push of `main` and `feat/weather-page`, verified on a fresh clone with
  `git log --all --find-object` for each blob id; date to be recorded here.

**Exposure.** The same account produced the 2026-10-26 prototype, the fact sheet and the driver.
A shared git author neither proves nor disproves shared exposure; the private record notes which
session saw which vendor-derived material. The rewrite of `wifi_rf_cal.c` and the removal of the
forced unit-specific registers are assigned to a fresh implementer context that has seen only the
public documents, and any historical separation claim is qualified by that record.

## Implementer workspace and hand-off

The implementer works in a fresh clone taken after the history purge, without the reader's
private store (the implementer keeps its own observation store, which reader-side material never
enters), without opening `firmware/src/wifi/nonfree/`, and with pinned copies of the
permitted references. The manifest of that workspace and its access boundary are recorded with
the hand-off.

Order of work (SPECSHEET §15 and §16): rewrite `wifi_rf_cal.c` from §10.1-§10.2 and retire the
forced registers, with the provenance checks below as unit tests in the same change; then the
hardware tests in the order §15 gives.

### Hand-off protocol

1. **Separate actor.** The implementer is a separate person or a fresh agent session that has
   seen none of the reader's material; a session that has read vendor libraries, disassembly or
   reader notes never writes driver code. In agent terms: a new top-level session started by the
   maintainer in the fresh clone at its own path (so it inherits no conversation and no session
   memory of the reader's project path), invoked with the implementer skill, declaring its inputs
   (the sheet revision, this file, the pinned references) at the start. Never a forked subagent
   of a reader session (it inherits the reader's context). A fresh subagent is acceptable only
   when the maintainer spawns it with a prompt limited to file paths and the task list. The
   reader session spawns reader-side agents only and never one that writes driver code.
2. **One channel in.** Hardware facts reach the implementer only through the sheet. A question
   from the implementer is a request for a fact ("the vendor value of register X", "is step Y
   specified") and is answered by a numbered subsection or an erratum in the sheet, never by a
   chat answer carrying vendor detail. Nothing from the reader's transcripts, captures or notes is
   pasted into the implementer's session.
3. **One channel out.** Results reach the reader only as observation files in the
   implementer's observation store: build id, what was changed, the registers read with their
   values, and the decision taken. The reader folds them into SPECSHEET §14 and §16 and never
   writes into that store.
4. **Unknowns stay visible.** Where the sheet gives no fact, the implementer stubs the step with
   a TODO citing the §16 id; it never guesses a value, imports a third-party implementation, or
   derives one from memory.
5. **Evidence kept on both sides.** Both transcripts are evidence; neither is redacted. The
   workspace manifest, the exposure record and the private evidence record are retained by the
   maintainer and the reader.

## Provenance checks ("passes clean-room")

1. **Constant trace.** Every hex and decimal literal, mask and generated array in
   `firmware/src/wifi` (excluding `nonfree/`) and the WiFi HAL headers, in every build variant,
   traces to a fact in SPECSHEET.md, to a public-standard allowlist (802.11 frame control, IE ids,
   ethertypes, RSN OUI, AES/SHA/CRC constants) or to an in-repo pinned reference (the rt2x00
   excerpts, lwIP, wpa_supplicant by version). Bar: zero untraced. Implementer-side.
2. **Citation resolution.** Every `SPECSHEET §x.y` in code resolves to a heading that supports
   the claimed address, behaviour and revision; section numbers are stable (new material only in
   new subsections). Known stale citations are listed in SPECSHEET §16.3. Implementer-side.
3. **Forbidden identifiers.** The reader scans the clean-room files against the private
   identifier list: zero hits. Reader-side.
4. **Published history.** After the purge, no 2026-10-26 blob is reachable from any published
   ref, verified on a fresh clone of origin. Maintainer-side.
5. **Audit re-run.** The 2026-11-04 audit's blocking and attribution items all closed; the three
   verdicts (provenance, sheet conformance, hardware validation) reported separately with
   PASS / FAIL / BLOCKED / NOT-APPLICABLE per mandatory fact. Maintainer-side.

Mechanical results establish bounded checks; they are not proof of historical independence or
legal clearance.

## References

- `docs/hardware/wifi/SPECSHEET.md`: the fact sheet (the barrier document)
- `firmware/upstream/optimist/docs/BLE-HW-FACTS.md`: the BLE precedent for this methodology
  (1,748 lines, evidence-tagged); `firmware/upstream/optimist/firmware/src/ble/ble_hw_wl82.c`:
  the BLE implementation written from it ("No vendor code, IR or disassembly")
- `firmware/NOTICES`: third-party attribution for open-source references
- JieLi `fw-AC79_AIoT_SDK`: https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK (Apache-2.0)
