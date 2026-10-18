# SPDX-License-Identifier: GPL-3.0-only
"""BIT DEPTH, heard: build/host/bits-24.wav is the same note at 24, 16, 12 and 8 bits (tests/bits.txt).
Each coarse one has to sit on its own grid (a multiple of 2^8, 2^12, 2^16 of the 24-bit scale, with
the filter and effects off, as they start), be made of fewer levels than the one before it, and be as
loud as the 24-bit note: rounding, not a change of level."""
import math, os, sys

D = sys.argv[1] if len(sys.argv) > 1 else os.path.join('build', 'host')
FS, N = 44100, 8192
b = open(os.path.join(D, 'bits-24.wav'), 'rb').read()[44:]
x = [int.from_bytes(b[i:i + 3], 'little', signed=True) for i in range(0, len(b), 6)]

bad, prev, ref = [], None, None
for bits, t in ((24, 1.5), (16, 3.3), (12, 5.1), (8, 7.1)):
    w = x[int(t * FS):int(t * FS) + N]
    step = 1 << (24 - bits)
    levels = len(set(w))
    peak = 20 * math.log10(max(abs(v) for v in w) / 8388607)
    off = sum(1 for v in w if v % step)
    print('%2d bits: %5d levels, peak %5.1f dBFS, %d samples off the grid' % (bits, levels, peak, off))
    if bits < 24 and off:
        bad.append('%d bits: not on its grid' % bits)
    if bits == 24 and not any(v % 256 for v in w):
        bad.append('24 bits: the note is already on the 16-bit grid')
    if prev is not None and levels >= prev:
        bad.append('%d bits: not fewer levels than the depth above it' % bits)
    if ref is not None and abs(peak - ref) > 1.0:
        bad.append('%d bits: the level moved' % bits)
    prev, ref = levels, ref if ref is not None else peak
for m in bad:
    print('FAIL:', m)
print('bits: %s' % ('FAIL' if bad else 'ok'))
sys.exit(1 if bad else 0)
