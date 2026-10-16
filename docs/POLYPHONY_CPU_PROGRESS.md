# CPU implementation evidence

Started 2026-10-16 on `experiment/recording-release-queue`, parent `b296f57`.
The original CPU plan is retained in `POLYPHONY_CPU_PLAN.md`.

## Stage 0: measurement infrastructure

Implemented a 64-bin, 64-us histogram of the preceding completed assembly
interrupt envelope. The final bin includes overflow; the maximum remains
exact in TIMER4 ticks. Reset discards the pending sample. Host percentiles
use upper bin edges, with the existing worst TIMER5 plus 4-us allowance;
overflow percentiles use the observed maximum. These are conservative bounds,
not exact quantiles. Inclusive spans and concurrent worker time remain separate.
Histogram cost is included in the following interrupt's measured duration.
Its incremental cost still requires device comparison with profiling disabled.

Seven appended spans separate panel blanking, row reads, shift/latch, LED/PWM,
debounce, encoders and diagnostic bookkeeping. Panel spans include all scans;
the existing `input` span counts only scans nested inside audio. Do not sum
them together. The four-us PWM blanking wait is intentional electrical timing.

`tools/audio_cpu_device.py` uses the shared pool and explicit per-track counts,
including zero for idle tracks. It records package/source/build metadata,
actual budget and engine assignment, raw histogram bins, worker counters,
deadline/80%-target outcomes and normal capacity decisions separately.
It rejects USB errors and lost requested voices for capacity screening.
The older fixed-reservation harness and contract remain historical tools.

The new harness keeps only the serialized runtime and current 604-byte
preferences in bounded host memory (maximum 256 KiB per object). It creates
no backup directory and sends no persistent restore. A laboratory guard blocks
autosave/global mirroring/settings saves and every storage commit. Guarded
preference restoration imports RAM only. Committed storage headers (length,
CRC, generation and existence) are read before/after, independently of live
preference exports. Runtime and preference inventory CRCs must also match.
Restoration failure leaves persistence guarded; host disconnect cannot expire
the guard and save a temporary fixture. The runner requires an interactive
terminal and keeps the original snapshot alive after a failed restoration,
offering reconnection/retry before it can exit. Every RAM transfer rechecks
the guard; a rebooted device cannot accidentally receive a persistent settings
restore. Explicitly typing `ABANDON` discards the host snapshot. Hard process
loss cannot recover that in-memory snapshot. Do not reboot or kill a failed
session without resolving restoration.

Tests must start stopped, with all voices released and no momentary edit or
legacy settings/template conversion pending. The guard rejects active music.
The serialized restore uses the existing project loader, which clears undo
history and resets transport/engine/FX running state; it restores project
content, not sample-exact oscillator state or an interrupted performance.
The guard restores the original project slot on exit. Avoid panel editing
during automated fixtures; physical input tests need their own explicit window.

### Actual baseline configuration

The `b296f57` build log reports FM6 kernels in internal DSP SRAM, data/BSS
95,888 B, pool 178,776 B, contiguous arena 153,056 B. It did **not** enable
the opt-in full audio residency pass. The 142,336-B arena floor in the plan
is the default-layout floor. Full residency uses a different 49,152-B floor.
The first Stage-0 attempt with full residency and weighted admission enabled
failed that linker floor. No floor was lowered and no such package is usable.
Keep this configuration change separate from CPU optimization comparisons.

Stage-0 baseline flags explicitly preserve the prior defaults:
`MELODEE_RAM_AUDIO=0 MELODEE_ADMISSION=0 MELODEE_AUDIO_PROFILE=1
MELODEE_AUDIO_BENCH=1`. The ordinary shared budget remains active during the
new harness's measurements; the fixed-benchmark bypass is never enabled.
`MELODEE_ADMISSION=1` selects different weights and must be measured separately.

Before the requested flash, read-only device inspection found `FM-1_900`, editor version
`MELODEE v1.4.0-rc.4`, 360 MHz, 128-frame halves, budget 16, CPU1 online,
profiling/benchmark/admission switches all zero and USB streams inactive.
Its accumulated counters include prior workloads; they are not baseline rows.
No device fixture was run during that inspection. The subsequent installation
is recorded below.

Build the baseline package and source manifest:

```sh
MELODEE_RAM_AUDIO=0 MELODEE_ADMISSION=0 MELODEE_AUDIO_PROFILE=1 \
MELODEE_AUDIO_BENCH=1 PYTHON="$PWD/build/venv/bin/python" ./build.sh \
  --release 1.4.0-cpu-stage0 --cpu-manifest build/cpu-stage0-build.json
```

Only after a separately requested installation of that exact package:

```sh
build/venv/bin/python tools/audio_cpu_device.py \
  --build-manifest build/cpu-stage0-build.json --output build/cpu-stage0-mixed-01 \
  --assignment 0124 --voices 1,1,1,2 --budget 16 --seconds 60 --repeat 3
```

Solo examples set other counts to zero. Repeat one and two voices, then add
voices individually; interleave installed baseline/candidate packages. Repeat
dry/modulated/shared-Chorus, steady/event bursts, and inactive/stereo USB.
This initial runner does not yet implement stems, sequencer recording fixtures,
external clock, controlled physical input, or complete production qualification.
Those coverage gaps remain Stage-0/Stage-7 work, not assumed passes.

## Stage 1: panel feasibility

Repository wiring is PA4 SER, PA3 SRCLK, PA1 latch for two 74HC595 devices.
The local AC79 SDK's `include_lib/driver/cpu/wl82/asm/spi.h`,
`SPI2_PLATFORM_DATA_BEGIN`, port C, maps DO=PA4 and CLK=PA3, selecting
IOMAP CON1 bit 17 while clearing bit 16. Its DI is PA2, also the LCD backlight;
a prototype must remain transmit-only and must not claim/configure PA2.
SPI0 owns flash; SPI1 owns the LCD; no current firmware SPI2 register owner
was found (only its IRQ number). PWM routing also edits CON1, so preserve
all unrelated bits and install SPI2 routing only after those owners initialize.
SDK code was consulted as hardware documentation; none was copied into a
new implementation.

Routing is feasible enough for a prototype, but no electrical replacement has
been accepted. Preserve GPIO latch and LED blanking, 16-bit MSB-first ordering,
10-kHz cadence and existing debounce. Measure current SRCLK edges before
selecting a conservative baud. Bound two-byte completion polling within one
100-us tick; latch only after completion. A timeout must deselect/recover
without ever sampling an uncertain column. Check all brightness planes,
neighbor-column leakage, chords, encoder turns and concurrent USB/TRS traffic
on hardware before enabling a peripheral path. The original GPIO RMW edges
remain the production comparator. No scan/brightness timing changed.

## Stage 2: opt-in modulation routing cache

### Initial installed revision (`292aa13`)

`MELODEE_MOD_CACHE=1` prepares each track's validated modulation routes,
merged destinations and descriptor ranges. Raw source/destination values,
engine and amount-zero membership fully key the structural mapping. Amounts,
LFO polarity, source values and base parameter values remain live reads at
their original rate. Zero crossings rebuild membership; engine/source/
destination edits invalidate immediately. Mutation paths need no generation
counter. Envelopes, LFO/noise, smoothing and worker publication retain their
original order. The cache occupies 352 B across four tracks, runtime only.

The first candidate keyed every raw amount and made unused matrices 61% slower
and moving amounts 25–46% slower. It was discarded. The revised candidate
keeps the original inactive scan and reads active amounts dynamically.

Host instructions per block, `cc -O2`, 200,000 blocks per row, excluding
synthesis; these do not establish device CPU headroom:

| Routing | Inputs | Reference | Candidate | Change |
| --- | --- | ---: | ---: | ---: |
| Inactive | Static | 54.0 | 55.0 | +1.9% |
| Inactive | Moving amount | 59.0 | 57.0 | -3.4% |
| Four engine destinations | Static | 567.2 | 433.6 | -23.6% |
| Four engine destinations | Moving amount | 572.3 | 435.0 | -24.0% |
| Four engine destinations | Source changes every block | 573.1 | 729.0 | +27.2% |
| Four voice destinations | Static | 353.1 | 351.0 | -0.6% |
| Four voice destinations | Moving amount | 358.1 | 353.0 | -1.4% |
| Four voice destinations | Source changes every block | 357.5 | 530.6 | +48.4% |

