#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The four effects, in samples.

    python3 tests/effectscheck.py build/host/effects

One held sine, through each effect in turn. CHORUS has to part the two sides; PHASER has to make
the note swell and dip as its notches sweep through it; DELAY has to play a short note again, as
long after as TIME says; REVERB has to be about as loud as the dry note and leave a tail that dies
away. With every MIX at 0 the output has to be the dry note, the same on both sides.
"""
import sys, os, struct, math

D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "host")
FS = 44100
bad = 0


def ok(name, good, detail=""):
    global bad
    if not good:
        bad += 1
    print("%-54s %s    %s" % (name, "ok  " if good else "WRONG", detail))


with open(os.path.join(D, "effects.wav"), "rb") as f:
    w = f.read()
pcm = w[w.index(b"data") + 8:]
ALL = struct.unpack("<%dh" % (len(pcm) // 2), pcm)
L, R = ALL[0::2], ALL[1::2]


def at(ms):
    return int(FS * ms / 1000)


def rms(x, t0, t1):
    v = x[at(t0):at(t1)]
    return (sum(float(s) * s for s in v) / max(1, len(v))) ** 0.5


def side_diff(t0, t1):
    return rms([a - b for a, b in zip(L, R)], t0, t1)


dry = rms(L, 800, 1300)
ok("dry, the note is there", dry > 1000.0, "rms %.0f" % dry)
ok("..and the same on both sides, to the sample", L[at(400):at(1500)] == R[at(400):at(1500)])

d = side_diff(2700, 3300)
ok("CHORUS parts the two sides", d > 0.1 * rms(L, 2700, 3300),
   "L-R rms %.0f against %.0f" % (d, rms(L, 2700, 3300)))

lv = [rms(L, t, t + 20) for t in range(4600, 7100, 20)]
swing = 20 * math.log10(max(lv) / max(1.0, min(lv)))
ok("PHASER makes a held note swell and dip", swing > 6.0, "%.1f dB between loudest and quietest" % swing)

THR = 0.25 * dry


def first(t0, t1, thr=THR):
    for i in range(at(t0), at(t1)):
        if abs(L[i]) > thr:
            return i
    return None


n0 = first(7950, 8150)
gap = rms(L, 8200, 8300)
e1 = first(8200, 8500)
ok("DELAY: the short note ends", n0 is not None and gap < 0.02 * dry, "rms %.0f after it" % gap)
dt = None if n0 is None or e1 is None else (e1 - n0) * 1000.0 / FS
ok("..and comes back 325 ms later", dt is not None and abs(dt - 325.4) < 3.0,
   "after %s ms" % ("%.1f" % dt if dt is not None else "never"))
e2 = first(8200 + 325, 8800, 0.08 * dry)    # FEEDBK 35 is a third as loud again
dt2 = None if e1 is None or e2 is None else (e2 - e1) * 1000.0 / FS
ok("..and again, on the feedback", dt2 is not None and abs(dt2 - 325.4) < 3.0,
   "after %s ms more" % ("%.1f" % dt2 if dt2 is not None else "never"))

wet = rms(L, 10400, 10900)
ratio = wet / dry
ok("REVERB, half in, keeps the note about as loud", 0.9 < ratio < 1.4, "%.2f of dry" % ratio)
t1, t2 = rms(L, 11200, 11500), rms(L, 12500, 12800)
ok("..leaves a tail after the key", t1 > 0.05 * wet, "rms %.0f, %.0f%% of the note" % (t1, 100 * t1 / wet))
ok("..which dies away", t2 < 0.5 * t1, "rms %.0f, from %.0f a second before" % (t2, t1))
ok("..and is wide", side_diff(11200, 11500) > 0.2 * t1, "L-R rms %.0f" % side_diff(11200, 11500))

after = rms(L, 14200, 14700)
ok("every MIX at 0: the dry note again", abs(after / dry - 1.0) < 0.01, "%.3f of dry" % (after / dry))
ok("..the same on both sides, to the sample", L[at(13800):at(15000)] == R[at(13800):at(15000)])
ok("..and nothing left of the effects after it", rms(L, 14900, 15100) < 0.01 * dry,
   "rms %.0f" % rms(L, 14900, 15100))

print("\n%d checks failed" % bad)
sys.exit(1 if bad else 0)
