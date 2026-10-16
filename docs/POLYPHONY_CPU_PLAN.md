# CPU savings plan for more polyphony

Increase the number of simultaneous instrument voices by reducing the work
needed to complete each audio buffer. Preserve musical timing, key response,
existing patches, sound quality and project compatibility. Evaluate all six
paths below, retain measured improvements, then recalibrate the shared voice
budget against the combined result.

Planning baseline: `experiment/recording-release-queue`, commit `b296f57`,
2026-10-16. The recording release queue has passed host tests and a target
build; its CPU benefit still needs device measurement. This plan describes
future implementation and validation.

## Execution order and experiment boundaries

| Stage | Path | First deliverable |
| --- | --- | --- |
| 0 | Current device baseline | Repeatable timing and voice-capacity results |
| 1 | Panel scanning | Cost breakdown and hardware routing feasibility |
| 2 | Control preparation | Cached modulation routing and exact change detection |
| 3 | Core scheduling | Better workload estimates and less completion wait |
| 4 | FX and mixing | Fewer buffer passes and correct effect dormancy |
| 5 | Sequencer indexing | Prepared ownership for recorded step notes |
| 6 | Engine fast paths | Exact specializations for measured expensive cases |
| 7 | Polyphony calibration | Validated voice allowances and combined stress results |

Start each candidate from a recorded, tested parent and keep it separately
switchable for host comparisons. Commit each accepted change separately; run
the combined firmware after each integration. Investigate panel hardware
feasibility early, but continue the software paths if that investigation needs
hardware work. Production defaults change only after the relevant device
checks pass. Firmware installation remains a separate requested action.

## Stage 0 Establish the current device baseline

Use the same clock, audio configuration, patches, input traffic and voice
counts for each before/after comparison. Keep 128-frame halves, 32-sample
control blocks and the current sample rate. Record the exact source revision,
package hash, build flags, device identity, budget and engine assignment.

1. Make a profiling build using the existing audio spans. Measure complete
   interrupt time including nested input, MIDI and USB service; instrument
   controls; each engine; mixer/FX/output; and worker execution and waiting.
   Keep inclusive spans separate so nested work and concurrent cores are not
   added together as if they were serial work.
2. Repeat representative cases in an interleaved baseline/candidate order.
   Record steady load, simultaneous attacks, releases, retriggers, patch/control
   changes, maximum observed time and high-percentile buffer times. Add bounded
   histogram support if required, and measure its own cost.
3. Cover solo instruments and mixed tracks, short and long release tails,
   free and quantized recording, 64/256/1,024 recorded events, overdubbing,
   modulation, insert/shared FX and internal/external clock. Repeat with USB
   audio inactive, stereo streaming and stem capture, plus panel/encoder input.
4. Measure the additional buffer time for one extra voice under each important
   engine/patch combination. This supplies the conversion from saved time to
   useful polyphony; host instruction counts alone do not supply that conversion.
5. Repeat acceptance cases in a normal build with profiling and benchmark
   bypass disabled, ordinary admission active and emergency shedding active.

The hardware deadline is `128 * 544 / 24 = 2,901.33 microseconds`. Use 80%
(`2,321.07 microseconds`) as the target for workloads being qualified for more
voices. Report target failures separately from deadline failures; preserve the
existing overload protection. Historical capped-package results in
[AUDIO_PERFORMANCE.md](AUDIO_PERFORMANCE.md) do not certify this shared-budget
branch or a higher voice allowance.

Reuse the device protocol and diagnostic tools. Adapt the older performance
harness before using it: it currently requires an existing backup directory
and has assumptions inherited from fixed per-track reservations. Tests must
use the current shared budget. Keep temporary runtime fixtures and restoration
in memory, verify persistent data by reads, and avoid autosaves during setup.
Creating a backup is not a prerequisite for this work.

**Exit:** a reproducible baseline, a ranked time breakdown and a list of
workloads whose next additional voice is currently too expensive.

## Stage 1 Reduce panel scanning overhead

Owners: `firmware/hal/fm1_input.h`, `firmware/hal/fm1_led_pwm.h`,
`firmware/hal/fm1_gpio.h` and `firmware/src/main.c`.

