# Generative ARP experiment: an endless jam band

Experimental, session-only implementation on **`experiment/generative-arp`**,
branched from nightly. Native renders exist; listening on hardware is still
required. Plan, research and history: [GENERATIVE_ARP_PLAN.md](GENERATIVE_ARP_PLAN.md).

Hold **ARP** for about 0.7 seconds to open the generator pages. This only opens the
controls. A short ARP tap still opens the ordinary arpeggiator. HOME leaves the
generator running. Kits cannot generate; they keep playing their patterns, so a
drum track makes a natural rhythm section for a jam.

## Quick start: a backing band in one turn

1. Choose sounds on your tracks (a pad or keys, a bass, a pluck or bell, a lead).
2. Select the track whose key you want, hold ARP, turn **JAM** to a feel.
3. Press PLAY. Play along; never stops, keeps changing.

Turning JAM from OFF to a feel makes that track the band leader (CHORD unless it
already has a role) and every other synth track joins: free tracks take the
missing roles in track order, **BASS**, then **ARP**, then an **ANSWER** to the
arp, each in its own register around the leader's home note, all in the leader's
key (scale, root, transpose and tuning are copied). Tracks that already generate
keep their roles and follow the leader. Switched on while playing, the band comes
in together on the next bar line.

The leader writes a song: sections of about 4, 8 or 16 bars (INTRO, VERSE,
CHORUS, BRIDGE, BREAK) from three themes. A theme is a progression, a change rate
and each part's style; a returning chorus plays its own progression and the same
melodic seed, so you recognise it. Each section has its own energy: the intro and
breaks thin out (parts rest), choruses lift, and the second half of a section that
leads into a chorus builds. After a whole form, VARY renews some themes, so the
song keeps moving without losing its identity. The header shows the section.

### JAM page (any member edits the leader's)

| Knob | Control | What it does |
| --- | --- | --- |
| 1 | **JAM** | OFF, POP, ROCK, FUNK, JAZZ, BALLAD, AMBIENT, EPIC, BLUES. Another feel starts a new song at the next chord change. |
| 2 | **DRIVE** | Energy: rhythmic density, faster subdivisions, wider register, velocity, shorter gates. |
| 3 | **SPICE** | Harmonic richness: triads -> add9/shell -> sevenths -> 6/9, 9/11, quartal (by feel), and more passing colour in melodies. |
| 4 | **VARY** | How fast the song changes: section length (16, 8, 4 bars), theme renewal each form, melodic evolution. Zero holds the current section forever. |

The feels choose progressions, part styles and timing:

| Feel | Progressions | Parts |
| --- | --- | --- |
| POP | POP, AXIS, DOOWOP, ROYAL | 1/8 arps (UP, BOUNCE, PINKY) or a melody, root/octave/fifth bass, quarter chords, light strum |
| ROCK | VAMP, RISE, POP, FALL | eighth strums (alternate down/up strokes), root bass, THUMB/STAIRS arps |
| FUNK | VAMP, one-chord HOLD, JAZZ, MARKOV | syncopated sixteenth stabs (rotated Euclidean rhythms), octave bass |
| JAZZ | JAZZ (ii V I vi), CIRCLE, MARKOV | walking bass, triplet comping or stride, shell/7th/9 voicings, melodic DRIFT |
| BALLAD | CANON, FALL, DOOWOP, ROYAL | half-note chords, ALBERTI arps, fifth/arp bass, wide strum |
| AMBIENT | PATH, NEAR, MARKOV | whole-note pads, pedal bass, slow drifting bells, open/quartal voicings |
| EPIC | POP, RISE, FALL, ROYAL | sixteenth arps, octave bass, wide voicings |
| BLUES | twelve-bar BLUES, VAMP, JAZZ | shuffle (8T Euclid 8 of 12) stride chords, boogie/walking bass |