Routing churn is an explicit limitation: rebuilding costs more than scanning.
Device comparisons must include rebuild bursts and automated routing changes.
Stage 2 is an experimental candidate, not an accepted production optimization.
The source-change revision below supersedes these host costs; it is not installed.

`tests/mod_route_test.c` compares complete prepared modulation state and
temporary/restored parameters with the original function in the same process:
100,000 seeded comparisons, all legal source/destination pairs across five
engines, duplicates, DEPTH, polarity, invalid destinations, zero crossings and
changing bases. ASan/UBSan passed. The complete cache-enabled host suite ended
`ALL HOST TESTS PASSED in 83 s` (`build/cpu-mod-suite.log`), including unchanged
goldens and 1,296 ordered concurrent engine assignments. Target lab build:
96,864 B data/BSS, same 153,056-B contiguous arena; register checks pass.

Build the directly comparable cache candidate:

```sh
MELODEE_MOD_CACHE=1 MELODEE_RAM_AUDIO=0 MELODEE_ADMISSION=0 \
MELODEE_AUDIO_PROFILE=1 MELODEE_AUDIO_BENCH=1 \
PYTHON="$PWD/build/venv/bin/python" ./build.sh --release 1.4.0-cpu-mod \
  --cpu-manifest build/cpu-mod-build.json
```

Lab packages (installation status as of 2026-10-16):

| Package in `build/` | Status | SHA256 |
| --- | --- | --- |
| `melodee-1.4.0-cpu-stage0.fwsc` | Prepared; not installed | `0571baa0e97a8f2cc87c229623568e18fa85365d61c2be03970e0c88643f49cc` |
| `melodee-1.4.0-cpu-mod.fwsc` | Installed at the user's request | `6908de941a24344b27810be3979452139acc61c57a5d3804fb65d20e05a46625` |

Before installation, the package hash and every firmware source hash matched
the tested build manifest; checkout `292aa13` was clean. The installer completed
and post-install reads verified `MELODEE v1.4.0-cpu-mod`, model `FM-1_91040`,
360 MHz, 128-frame halves and budget 16. Profiling and benchmark support are
compiled in; the fixed-benchmark bypass and persistence guard are inactive.
The routing cache is enabled in this package; full residency and the alternate
weighted-admission profile are disabled. This confirms installation/identity,
not physical audio, controls, saved-state preservation or CPU improvement.

Build JSON files capture exact firmware hashes and flags, including uncommitted
experiment content. Editor identity checks the version, not a cryptographic
readback of installed application bytes. Both packages use the same model
marker; select the explicit package filename. The earlier
`melodee-recording-release-queue-b296f57.fwsc` remains untouched.

The normal build also passes with profiling, benchmark support and routing
cache disabled: 95,888 B data/BSS, contiguous arena 153,056 B. Its package is
`build/melodee-1.4.0-cpu-default.fwsc`; no experimental cache is enabled.

### Source-change revision (`dc7c5ce`)

Source selection is no longer a structural dependency. Each key packs the raw
16-bit destination with one bit for valid, enabled source/amount membership.
The engine remains part of the key. Valid source changes, including transitions
between per-voice and per-track sources, do not rebuild a destination layout.
Sources/amounts/polarity remain live; DEPTH runs first, voice dependencies are
prepared from current sources and per-block offsets retain their original
arithmetic and merge order. Invalid nonzero sources retain the reference's
active-flag behavior. The cache occupies 256 B, down from 352 B.

Two intermediate experiments were discarded: a single mixed term loop regressed
voice-only routing; a lazy descriptor-only cache removed the source rebuild but
saved no useful engine-routing work. Separate destination lists for voice and
track terms preserve the useful fast path without keying the source selection.

Expanded routing-only host benchmark (`cc -O2`, 200,000 blocks per row):

| Routing | Changes each block | Reference | Revised | Change |
| --- | --- | ---: | ---: | ---: |
| Inactive | None | 60.0 | 60.0 | 0.0% |
| Four engine destinations | None | 573.2 | 397.0 | -30.7% |
| Four engine destinations | Amount | 574.1 | 398.1 | -30.7% |
| Four engine destinations | One source | 579.0 | 403.1 | -30.4% |
| Four engine destinations | All sources | 621.4 | 446.2 | -28.2% |
| Four voice destinations | None | 358.1 | 349.0 | -2.5% |
| Four voice destinations | One source | 362.6 | 353.6 | -2.5% |
| Four voice destinations | All sources | 357.8 | 354.8 | -0.8% |
| Four engine destinations | Destination | 554.2 | 645.2 | +16.4% |
| Four engine destinations | Amount crosses zero | 523.1 | 614.6 | +17.5% |
| Four engine destinations | Engine | 579.1 | 633.1 | +9.3% |
| Four voice destinations | Destination | 363.5 | 471.6 | +29.7% |
| Four voice destinations | Amount crosses zero | 334.5 | 439.3 | +31.3% |
| Four voice destinations | Engine | 364.0 | 458.2 | +25.9% |

The expanded benchmark includes additional workload-selection overhead, so
absolute counts differ from the initial table. Compare reference/candidate in
the same row. Source-change regressions are removed; structural-churn penalties
remain. No complete-device percentage or voice allowance follows from this table.
Host gates require at least 15% savings for the engine-routing hit/source cases
and no more than 4% overhead for inactive/voice cases; rebuild rows are reported
without acceptance. Instrumented sanitizer runs use `--parity-only` because
sanitizer instructions are not a production cost comparison.

Full prepared-state/reference parity covers the prior 100,000 seeded comparisons,
all source/destination pairs across five engines, invalid signed sources,
DEPTH/duplicates and every valid source transition without changing the routing
key. A detached-track caller exercises the reference fallback. ASan/UBSan pass.
The full cache-enabled suite reports `ALL HOST TESTS PASSED in 81 s`, including
unchanged goldens and 1,296 concurrent engine assignments. Logs:
`build/cpu-mod-source-suite.log`, `build/cpu-mod-source-bench.log`,
`build/cpu-mod-source-sanitized.log`, `build/cpu-mod-source-target.log` and
`build/cpu-mod-source-target-budget.log`.

The lab target passes at 96,768 B data/BSS, with 153,056 B contiguous arena and
the unchanged 142,336-B floor. Normal flags still pass at 95,888 B data/BSS.
The normal target instruction gates also pass; see
`build/cpu-source-default-target.log`. The new packed-key flag is expressed as
a bit shift to avoid colliding with the register-address source checker;
no checker exception or memory floor was relaxed.

Prepared, **not installed**:

```sh
MELODEE_MOD_CACHE=1 MELODEE_RAM_AUDIO=0 MELODEE_ADMISSION=0 \
MELODEE_AUDIO_PROFILE=1 MELODEE_AUDIO_BENCH=1 \
PYTHON="$PWD/build/venv/bin/python" ./build.sh --release 1.4.0-cpu-mod-source \
  --cpu-manifest build/cpu-mod-source-build.json
```

The lab package SHA256 is
`f6803660b0cabf78db79c305bf55612a40da948c3428799c0182cd914e4f8292`.
The normal package is `build/melodee-1.4.0-cpu-source-default.fwsc`, SHA256
`0bf3ef6a4727cad486737d0a0ecb34e0ad67efa356962d56bca145d501f340a1`.
Both package hashes and all firmware source hashes match their manifests.
Neither package is installed or replaces the installed `1.4.0-cpu-mod` identity.

## Stage 0 continuation: controlled screening (`05ace19`)

Read-only inspection reconfirmed the original `1.4.0-cpu-mod` identity and
360-MHz/128-frame configuration. The live budget had changed to 32; accumulated
overload counters were not used as baseline results. Each controlled run sets
budget 16 temporarily and uses the ordinary shared pool with bypass inactive.

The runner now uses the larger complete-envelope bound from statistics and
histogram data for timing, headroom and acceptance. It preserves the original
statistics bound in `stats_peak_complete_upper_us`. Ranked spans are emitted
in separate audio-inclusive and all-panel groups; nested and concurrent spans
are never summed. Fourteen host tests cover these decisions, restoration,
read-only retry boundaries and engine-destination/source-churn fixture setup.