An older device trace measured about 10 microseconds per input tick. At
10 kHz this is a material fixed cost on CPU0. Remeasure it on the current
firmware and separate shifting, GPIO reads, LED/PWM setup, debounce, encoders
and diagnostic bookkeeping.

1. Check the board wiring, pin multiplexing, available peripherals and shared
   bus ownership for hardware-assisted shift-register transmission. Include
   latch timing, LED blanking and completion timing in the feasibility check.
2. If hardware shifting is practical, prototype the same 16-bit column word
   and latch sequence through that peripheral. Keep the existing path as the
   comparator. Bound any completion wait inside the input interrupt.
3. Independently precompute column/LED values when their inputs change, reduce
   repeated address/mask arithmetic and remove redundant register operations
   whose ordering is not required by the board.
4. Keep the 10 kHz scan cadence, debounce thresholds, encoder interpretation
   and LED brightness behavior. The current shift routine documents that
   excessively short write-only edges selected neighboring columns; preserve
   its electrical timing until measurements establish a valid replacement.

Validate with `input_test`, target register/residency checks and device tests:
fast chords, bouncy presses/releases, fast encoder turns, all LED brightness
planes, heavy audio load and concurrent USB/TRS traffic. Measure end-to-end
key latency and missed/duplicate inputs, alongside input time and full-buffer
savings. Host input tests cannot establish shift-register electrical timing.

**Exit:** lower measured input cost without a response or LED regression. If
hardware routing is unsuitable, retain only verified software improvements.

## Stage 2 Prepare controls only when their inputs change

Owners: `firmware/src/mod.c`, `firmware/src/voice.c`,
`firmware/src/mix.c` and the engine control preparation functions.

Several engine coefficient caches already exist. Start with repeated work
outside those caches, especially the modulation routing rebuilt in `mod_begin`.

1. Separate static modulation routing from changing source values. Prepare
   validated source/destination mappings, destination ranges, merged parameter
   destinations and per-voice dependencies when the configuration changes.
2. Use a complete small routing key initially. Audit every mutation source
   before relying on generation counters: panel edits, MIDI, automation,
   temporary controls, native editors, loads, engine changes and undo/redo.
3. Cache exact pitch/bend/control results when all their inputs match. Include
   A4 tuning, scale offsets, native patch/function settings, engine macros and
   modulation where relevant. Optimize the quiet and unchanged cases first.
4. Share track-wide results across voices and reduce unnecessary parameter
   snapshots. Preserve private, fully published worker inputs and the existing
   order of modulation, engine controls and FX processing.
5. Keep evolving state running: envelopes, LFO phase/noise, smoothing, glide,
   active automation and FM6 quiet voice state. Cache routing and invariant
   arithmetic without lowering their update rate.

Compare cached and uncached PCM through control edits, automation, imported
native values, tuning, all voice modes, engine changes and undo. Use modulation,
tuning, native-control, engine and concurrent matrix tests. Measure cache hit
rates and costs for both static patches and continuous controller movement.

**Exit:** exact output/state parity and a useful reduction in control time,
with no material penalty in the continuously changing case.

## Stage 3 Improve scheduling across both cores

Owners: `firmware/src/audio_batch.h`, `firmware/src/audio_worker.h`,
engine job descriptors and the mixer preparation pipeline.

The current pipeline already overlaps work across tracks. Its cost table has
one estimate per engine family, including one for all 909 circuits. Improve
assignment within that pipeline rather than adding a second rendering system.

1. Measure sample-job cost by engine, circuit/algorithm and enabled expensive
   features, together with CPU0 control/service cost and descriptor overhead.
2. Build bounded conservative cost classes from those measurements. Avoid
   timing-driven scheduling that changes musical state or adds a profiling
   dependency to production firmware.
3. Compare the existing assignment with the refined estimates. Track the
   completion time of both cores, CPU0 wait, early worker idle time and the
   busiest buffer. Avoid pushing tiny jobs to CPU1 when coordination costs more.