JAM writes the parts' ordinary settings (STYLE, RATE, LENGTH, FILL, GATE, RANGE,
COLOR, VOICING, WHEN, DYNAMICS, STRUM, SEED; HARMONY and EVERY on the leader), so
every change commits at the part's own phrase boundary and you can watch the knobs
move. Hand-tuning a part lasts until the next chord change; turn JAM OFF to keep
your own settings (the band keeps playing as it is).

**New idea** (Idea tools) on any member starts a new song at the next change.
**Keep idea / Return to kept** snapshot and restore the whole band, including the
song form and section.

## Keep what you hear: hold REC

Holding **REC** on a generating track captures what it just played into the
track's pattern, exactly like capturing notes you played without recording (the
same window and quantising rules; it is undoable). Generation keeps running, so
the jam goes on; turn ROLE OFF (or JAM OFF and ROLE OFF) to hear the captured
pattern. Capture one track at a time: select each band member and hold REC.
Tapping REC, REC+PLAY and live recording stay disabled on generating tracks.

## Pages

Turn SELECT through five pages:

| Page | Knob 1 | Knob 2 | Knob 3 | Knob 4 |
| --- | --- | --- | --- | --- |
| GEN JAM | JAM | DRIVE | SPICE | VARY |
| GENERATE | ROLE: OFF / ARP / BASS / CHORD / ANSWER | STYLE: the role's own list | RATE | FILL: exact onsets |
| GEN PHRASE | LENGTH: 1–16 slots | GATE | RANGE: 1–4 scale periods | COLOR: chord tones toward the full scale |
| GEN CHORD | HARMONY | EVERY: 1 / 2 / 4 / 8 / 16 bars | VOICING | SOURCE: LOCAL / T1–T4 |
| GEN EVOLVE | CHANGE: memory replacement 0–100 | WHEN: ALWAYS / ODD / EVEN / 4TH / REST | DYNAMICS: FLAT / PHRASE / SWELL / HUMAN | STRUM: 0–100 ms |

The page picture shows the chord's notes, the phrase (height = pitch, bright =
playing) and the leader's harmonic cycle as numerals from home, the sounding
chord bright.

### Styles

- **ARP:** DRIFT (Marbles-style memory melody, below), UP, DOWN, BOUNCE, INWARD
  (outside-in), PINKY (alternating with the top note), THUMB (alternating with the
  bottom), ALBERTI (low-high-mid-high), STAIRS (overlapping rising threes). The
  patterns arpeggiate the current chord (or a latched multi-note palette) across
  RANGE octaves, one onset at a time; COLOR lets memory-chosen offbeats take the
  scale note above, CHANGE evolves which ones.
- **BASS:** DRIFT (root-and-fifth weighted memory), ROOT, OCTAVE, FIFTH (root,
  fifth below), WALK, ARP (1-3-5-8-5-3), BOOGIE (1-3-5-6-7-6-5-3), PEDAL (home
  note under every chord). **WALK** sees the next chord before it arrives: it
  steps through the bar from the root, chord tones on strong beats, and approaches
  the next root from a scale step, keeping its octave close to the last note so
  the line keeps walking. When the chord stays, it plays one of four classic
  figures (1-3-5-6, 1-2-3-5, 1-7-6-5, 1-5-6-7).
- **CHORD:** BLOCK (the voicing on each onset) or STRIDE (the root below the
  voicing on even onsets, the chord between). Strummed chords alternate down- and
  up-strokes and soften toward the last string.
- **ANSWER:** ECHO / REVERSE / TURN / INVERT / BRANCH / CHORD (below).

**DRIFT melodies** keep Marbles' repeatable memory but shape each draw like a sung
line: steps and thirds are likelier than leaps, a leap is followed by a step back
(gap fill), strong beats favour chord tones and the phrase's last onset resolves on
one (the root most often). RANGE/COLOR still reshape the same decisions reversibly.

### Harmony

**HOLD** stays. **NEAR** chains related chords (common tones, small movement).
**PATH** plans an eight-change journey through Western function families with a
deceptive middle and a return; other scales use a pitch-distance graph. **MARKOV**
draws from Walter Piston's table of usual root progressions (a minor-key reading
for minor homes), avoids repeating heard chords, prepares the return with a
dominant or plagal chord and comes home every eight changes. Named progressions
(scale-degree offsets from your home, read in major or minor by its third):