`--modulation-routing engine` selects E1..E4 instead of the original voice-only
fixture. `--source-churn` cycles all ten legal sources at host-paced diagnostic
intervals; it does **not** claim one edit per control/audio block. Every run
continues to reject capacity steals, requested-voice loss and USB errors.

Initial device screens on the **original installed cache**, inactive USB:

| Workload | Duration | Complete bound | Result |
| --- | --- | --- | --- |
| `0124`, voices `1,1,1,2`, dry | 10 s | 1,997 us | Failed: 184 capacity steals during repeated drum hits |
| `0123`, voices `1,1,1,1`, voice modulation | 10 s | 2,479 us | Failed: one 80%-target exceedance; zero deadline misses/steals |
| `0123`, voices `1,1,1,0`, engine modulation | 3 x 10 s | 1,779 / 1,754 / 1,770 us | Passed these bounded screens |
| Same voices, engine modulation plus source changes | 10.1 / 14.9 / 10.0 s | 1,556 / 1,700 / 1,844 us | Passed timing/counter screens; four diagnostic retries |

The first two runs preceded the bound correction: their reported statistics
maxima were 1,991 and 2,479 us; histogram bounds were 1,997 and 2,478 us.
The table conservatively uses the larger value. Drum-trigger capacity accounting
needs investigation before that fixture can qualify extra voices; it was not
waived. Ten-second screens are initial evidence, shorter than the planned
60-second screening and final long stress cases. Source-changing runs vary the
actual sound/control values; their lower maxima are not an optimization claim.
Four retried diagnostic-31 requests (1/2/1 per repeat) interrupted host-paced
input; the second window extended to 14.9 s. Controller cadence is not uniform
and this is not controlled physical-panel or per-audio-block churn evidence.

For the first engine-modulation row, inclusive microseconds per half ranked:
Prophet 387, CZ 313, track control 200, TIMER5 184 (nested input 170), FM6 136,
output 131, mix 108, events 96, envelope 25 and modulation 10. The inclusive
half mean was 1,479 us. These overlap and include concurrent kernel work;
they are not additive CPU0 costs. Panel blanking averaged 4.21 us/scan,
shift/latch 3.70, LED/PWM 1.28, keys 0.56, diagnostics 0.18 and rows 0.10.
Encoders averaged 3.59 us per encoder invocation (one per eleven scans).
GPIO/PWM timing and the four-us blanking are unchanged.

Evidence directories: `build/cpu-mod-installed-screen-01`,
`build/cpu-mod-installed-melodic-01`,
`build/cpu-mod-installed-engine-static-01` and
`build/cpu-mod-installed-engine-churn-02`. Each completed session verified
runtime/preferences restoration and all 31 committed-object inventory entries;
original runtime CRC `61439115`, no snapshot on disk. This checks serialized
content and persistent objects, not uninterrupted oscillator state or physical
sound/control behavior. Final read-only checks confirmed guard inactive, original
`1.4.0-cpu-mod` identity and budget 32 restored. No firmware installation occurred
during this continuation.

The first source-change attempt, `build/cpu-mod-installed-engine-churn-01`,
failed on a runtime chunk read before fixture setup. It captured no completed
snapshot and made no fixture edits; its `restoration.json` therefore says
`verified: false`. A subsequent read confirmed guard inactive, original runtime
CRC `61439115` and budget 32. `8c1f104` adds bounded retries for idempotent
command-66 reads; tests retain the no-automatic-retry rule for command-67 writes.
The rerun above completed with verified restoration.

Reproduce static/changing-source rows against each explicitly installed package:

```sh
build/venv/bin/python tools/audio_cpu_device.py \
  --build-manifest build/cpu-mod-build.json \
  --output build/CHOOSE-A-FRESH-OUTPUT --assignment 0123 --voices 1,1,1,0 \
  --budget 16 --seconds 10 --repeat 3 --modulation --modulation-routing engine
# Add --source-churn for the matched changing-source workload.
```

Baseline/candidate interleaving, profiling overhead, pitch/control preparation,
electrical panel acceptance and Stages 3–7 remain open. The installed original
cache and prepared revision must not be conflated when comparing measurements.

## Stage 2 continuation: exact control preparation

Parent `704ad35`. The general per-voice increment cache was evaluated and
rejected, with an isolated reproducible experiment retained in
`tests/pitch_prepare_bench.c`. It keys effective clamped pitch, the complete
fine residual and device A4. Full arithmetic comparison covers all 2,048
pitches, 400–480 Hz and residuals -96..96 in steps of 16, both misses and hits.
With matching noinline wrappers under `cc -O2`, host instructions per lookup:

| A4 | Changes | Reference | Cache |
| --- | --- | ---: | ---: |
| 432 | None | 42 | 28 |
| 432 | Pitch or fine every lookup | 42 | 55 |
| 440 | None | 34 | 28 |
| 440 | Pitch or fine every lookup | 34 | 47 |

The proposed 768-B cache is absent from firmware. Its 31–38% continuous-motion
penalty and small default-reference hit gain fail the plan's churn criterion.
An earlier inlined prototype gave different absolute costs but the same
rejection. These are host instructions, not device timing.

`MELODEE_CONTROL_FAST=1` instead specializes centered Prophet bend: it writes
an exact zero target without loading/clamping a native range or dividing.
It defaults to zero, has no persistent fields or additional RAM, and leaves
smoothing, silent-track clocks and nonzero arithmetic at their existing rate.
The independent oracle covers every 14-bit bend value and native range byte
0–127 on every track. Isolated matching-wrapper host counts are 36→21 for
centered bend, 36→37 for held/moving nonzero bend (2.8% isolated penalty).
No complete-buffer saving is inferred. General pitch preparation remains the
reference arithmetic; existing TUNE and engine caches remain in place.

Full combined routing/control host suite: `ALL HOST TESTS PASSED in 81 s`,
`build/cpu-control-suite.log`, unchanged goldens and all 1,296 concurrent
assignments. ASan/UBSan oracle passed with `--parity-only`,
`build/cpu-control-sanitized.log`. Lab target and target instruction gates
passed: 96,768-B data/BSS, unchanged 153,056-B arena and 142,336-B floor.
Logs: `build/cpu-control-bench.log`, `build/cpu-pitch-rejected.log`,
`build/cpu-control-target.log`. This small fast path is an experimental
candidate awaiting device comparison, not production acceptance.

Prepared, **not installed**:

```sh
MELODEE_CONTROL_FAST=1 MELODEE_MOD_CACHE=1 MELODEE_RAM_AUDIO=0 \
MELODEE_ADMISSION=0 MELODEE_AUDIO_PROFILE=1 MELODEE_AUDIO_BENCH=1 \
PYTHON="$PWD/build/venv/bin/python" ./build.sh --release 1.4.0-cpu-control \
  --cpu-manifest build/cpu-control-build.json
cc -O2 -ffp-contract=off -w -Ibuild/gen -Ifirmware/src \
  tests/pitch_prepare_bench.c -o build/host/pitch_prepare_bench -lm
build/host/pitch_prepare_bench
```

Read-only inspection reconfirmed installed `MELODEE v1.4.0-cpu-mod`,
FM-1_91040, 360 MHz, 128-frame halves, budget 32, guard inactive and runtime
CRC 61439115. No installation occurred.

## Stage 3: scheduling measurement infrastructure

Parent `0a6efbb`. Append four profiling spans without changing the scheduler:
`batch_prepare` (descriptor copy, assignment/publication and submission,
excluding a preceding full-batch flush), `batch_merge` (ordered PCM addition),
`batch_cpu0` and `batch_cpu1` (sample-kernel spans grouped by executing core).
The kernel spans exclude scratch clearing and overlap existing engine spans.
CPU0 reads worker elapsed values only after joining. Failed worker spans are
not reported as successful work; no partially advanced job is replayed.
Serial fallback is classified on CPU0. Worker callback/wait scalars remain
available but callback time may include waiting for descriptor publication;
none of these inclusive/concurrent totals should be added as serial time.

