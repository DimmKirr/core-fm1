#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Operator waves, in samples and in pixels.

    python3 tests/wavescheck.py build/host/waves

Three claims.

That each wave is the shape it is named. INIT VOICE is OP1 alone as the carrier, so its shape is the
sound: on middle C, SINE has no harmonics; HALF SINE has a second 7.5 dB under the note and no
third; ABS SINE repeats every half cycle, so it has nothing at the note and everything at twice it;
SQUARE has a third 9.5 dB down and no second; SAW has a second 6 dB down and a third 9.5 dB down.

That it is drawn. A tap of EDIT draws OP1's wave in its box, every wave differently, and LEVEL
shrinks it; on HOME a box that is not a sine wears a small mark of its shape and nothing else
changes.

That WAVE is a ring, SAW and SINE next to each other. And that it is kept. Saved, left and come back to, the voice is still a saw, mark and sound.
"""
import sys, os, struct, math

D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "host")
ALG_Y = (64, 200)                       # the diagram's room on HOME
BANNER_Y = (18, 50)                     # the banner, which names what a knob just turned
bad = 0


def ok(name, good, detail=""):
    global bad
    if not good:
        bad += 1
    print("%-54s %s    %s" % (name, "ok  " if good else "WRONG", detail))


def ppm(ms):
    with open(os.path.join(D, "waves-%d.ppm" % ms), "rb") as f:
        d = f.read()
    return d[-240 * 240 * 3:]


def band(ms, ys):
    return ppm(ms)[ys[0] * 240 * 3:ys[1] * 240 * 3]


def diff(a, b, ys):
    x, y = band(a, ys), band(b, ys)
    return sum(1 for i in range(0, len(x), 3) if x[i:i + 3] != y[i:i + 3])


def lit(ms, ys):
    b = band(ms, ys)
    return sum(1 for i in range(0, len(b), 3) if b[i:i + 3] != b"\0\0\0")


# ---- the screens
shots = (900, 2600, 4300, 6000, 7700)
ok("a tap of EDIT draws OP1's wave, each one its own",
   len(set(band(t, ALG_Y) for t in shots)) == len(shots),
   "%d different" % len(set(band(t, ALG_Y) for t in shots)))
names = len(set(band(t, BANNER_Y) for t in shots[1:]))
ok("..and the banner names it as the fourth knob turns", names == len(shots) - 1, "%d different" % names)
ok("LEVEL down shrinks the drawing", lit(9400, ALG_Y) < lit(7700, ALG_Y),
   "%d lit pixels, from %d" % (lit(9400, ALG_Y), lit(7700, ALG_Y)))
ok("one past SAW is SINE", band(9600, ALG_Y) == band(900, ALG_Y))
ok("..and one back from SINE is SAW", band(9650, ALG_Y) == band(7700, ALG_Y))
n = diff(10000, 400, ALG_Y)
ok("on HOME, a saw wears a small mark and nothing else", 4 <= n <= 80, "%d pixels changed" % n)
ok("saved, left and come back to, it still does", band(11000, ALG_Y) == band(10000, ALG_Y))

# ---- the audio
with open(os.path.join(D, "waves.wav"), "rb") as f:
    w = f.read()
pcm = w[w.index(b"data") + 8:]
FS, N = 44100, 8192
F0 = 261.63
HANN = [0.5 - 0.5 * math.cos(2 * math.pi * i / (N - 1)) for i in range(N)]


def seg(t_ms):
    v = struct.unpack_from("<%dh" % (N * 2), pcm, int(FS * t_ms / 1000) * 4)
    return [v[2 * i] * HANN[i] for i in range(N)]


def db(x, f):
    c = 2 * math.cos(2 * math.pi * f / FS)
    s1 = s2 = 0.0
    for xi in x:
        s1, s2 = xi + c * s1 - s2, s1
    return 10 * math.log10(s1 * s1 + s2 * s2 - c * s1 * s2 + 1e-9)


def harm(t):
    x = seg(t)
    return [db(x, F0 * k) for k in (1, 2, 3)]


h = {name: harm(t) for name, t in (("sine", 1300), ("half", 3000), ("abs", 4700),
                                    ("square", 6400), ("saw", 8100), ("saved", 11500))}


def rel(name, k):
    return h[name][k] - h[name][0]


def show(name):
    return "h2 %+.1f  h3 %+.1f dB" % (rel(name, 1), rel(name, 2))


ok("SINE has no harmonics", rel("sine", 1) < -40 and rel("sine", 2) < -40, show("sine"))
ok("HALF SINE: a second 7.5 dB down, no third",
   abs(rel("half", 1) + 7.5) < 1.5 and rel("half", 2) < -30, show("half"))
ok("ABS SINE: nothing at the note, all at twice it", h["abs"][1] - h["abs"][0] > 30 and
   h["abs"][1] - h["abs"][2] > 30, "2f over f %+.1f dB" % (h["abs"][1] - h["abs"][0]))
ok("SQUARE: odd harmonics only, the third 9.5 dB down",
   rel("square", 1) < -30 and abs(rel("square", 2) + 9.5) < 1.5, show("square"))
ok("SAW: every harmonic, 6 and 9.5 dB down",
   abs(rel("saw", 1) + 6.0) < 1.5 and abs(rel("saw", 2) + 9.5) < 1.5, show("saw"))
ok("saved and loaded again, it is still the saw",
   all(abs(h["saved"][k] - h["saw"][k]) < 0.5 for k in range(3)), show("saved"))

print("\n%d checks failed" % bad)
sys.exit(1 if bad else 0)
