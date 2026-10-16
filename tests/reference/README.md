# Generative memory reference

The following files are unmodified from Mutable Instruments `eurorack` revision
`f4cfa7697095a28d292bff758beb51deb49b5a86`:

- `marbles/random_sequence.h`
- `marbles/random/quantizer.h`, `quantizer.cc`, `distributions.h`
- `marbles/resources.h`

`stmlib/stmlib.h`, `stmlib/dsp/dsp.h`, and
`stmlib/dsp/hysteresis_quantizer.h` are unmodified from its pinned stmlib submodule,
revision `e3bd7c9cc00e4364166f9905c0509b6ffd0535ec`.
All retain their original permissive notices.

`marbles/random/random_stream.h` is an original Melodee deterministic host shim;
it supplies identical words to both memory algorithms and is not Marbles' RNG.

`tests/generator_memory_test.cpp` compares the fixed-point C adaptation with the
unmodified C++ random-mode reference across 327,680 decisions: every length,
lock, partial/full replacement, and partial/full remix. The firmware maps CHANGE
linearly; Marbles maps its Deja Vu control nonlinearly. Melodee also explicitly
aligns its memory read to literal phrase slots. This is a memory component port,
not an emulation of the whole Marbles instrument.

The reference audio trace uses the pinned Marbles memory **and quantizer** with
its major-scale factory weights. Melodee audio uses the same initial random words,
register, tempo, rhythm, sound and levels, but its own COLOR-weighted pitch mapper.
The two quantizers are intentionally different. The progression examples use the
original Melodee harmonic planner; they are not reference Marbles recordings.