The decoder accepts the original 21-span, panel 28-span and new 32-span layouts.
Fifteen harness tests pass, including old layouts and single-circuit fixtures.
A deterministic batch accounting test checks both cores, offline fallback,
failed-worker PCM drops and stale elapsed invalidation. The profiling-enabled
real concurrent suite passes 1,536 FM6 + 348 Prophet cases, worker retirement,
wrap, corruption and shared-FX serial/concurrent equality; see
`build/cpu-batch-profile-concurrent.log`. Full combined nonprofiling host suite:
`ALL HOST TESTS PASSED in 79 s`, `build/cpu-batch-profile-suite.log`, unchanged
sound goldens and 1,296 ordered assignments. Lab target: 96,816-B data/BSS
(+48 B profiling spans), 153,056-B arena, unchanged 142,336-B floor. Normal
build: unchanged 95,888-B data/BSS and arena. Both pass register/memory checks;
full-suite target instruction gates pass.

`tests/audio_job_bench.c` ranks actual queued sample kernels on the host,
with normal control preparation between jobs. One mono voice is retriggered
every 32 blocks; the first four blocks are labeled attack and the other 28
body. It covers two Prophet filter revisions with Poly-Mod amount off/on,
all 32 FM6 algorithms with one/all nonzero operator levels, four CZ line
settings, both SID models with one/six oscillators, all sixteen 808 sounds
and eleven 909 sounds. It measures 103 configurations, two phases each.
The separate instruction-query overhead is substantial (minimum 5,714 host
instructions in the recorded run), included in rows and not subtracted.
No scheduling weight is inferred from these host costs or their ratios.
The 909 body means span about 10,216 (clap) to 13,987 (tom) instructions,
including that overhead; a single family estimate hides useful differences.
Target class costs, CPU0 preemption and coordination still need measurement
before bounded conservative classes can be calibrated.

```sh
cc -O2 -ffp-contract=off -w -Ibuild/gen -Ifirmware/src \
  tests/audio_job_bench.c -o build/host/audio_job_bench -lm
build/host/audio_job_bench > build/cpu-job-costs.csv 2> build/cpu-job-costs.log
```

`audio_cpu_device.py --drum-note NOTE` selects a single GM circuit on every
active drum track, requiring exactly one requested voice there. The default
mixed fixture remains unchanged. This permits matched per-circuit target
profiles with the existing assignment/variant controls. Explicitly install a
recorded package before using its manifest. Profiling overhead remains an
open before/after measurement, and the old installed package lacks new spans.
No assignment policy, result ordering or production weight changed.

Prepared, **not installed**: `build/melodee-1.4.0-cpu-batch-profile.fwsc`,
SHA256 `d34d02072ad91a1377f9c66e076d1ed689748ce930b0e1a32246a778db4b9e0a`,
manifest `build/cpu-batch-profile-build.json`; flags enable routing/control
candidates, profiling and lab support, retain RAM_AUDIO=0 and ADMISSION=0.
Normal package: `build/melodee-1.4.0-cpu-batch-default.fwsc`, SHA256
`e4e2616115d7d3638943d3d043e9320c4ed5ba9767873a62095ca6c69abd970c`,
manifest `build/cpu-batch-default-build.json`; all candidates/profiling off.
Logs: `build/cpu-batch-profile-target.log`, `build/cpu-batch-default-target.log`.
The earlier control-only package SHA256 is
`a81f8a7a1035731293bcb7edce8fe64c5b82bf33df1494ba54bf277bb23b7c80`.

## Stage 0 continuation: full screening windows

The installed **original routing cache** completed 3 x 60 s of assignment
0123, voices 1,1,1,0, engine modulation, inactive USB, budget 16, ordinary
shared admission and no fixed bypass. Complete upper bounds were
1,476 / 1,502 / 1,500 us. All three strict screens passed with zero reported
late/deadline/target failures, shedding, capacity steals, worker faults,
resource or USB errors, and zero diagnostic retries. Evidence:
`build/cpu-mod-installed-engine-static-60`. Restoration verified original
runtime CRC 61439115, preferences and 31 committed objects; no snapshot on
disk. Original budget 32 restored. These are candidate-only workload screens;
there is still no baseline/candidate saving or increased voice claim.

## Stage 0: same-circuit drum retrigger accounting

The failed initial 0124 drum screen is retained as a failed historical row.
Investigation reproduced an accounting defect through the real MIDI ownership
path. A repeated MIDI note-on first releases its old owner; `voice_start`
counted a sounding gate-off voice as capacity replacement even when DRUM
reuses the same pitch's one-shot circuit without allocating another voice.

The correction excludes only same-pitch one-shot reuse from that `voice_start`
classification. Different-pitch lane replacement and actual budget-driven
kills still count. It changes no allocation, gate, envelope, sound or stealing
policy. The regression plays 100 MIDI retriggers on each of two drum circuits;
it failed before the correction and passes afterward. Different-pitch lane
replacement and real shared-budget pressure checks both pass. Logs:
`build/cpu-drum-accounting-before.log`, `build/cpu-drum-accounting-after.log`.
Full host suite (with the first opt-in mixer experiment) passed in 82 s,
`build/cpu-mix-fast-suite.log`, unchanged goldens and concurrent assignment
matrix. The correction is **not installed**. Do not reinterpret old device
rows as passes or subtract assumed retriggers from them; remeasure the drum
fixture on a recorded corrected package before qualifying capacity.

## Stage 4: exact centered mono send path

Parent `f5602f9`. `MELODEE_MIX_FAST=1`, default zero, specializes mono
tracks whose effective PAN is exactly zero and whose shared send is nonzero.
The post-level sample is clamped to +/-524287; both unity pan multiplies
therefore fit signed 32-bit and return that exact sample. The specialized
loop adds equal dry channels and equal sends; it preserves level arithmetic,
peak detection and capture placement. Noncenter and dry cases use the
original separately compiled loop. No pass is moved across the worker join;
spread, insert processing, smoothing, limiter/DC and FX state are unchanged.

The first branch-in-one-function experiment was exact but regressed dry and
noncenter host paths. It is rejected. A first split-loop benchmark still
allowed asymmetric compiler specialization across the two calls; its costs
are superseded by matching volatile indirect calls below. Do not interpret
the earlier 6% probe or the unmatched figures as the retained comparison.

`tests/mix_fast_test.c` retains the Melodee-authored parent mono function as
an independent comparator (source history: `1e473874`, `cb118c95`, `25a2bef6`).
All 128 levels, 128 pans, sends 0/1/127, capture off/on and four tracks compare
exactly for both outputs, both sends, peaks and every stem, including
clipping boundaries, negative rounding and INT32 input extremes. ASan/UBSan
passes with `--parity-only`, `build/cpu-mix-revised-sanitized.log`.

Host `cc -O2` instructions/sample, including identical fixture-reset work and
matching indirect-call overhead, with host SIMD/optimizer behavior:

| PAN | Send | Reference | Candidate |
| --- | --- | ---: | ---: |
| Center | Off | 40.3 | 40.3 |
| Center | Full | 57.4 | 15.1 |
| +17 | Off | 40.2 | 40.3 |
| +17 | Full | 57.4 | 57.5 |

These isolated host reductions do **not** establish FM-1 time savings.
The benchmark gates the center/send benefit at 15% and other rows at 4%
maximum overhead. No allocation or cache RAM is added. Full combined suite:
`ALL HOST TESTS PASSED in 83 s`, `build/cpu-mix-revised-suite.log`, unchanged
goldens and all 1,296 concurrent assignments. Lab target/register/residency
and target instruction checks pass; 96,816-B data/BSS, unchanged 153,056-B
arena and 142,336-B floor. See `build/cpu-mix-revised-target.log` and
`build/cpu-mix-fast-revised-bench.log`.

Prepared, **not installed**: `build/melodee-1.4.0-cpu-mix-revised.fwsc`, SHA256
`7f7ae697da8a13ac93ebdf9011582030c61f7872a5f68f49facec1df759ac469`, manifest
`build/cpu-mix-revised-build.json`. Routing, centered-bend and mono-send
candidates are on, profiling/lab support on, RAM_AUDIO=0 and ADMISSION=0.
A matched all-candidates-off profiling reference is also prepared:
`build/melodee-1.4.0-cpu-reference-v2.fwsc`, SHA256
`8c7bec5bb2bd0da0a7a375f10b74b5bdf959c4f010b96b1788bdcc1a3a17bacc`, manifest
`build/cpu-reference-v2-build.json`. Both contain the same 32-span profiling
infrastructure and corrected drum accounting, so those changes do not bias
a matched isolated optimization comparison. Source/package hashes matched
both manifests before committing. The older installed package has neither
this instrumentation nor the accounting correction.

