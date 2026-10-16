# Generative ARP: research, implementation plan, and handoff

Updated: 2026-10-16, Europe/Brussels.

## v3 (2026-10-16): evolution and taste

User request after v2.1: "can we ensure that we have some better algorithms for
changing notes and progressions? It still feels like the same type all the time after
a while. I want evolution, self improvement."

Implemented (`firmware/src/generator_evolve.c`):
- **Chord scales:** per-chord seven-note masks (parallel minor/major, mixolydian,
  dorian, lydian, harmonic minor, phrygian; secondary dominants built from the target's
  leading tone and seventh) applied to every linked part through `generator_mask`;
  only masks that keep one note per letter of the key, so degrees keep their meaning.
- **Progression GA** (HARMONY EVOLVE, in every JAM feel): 8 genomes per leader, seeded
  from the feel's progressions and Piston walks; tournament selection, one-point
  crossover, function-aware mutations, one immigrant per generation; fitness = root
  motion + Piston habits + common tones + cadence + SPICE colour budget + section
  tension + novelty vs an 8-entry archive + parent distance when developing + taste.
  The pick never repeats one of the last three. Returning sections develop (2–5 genes).
- **MOTIF** ARP style: phrase-by-phrase development by eight classical operators,
  fittest of eight candidates, 4-motif novelty archive.
- **Taste learning:** progression (10) and melody (6) feature weights; More/Less like
  this (new Idea tools rows), Keep (+1), New (-1), hold-REC capture (+2); learning
  relative to the running feature means. Session-only in `gen_evo` (pool), not rolled
  back by Return to kept. Less like this renews the theme and moves on.
- Template variants pick up a borrowed or secondary chord with SPICE.
- Idea tools trimmed to seven rows (the sheet's fit; v1's eight already overflowed):
  Seed, Remix rhythm and Lock melody rows removed (actions remain).

Measured (host): borrowed colour grows with SPICE (5 -> 20 coloured chords over 16
bred progressions); 13–16 distinct of 16 successive progressions, no repeat of the
last three; 30 likes of coloured progressions raise colour from 16 to 112 per 30
sections. Tests: `evolution()`. Image 551,668 B (+8.4 KB); arena 157,408 B. Full
suite passed (79 s). Not yet heard on hardware.

Next: listen; tune fitness weights by ear; consider persisting taste in settings;
tritone substitutions and chromatic approach chords need a non-letter-preserving
representation (deferred).

## v2.1 (2026-10-16): JAM variety after the first try

User feedback on v2 (b618fb4): "it is always going the same route instead of
trying new things. Same genre always pushes the same thing, no variation or change
whatsoever." Causes found: each song was seeded from SEED x feel (so one feel always
wrote one song), PLAY restarted the same section, each feel had four progressions
and two chord styles, named progressions played verbatim, and themes only renewed
after a whole form (2–3 minutes) with 50 % chance.

Changes: songs seed from fm1_ms, a running count and the settings; six-entry
progression and arp pools per feel; theme variants (rotation, same-function chord
substitutions, a home on a related degree for bridges and some verses, voicing
step, comping rhythm, register, a second arp style); every return of a theme varies
1–3 aspects with VARY's chance; alternate passes lift; eight forms; the bridge
renews more often; PLAY carries on with the next section; intro states the key;
BLUES keeps its twelve bars and dominates its feel. Measured: 10 of 10 restarts of
one feel differ in their first 32 bars; 600 bars give 46–55 distinct section
settings from 63–77 section starts while themes still return. Tests:
`jam_variety()`. Image 543,284 B; arena 160,288 B. Full suite passed (68 s). Not
listened to on hardware yet.

## v2 (2026-10-16): an endless, self-arranging jam band

Requests that set this stage, in the user's words: "Make it as amazing and
beautiful sounding as possible. Use all possible popular algorithms to generate
chords, arpeggiations, bass lines." Then: "Make it as automated as possible with
generalized tweaking, like a guitar jam backing track that never ends and always
changes. We can pull patterns from holding REC the same way we do normal random
presses without record."

Reading of the first-stage results that shaped v2: the v1 parts were tidy but
generic. ARP was only a weighted random walk, BASS a weighted root/fifth draw, the
only progressions were HOLD/NEAR/PATH, no part could see the next chord, and every
ensemble had to be wired by hand over four pages per track. Parts at different
rates also drifted apart at most tempos (truncated slot lengths), which an endless
jam would expose.

### Implemented (all session-only, no project/preset/editor format change)

