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

Read-only connected-device inspection found `FM-1_900`, editor version
`MELODEE v1.4.0-rc.4`, 360 MHz, 128-frame halves, budget 16, CPU1 online,
profiling/benchmark/admission switches all zero and USB streams inactive.
Its accumulated counters include prior workloads; they are not baseline rows.
No firmware was installed and no device fixture was run.

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

## Acceptance status

No measured device CPU saving or extra voice allowance is claimed. No engine,
FX, scheduler, event format, admission default or project format changed.
Stage 0 awaits an authorized profiling flash and controlled measurements.
Stage 1 awaits electrical measurements. Later software candidates may proceed
behind explicit comparison switches, with production defaults retained until
their relevant device checks pass.

Stage-0 validation: target build passed, data/BSS 96,512/98,304 B,
contiguous arena 153,056 B (142,336-B floor). Full host suite:
`ALL HOST TESTS PASSED in 79 s`, `build/cpu-stage0-suite.log`.
Independent host tests cover histogram overflow/timer wrap, both old/extended
span decoders, bounded snapshots, restoration after workload errors, guarded
restore failure, changed committed storage, invalid preferences and blocked
flash writes. Physical input, audible behavior and device CPU remain untested.