The updated normal package also passes: 95,888-B data/BSS, unchanged arena
and memory floors, register checks and target instruction gates. Package:
`build/melodee-1.4.0-cpu-mix-default.fwsc`, SHA256
`321841beceeb68ebf4f4122609080b83342fe5ba429135b1a641176adbe7f002`, manifest
`build/cpu-mix-default-build.json`, log `build/cpu-mix-default-target.log`.
All three current packages and firmware source hashes matched their manifests
at final verification. Final read-only device check confirmed original
`1.4.0-cpu-mod`, guard off, budget 32 and runtime CRC 61439115.

Exact effect dormancy, other pass fusion, insert/CHAIN/SPLIT comparisons and
long-tail/wakeup evidence remain open. This candidate is not production
acceptance of Stage 4.

## Stage 0/4: stereo USB and shared Chorus screen

The original installed cache completed one 60.03-s assignment 0123 screen,
voices 1,1,1,0, engine modulation, full shared Chorus, bidirectional int24
stereo USB and budget 16. The complete bound was 1,851 us. Device counters
passed: zero late/target/deadline failures, shedding, steals, worker/resource
faults or USB errors; 60,018 packets received and sent; zero diagnostic
retries. Inclusive means/half include mix 144.1 us, FX 94.8, USB 74.6 and
output 173.2. They overlap other spans and concurrent work and are not added.
Evidence: `build/cpu-mod-installed-engine-fx-stereo-60`.

CoreAudio/PortAudio emitted `-10863: cannot do in current context` during the
session, although `capture.status.json` contains no callback status entries.
Keep that host error alongside the passing device counters; this is not
clean end-to-end streaming qualification, glitch-free capture or listening
validation. The runner now copies host callback statuses into each future
row and rejects any reported callback error. Fifteen harness tests pass,
including that rejection. Unstructured host stderr still needs inspection.
Restoration verified runtime/preferences, CRC 61439115 and 31 committed
objects, with no disk snapshot and budget 32 restored. No firmware was flashed.

## Acceptance status

No measured before/after device CPU saving or extra voice allowance is claimed.
The routing, centered-bend and centered mono-send candidates default off.
Scheduling policy, sound/FX state, event/project formats and admission defaults
are unchanged. Same-circuit one-shot reuse accounting is corrected in source.
Only the original profiling routing candidate is installed. Matched current
reference/candidate packages are prepared, not installed. Stage 0 still needs
interleaved comparisons and broader coverage; Stage 1 needs electrical
measurements; Stage 3 needs target class measurements before changing costs;
Stage 4 needs device comparisons and further FX work; Stages 5–7 remain open.
Independent software candidates should continue while hardware dependencies
remain pending, without silently enabling defaults or raising allowances.

Stage-0 validation: target build passed, data/BSS 96,512/98,304 B,
contiguous arena 153,056 B (142,336-B floor). Full host suite:
`ALL HOST TESTS PASSED in 79 s`, `build/cpu-stage0-suite.log`.
Independent host tests cover histogram overflow/timer wrap, both old/extended
span decoders, bounded snapshots, restoration after workload errors, guarded
restore failure, changed committed storage, invalid preferences and blocked
flash writes. Physical input, audible behavior and device before/after savings
remain untested.

## Stage 4 continuation: exact Chorus dormancy

Parent `db1efe6`. `MELODEE_FX_DORMANT=1` (default zero) skips Chorus's
sample kernel only after 512 consecutive zero input writes prove the complete
stereo ring is zero. Any nonzero sample wakes it before processing that block.
It retains ring ownership/allocation, position, block-rate phase/design, delay
ramps and the exact integer mix slew. Its otherwise idle `echo` field holds a
conservative zero-write count; there are no additional state or cache bytes.
No threshold truncation, changed effect IDs or changed project bytes. Other
effects retain their existing processing, including their tails and smoothing.
Epoch resets, type changes and failed-worker retirement clear the proof with
the existing state reset. A zero-sized block preserves state.

`tests/fx_dormant_test.c` retains the parent Melodee-authored Chorus kernel as
an independent comparator. 40,000 blocks cover lengths 0..32, hot and tiny
positive/negative stereo inputs, late-only input, arbitrary delay/mix changes,
ring wraps, long silence and repeated wakeups. PCM, the entire ring and every
original state field compare exactly. The one extra derived count is checked
against actual ring contents. ASan/UBSan passes (`build/cpu-fx-dormant-sanitized.log`).
Separate candidate/reference executables compare complete insert/CHAIN/SPLIT
traces with each other effect in the middle slot, three-slot Chorus chains,
RATE/DEPTH/MIX/PAN/spread edits, bypass/type changes, epoch resets, long tails
and attacks after silence. Both traces hash to `d949a9df622d3f3b`; histories,
allocated ring contents and shared master output are included.

Matching indirect-call host instructions/sample (including identical input
reset work), `build/cpu-fx-dormant-bench.log`:

| Input | Reference | Candidate |
| --- | ---: | ---: |
| Dormant zero | 90.50 | 19.53 |
| Continuous stereo signal | 90.48 | 91.11 |
| Only last sample nonzero | 90.48 | 92.97 |
| Attack followed by silence | 90.50 | 39.09 |

The initial early-exit scan regressed the late-only row by 8.4%; it was replaced
by a first-sample check plus a full OR reduction. Retained gates require 30%
dormant gain and at most 4% overhead in each other row. These are host kernel
costs with host compiler/SIMD behavior, not target microseconds or extra voices.
Full combined routing/control/mixer/dormancy suite: `ALL HOST TESTS PASSED in
79 s`, `build/cpu-fx-dormant-suite.log`, unchanged goldens and all 1,296
concurrent assignments. Target instruction checks pass without budget changes.
The existing Chorus kernel symbol and normal-build direct path are retained.

Lab target/register/residency checks pass: 96,816-B data/BSS, 152,864-B arena
(the new helper consumes 192 B of POOL code space), unchanged 142,336-B floor.
Prepared, **not installed**: `build/melodee-1.4.0-cpu-fx-dormant.fwsc`, manifest
`build/cpu-fx-dormant-build.json`, log `build/cpu-fx-dormant-target.log`.
All four candidates, profiling and lab support on; RAM_AUDIO=0, ADMISSION=0.
No production default or voice allowance changed. Device comparisons and
physical transition/listening checks remain pending. The installed firmware
is still the original `1.4.0-cpu-mod` package; a read-only installer query
confirmed running FM-1_91040 on port Melodee. No installation occurred.

Verified package SHA256, `cpu-fx-dormant`: `6139af331eec6f9c01d999e5db31324518d5d419e4993ccc915a2103b045ec20`.

Verified package SHA256, `cpu-fx-default`: `65ec92a513db68ee01551c616dd93a2d4774946c62aa00d1a348cfa90cc4df7b`.

Normal package `build/melodee-1.4.0-cpu-fx-default.fwsc` (manifest
`build/cpu-fx-default-build.json`, log `build/cpu-fx-default-target.log`)
passes target/register/residency checks: 95,888-B data/BSS, 153,056-B arena,
unchanged floor. All four candidates, profiling and bypass support off. Both
packages and all firmware sources match their manifests. Neither is installed.

## Stage 5: compact overview pitch ownership index

Parent `a60f2a9`. `MELODEE_RECORD_INDEX=1` (default zero) stores four
128-pitch words per overview cell for each active track: 4,096 B in POOL,
plus 104 B logical metadata (dirty cells, active bank/LEN keys and 32 bank
counts). Linked target data/BSS grows by 96 B after alignment. The lab build
has 96,912-B data/BSS and a 148,768-B contiguous audio arena; the 142,336-B
floor is unchanged. This is an explicitly different experimental memory
layout; the normal default build omits it. No RAM_AUDIO/admission policy
change or voice allowance increase is hidden in the index.

