# Changelog

FM Lab's releases, newest first. Each one is on the
[releases page](https://github.com/jsmith13/fm_lab/releases) with its `.fwsc`; the version a build is shows in
GLOBAL's top-right corner. Versions are `MAJOR.MINOR.PATCH`: a patch fixes what was there, a minor adds to it.

## 1.1.0 — 2026-10-18

### Added
- **OVERSAMPLE** (GLOBAL, last bank, `ON` to start). The operators run at 88.2 kHz and a half-band filter
  brings the sound back to 44.1 kHz, so the harmonics FM makes above 22 kHz no longer fold back into the
  audible range as tones that don't belong to the note: about 40 dB less of them on a bright patch played
  high. It roughly doubles the engine's CPU: ~72 % (82 % peak) with eight voices, the filter and every
  effect on. `OFF` gives that back.

### Changed
- **24-bit output.** The sound used to be cut to 16 bits right after the voices were mixed, and only then
  sent to the 24-bit DAC. The mix leaves room for an 8-voice chord, so a single soft note used only ~11 of
  those 16 bits, and its decay ran out of steps and turned gritty (the DC blocker's rounding added a
  distortion that followed the note). The mix, the filter, the effects and MASTER now carry 24 bits to the
  DAC. USB audio stays 16-bit and is dithered. The delay's echoes are still stored at 16 bits, which saves
  128 KiB of RAM.

## 1.0.1 — 2026-10-18

### Fixed
- **The 3.5 mm MIDI IN jack works.** It was switched off in the platform FM Lab is built on, so a keyboard
  or pad controller on the jack played nothing; only USB-MIDI was heard. The jack now feeds the same input
  as USB — the GLOBAL `MIDI CHANNEL` filter and `PROG CHANGE` apply to both. It takes notes and program
  changes; SysEx and MIDI out stay on USB. Through a TRS-to-DIN adapter: if nothing plays, try the other
  type (A / B). The reader is Felucca's, as dxsloop carries it.

## 1.0.0 — 2026-10-18

The first release: a six-operator FM synthesizer for the M-VAVE FM-1, built to learn FM on.

- The DX7 engine (Dexed's, by way of dxsloop): 8 voices, 32 algorithms, feedback, per-operator envelopes,
  keyboard scaling, the LFO and the pitch envelope
- The algorithm drawn live, a spectrum analyser and a scope, and envelopes drawn to scale
- Every parameter under a knob with the sound playing; operator mute, solo and an A/B compare
- Operator waves, a global amp envelope, a filter, and chorus, phaser, delay and reverb
- A small arpeggiator and a one-phrase looper with a step view
- 20 factory voices and 32 user slots; DX7 SysEx in and out; MIDI channel and program change
- The firmware version on the GLOBAL page
