# Generative ARP experiment: memory and harmonic journeys

Experimental, session-only implementation on **`experiment/generative-arp`**,
branched from nightly. The earlier contour/DP
planner is replaced. The first single-chord memory comparison was not musically
accepted: the user requested more variety and depth in the progressions. These
controls now include a shared harmonic source and complete harmonic journeys;
listening and hardware evaluation are still required.

Hold **ARP** for about 0.7 seconds to open GENERATE. This only opens the controls.
Turn ROLE to enable a track; PLAY starts generation. HOME leaves it running. A
short ARP tap opens the ordinary arpeggiator. ROLE OFF returns the track to its
stored pattern. Kits cannot generate melodic parts.

Enabled tracks bypass pattern/motion playback and reject REC, REC+PLAY and
retrospective capture. Existing patterns remain intact; other tracks work
normally. Generation is not stored in a project or sent as MIDI notes. Project
load and power-off clear it, including kept ideas. Record audio to preserve a take.

## Controls

Turn SELECT through these four pages:

| Page | Knob 1 | Knob 2 | Knob 3 | Knob 4 |
| --- | --- | --- | --- | --- |
| GENERATE | ROLE: OFF / ARP / BASS / CHORD / ANSWER | RATE: note division | FILL: exact onsets | CHANGE: melodic memory replacement, 0–100 |
| GEN PHRASE | LENGTH: 1–16 clock slots | GATE: fraction of one slot | RANGE: 1–4 scale periods | COLOR: preferred tones toward full scale, 0–100 |
| GEN CHORD | HARMONY: HOLD / NEAR / PATH | EVERY: 1 / 2 / 4 / 8 / 16 bars | VOICING: TRIAD / 7TH / OPEN / 9/11 / SUS2 | SOURCE: LOCAL / T1 / T2 / T3 / T4 |
| GEN ANSWER | ANSWER: ECHO / REVERSE / TURN / INVERT / BRANCH / CHORD | WHEN: ALWAYS / ODD / EVEN / 4TH | DYNAMICS: FLAT / PHRASE | STRUM: 0–100 ms |

- **ARP** holds at most one generated melodic note. **BASS** favors the current
  root and fifth scale degree interval. **CHORD** plays a bounded voiced chord:
  three notes for TRIAD/OPEN/SUS2, four for 7TH/9/11. Mono sounds use one voice.
  These are held-note limits; sound releases/reverb can continue after note-off.
- **FILL** produces exactly `min(FILL, LENGTH)` Euclidean onsets per phrase;
  FILL zero is silent. Melody changes cannot secretly change rhythm or length.
- **GATE** is finite and never exceeds a slot. Repeated pitches retrigger. Tiny
  gates round to the minimum audible control duration. A long synth release is
  separate from note length.
- **CHANGE zero** repeats the retained melodic memory. Raising it gradually
  replaces decisions at phrase boundaries. It does not turn ARP/BASS into chords.
  STOP/PLAY retains the evolved memory and harmonic position, restarting rhythm.
- **COLOR zero** restricts melodies to preferred tones; higher values explore
  more of the selected scale. The first onset favors the current chord (the root
  for BASS) and takes a nearby register. RANGE/COLOR reshape the same decisions;
  returning their settings recovers a locked idea while harmony is unchanged.
- **RANGE one** covers a scale period upward from the played register; larger
  ranges expand around it. Edge-of-MIDI pitches are filtered to playable notes.
  RANGE shapes melodic roles; chord register comes from the input and voicing.
- Tap a note to set a starting register/root. Hold several notes to establish a
  preferred palette on a LOCAL part; releasing them latches it. The next first
  press replaces it. On linked parts, played notes set register while the source
  controls harmonic pitch classes. Input does not play extra notes or record.
- **HOLD** keeps harmony still. **NEAR** chooses related chords, favoring common
  tones and small pitch movement. **PATH** plans eight harmonic changes, including
  departure, preparation, tension, a deceptive middle and return. Its choices
  vary by seed; it avoids exhausting the journey on two repeating chords.
  Diatonic scales use functional families; other scales use a pitch-distance and
  common-tone graph with a planned return, without forcing Western functions.