The query suppresses the same overview notes/GM lane hits as the original
linked-bank scanner. Event timings, release heaps, references, quantization,
voice/MIDI ordering and stored bytes are unchanged. Membership uses original
onset < LEN but is indexed by rounded/wrapped overview cell. Overview
TIME/SF_RECORDED stay live. Same-pitch duplicates use OR membership; removal
marks a cell dirty, so deleting one owner retains all others after rebuilding.
Sparse banks with fewer than four events retain the scanner, paying a measured
19 host instructions/query for dispatch instead of bitmap lookup work.

Maintenance is coalesced at the next recorded NOTE query. Unchanged steps do
no event traversal. Additions update bits directly; dirty rows are cleared
and all their retained memberships rebuilt in one bank traversal, regardless
of how many removals preceded the query. Bank/LEN changes and bulk reindexing
rebuild all cells once. This bounded edit/cold-query burst still needs target
measurement; it is not claimed to eliminate all edit/loop-boundary spikes.
No generation counter is assumed to cover all mutation paths.

Mutation audit:

| Path | Maintenance |
| --- | --- |
| Capture/replacement, jam capture, note conversion/editor inserts | Existing unlink marks old cell; insertion adds new pitch and bank count |
| Remove/delete, erase/prune, clear, move/restore | Unlink decrements count and marks old cell; restore also marks any prior membership |
| Punch-out quantization | Mark old cell before changing original/rounded step; add new cell afterward |
| Copy/double/halve/track moves, undo/redo, runtime/bank load | Existing `recording_reindex` reconstructs counts and invalidates active keys |
| Bank switch and LEN edit, including automation/native writes | Complete bank/LEN key checked on query; no writer-specific notification needed |
| Duration, velocity while nonzero, pitch offsets, release/exponent changes | Ownership unchanged; no extra rebuild |
| Overview flags/time edits | Evaluated at query time, independently of cached membership |

The moved reference scanner was introduced in Melodee's `b565eab` and indexed
by `686b98f`; source history confirms Kerem Kilic authorship. Mixed `seq.c`
notices are preserved; the independently written test/index retain Melodee's
GPL-3.0-only header.

`tests/recording_index_test.c` has an independent all-slot oracle, separate
from the original linked-bank benchmark comparator. It covers all 32 banks,
four tracks, every cell, changing LEN, original versus rounded/wrapped cells,
live flags/time, 640,000 pitch queries under 20,000 interleaved removals and
replacements, reindexing, counts, and all 1,024 same-cell/same-pitch owners
removed in batches. A specific wrapped 15/31 onset case verifies shortening
and restoring LEN without losing ownership. ASan/UBSan passes; log
`build/cpu-record-index-sanitized.log`. Existing raw recording tests pass with
the index enabled, `build/cpu-record-index-existing.log`.

Isolated host instructions/query, including matching indirect calls,
`build/cpu-record-index-bench.log`:

| Events in active bank | Scanner | Index steady | Full rebuild | One dirty cell rebuild |
| --- | ---: | ---: | ---: | ---: |
| 0 | 33.3 | 52.3 | 698.7 | 572.7 |
| 1 | 99.1 | 118.1 | 735.7 | 609.7 |
| 2 | 133.8 | 152.8 | 765.7 | 630.7 |
| 3 | 168.7 | 187.6 | 795.7 | 651.7 |
| 4 | 203.4 | 164.3 | 825.7 | 672.7 |
| 64 | 2,290.2 | 164.3 | 2,628.9 | 1,932.7 |
| 256 | 8,966.2 | 164.3 | 8,385.7 | 5,994.9 |
| 1,024 | 35,673.8 | 164.3 | 31,436.1 | 22,227.7 |

The same tool separately measures unchanged release-queue overview polling,
without synthesis/onset work or expiring releases: about 45 instructions with
no sounding events, 93 with one and 757 with all 64 watched cells, independent
of whether 64, 256 or 1,024 events own them. No further queue-index change is
justified by these host results. These are host counts, not target savings.
Rebuild rows are additional to the following query and include measurement
query overhead; neither they nor sparse dispatch overhead are hidden in the
steady row. Gates require >75% steady gain at 64+ events and <=24 additional
instructions in the 0..3 event fallback.

Target/register/residency checks pass, `build/cpu-record-index-target.log`.
Prepared, **not installed**: `build/melodee-1.4.0-cpu-record-index.fwsc`,
manifest `build/cpu-record-index-build.json`, all five candidates, profiling
and lab support on, RAM_AUDIO=0 and ADMISSION=0. Hardware buffer/edit timing,
external-clock stress and physical validation remain pending.

Full combined routing/control/mixer/dormancy/index host suite: `ALL HOST
TESTS PASSED in 79 s`, `build/cpu-record-index-suite.log`. Unchanged sound
goldens, every existing recording/erase/note-editor/undo/persistence/MIDI
clock test and all 1,296 concurrent engine assignments pass. Target instruction
checks pass without modified budgets.

Verified package SHA256, `cpu-record-index`: `b668c3ba7d75703d53f55452d1c872941e3c3a58d286d023d74ffa98c6fdbcfc`.

Verified package SHA256, `cpu-index-default`: `56494d0b9a4b3fcfadb716f849f34a585a2b4225c9404e9f50afaee425cd758d`.

Normal package `build/melodee-1.4.0-cpu-index-default.fwsc` (manifest
`build/cpu-index-default-build.json`, log `build/cpu-index-default-target.log`)
also passes target/register/residency checks: 95,888-B data/BSS and 153,056-B
arena, unchanged floor. All five candidates, profiling and lab support off.
All firmware source hashes and both packages matched their manifests. Neither
package was installed. Remaining software includes Stage 6 exact engine
paths and a production long-stress observer/runner; target comparisons, panel
electrical checks and calibrated extra voices remain open.

## Stage 0/7: normal-build long-stress observer

Parent `8c3e668`. `tools/audio_cpu_stress.py` observes an ongoing performance
without editing music, transport, preferences or persistent objects. Only
its diagnostic counters are reset at the start. It defaults to ten minutes,
requires shared-budget normal firmware (profiling and benchmark support both
compiled out), and leaves playback running on completion or failure. No
snapshot, backup directory or restoration is needed for an observation-only
session. It never calls the legacy fixture/restore pipeline.

The observer continuously polls complete timing bounds, voice counts, worker,
allocation and USB errors, accumulating 32-bit counter deltas per poll. It
records every sample, build manifest/package hash, identity, budget and clock,
and rejects mid-run timing/build/budget changes, fixed-benchmark bypass,
faults, exceeding the 80% target or observing no voices for the whole session.
Sequenced music may legitimately reach zero voices; `--minimum-voices` adds a
stricter floor for static held-voice workloads. Production runs have no
profiling totals to wrap at 120 s; diagnostics are not reset between windows
and the musical workload is not restarted.

Optional int24 stereo USB capture records callback statuses and captures
native driver stderr through the process file descriptor, including errors
at stream close. Any native stderr requires review and prevents a clean pass,
even if callbacks report no error. This handles the class of CoreAudio -10863
message missed by the previous callback-only report. It does not reinterpret
or upgrade that earlier failed host-streaming qualification.

Eleven host tests pass (`build/cpu-stress-tests.log`): simulated continuous
600-second observation, final partial poll, voice gaps/floor, every fault,
32-bit counter rollover, rejected profiling/lab/clock/frame builds before
reset, mid-run configuration changes, USB alternates, callback/native stderr,
CLI failure evidence and closing the connection without silence/transport/
parameter/persistence commands. The existing 15 baseline/restoration harness
tests still pass. Firmware is unchanged by this tooling commit; the preceding
combined firmware suite/build evidence remains applicable.

After explicitly installing a recorded **normal** package, start the desired
music/clock/controllers and observe it, for example:

```sh
build/venv/bin/python tools/audio_cpu_stress.py \
  --build-manifest build/cpu-index-default-build.json \
  --output build/CHOOSE-A-FRESH-STRESS-OUTPUT --seconds 600 \
  --label 'Describe the actual pattern, clock and controller workload'
# Add --usb stereo for bidirectional streaming/capture.
# Add --minimum-voices N only for a workload that must hold that many voices.
```

**Not run on hardware.** The installed original `1.4.0-cpu-mod` profiling
package is deliberately rejected. This observer supplies the long normal
measurement window, not recording/stem/clock fixtures or physical/listening
acceptance. Those workload cases and actual stress execution remain pending;
no extra voice allowance or production default is certified.

