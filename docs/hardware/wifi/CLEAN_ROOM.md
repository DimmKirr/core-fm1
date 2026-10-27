# WiFi Clean-Room Methodology

This document describes the clean-room methodology used to produce the
GPL-3.0 WiFi driver for the AC791N (WL82) radio in core-fm1.

## Methodology

### Phase 1: Analysis (completed 2026-10-25)

The analyst (the "reader") examined JieLi's SDK binary libraries to
identify register addresses, initialization sequences, bus protocols,
and calibration algorithms. The AC791N SDK distributes WiFi drivers as
pre-compiled static libraries without source. Standard disassembly tools
were used; no encryption, DRM, or technical access controls were bypassed.

The analyst produced a single public artifact:

- `docs/hardware/wifi/SPECSHEET.md`: the hardware fact sheet (register maps, init
  sequences, calibration procedures, behavioral descriptions)

Additional internal analysis notes were produced during Phase 1 and are
retained privately. They are not part of the clean-room record and are
not accessible to the implementer.

### Phase 2: Implementation

The implementer writes all WiFi driver code from:

1. `docs/hardware/wifi/SPECSHEET.md` (the hardware fact sheet)
2. IEEE 802.11 standard (publicly available)
3. Permitted open-source references:
    - Linux kernel `drivers/net/wireless/ralink/rt2x00/` (GPL-2.0-or-later)
    - lwIP 2.x (BSD-3-Clause, savannah.nongnu.org/projects/lwip)
    - wpa_supplicant / hostapd (BSD-3-Clause, w1.fi)
    - ESP-IDF WiFi components (Apache-2.0, github.com/espressif/esp-idf)
4. Public standards: FIPS 197, FIPS 180-4, RFC 2104, RFC 2898, IEEE 802.11i

The implementer does NOT have access to:

- Any disassembled or decompiled vendor output
- JieLi SDK binary libraries
- Any vendor source code or internal analysis artifacts

### Barrier

The hardware fact sheet (`docs/hardware/wifi/SPECSHEET.md`) is the barrier between
analysis and implementation. It contains only observable facts: register
addresses, bit field definitions, initialization sequences (described as
ordered groups of writes with timing), calibration procedures (described
as algorithms with register inputs and outputs), and measured behavior.

It contains no vendor code, pseudocode, or direct translations of
decompiled functions.

This follows the same methodology as the BLE stack in Optimist (`firmware/upstream/optimist/`), whose hardware driver
`ble_hw_wl82.c`
was written from `docs/BLE-HW-FACTS.md` with the explicit statement:
"No vendor code, IR or disassembly."

## Legal Basis

### Interoperability analysis right

- **US**: DMCA 17 U.S.C. section 1201 (f) interoperability exemption:
  analysis of binary code is permitted to achieve interoperability with
  independently created programs
- **EU**: Software Directive (2009/24/EC) Article 6: decompilation is
  permitted for interoperability when the information is not otherwise
  available from the rightsholder

### Purpose

Enabling GPL-3.0-licensed firmware to operate on the AC791N hardware.
No public documentation exists for the WL82 WiFi radio register map,
RF calibration sequences, or internal SDIO bus protocol. JieLi provides
only binary blob libraries and application-level SDK headers.

### What was analyzed

Binary libraries distributed in JieLi's publicly available SDK repository.
Standard disassembly tools (part of the public LLVM toolchain) were used
to extract hardware register behavior. No encryption, DRM, or technical
access controls were bypassed.

### Clean-room separation

The reader and implementer are separate actors with an information
barrier between them. The implementer has never accessed the analysis
artifacts, vendor binaries, or disassembled output.

- **Reader**: analyzed vendor binary libraries, produced the hardware
  fact sheet
- **Implementer**: writes code from the fact sheet, public standards,
  and permitted open-source references only
- The implementer has no access to vendor binaries or analysis artifacts

## Layers and Their Sources

| Layer             | Clean-room from fact sheet?          | Implementation source                        |
|-------------------|--------------------------------------|----------------------------------------------|
| RF HAL            | Yes (vendor-proprietary registers)   | `docs/hardware/wifi/SPECSHEET.md`            |
| SDIO bus          | Yes (vendor-proprietary on-chip bus) | `docs/hardware/wifi/SPECSHEET.md`            |
| BBP programming   | Yes (vendor-proprietary baseband)    | `docs/hardware/wifi/SPECSHEET.md`            |
| MAC / 802.11      | No (public standard + GPL code)      | IEEE 802.11, Ralink RT2x00 (GPL-2.0+)        |
| STA state machine | No (public standard + GPL code)      | IEEE 802.11, Ralink RT2x00 (GPL-2.0+)        |
| WPA2-PSK          | No (public standards + BSD code)     | IEEE 802.11i, FIPS 197, wpa_supplicant (BSD) |
| lwIP integration  | No (BSD library)                     | lwIP 2.x (BSD)                               |
| RTOS shim         | Partial (API from fact sheet)        | Linux kernel patterns, bare-metal adaptation |
| WiFi API          | No (original)                        | Our design                                   |

## References

- `docs/hardware/wifi/SPECSHEET.md`: the fact sheet (the barrier document)
- `firmware/upstream/optimist/docs/BLE-HW-FACTS.md`: the BLE precedent
  for this methodology (1,748 lines, evidence-tagged)
- `firmware/upstream/optimist/firmware/src/ble/ble_hw_wl82.c`: the BLE
  implementation that demonstrates the clean-room process working
- `firmware/NOTICES`: third-party attribution for open-source references