4. Evaluate descriptor preparation/merge overhead and batch boundaries after
   assignment improves. Preserve result accumulation order, private scratch,
   publication barriers and joining before state reuse.
5. Retain worker retirement and serial fallback. A job partially advanced by a
   failed worker must never be replayed as if it had not run.

Use worker fault/wrap tests, genuinely concurrent serial/parallel PCM equality
and the 1,296 ordered engine-assignment host matrix. Device comparisons must
include mixed heavy/light patches and input/USB preemption. Report total work
and elapsed buffer time separately: less waiting can increase usable capacity
without reducing total instructions across both cores.

**Exit:** a shorter completion time for constrained workloads, no material
regression for balanced workloads, and unchanged ownership/fallback behavior.

## Stage 4 Reduce FX and mixer work

Owners: `firmware/src/fx.c`, `firmware/src/mix.c`,
`firmware/src/audio.c` and USB capture paths.

1. Profile dry, insert, shared CHAIN and SPLIT routes independently. Trace
   buffer clears, copies, gain/pan/solo passes, sends, returns and output work.
2. Fuse compatible passes and specialize common zero/unity/default cases.
   Preserve arithmetic order, rounding, clipping and channel routing. Keep
   parallel overlap where merging passes would otherwise increase worker wait.
3. Extend bypasses to selected effects with silent inputs and finished tails.
   Use the effect's actual state and required history; zero wet input alone
   does not establish silence. Continue cheap phase/smoothing work needed for
   the next audible block and wake processing without a discontinuity.
4. Use an exact dormant-state path first. Treat any threshold-based tail
   truncation as a separate sound-affecting proposal rather than silently
   including it in this optimization.
5. Keep limiter/DC protection, stereo spread, solo/mute ramps, metronome and
   USB master/stem semantics intact. Inspect copies within masked-interrupt
   sections as well as copies within the mixer.

Use FX correctness/performance tests, mixer goldens, USB/stem tests and the
concurrent matrix. Device cases include long feedback/reverb tails, silence
followed by an attack, zero/unity gains, hot levels, live sends/routing edits
and multiple effects. Record audible transitions as well as CPU measurements.

**Exit:** fewer measured mixer/FX microseconds with exact retained signal
behavior and correct tails, wakeup and streaming.

## Stage 5 Complete sequencer indexing

Owners: `firmware/src/recording.c`, `firmware/src/seq.c` and recording/edit
operations that change step ownership.

The onset cursor and release queue already handle event timing. `seq_step`
still searches the selected bank for recorded-note ownership each step.

1. Measure that scan and the release queue's overview checks separately from
   synthesis, capture allocation and ordinary step dispatch.
2. Prepare a compact per-step pitch ownership index for active banks. Query it
   when suppressing duplicate overview notes and drum hits. Prefer a bounded
   representation with explicit RAM accounting.
3. Preserve the distinction between original onset step and rounded overview
   cell, wrapped cells and notes outside LEN. Maintain exact pitch ownership
   when many events share a cell or pitch; deleting one must retain the others.
4. Define invalidation/update behavior for capture/replacement, punch-out
   quantization, deletion, erase passes, copy/double, move, undo/redo, load,
   bank switches and LEN changes. Keep expensive rebuilds out of the steady
   tick and measure the longest rebuild/edit burst.
5. Inspect other repeated scans only if the profile identifies them. Capture
   allocation and background jam capture can be separate follow-ups within
   this path; preserve their existing recording and history semantics.

Extend the independent scanner oracle and recording benchmarks. Exercise all
1,024 events concentrated on one track, repeated pitches, sparse banks,
quantization collisions, overlapping releases, edits and external clock.
Use existing recording, note-editor, erase, undo and persistence tests.

**Exit:** identical event/voice behavior, lower step-dispatch cost and bounded
maintenance spikes that do not erase the timing gain.

## Stage 6 Add exact engine fast paths

Owners: `eng_prophet_test.c`, `eng_fm6.c`, `fm6_core.c`, `cz_native.c`,
`eng_sid.c`, `eng_drum.c` and their underlying drum kernels.

Rank actual instrument costs after the infrastructure changes. Work on the
patches limiting the desired extra voice, with a separate comparator for each
specialization.