## Stage 6: exact Prophet zero-wheel preparation

Parent `2a6f18a`. Existing job ranking remains the starting evidence: the
103-configuration host report's body rows range about 16,106..20,552
instructions/job for Prophet, 6,655..10,501 for FM6, 9,705..12,280 for CZ,
11,957..14,177 for SID and 7,911..14,005 across 808/909 circuits. Each includes
substantial instruction-query overhead (about 5,714); neither ratios nor
host ranking establish the target's limiting patch or scheduler weights.
Target engine/circuit ranking and additional-voice costs are still required.

`MELODEE_PROPHET_FAST=1` (default zero) specializes track-wide wheel
preparation when its effective amount is exactly zero. It preserves every
waveform/noise calculation, signed zero in the wheel signal, the noise RNG
sequence, phase and final LFO value. Both signed zeros yield exactly 1.0 in
the existing pitch-ratio table expression, so that stateless expression is
replaced with unity in this case. The selection occurs once per block;
nonzero held/moving input uses the original separately compiled kernel.
No voice state, worker inputs/publication/result order, smoothing, envelopes,
patch formats, tuning or sample-render arithmetic changed. No added RAM.

The first sample-by-sample zero branch was exact but increased the held
nonzero host row from 65.34 to 69.36 instructions/sample (~6.2%). It was
rejected. The retained split kernel passes the isolated >=15% zero benefit
and <=4% other-row cost gates, with matching volatile indirect calls:

| Wheel amount pattern | Reference | Candidate |
| --- | ---: | ---: |
| Zero | 65.42 | 43.57 |
| Held full | 65.57 | 65.80 |
| Continuously changing 0..127 | 65.48 | 65.75 |
| Alternating zero/full | 65.58 | 54.83 |

These are host instructions per wheel sample, not target microseconds or
sample-job/whole-buffer savings. Log `build/cpu-prophet-fast-bench.log`.
`tests/prophet_fast_test.c` retains the original Melodee-authored kernel as
an independent comparator (source history `1fbeccdc`, `fa64ef07`, `25a2bef6`).
All 393,216 combinations of eight waveform selections, every rate/mix byte,
zero/min/max amounts plus phase boundaries compare the entire part state
bit-for-bit, including signed zero, wheel arrays and exact RNG advancement.
The same matrix and ASan/UBSan pass at optional 2x oversampling
(`build/cpu-prophet-fast-os2-sanitized.log`); full-suite costs are
64.21->42.28 zero, 64.28->64.39 held full, 64.24->64.36 moving, 64.58->53.46
alternating. ASan/UBSan passes (`build/cpu-prophet-fast-sanitized.log`).

Full combined six-candidate suite: `ALL HOST TESTS PASSED in 78 s`,
`build/cpu-prophet-fast-suite.log`, unchanged sound goldens and all 1,296
ordered concurrent engine assignments. This includes the new long-stress tool
host tests and complete recording/index and FX routing comparators. Target
instruction gates pass with unchanged budgets and the original kernel symbol.
Target/register/residency checks pass: 96,912-B data/BSS, 148,736-B audio arena
(32 B POOL code beyond the preceding index build), unchanged 142,336-B floor.
The new kernel uses 448 B of low DSP SRAM code; 1,144 B remains there.
Prepared, **not installed**: `build/melodee-1.4.0-cpu-prophet-fast.fwsc`, manifest
`build/cpu-prophet-fast-build.json`, log `build/cpu-prophet-fast-target.log`.
All six candidates plus profiling/lab support on; RAM_AUDIO=0, ADMISSION=0.

This is the first Stage-6 candidate, in shared Prophet preparation rather
than its per-voice filter. Broader exact FM6/CZ/SID/808/909 path evaluation,
target patch ranking and comparisons remain open. Existing FM6 quiet-state,
phase advancement, envelope/portamento preparation and filter coefficient
caches must not be bypassed merely because output/operator gain is zero.
No hardware installation or production default change occurred.

Verified package SHA256, `cpu-prophet-fast`: `85cd3518c0f4a2df866288c32f876651e552f44fde2609f3897aa51bb3165542`.

Verified package SHA256, `cpu-prophet-default`: `a6db05e36ad60d84aeaeb3c6fb6e6679361a3b61f9150effe6141159107c4154`.

Verified package SHA256, `cpu-reference-v3`: `7d337a70afaa7b6a1a5fec6917173222ffec67e7afec8549d3353af2867caace`.

Normal package `build/melodee-1.4.0-cpu-prophet-default.fwsc` (manifest
`build/cpu-prophet-default-build.json`, log `build/cpu-prophet-default-target.log`)
passes: 95,888-B data/BSS, 153,056-B arena. All six candidates/profiling/lab
support off. Current all-candidates-off profiling reference:
`build/melodee-1.4.0-cpu-reference-v3.fwsc`, manifest
`build/cpu-reference-v3-build.json`, log `build/cpu-reference-v3-target.log`:
96,560-B data/BSS, 153,056-B arena. It has the same 32-span instrumentation
and drum accounting, RAM_AUDIO=0 and ADMISSION=0. The index-enabled candidate
intentionally uses 4,096 B more POOL RAM, as accounted above; these packages
must not be described as identical memory layouts. Both retain the unchanged
142,336-B floor. All current firmware source hashes and all three packages
match their manifests. Earlier filenames/evidence remain preserved.

Final read-only inspection: installed `MELODEE v1.4.0-cpu-mod`, FM-1_91040,
port Melodee, 360 MHz, 128-frame halves, budget 32, guard inactive. Runtime
length 27,904 B, CRC 61439115. No hardware workloads, firmware installations,
pushes or physical sound/control tests occurred in this continuation. The
new packages and accounting/index/Chorus/Prophet candidates are not installed.

Next independent software work: finish measured exact engine-path evaluation,
consider remaining control-routing churn and panel LED precomputation without
changing GPIO edge/latch/blanking timing, inspect any additional compatible
buffer passes. Broader recording/stem/external-clock stress fixture coverage
can extend the tools; the new ten-minute observer does not supply those
fixtures. Electrical panel acceptance, target class calibration and actual
reference/candidate runs remain external dependencies. Keep production flags
and allowances unchanged until their explicit acceptance evidence exists.

## Stage 1: fixed column words and rejected brightness cache

Parent `01fb13b`. `MELODEE_PANEL_FAST=1`, default zero, replaces the repeated
column-selection arithmetic with eleven constant 16-bit words (22 B in flash,
no added RAM). Both polled and interrupt scans use the same helper; column 0/1
retain their extra octave-indicator selection bits. The 16 read-modify-write
SER/SRCLK edge pairs and latch operations, the four-us PWM blanking wait,
10-kHz cadence, row reads, debounce and encoders are unchanged. This is a
small candidate for target measurement, not an electrically accepted new scan.
The isolated host loop/call measurement is 20.01 -> 15.01 instructions/column;
it does not establish complete scan or audio-buffer savings.

A column-brightness cache was investigated and **rejected**. The first full
column key added 132 B and regressed edits; a revised 12-B duty-combination
cache plus 256-B exact lookup reduced dense costs but still regressed sparse
patterns. `tests/panel_prepare_bench.c` retains the independent original
Melodee PWM path and the rejected prototype outside firmware, for reproduction.
Its ten rows include all-dark, full-bright, dense planes, changing bright bits,
raw-mask churn, combined churn, single-row/single-plane, full single-plane,
partially bright and canonical 0/1/3/7/255 mask changes. Representative final
host instructions/column, loop/call included:

| Pattern | Original | Rejected probe |
| --- | ---: | ---: |
| Dark | 60.01 | 49.01 |
| All bright | 48.01 | 39.01 |
| Three full dim planes | 128.02 | 101.01 |
| Bright bits changing | 88.29 | 87.51 |
| Raw mask changing | 130.03 | 104.03 |
| Bright and mask changing | 88.26 | 92.52 |
| One dim row in one plane | 63.01 | 101.03 |
| Full single dim plane | 72.03 | 101.01 |
| Three bright rows | 67.02 | 85.01 |
| Canonical mask changes | 137.02 | 126.03 |

