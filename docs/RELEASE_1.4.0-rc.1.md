Melodee 1.4.0-rc.1 is the first release candidate for 1.4, based on `next`.

- **One to four tracks:** new songs start with one track. On HOME/Stage, hold
  OCT+ to add a track, or remove the selected track while stopped after confirming.
  Removing a track moves the remaining sounds, patterns, song sections, motion and
  recordings together. Projects and templates retain their track count.
- **HOME chord view:** tap HOME again to see the chord on a circle of fifths,
  sounding notes, track levels, waveform and keyboard. A single track uses a larger
  Stage layout; the other screens show the tracks in use.
- **Assignable SELECT:** HOME's held menu lets SELECT control BPM, mod wheel,
  level, swing, transpose, chord, arpeggiator rate or pattern, or turn it off.
- **Audio performance:** optimized synthesis, mixing and dual-core scheduling
  reduce audio work while preserving native DSP behavior. GLO > GLOBAL knob 2
  sets a shared BUDGET of 8–32 weighted points; idle tracks reserve no voices.
  The web editor exposes the same saved device setting.
- **Single-core build:** audio batching uses serial access when CPU1 is disabled,
  so the optimized audio path builds for both firmware variants.
- **1.3.2 fixes included:** startup stays at the boot loader's 360 MHz, optional
  320 MHz retuning has fault protection, retired CPU1 stays off until power-off,
  and recovery accepts the verified official M-VAVE V15 package.

This is a prerelease for testing. Earlier audio measurements apply to the specific
packages and settings recorded in `docs/AUDIO_PERFORMANCE.md`; they do not certify
every combination or higher BUDGET setting in this candidate. The exact release
candidate has not been flashed or physically auditioned as part of publication.

Downloads include Standard (dual-core) and Single core firmware, application
binaries, and native UBOOT recovery tools for Windows x64, macOS Apple Silicon,
macOS Intel and Linux x64. `BUILD_INFO.json` records the source commit and native
smoke tests; `SHA256SUMS` covers all assets. See `BUILDING.md` for installation and
recovery instructions. The stable installer remains on 1.3.2.
