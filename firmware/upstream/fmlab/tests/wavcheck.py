"""Is build/host/operators.wav silent exactly where the operator switches say it should be?

Pairs with tests/operators.txt: the screenshots show what the diagram claims, this shows whether
the sound agrees. Run after:  build/host/fmlab build/host/operators tests/operators.txt
"""
import struct
import sys

path = sys.argv[1] if len(sys.argv) > 1 else 'build/host/operators.wav'
raw = open(path, 'rb').read()
i = 12
while i < len(raw):
    cid, sz = struct.unpack_from('<4sI', raw, i)
    if cid == b'data':
        data = raw[i + 8:i + 8 + sz]
        break
    i += 8 + sz + (sz & 1)
pcm = struct.unpack('<%dh' % (len(data) // 2), data)
FS = 44100
CH = struct.unpack_from('<H', raw, 22)[0]       # the host writes stereo


def peak(t0, t1):
    a, b = int(t0 * FS / 1000) * CH, int(t1 * FS / 1000) * CH
    return max(abs(s) for s in pcm[a:b]) if b > a else 0


WINDOWS = [
    ('nothing switched off', 1150, 1400, 'loud'),
    ('OP1 off -- the only carrier', 1950, 2200, 'silent'),
    ('OP1 on again', 2750, 3000, 'loud'),
    ('OP2 soloed -- a modulator', 4250, 4500, 'silent'),
    ('all six on again', 5550, 5800, 'loud'),
    ('switched to B, unchanged', 7050, 7300, 'loud'),
    ('B with OP1 level down 20', 8450, 8700, 'quieter'),
    ('back on A', 9250, 9500, 'loud'),
    ('B again', 10050, 10300, 'quieter'),
    ('F#5 on plain HOME plays', 11050, 11250, 'sounds'),
    ('..and the side is still B', 11450, 11650, 'quieter'),
]

full = peak(1150, 1400)
bad = 0
for name, t0, t1, want in WINDOWS:
    p = peak(t0, t1)
    if want == 'silent':
        ok = p == 0
    elif want == 'sounds':
        ok = p > full // 20 and peak(10900, 10990) < 20
    elif want == 'loud':
        ok = p == full
    else:
        ok = 0 < p < full * 9 // 10
    bad += not ok
    print('%-30s peak %6d (%3d%% of the reference)  want %-8s %s'
          % (name, p, p * 100 // max(full, 1), want, 'ok' if ok else '<-- WRONG'))
print('\n%d of %d windows wrong' % (bad, len(WINDOWS)))
sys.exit(1 if bad else 0)