Retaining only the dense-row win would conceal up to ~60% sparse-row overhead.
No brightness cache, duty table, writer hook or PWM change enters firmware.
Correctness evidence for the rejected probe: every byte value of each mask,
overlapping/ignored plane bits, a cold zero key, 200,000 direct/key-helper edits
with all eleven columns revisited, PWM/GPIO fallback and timer wrap compare
register results exactly. ASan/UBSan passes. Logs:
`build/cpu-panel-prepare-bench.log`, `build/cpu-panel-prepare-sanitized.log`.

The retained fixed words compare independently against the original expression
for all eleven columns in `led_pwm_test`; both flag variants exercise the full
mock-MMIO scan, PWM conflict recovery, dim levels and wrap. Combined seven-flag
suite: `ALL HOST TESTS PASSED in 79 s`, `build/cpu-panel-word-suite.log`, with
unchanged goldens, all 1,296 concurrent assignments and unchanged target gates.
This includes the rejected probe's exactness, without a false performance pass.
Firmware/probe source notices retain their applicable provenance; the original
PWM preparation is Melodee-authored and the mixed input HAL keeps its notices.

Prepared, **not installed**, combined laboratory package:
`build/melodee-1.4.0-cpu-panel-word.fwsc`, manifest
`build/cpu-panel-word-build.json`, log `build/cpu-panel-word-target.log`.
All seven candidates and profiling/lab support enabled, RAM_AUDIO=0,
ADMISSION=0. Data/BSS 96,912 B, audio arena 148,736 B, unchanged 142,336-B
floor and 1,144-B low-DSP tail. The fixed words add no RAM or DSP SRAM code.
Normal all-candidates/profiling/lab-off package:
`build/melodee-1.4.0-cpu-panel-default.fwsc`, manifest
`build/cpu-panel-default-build.json`, log `build/cpu-panel-default-target.log`.
Its data/BSS is 95,888 B, audio arena 153,056 B. Register/residency checks pass.
No production flag, voice allowance, GPIO ownership or firmware installation
changed. Device CPU/electrical/brightness/key-latency acceptance remains open.

A matching six-candidate laboratory control keeps PANEL_FAST=0:
`build/melodee-1.4.0-cpu-panel-control.fwsc`, manifest
`build/cpu-panel-control-build.json`, log `build/cpu-panel-control-target.log`.
It retains the same data/BSS and arena as the panel-word candidate. In the
compiled interrupt the new address/indexed-halfword loads replace six or seven
word-construction instructions (depending on the column). The separate
19-instruction `fm1__sr_word` kernel has identical assembly after resolving
addresses; GPIO edges/latch code was not tightened. Listings are preserved as
`build/cpu-panel-word.dis` and `build/cpu-panel-control.dis`. This static count
is not an electrical or target timing result.

Current all-candidates-off profiling reference, prepared and not installed:
`build/melodee-1.4.0-cpu-reference-v4.fwsc`, manifest
`build/cpu-reference-v4-build.json`, log `build/cpu-reference-v4-target.log`.
It retains 96,560-B data/BSS and 153,056-B arena. Unchanged instruction gates
pass (`build/cpu-reference-v4-instructions.log`).

Verified package SHA256 (all firmware source hashes match their manifests):

- `cpu-panel-word`: `a1ee4b3242d8f077292b650e99e11c578c7a8580c86c55fa139e7634ae14c789`.
- `cpu-panel-control`: `4f76d04d98814f0700529f26d0eb25ff939d55a962b312d8edbd82a48046e324`.
- `cpu-panel-default`: `3591166efd2881f19b957d60c817c972c05b70134b93461ce72ee8731154e559`.
- `cpu-reference-v4`: `a5f21678d228208c925cb52968f57745ee7d8e18b2348563a48b138bae68c79d`.

## Stage 6: exact FM6 Modern zero-level preparation

Parent `5da9f0d`. `MELODEE_FM6_FAST=1` (default zero) specializes only the
Modern mode's stateless gain preparation for an exactly zero level:
`fm_exp2f(-14.0f)` is bit-exact `1.0f / 16384.0f` in the existing polynomial.
The nonzero expression and Mark I/OPL preparation remain intact. Every operator
keeps the original routing, thresholds, phase advancement, gain interpolation,
feedback/loop plan, envelope and quiet/ghost behavior. There is no new cache,
key, voice state, update-rate change or sample-kernel edit. The existing private
worker publication and accumulation order remain unchanged.

`tests/fm6_prepare_test.c` retains the original complete routing plan, checks
all 32 algorithms, three modes, 17 feedback-shift choices, eight level patterns
and seven preceding-gain cases (including both signed zeros and the run
threshold): 91,392 cases. A further 200,000 changing blocks compare the entire
voice state after each call, including phases, gains, plans and feedback.
ASan/UBSan passes. The retained plan's Melodee changes trace to `32e26696` and
`fa64ef07`; its preceding Felucca C port derives from MSFA/Dexed. Applicable
notices and references are retained, including the mixed firmware's notices.

Host instructions per isolated plan, including loop/call/level setup, are not
sample-job or buffer savings:

| Input / mode | Original | Candidate |
| --- | ---: | ---: |
| All zero / Modern | 498.49 | 311.15 |
| One nonzero operator / Modern | 528.77 | 373.33 |
| All nonzero / Modern | 626.26 | 631.16 |
| Continuous nonzero levels / Modern | 649.74 | 655.28 |
| Alternating all zero/nonzero / Modern | 630.63 | 543.04 |
| All nonzero / Mark I | 473.42 | 472.04 |
| All nonzero / OPL | 477.42 | 476.09 |

The isolated >=15% zero/one-active gain and <=4% other-row cost gates pass.
Logs `build/cpu-fm6-prepare-bench.log`, `build/cpu-fm6-prepare-sanitized.log`.
These are control preparation costs; they do not establish that an FM6 patch
limits target polyphony or that zero-output operators can stop advancing.

Combined eight-candidate suite: `ALL HOST TESTS PASSED in 78 s`,
`build/cpu-fm6-prepare-suite.log`, unchanged goldens and all 1,296 concurrent
assignments. Target instruction checks pass without changing budgets
(`build/cpu-fm6-prepare-instructions.log`), along with register/residency checks.
Prepared, **not installed**:
`build/melodee-1.4.0-cpu-fm6-prepare.fwsc`, manifest
`build/cpu-fm6-prepare-build.json`, log `build/cpu-fm6-prepare-target.log`.
All eight candidates/profiling/lab support on, RAM_AUDIO=0 and ADMISSION=0.
Data/BSS remains 96,912 B, low-DSP tail remains 1,144 B; 32 B additional POOL
code changes the audio arena from 148,736 to 148,704 B. The 142,336-B floor
is unchanged. No state RAM was added, and this layout difference remains part
of any target comparison. The preceding panel-word package is the recorded
seven-candidate parent; earlier filenames/evidence are preserved.

Prepared normal package `build/melodee-1.4.0-cpu-fm6-default.fwsc` (manifest
`build/cpu-fm6-default-build.json`, log `build/cpu-fm6-default-target.log`)
passes: all eight candidates/profiling/lab support off, 95,888-B data/BSS and
153,056-B arena. New all-candidates-off profiling reference:
`build/melodee-1.4.0-cpu-reference-v5.fwsc`, manifest
`build/cpu-reference-v5-build.json`, log `build/cpu-reference-v5-target.log`:
96,560-B data/BSS, 153,056-B arena. Its unchanged instruction gates pass
(`build/cpu-reference-v5-instructions.log`). These packages are not installed.

All current source hashes and package SHA256 values were verified against
these manifests:

- `cpu-fm6-prepare`: `3ceb646be2634438bdf2850e66944d1703f365a792450892b78a6e321f541ff6`.
- `cpu-fm6-default`: `8cb40b724cb15fdc3dfe21a0f2ea310f6ef5cf95f26298ac06b4f732e3898ce7`.
- `cpu-reference-v5`: `3ca279eaced12e25ab0713e4872dbeace1a02b76351b546eea4956a349209d6c`.

Target patch ranking, CPU comparisons, physical transitions and additional
voice proof remain open. No package was flashed, pushed or used for a hardware
workload in this continuation. Production defaults/allowances remain unchanged.