- Chromatic and pentatonic chord shapes choose nearby legal thirds/fifths by
  pitch distance; they do not mistake two scale steps for a third in every scale.
- **9/11** uses seventh, third and ninth with the fifth omitted to fit four
  voices. On diatonic chords where that ninth would be flat, it uses an eleventh
  instead. Voicings minimize movement between parts of successive chords.
- **EVERY** counts four-beat bars. The LOCAL source applies a due change at its
  next phrase boundary. Followers update their harmonic context at that same
  timestamp, keeping independent rhythms, register and finite gates. Already
  sounding notes finish their gate; future onsets follow the new harmony.
- Linked tracks require the same scale, root, transpose and effective tuning
  mode. SOURCE chains are supported; cycles or unavailable/disabled/kit sources
  suspend affected parts with **GEN SRC**. Incompatible tuning shows **GEN KEY**.
  Fix the source or use **Match source key** in the tools. A track cannot select
  itself as its source. LOCAL always lets it run independently.
- RATE/LENGTH/FILL/GATE/RANGE/COLOR/VOICING/SEED commit at the next phrase boundary.
  Played gestures take effect at the next onset. Source routing applies on the
  next scheduler call. The shared header shows **GEN NEXT** for queued changes,
  **GEN LOCK** for locked melody, **GEN RUN** for evolving melody, **GEN STOP**
  while stopped; source/key errors take priority. Melodic lock does not freeze
  harmonic movement—use HARMONY HOLD on the source for that.

## Answering and developing a part

**ROLE ANSWER requires an explicit SOURCE track.** It waits for that track's first
completed audible phrase (**GEN WAIT**), then derives its material from the heard
onsets, excluding rests. Each answer retains its own LENGTH/FILL/RATE/GATE and
register. It copies the source material at its own phrase boundary, so an answer
stays recognizable for that phrase. New harmony remaps the retained relative
intervals into the active chord context. No history grows beyond sixteen notes.

ANSWER styles on GEN ANSWER:

- **ECHO** follows the source's relative intervals in its own register/harmony.
- **REVERSE** reverses the heard onset order; **TURN** rotates it by half;
  **INVERT** reflects the relative intervals around the active root.
- **BRANCH** uses a bounded Fibonacci grammar (`A -> AB`, `B -> A`). A follows
  the parent order; B reverses it and nudges one selected-scale step. Over twelve
  phrases the grammar grows through five generations, retreats, and returns to
  the parent. At most thirteen symbols shape the fixed phrase; this cannot
  extend LENGTH, add onsets or create extra held notes.
- **CHORD** is a single-note harmonic companion: it follows the source contour
  using nearby tones of the current chord. It does not become a polyphonic pad.

**WHEN** applies to all roles. ALWAYS plays every phrase; ODD plays the first,
third, etc.; EVEN plays the second, fourth, etc.; 4TH plays every fourth phrase.
FILL remains exact on active phrases; inactive phrases are intentionally silent.
History retains the last completed audible phrase during a source's silent turn.
**DYNAMICS** chooses constant velocity or restrained phrase accents. **STRUM**
staggers CHORD's onsets only, shrinking when necessary to retain audible finite
gates inside the slot. STOP, disabling, and source errors cancel pending notes.
These expression/style settings commit at the part's next phrase boundary.

For a conversation, use the quartet below but set Track 1 to ANSWER, SOURCE T4,
LENGTH 8, FILL 4, ANSWER REVERSE, WHEN EVEN. Set Track 4 WHEN ODD. For ambient
branching, set Track 1 LENGTH 13, WHEN ALWAYS, ANSWER BRANCH; restore Track 4 WHEN
ALWAYS. Try ANSWER CHORD for the quieter harmonic companion. Set the pad STRUM
around 45 ms for gentle movement. Keep/return includes the answer parent history
and its development phase as well as the linked harmony.