- **Prophet:** extend existing disabled-waveform and control caches; inspect
  wheel modulation, invariant oscillator/pitch inputs and inactive routes.
  Preserve sync, audio-rate Poly-Mod, filter histories, drift and noise order.
- **FM6:** inspect algorithm/operator dependencies and zero-contribution paths.
  Skip sample work only when feedback/routing and future voice state remain
  exact. Retain all algorithms, engine modes, envelope and quiet-state behavior.
- **CZ:** extend existing tone/pitch caches and specialize unchanged line/window
  cases. Preserve both lines, ring/noise behavior, envelopes and DC histories.
- **SID:** inspect waveform combinations and inactive signal paths beyond the
  existing frequency/filter caches. Keep chip counters, sync, ring and noise
  state advancing correctly.
- **808 and 909:** specialize expensive circuits and constant/zero mixes, share
  invariant preparation, and inspect genuinely finished circuits. Preserve
  resonators, noise sequences, choke/retrigger behavior and concurrent scratch.

Use unchanged golden renders, numerical reference tests, native patch/control
coverage, serial/concurrent parity and per-engine health checks. Test extreme
notes and parameters, controller motion, long tails and randomized patches.
Use recordings/listening for transitions and any case not established by exact
comparison. Keep sample-rate, precision, oversampling and envelope-resolution
changes outside these exact optimizations; any quality tradeoff needs a
separate explicit decision.

**Exit:** measured improvement in the limiting instrument cases with preserved
state and sound, including patches that do not take the new fast path.

## Stage 7 Convert savings into tested voice capacity

Re-run the original workloads after every accepted change and on the combined
candidate. Measure interactions: less CPU0 service work can change the best
core assignment, and fused mixing can change the overlap available to FX.

1. Hold voice counts fixed to establish actual time saved. Record the same
   workload at the original source, each accepted stage and the combined result.
2. Increase requested voices incrementally using the current shared budget.
   Test the extra voice under simultaneous attacks and release overlap, not
   just while holding a settled chord. Include native engine caps and the RAM
   needed for extra voice state.
3. Revisit budget weights and allowance only where the measured workload
   supports it. Preserve protected-note/stealing priorities and CPU1 fallback.
   Keep limits conservative enough for the tested patch/control range.
4. Run the broad host suite and build/residency/memory checks. Re-run the full
   relevant device assignment matrix, then longer stress cases for the slowest
   workloads, USB/clock/controller bursts and playing panel gestures. Start
   with 60-second screening and a proposed 10-minute final stress run per
   representative limiting case; finite testing remains workload-specific.
5. Validate the production package with profiling/bypass disabled. Require zero
   observed deadline misses, late buffers, emergency voice shedding, resource
   failures, worker faults and USB stream errors in the qualified workloads.
   Count normal capacity steals separately; the additional requested voices
   must actually remain available in the capacity tests.

For each result, publish before/after buffer time and headroom, worker wait,
RAM use, errors and sustained voices by engine combination. Include cases with
little gain. Do not add overlapping percentage savings from separate stages or
apply old fixed-capacity guarantees to the expanded voice budget.

## Shared constraints and completion criteria

The baseline build uses 95,888 of 98,304 bytes of data/BSS and leaves a
153,056-byte contiguous audio arena against a 142,336-byte minimum. New caches,
indices and descriptors must fit measured memory limits and the intended
simultaneous engine/FX workloads. Any code relocation must retain the checked
resident audio graph. No new unbounded wait, allocation, sort or lock belongs
in the steady audio path.

Keep projects, preset formats, free recording timing and independent note
lengths compatible. Retain source provenance and applicable notices when
extracting or moving implementations. This work does not require a new UI.
If a status indicator is needed later, follow the shared-header rules in
[the design guide](design/README.md).

A path is complete when its candidates have been measured, the worthwhile
changes pass their correctness/device checks, and rejected candidates have a
recorded reason. Overall success requires a demonstrated increase in usable
polyphony for stated workloads while retaining the required timing margin
and musical behavior. No numerical savings or extra voice count is promised
before the measurements establish it.