- **JAM director** (`generator_jam.c`): JAM on a track forms a band (leader CHORD,
  then BASS, ARP and an ANSWER to the arp on free synth tracks, own registers, the
  leader's key) and writes a song: four 8-section forms over INTRO, VERSE, CHORUS,
  BRIDGE and BREAK; three themes (progression, change rate, part styles, seed,
  comping rotation) with recurring choruses; section energy, dropouts (WHEN REST)
  and a build into each chorus; VARY renews themes per form. Eight feels (POP,
  ROCK, FUNK, JAZZ, BALLAD, AMBIENT, EPIC, BLUES) choose progressions, styles and
  timing. Three macros: DRIVE (energy), SPICE (harmonic richness), VARY (rate of
  change). The director only writes ordinary settings, committed per phrase.
- **Progressions:** MARKOV from Piston's usual root progressions with a cadence
  every eight changes; POP, AXIS, DOOWOP, CANON, ROYAL, CIRCLE, JAZZ, FALL
  (Andalusian in minor), RISE, VAMP, twelve-bar BLUES, read in major or minor by
  the home chord's third; diminished substitutes; every moving harmony plans its
  cycle ahead (`generator_upcoming`). Restricted to twelve-tone heptatonic scales;
  other scales and tunings keep PATH's pitch graph.
- **Styles:** ARP DRIFT/UP/DOWN/BOUNCE/INWARD/PINKY/THUMB/ALBERTI/STAIRS; BASS
  DRIFT/ROOT/OCTAVE/FIFTH/WALK/ARP/BOOGIE/PEDAL with a look-ahead walking bass;
  CHORD BLOCK/STRIDE; strums alternate down/up strokes.
- **DRIFT melody:** step/third preference, gap fill after leaps, chord tones on
  strong beats, cadence on a chord tone; still a pure function of memory, so
  locked ideas and reversible RANGE/COLOR are kept.
- **Voicings:** + ADD9, SUS4, 6/9, QUARTAL, SHELL; register gravity in voice leading.
- **Dynamics:** + SWELL, HUMAN; a JAM sets a level per part from energy.
- **Timing:** Bresenham slot lengths (no drift between rates); parts starting
  mid-play wait for the next bar line; JAM switched on while playing restarts the
  band on the next bar.
- **Capture:** at the user's request, generated notes now enter the retrospective
  capture ring at their exact times; hold REC keeps a generating part's last bars
  as its pattern while generation continues. The ring grew to 256 events in the
  pool; the capture copy is a filtered 1 KB stack buffer. REC/REC+PLAY stay off.
- **UI:** GEN JAM page first; GENERATE gains a role-specific STYLE slot; GEN
  ANSWER became GEN EVOLVE (ANSWER styles moved to STYLE); header shows the JAM
  section; the page picture's hint row became the leader's progression in numerals.

### Checkpoint (2026-10-16)

- `ALL HOST TESTS PASSED in 66 s`; generator test adds progressions, Markov,
  voicings, every style's order/bounds, DRIFT step/cadence statistics, walking
  approaches, STRIDE, JAM band/sections/bar grid/batch independence/VARY 0/New
  idea/mid-bar join, and generated-note capture.
- UI renders: 0 lint findings, 0 MONO pixels off gray (pre-existing fx_tape
  ellipsis only); section labels verified in both fonts and all palettes.
- Eight JAM renders (`build/generator-jam/jam-*.wav`, 96 s each) pass output
  health, held-voice bounds and STOP release. **Not listened to yet.**
- Build: image 541,448 B (+10.7 KB; body limit 573,440 B); `.data+.bss` 94,744 B
  (-1 KB); pool 171,336 B; audio working arena 160,512 B (-2.5 KB, minimum 142,336).
- Not pushed, not flashed.

### Next (ordered)

1. Listen to the eight renders; tune per-feel tables (rates, densities, levels,
   which progressions belong to which feel) by ear. Then a hardware audition.
2. Drums for kit tracks: a JAM-aware groove generator (genre Euclidean templates,
   fills into sections, energy-driven density) so the band includes its rhythm
   section without stored patterns.
3. Optional "band sounds" action: load role-suited presets onto band tracks.
4. Push/anticipation (chords landing an eighth early), swing following the
   project, and a turnaround fill on the last bar of a section.
5. Versioned persistence of generator and JAM state once the model settles.

## v1 execution update: implemented experiment, listening and final checks

Current development branch: **`experiment/generative-arp`**, created at the
user’s request on 2026-10-15 from nightly base
`747e10f9e04f11183eea63f03fac96097e5f7bed`, carrying the uncommitted experiment
changes. Keep further experiment work on this branch. The nightly and next refs
were not advanced by branching. Do not push, commit or flash implicitly. Do not reapply the recovery stash. The implementation has replaced the
rejected contour/DP engine; the guide now describes the actual replacement.

Implemented:

- Fixed-point C Marbles random memory adaptation, verified against pinned,
  unmodified C++ source over 327,680 decisions; independent melody/harmony/rhythm
  random streams, locked memory, gradual replacement and remix.
- Weighted selected-scale palette, reversible COLOR/RANGE, latched multi-key
  gestures, strict melodic ownership, finite gates, exact active-phrase FILL.
- HOLD/NEAR/PATH harmony; eight-change journeys with planned return, seeded
  family choices for diatonic scales, common-tone/actual-pitch graph otherwise.
  TRIAD/7TH/OPEN/9/11/SUS2 voicings with economical voice movement. 9/11 avoids
  diatonic flat ninths by choosing an eleventh instead. Chromatic/pentatonic
  chord intervals are selected by actual pitch distance, avoiding ordinal clusters.
- Explicit source chains, exact tuning compatibility, source/cycle failure states,
  Match source key, shared chronological scheduling independent of track order.
- Group KEEP/RETURN including heard settings, routing, RNG histories, harmonic
  paths, answer parent material and development phase. Return commits atomically
  at the kept leader's boundary even when followers finish phrases earlier.
- ARP/BASS/CHORD/ANSWER roles. ANSWER captures up to sixteen heard source onsets,
  retains its parent for a whole phrase and reharmonizes in its own register.
  Echo/reverse/rotate/invert, bounded Fibonacci branching, and chord companion.
- ALWAYS/ODD/EVEN/4TH whole-phrase conditions, flat/phrase dynamics, optional finite
  strum with pending-event cancellation on STOP/disable/source error.
- Four generator pages, action sheet, ongoing shared-header statuses, regression
  checks and native engine renders. No P_COUNT/project/editor format changes.

Listening evidence:

1. The user rejected the initial static memory A/B: **“Neither, I'd have more
   variety and deepness in the progressions.”** Do not present those loops as
   accepted results.
2. After the longer dry minor and ambient harmonic journeys, the user answered
   **“This is a good start!”** This supports advancing the experiment; it does not
   establish final approval or physical-hardware behavior.
3. Answer/conversation, branch and harmonic-companion renders were added after
   that feedback. Their musical usefulness still needs listening evaluation.

## Final validation/package checkpoint (2026-10-15)

- **Final full suite:** `ALL HOST TESTS PASSED in 66 s`.
- **UI:** zero lint findings, zero MONO pixels off gray; both fonts and all
  palettes/page values, source/key/wait/lock/pending status lifetimes covered.
- **Pinned memory reference:** zero mismatches across 327,680 decisions.
- **Native audio:** thirteen scenes pass output-health, held-note bounds and
  STOP-release checks. Dry reference/memory comparisons, solo bass/pad,
  performance actions, progressing major/minor/ambient quartets, conversation,
  branching and companion examples. WAVs: `build/generator-harmonic-journeys/`.
- **Build:** 530,736-byte image; `.data + .bss` 95,752/98,304 bytes; pool 168,872
  bytes; audio working arena 162,976 bytes (minimum 142,336).
- **Exact package:** `build/generative-arp-nightly-20261015/melodee-generative-arp-nightly.fwsc`.
- **Package SHA-256:** `9ececbab7d7e37e3ba6454f5147abca946d9561e5c76b4a2d0aab36395640278`.
- **Source/evidence checkpoint:** same directory, `manifest.json`, `source.patch`,
  `untracked/` and copied build/test/audio/UI reports. Manifest hashes build/test
  inputs and generated headers; documentation is excluded from that input hash.
- **Branch:** nightly at base `747e10f9e04f11183eea63f03fac96097e5f7bed` plus local
  changes. next remains `a743dca6b0dd24929d0c8e45a7107e02f92c8d70`.
- **Not committed, pushed or flashed.** Device identity is not checked in this
  pass. `FM-1_900` is the package's generic identity, not proof of its musical
  revision or current device installation.

Tests include a follower ending before its leader without partially restoring a
queued group RETURN, role OFF cancelling queued RETURN, exact tuning matching,
answer parent/development recovery, clock-batch equivalence, finite strum and
cancellation, and real thirds/fifths in chromatic and major/minor pentatonic roots.
The bounded grammar and harmonic companion are implemented experiments awaiting
listening feedback. Physical audio/controls, device scheduler timing, and musical
assessment across the entire tuning catalogue remain open. Persistence still
waits for the musical model to settle; do not change project formats prematurely.

## Read this first after compaction

Read the v2 section first, then the v1 execution update, before acting on the older
research/proposal sections. The old WEAVE/WALK contour planner is gone; do not start stage 1 again. The current
musical direction is progressing linked parts, with the user's qualified positive
feedback above. Check actual branch, working tree and package state.

[GENERATIVE_EXPERIMENT.md](GENERATIVE_EXPERIMENT.md) now documents current controls
and soundscape recipes. Persistence remains intentionally deferred until the
musical model settles. Hardware audition, physical controls and worst-case device
scheduler timing are not established by host renders or a successful build.

## User intent and fixed requirements

- Make a distinctive, intuitive instrument for live playing and ambient music.
- Enter generation through holding ARP on a track. Preserve conventional short
  ARP access. Opening the page must not itself enable generation or recording.
- A played note should establish a musical starting point; multiple notes should
  be useful for steering harmony and preferred tones.
- The user explicitly chose **“Explore the selected scale”**, rather than limiting
  melodic output to the played notes.
- Generate compatible next chords and make layered parts work together.
- Enabled tracks use generation instead of stored-pattern playback and recording.
  Keep existing patterns intact. Other tracks must remain independently usable.
- Respect note range, phrase length, rhythmic activity, and note duration. Melodic
  generation must not gradually become sustained polyphonic chords.
- Use proven open-source ideas and investigate what musicians value about them.
  Do not claim historical uniqueness. Distinctiveness must come from the musical
  behavior and playable interaction.

## Checkout, recovery, and deployment state

Verified at this handoff; inspect actual state before further consequential work:

| Item | State |
| --- | --- |
| Repository | `/Users/kerem/code/personal/Felucca` |
| Active branch | `nightly` |
| Local nightly base | `747e10f9e04f11183eea63f03fac96097e5f7bed` |
| Nightly tracking state | 29 commits ahead of `origin/nightly` at the last fetch; do not publish these implicitly |
| `next` | Remains at `a743dca6b0dd24929d0c8e45a7107e02f92c8d70` |
| Experiment | Uncommitted tracked edits and five new implementation/test/guide files |
| Recovery stash | `41fb6d8caf06daeee52113af7ac0028d5d23279e` — original experiment on next, retained after transfer |
| Latest transferred baseline validation | Firmware build passed; `ALL HOST TESTS PASSED in 57 s`; UI had zero lint findings |
| Nightly package | `build/generative-experiment-nightly/melodee-generative-experiment-nightly.fwsc` |
| Nightly package SHA-256 | `3169053d98e5de99c19953f27da5ae76a8e4acfb3ef4c2679c8f0171552b9c06` |
| Package/source manifest | `build/generative-experiment-nightly/manifest.json` |
| Nightly package installation | **Not flashed** |

The previously installed package was revision 2 built on next with local changes:
`build/generative-experiment-v2/melodee-generative-experiment-v2.fwsc`, SHA-256
`7d45eec391c8ba5bb54f159136a4873553611879f616344a2d51d113ee90ad69`.
Its installer and independent info query reported `FM-1_900` running. That identity
is generic; it does not uniquely identify the musical implementation. The user
subsequently rejected its sound. Recheck device state before any installation.

The transfer retained nightly's FX rack, SID, navigation, and recording-status
changes. It resolved overlapping UI edits and removed the transferred renderer's
dependency on the knob-hero UI that nightly had already removed.

Build artifacts are ignored and are not durable documentation. Preserve useful
source and this plan when making later checkpoints. Do not apply the recovery
stash again over the ported experiment without inspecting it.

## Research: evidence and the useful features

Reviews identify promising workflows, not a guarantee that our adaptation will
sound good. Distinguish an independent review from documentation or a performance
report. Do not present general praise for a module as a review of every algorithm.

### Mutable Instruments Marbles — strongest interaction reference

- [MusicRadar review, Philip Wise, 2019](https://www.musicradar.com/reviews/mutable-instruments-marbles):
  praises independent rhythm/pitch locking, reshaping locked material with pitch
  controls, and learning note importance from keyboard input. It also notes the
  lack of reset and finds the dedicated drum mode less inspiring.
- [Resident Advisor review, Mark Smith, 2018](https://ra.co/reviews/22997):
  emphasizes controlled pitch distributions, playable Spread/Steps/Bias controls,
  and gradually introducing fresh information while retaining a loop.
- [Official manual](https://pichenettes.github.io/mutable-instruments-documentation/modules/marbles/manual/).
- Source inspected: [random sequence memory](https://github.com/pichenettes/eurorack/blob/master/marbles/random/random_sequence.h)
  and [weighted quantizer](https://github.com/pichenettes/eurorack/blob/master/marbles/random/quantizer.cc).
- Source repository revision inspected: `f4cfa7697095a28d292bff758beb51deb49b5a86`.

Adopt repeatable decisions, gradual replacement, rearrangement of existing
material, separate pitch/rhythm memories, and reversible shaping of note emphasis
and register. A player should be able to keep a good discovery immediately.

Important incompatibility: Marbles' LENGTH counts repeating decisions; independent
streams can create a longer audible loop. Its manual explicitly documents this.
**Melodee LENGTH remains literal clock slots.** Do not port this ambiguity.

### Stochas — understandable variation and conditions

- [KVR user reviews](https://www.kvraudio.com/product/stochas-by-surge-synth-team/reviews/6989):
  users praise the understandable interface, unexpected changes, and combining
  layers. One detailed historical review values seeing other layers while
  composing polyrhythms, but reports host routing limitations.
- [Official features](https://stochas.org/stochas/) document conditional chains,
  probability, independent layers, groove, and mono/poly behavior.
- [Sequencing source](https://github.com/surge-synthesizer/stochas/blob/main/src/StochaEngine.cpp)
  was inspected, including separate mono/poly selection, event scheduling, and
  dependency tracking. Repository revision: `7de0c02bf149184e71f5917d32591fd0dd2d5094`.

Adopt fixed musical anchors with a few understandable alternatives and conditions:
alternate endings, answering after a rest, or responding to another part's event.
Users praising chaining multiple plugin instances is not proof they were praising
the internal conditional-chain feature; keep these claims separate.

### Ornament & Crime — related parts, memory, and chord movement

- [Waveform review, Ian Rapp](https://waveformmagazine.com/waveform-reviews/o_c-se-tall-dog-electronics/):
  praises related patterns from CopierMaschine, freezing discoveries, and changing
  order while retaining note material. Also discusses Quantermain. This is not a
  feature-specific review of Harrington or Automatonnetz.
- [Official manual](https://ornament-and-cri.me/user-manual-v1_3/) documents
  Harrington's chord transformations and Automatonnetz's vector sequencing.
- [Tonnetz implementation](https://github.com/mxmxmx/O_C/blob/master/software/o_c_REV/tonnetz/tonnetz.h)
  and abstract triads were inspected. Repository revision:
  `cfbffab4e2a1b162480264d4ef57c2bcb41580c6`.

Adopt delayed/rearranged relatives of one idea, plus chord transitions that retain
tones and move voices economically. Basic P/L/R transformations retain two triad
tones, but can leave the selected scale. Admit only compatible destinations in
scale-constrained operation. Do not force major/minor Tonnetz theory onto every
scale or tuning.

### Dreamsequence — shared harmony and separate musical roles

- [Documentation and source](https://github.com/dstroud/dreamsequence): one active
  chord is available to downstream sequencers/harmonizers; parts can map into the
  chord or song scale. Also provides voicing, thinning, strumming, and dynamics.
- [Independent performance report, Thermo, post 964](https://www.elektronauts.com/t/share-your-tunes-part-2/223503?page=46):
  an enjoyable single-take session using chords, bass, background arpeggiation,
  and a harmonized melody across multiple instruments. This is a practical user
  report, not a formal review or a controlled evaluation of individual features.
- `lib/generator.lua` and `lib/theory.lua` were inspected. Repository revision:
  `c279ac2f43a428b52c28e191958da1eb7c3b318e`.

Adopt one harmonic context for explicitly linked tracks, with independent musical
roles and rhythms. Study the architecture; do not blindly copy its randomization
algorithms or unbounded retry loops into our real-time firmware.

### Flora — longer development and an accompanying voice

- [Documentation and source](https://github.com/jaseknighter/flora) and
  [expanded documentation](https://norns.community/flora/): L-system phrase
  development, rests, parameter sequencing, and the Tintinnabuli-inspired Tinta
  accompaniment.
- [Ambient performance report](https://www.reddit.com/r/synthesizers/comments/lzdfxp/)
  and [a released track crediting Flora as its foundation](https://gooutsidemusic.bandcamp.com/track/flora)
  support creative usefulness. They do not establish which individual feature
  produced the favorable result. Independent feature-review evidence is limited.
- [Creator demonstration](https://vimeo.com/496481575).
- `lib/l_system.lua`, `lib/plant_sounds.lua`, and `lib/tinta.lua` were inspected.
  Repository revision: `1a708fde8140ccc6861520b7b457ae021d23bc68`.

Adopt bounded development through related branches and a harmonic companion voice
as later experiments. We need neither the plant graphics nor a Lua runtime. Even
Flora caps expanded sentence length; do not allow unbounded growth in firmware.

The lines discussion pages for Dreamsequence and Flora could not be read through
the web tool because access was blocked. Do not invent feedback from those threads.

## Proposed engine

### 1. Preserve a strict timing and ownership foundation

- LENGTH remains 1–16 clock slots initially. FILL means exactly
  `min(FILL, LENGTH)` onsets; zero is silence.
- RATE, LENGTH, FILL, RANGE, GATE, and voicing edits commit at documented
  boundaries, with visible pending state.
- Gates are finite, retriggered correctly, and bounded by the next slot/phrase.
  Preserve the minimum audible gate handling for very short durations.
- Melodic roles own at most one generated held note. Chord roles own a bounded
  voicing; earlier voicings never accumulate held ownership.
- Pitch evolution does not autonomously change rhythm, rate, length, or gate.
  Swing/timing variation starts disabled and must be explicitly selected.
- Sound release and FX may ring after note-off. Test these tails against the
  shared voice budget as well as testing held-note counts.

### 2. Replace the contour/DP planner with musical memory

- Store a fixed-length sequence of normalized decisions separately from rendered
  pitches, timing, and note ownership.
- REPEAT reuses decisions; EVOLVE admits a controlled amount of fresh material;
  REMIX changes order without replacing the stored material.
- Keep pitch and rhythm memory independent. Default to a stable rhythmic phrase.
- Make range and note emphasis transformations reversible: returning controls to
  their previous settings should recover the mapped idea if its memory is locked.
- KEEP/RETURN includes decisions, random state, applicable settings, and harmonic
  state. Linked-group snapshots restore related parts together.
- Specify STOP/PLAY semantics explicitly. A seed is not a substitute for saving
  an evolved phrase. Prefer replaying retained material until NEW is requested;
  verify this behavior in the prototype before finalizing the UI contract.

### 3. Build a weighted palette from musical input

- One note establishes a root and a scale-derived chord.
- Several notes establish preferred tones; do not let each key simply overwrite
  the previous root. Define chord collection, release, and latching behavior.
- Keep physical/MIDI input ownership separate from generated-note ownership.
- COLOR moves from strong preferred/chord-tone emphasis toward exploration of
  the full selected scale. RANGE controls register around the input.
- Consider learned note-frequency weights later. They update generator memory,
  never the pattern recorder, and must not accidentally erase the chosen scale.

### 4. Share a deliberate harmonic context

- LOCAL tracks remain independent; a linked track follows a named source track.
  Track selection in the UI must not change the harmonic source.
- Make scale/root/tuning ownership explicit for linked tracks. Resolve mismatched
  tunings visibly rather than silently mapping incompatible pitches.
- Reject source cycles; handle a disabled/deleted/reassigned source predictably.
- Advance shared harmony before rendering dependent events at the same timestamp.
  Results must not depend on track index or clock-batch size.
- Enumerate scale-compatible chord candidates, favor common tones and economical
  voice movement, and avoid uncontrolled register drift.
- HOLD retains the chord; NEAR seeks a related chord; PROGRESS follows a short
  harmonic phrase with a planned return. HOLD is the default.
- Scope major/minor transformation and progression models to suitable scales.
  Other scales use their own degree relationships. Microtonal movement compares
  actual pitches in the selected tuning, not rounded pitch classes.
- Define how a locked relative melody follows a changing chord. Melody lock alone
  need not freeze absolute pitch; locking harmony as well must repeat exact notes.

### 5. Add roles and related expression incrementally

- Start with ARP, BASS, and CHORD. OFF disables generation.
- ARP explores the weighted palette; BASS favors the harmonic foundation; CHORD
  uses controlled voicings and respects the sound's voice mode.
- ANSWER derives material from another part using delay, rearrangement, and
  chord-aware mapping. Bound history and resolve source ordering explicitly.
- Introduce a few Stochas-inspired conditional behaviors, not a large probability
  editor. Preserve the meaning of FILL and fixed rhythmic anchors.
- Add optional strum and restrained dynamics after the basic roles sound good.
  Strummed events still need finite releases and exact cancellation on STOP.
- Later, try a Flora-inspired branching style and a constrained companion voice.
  Compile a bounded phrase within the selected length/activity/range; grammar
  expansion must not silently increase phrase duration or note density.

## Interface — implemented experimental mapping

Hold ARP opens five pages (v2; v1 had four, without GEN JAM and STYLE):

| Page | Knob 1 | Knob 2 | Knob 3 | Knob 4 |
| --- | --- | --- | --- | --- |
| GEN JAM | Jam feel / OFF | Drive | Spice | Vary |
| GENERATE | Role / OFF | Style (the role's own) | Rate | Fill |
| GEN PHRASE | Length | Gate | Range | Color |
| GEN CHORD | Harmony | Every (bars) | Voicing | Source |
| GEN EVOLVE | Change | When | Dynamics | Strum |

Use the existing action sheet for KEEP, RETURN, REMIX, NEW, and lock operations.
Make selected-track versus linked-group scope visible. Avoid accidental loss of
the current material while browsing choices. Never overload normal REC to keep a
generator phrase; REC remains excluded on generating tracks.

Follow `docs/design/README.md` → Status indicators. Ongoing running, stopped,
locked, linked, and pending states belong in the shared header for their state
lifetime. Fit concise labels around transport/BPM/REC; document priority and
verify both fonts. Do not show persistent state as a timed toast.

## Implementation stages and completion gates

### Stage 1 — memory and pitch shaping

- [x] Build a source-faithful memory/quantization reference in a host experiment
  and compare our adaptation under identical musical inputs.
- [x] Replace arbitrary contour templates and the banded DP melody planner.
- [x] Implement independent decision memories, repeat/evolve/remix, weighted scale
  mapping, range shaping, and KEEP/RETURN.
- [x] Implement and test one-note and multi-note input semantics.
- [x] Render comparable dry pluck, bass, and pad examples, then an ambient mix.
- [ ] Audition several seeds and control changes. Assess recognition, useful
  variation, interval movement, rests, and recovery of kept material.

First acceptance deliverable: short audio comparisons with the same tempo, sound,
level, and FX conditions. Listening determines which musical approach advances.
Do not repeat the previous mistake of presenting test success as musical approval.

### Stage 2 — shared harmony and roles

- [x] Add explicit harmonic source/linking, compatible scale/tuning handling,
  source lifecycle, and simultaneous updates independent of track order.
- [x] Add HOLD/NEAR and a suitable PATH model with planned return.
- [x] Add ARP/BASS/CHORD roles and controlled voice leading.
- [ ] Audition two- and four-track examples; verify held and released voice load.

### Stage 3 — conditional answers and expression

- [x] Add ANSWER and bounded related-history operations.
- [x] Add a small set of clear conditional variations and optional strum/dynamics.
- [x] Verify KEEP/RETURN across linked parts, event cancellation, and live changes.

### Stage 4 — ambient branching and durable saving

- [x] Implement bounded Fibonacci branching and a nearby chord-tone companion;
  render each independently.

- [ ] Audition bounded branching and companion-voice approaches independently.
- [ ] Retain only musically useful styles; do not add controls merely to cover
  every source project's feature list.
- [ ] Design versioned generator-state persistence after the musical model settles.
  Keep it separate from recording generated events into ordinary patterns.
- [x] Update the user guide and soundscape recipes to match actual behavior.

## Integration and resource boundaries

Keep and extend the existing correct transport, input, note-off, REC exclusion,
capture exclusion, and pattern-bypass work. Stored patterns and motion remain
intact. Do not route generated events through live recording; since v2 they do
enter retrospective capture (hold REC), as the user asked. Normal arp, ordinary
tracks, song/bank timing, and external MIDI clock must retain their behavior.

The device has four synth tracks sharing a weighted voice budget, not an unlimited
polyphony allocation for each track. Respect the existing allocator and voice
mode. Kits remain outside this melodic experiment initially. Generated MIDI output
is currently absent; adding it would require explicit ownership and release work.

Use fixed buffers, bounded searches, and no unbounded retries or allocation in the
audio interrupt. Precompute candidate/mapping data away from sample rendering;
measure the worst scheduling burst when several phrases/chords change together.

Resource numbers differ by branch. The earlier next build's 1,224-byte data-RAM
headroom is stale for nightly. The transferred nightly build reported:

- Main data SRAM tail: 2,552 bytes.
- Low SRAM tail: 1,892 bytes.
- Total audio resources across separate banks: 171,404 bytes.
- Firmware image: 523,488 bytes.

These are measurements of the baseline, not an approved allocation for new work.
Prefer reusing generator pool state; target small, measured growth and check the
audio working-arena minimum. Do not treat fragmented SRAM totals as one buffer.

Keep the initial replacement session-only and avoid casually extending P_COUNT,
project records, preset formats, or editor protocol. Persistence needs an explicit
versioned design, validation, and compatibility tests in stage 4.

### Owning code and tests

| Area | Current files |
| --- | --- |
| Generator state and music | `firmware/src/generator_state.h`, `firmware/src/generator.c`, `generator_memory.h`, `generator_harmony.c`, `generator_branch.h`, `generator_jam.c` |
| Scale/tuning and chord helpers | `firmware/src/seq.c`, `firmware/src/microtonal.h`, `firmware/src/chord.c` |
| Transport, recording, input routing | `firmware/src/seq.c`, `firmware/src/midi_control.c` |
| Descriptors/pages and lifecycle | `firmware/src/params.c`, `firmware/src/ui.c`, `firmware/src/project.c` |
| Controls and action sheets | `firmware/src/ui_input.c`, existing popup/action-sheet implementation |
| Rendering/status | `firmware/src/ui_draw.c`, `firmware/src/ui_pages.c`, browser/song/scale headers |
| Behavior and sound checks | `tests/generator_test.c`, `tests/generator_audio_test.c` |
| Display checks and suite | `tests/ui_render.c`, `tests/run_tests.sh` |

Extract helpers into additional files only when useful; this firmware uses static
functions in an included-source structure. Check declaration/include order and
source provenance when moving code.

## Verification and installation

Preserve existing coverage and add meaningful checks for new contracts:

- Exact phrase duration, onset count, gate release, and repeated-note retriggering.
- Stable memory when locked, reversible mapping, and complete KEEP/RETURN state.
- Determinism independent of clock batching, unrelated tracks, and DSP randomness.
- Selected-scale/register constraints, pitch boundaries, and microtonal behavior.
- Simultaneous shared-harmony updates, source cycles/lifecycle, and tuning mismatch.
- No generated-note or motion recording; other tracks still record normally.
- STOP/panic, project load, engine/kit switches, and disabling generation release
  ownership correctly.
- Actual rendered-audio behavior over sustained four-track use, including release
  tails, voice stealing, output health, and worst-case scheduling/CPU pressure.
- UI coverage for both fonts/palettes, crowded headers, pending/lock/link lifetime,
  and restoring custom headers after state changes.

Use the repository environment and the firmware build/install skill:

```sh
PYTHON="$PWD/build/venv/bin/python" ./build.sh
PATH="$PWD/build/venv/bin:$PATH" tests/run_tests.sh
```

Record the exact source/package snapshot and final test result. Keep compilation,
automated audio checks, installation, and physical musical/control evaluation
distinct. The current plan puts an audio comparison before another firmware
audition; do not infer a new instruction to flash from an old completed install.
When installation is requested, use the repository installer and verify the final
running identity. Do not add backup/restore steps unless requested.

## Attribution

Follow repository `AGENTS.md`. Independently authored Melodee code is credited to
`Kerem Kilic (Ellic Studio)` with the applicable SPDX identifier. Preserve actual
upstream and retained Felucca notices in mixed files. Adapted third-party code
retains its authorship and licence notices; studying a source does not justify
relabeling copied code as independently authored.

The inspected Marbles memory/quantizer and O_C Tonnetz files carry permissive
notices; Stochas, Dreamsequence, and Flora have GPL notices. Recheck the exact
files/revisions actually used before porting rather than assuming a repository's
top-level licence settles every file's provenance.

## Immediate resume checklist

1. Read this document, inspect the actual branch/worktree and changes, and retain
   nightly's unrelated features. Do not reapply the transfer stash by default.
2. Check the final validation/package checkpoint below, then keep the
   new answer/branch/companion listening gate distinct from the accepted starting
   direction of the longer progressions; do not restart the old contour planner.
3. Keep strict timing and REC isolation while replacing musical decision-making.
4. Update the stage checkboxes and handoff state as work is actually completed.
5. Present listening evidence before claiming musical success or moving to a new
   device audition.