## Keep a discovery

On a generator page, hold **OCT+** to open Idea tools:

- **Keep idea:** snapshots the whole linked group: memories, random histories,
  heard phrase settings, register, tuning and harmonic plan. This is temporary
  session memory, not pattern recording or a saved project.
- **Return to kept:** restores the group together at its original source's next
  phrase boundary. Parts with different lengths restart together. While stopped,
  restoration is immediate. An incomplete/replaced group snapshot is rejected.
- **Remix melody:** rearranges retained decisions without admitting fresh ones.
- **New idea:** changes the seed and builds fresh melodic memory and harmonic
  choices at the next phrase boundary.
- **Remix rhythm:** rotates the exact-onset rhythm independently of pitch memory.
- **Lock melody:** sets CHANGE zero, retaining the current melodic decisions.
- **Seed:** directly select another repeatable starting identity.
- **Match source key:** copies scale/root/transpose/quantization from the selected
  source. This explicitly changes the follower's tuning settings.

## Soundscape: progressing quartet

Use the same **C MAJOR** on all four tracks. Begin dry so you can hear chord changes.
Set tempo to **90 BPM**, choose POLY sounds, and keep levels moderate.

1. **Track 4 — harmonic source and high bells.** Choose a soft bell. Hold ARP;
   ROLE ARP, RATE 1/8, FILL 3, CHANGE 12. On GEN PHRASE: LENGTH 8, GATE about
   58%, RANGE 1, COLOR 18. On GEN CHORD: HARMONY PATH, EVERY 2 bars, VOICING
   9/11, SOURCE LOCAL. Tap a high C.
2. **Track 2 — moving pad.** Choose a soft pad. ROLE CHORD, RATE 1/2, FILL 4,
   CHANGE 0; LENGTH 4, GATE about 91%; HARMONY HOLD, VOICING 9/11, SOURCE T4.
   Tap middle C. If GEN KEY appears, choose Match source key in Idea tools.
3. **Track 3 — bass.** Choose a rounded bass. ROLE BASS, RATE 1/4, FILL 2,
   CHANGE 0; LENGTH 8, GATE about 71%, RANGE 1, COLOR 0; SOURCE T4. Tap C two
   octaves below middle C. This part follows roots, independently of pad rhythm.
4. **Track 1 — exploring melody.** Choose a quiet mallet. ROLE ARP, RATE 1/8,
   FILL 5, CHANGE 12; LENGTH 13, GATE about 58%, RANGE 1, COLOR 18; SOURCE T4.
   Tap middle C. Its thirteen-slot phrase drifts against the harmony's bar grid.
5. PLAY for at least 45 seconds. Listen for departure and a return after eight
   changes. Keep idea from any group member. Try Remix melody on Track 1, then
   Return to kept to recover the ensemble together.
6. Try C MINOR on all tracks, slower tempo (72 BPM), softer levels and moderate
   reverb. Keep the same routing. Allow about a minute for the whole journey.

The rendered `journey-major-dry`, `journey-minor-dry`, and `journey-ambient` WAVs in
`build/generator-harmonic-journeys` use these relationships. They are native engine
renders, not recordings from flashed hardware.

## Scope and validation

The memory component adapts Marbles' random sequence algorithm with its MIT
notice intact. A pinned upstream C++ reference checks 327,680 decision transitions.
The scale mapper, harmonic journey, role routing, scheduler and UI integration
are original Melodee work. Reference provenance is in `tests/reference/README.md`.
No claim of historical uniqueness is made.

Tests cover all scales and pitch limits, deterministic small/batched scheduling,
finite gates, recording isolation, keyboard/MIDI ownership, source chains/cycles,
tuning compatibility, group KEEP/RETURN and project reset. Native renders check
output health and release on STOP. These do not establish musical quality or
physical-device timing/controls. Answer/branch/companion renders need listening
feedback; versioned project persistence remains deferred until the musical model
settles, as specified in
[GENERATIVE_ARP_PLAN.md](GENERATIVE_ARP_PLAN.md).
