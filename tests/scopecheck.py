#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""HOME's third screen, the waveform, in pixels.

    python3 tests/scopecheck.py build/host/scope

The trace is the white in the plot, read as one height per column. A sine on middle C has to come
out as two cycles that start on the way up through zero and fill most of the height; two pictures a
tick apart have to be the same, which is what the trigger is for; a SQUARE has to jump across the
middle rather than pass through it (its tops lean, because the output's DC blocker is a high pass); and nothing playing is a flat line on the middle.
"""
import sys, os

D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "host")
Y0, H = 64, 136
MID = Y0 + H // 2
HALF = H // 2 - 16
TEAL = (106, 202, 164)
bad = 0


def ok(name, good, detail=""):
    global bad
    if not good:
        bad += 1
    print("%-54s %s    %s" % (name, "ok  " if good else "WRONG", detail))


def ppm(ms):
    with open(os.path.join(D, "scope-%d.ppm" % ms), "rb") as f:
        d = f.read()
    return d[-240 * 240 * 3:]


def px(img, x, y):
    i = (y * 240 + x) * 3
    return tuple(img[i:i + 3])


def trace(ms):
    """per column, the white span's middle as a height above the centre line, or None"""
    img, out = ppm(ms), []
    for x in range(240):
        ys = [y for y in range(Y0, Y0 + H) if px(img, x, y) == (255, 255, 255)]
        out.append(MID - (min(ys) + max(ys)) / 2 if ys else None)
    return out


def band(ms):
    return ppm(ms)[Y0 * 720:(Y0 + H) * 720]


quiet = trace(600)
ok("nothing playing is a flat line on the middle",
   all(t is not None and abs(t) <= 0.5 for t in quiet), "%d columns off it" %
   sum(1 for t in quiet if t is None or abs(t) > 0.5))

s = trace(1500)
ok("a sine fills most of the height", max(s) > HALF * 0.8 and min(s) < -HALF * 0.8,
   "from %+.0f to %+.0f of %d" % (min(s), max(s), HALF))
ups = sum(1 for i in range(1, 240) if s[i - 1] < 0 <= s[i])
ok("..two cycles of it", ups in (1, 2) and sum(1 for i in range(1, 240) if s[i - 1] >= 0 > s[i]) == 2,
   "%d up, %d down through the middle" % (ups, sum(1 for i in range(1, 240) if s[i - 1] >= 0 > s[i])))
ok("..starting on the way up through zero", abs(s[0]) < HALF * 0.15 and s[6] > s[0],
   "column 0 at %+.0f, column 6 at %+.0f" % (s[0], s[6]))
s2 = trace(1620)
dev = max(abs(a - b) for a, b in zip(s, s2))
ok("two pictures a tick apart are the same picture", dev <= 2, "%.1f px apart at most" % dev)

def near_zero(t):
    return sum(1 for v in t if abs(v) < HALF * 0.3)


q = trace(3000)
ok("a SQUARE jumps across the middle rather than passing through it",
   near_zero(q) <= 8 and near_zero(s) > 30,
   "%d columns near the middle, against the sine's %d" % (near_zero(q), near_zero(s)))

f = trace(4000)
ok("a tap of FX leaves the wave on screen", near_zero(f) <= 8, "%d columns near the middle" % near_zero(f))
b = trace(4400)
ok("..and so does HOME, taking the focus off", near_zero(b) <= 8, "%d columns near the middle" % near_zero(b))
ok("HOME once more is the diagram", band(5000) != band(4400) and band(5000) != band(1500))


def title(ms):
    return ppm(ms)[18 * 720:44 * 720]


ok("the scope has its own title, not the instrument's", title(600) != title(5000))

print("\n%d checks failed" % bad)
sys.exit(1 if bad else 0)