| Name | Major home | Minor home |
| --- | --- | --- |
| POP | I V vi IV | i VI III VII |
| AXIS | vi IV I V | iv VI i VII |
| DOOWOP | I vi IV V | i VI iv v |
| CANON | I V vi iii IV I IV V (Pachelbel) | same degrees |
| ROYAL | IV V iii vi (royal road) | VI VII v i |
| CIRCLE | I IV vii iii vi ii V (descending fifths) | same degrees |
| JAZZ | ii V I vi | ii v i VI (V in harmonic minor) |
| FALL | IV iii ii I | i VII VI v (Andalusian; V in harmonic minor) |
| RISE | I ii IV V | i iv VI VII |
| VAMP | I IV | i iv (Dorian: i IV) |
| BLUES | twelve-bar I I I I IV IV I I V IV I V | same degrees |

A named progression starts on its own first chord at PLAY or when you tap a new
home note. A diminished chord where a progression expects a stable one becomes the
chord a third below (vii -> V) or above; CIRCLE and JAZZ keep their colour. Named
progressions and MARKOV need a twelve-tone seven-note scale; other scales and
tunings use PATH's graph. Every moving harmony plans its whole cycle ahead, so
followers can see the next chord.

**VOICING:** TRIAD, 7TH, OPEN, 9/11, SUS2, ADD9, SUS4, 6/9, QUARTAL, SHELL (root,
third, seventh). Voices move as little as possible between chords, with a gentle
pull back to the played register so long journeys never drift up or down.

### Timing

Every part's slots carry their remainders exactly, so parts at different rates
stay on one bar grid however long a jam runs; a part that starts while the
transport runs waits for the next bar line. FILL is exact (Euclidean); WHEN REST
silences a part's phrases (JAM uses it for intros and breaks).

## Manual ensembles (JAM OFF)

Everything still works by hand: give tracks roles, link followers with SOURCE,
choose HARMONY on the LOCAL leader. ANSWER requires an explicit SOURCE and waits
(**GEN WAIT**) for its first completed phrase. Linked tracks need the same key
(**GEN KEY**, fix with Match source key); cycles or missing sources show **GEN SRC**.
The header shows GEN NEXT / GEN LOCK / GEN RUN / GEN STOP as before.

ANSWER styles: **ECHO** follows the source's intervals in its own register;
**REVERSE** reverses the heard order; **TURN** rotates it by half; **INVERT**
mirrors intervals around the root; **BRANCH** grows a bounded Fibonacci word over
twelve phrases and returns; **CHORD** is a one-note companion on nearby chord tones.

### Idea tools (hold OCT+ on a generator page)

Keep idea, Return to kept, Remix melody, New idea, Remix rhythm, Lock melody,
Seed, Match source key, as before; New idea in a JAM starts a new song.

## Renders

`build/generator-jam/jam-*.wav` (one per feel, 96 s, native engines, role-chosen
FM6/CZ/Prophet presets) and the earlier memory/journey scenes, from:

```sh
cc -O1 -ffp-contract=off -w -Ibuild/gen -Ifirmware/src -o build/host/generator_audio_test tests/generator_audio_test.c -lm
build/host/generator_audio_test build/generator-jam build/host/generator_reference_notes.txt
```

They print each section and its chords. They are host renders, not hardware
recordings, and do not establish musical quality.

## Scope

The memory component adapts Marbles' random sequence algorithm with its MIT notice
intact (reference check in `tests/reference/`). Piston's root-progression habits,
the named progressions, arpeggiator orders, walking-bass practice and Euclidean
rhythms are common musical knowledge; their implementations, the JAM director,
scale mapping, scheduler and UI are original Melodee work. Generation is not stored
in projects; project load and power-off clear it (capture a pattern to keep a part).
