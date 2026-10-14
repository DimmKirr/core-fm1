Melodee 1.3.5 adds a boot stub and completion marker to recover interrupted
application updates. It builds on 1.3.4 and keeps its clock behavior: Melodee
never writes the system PLL.

The startup display hotfix brings forward the 1.4 development line's early
screen and maintenance progress. The FM-1 shows **STARTING UP** before reading
stored data, then **INITIALIZING PRESETS** with a progress bar when first-boot
banks are created, or **FINISHING UPDATE** during update cleanup. The display
says to keep power connected and wait. The watchdog is serviced between
maintenance writes. A failed operation stays visible with **Restart to retry**;
press **OCT+** to acknowledge and continue. Existing preset/settings formats
and bank contents follow the same initialization rules as 1.3.4.

- Before starting Melodee, the stub checks the completion marker and the
  application checksum. An incomplete application requests the retained USB MIDI
  update loader. Reconnect USB and install the same package again; matching
  sectors are skipped. ROM UBOOT is the fallback if the loader cannot be used.
- Hold **OCT− alone** while switching on to request update mode even with a
  complete application. Three short key-light flashes signal loader handover;
  one long flash signals a request for ROM UBOOT. Both octave buttons at
  power-on still select calibration.
- Successful boots retain the rescue loader and clear only its separate update
  record. Boot-stub version 3 is pinned by checksum. Builds with the same stub
  and SDK layout keep the application boot sector identical, including between
  Standard and Single core variants, so ordinary updates skip that sector.
- Boot protection does not repair arbitrary startup faults, and installing
  stock firmware or older firmware without a compatible stub removes it.

**First-install limit:** stock V15 and Melodee through 1.3.4 have no compatible
stub. Installing 1.3.5 must write sector `0x4000`. The initial update phase is
unprotected until that sector is complete, including the preceding marker-sector
erase; an interruption in that phase can still require hardware recovery.
The same limit applies to a later stub or directory/SDK layout change. Keep
the FM-1 charged, USB connected and the computer awake throughout installation.

Automated boot-stub, package and interruption checks exercise recovery decisions,
retained-loader handling, write ordering and protected flash regions. Physical
warm/cold boot, OCT− rescue and interrupted-update recovery validation remains
pending; automated results do not establish hardware recovery behavior.

Firmware downloads include Standard (dual-core) and Single core variants.
For a device already in ROM UBOOT, use the platform recovery tool and retain
its backup before restoring firmware; see `RECOVERY.md` in the archive.
`BUILD_INFO.json` records source and native-build provenance, and `SHA256SUMS`
covers the release assets.

The boot protection is adapted from ChoralRoot FM-1 v0.16 under GPL-3.0-only;
its notices and the shared `CRSTUB` / `CRBM` format are retained.
