# SPDX-License-Identifier: GPL-3.0-only
"""Does the sound reach the DAC with its 24 bits? build/host/depth-24.wav is what the DAC gets,
build/host/depth.wav the 16-bit copy (USB audio, rounded on the host).

A soft note sits ~26 dB under full scale, so the 8 bits under 16 are where its tail lives: they have
to be there, the 16-bit copy has to be them rounded, and the tail has to decay smoothly down to
where 16 bits would have run out of steps."""
import math, os, struct, sys

D = sys.argv[1] if len(sys.argv) > 1 else os.path.join('build', 'host')


def frames(name):
    b = open(os.path.join(D, name), 'rb').read()
    bps = struct.unpack_from('<H', b, 34)[0]
    d = b[44:]
    if bps == 16:
        return list(struct.unpack('<%dh' % (len(d) // 2), d))
    return [int.from_bytes(d[i:i + 3], 'little', signed=True) for i in range(0, len(d), 3)]


w24, w16 = frames('depth-24.wav'), frames('depth.wav')
bad = []
if len(w24) != len(w16):
    bad.append('the two WAVs differ in length')
if any(min(32767, (a + 128) >> 8) != b for a, b in zip(w24, w16)):
    bad.append('the 16-bit WAV is not the 24-bit one rounded')

L = w24[0::2]
peak = max(abs(x) for x in L)
fine = sum(1 for x in L if x & 255) / max(1, sum(1 for x in L if x))
if fine < 0.9:
    bad.append('only %.0f %% of the samples use the bits under 16' % (100 * fine))
# the tail, once it is under one 16-bit step (256): still there in 24 bits, and still moving
low = [x for x in L if 0 < abs(x) < 256]
if peak < 1 << 18:
    bad.append('the note is too quiet to tell: peak %d' % peak)
if len(set(low)) < 50:
    bad.append('under one 16-bit step the tail has %d values: it has run out of bits' % len(set(low)))

print('depth: peak %.1f dBFS, %.0f %% of samples using the bits under 16, %d values in the sub-16-bit tail'
      % (20 * math.log10(peak / 8388607), 100 * fine, len(set(low))))
for b in bad:
    print('  BAD', b)
sys.exit(1 if bad else 0)
