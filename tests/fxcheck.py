#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The filter, in pixels and in samples.

    python3 tests/fxcheck.py build/host/fx

Three claims.

That it filters, by the amounts a filter of its kind has to. One steady note -- ORGAN on middle C,
partials at 131, 262, 523 and 1046 Hz -- is measured with the filter off and then through each
setting, so the voice cancels out and what is left is the filter: LOW PASS at 230 Hz keeps the
fundamental and takes about 26 dB off the 1046 two octaves up; HIGH PASS at 1.86 kHz does the
opposite; and RESONANCE at the top stands the harmonic under the cutoff up by about 24 dB.

That it is reached the way the other three are. A tap of FX on HOME puts CUTOFF RESONANCE TYPE
under the knobs with the curve where the diagram was, a second tap steps the type, HOME puts it all
back, the black keys go on playing, and over the spectrum the curve is drawn on the sound. A hold
gives the page: the curve over three cells.

And that off is off: the picture is a flat line and nothing on it is the curve's colour.
"""
import sys, os, struct, math

D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "host")
BANNER_Y = (18, 50)                     # HOME's banner, which says what the knobs have become
LEG_Y = (208, 222)                      # what the four knobs are, under the four knobs
PLOT_Y = (44, 148)                      # the curve on its own page
CELL_Y = (152, 184)                     # the three cells under it
ALG_Y = (64, 200)                       # the diagram's room on HOME, which the curve borrows
ACC = (180, 141, 255)                   # C_ACC, lilac, after 565 and back
TEAL = (106, 202, 164)                  # C_FLT, the curve over the spectrum
bad = 0


def ok(name, good, detail=""):
    global bad
    if not good:
        bad += 1
    print("%-54s %s    %s" % (name, "ok  " if good else "WRONG", detail))


def ppm(ms):
    with open(os.path.join(D, "fx-%d.ppm" % ms), "rb") as f:
        d = f.read()
    return d[-240 * 240 * 3:]


def band(ms, ys):
    return ppm(ms)[ys[0] * 240 * 3:ys[1] * 240 * 3]


def count(ms, ys, rgb):
    b = band(ms, ys)
    return sum(1 for i in range(0, len(b), 3) if tuple(b[i:i + 3]) == rgb)


def px(ms, x, y):
    i = (y * 240 + x) * 3
    return tuple(ppm(ms)[i:i + 3])


# ---- a tap on HOME
ok("a tap of FX puts the filter where the diagram was",
   band(1000, ALG_Y) != band(400, ALG_Y) and band(1000, LEG_Y) != band(400, LEG_Y) and
   band(1000, BANNER_Y) != band(400, BANNER_Y), "picture, knobs and banner all changed")
ok("..and it starts off: a flat line, nothing in the curve's colour",
   count(1000, ALG_Y, ACC) == 0, "%d lilac pixels" % count(1000, ALG_Y, ACC))
ok("..until LOW PASS draws a curve", count(3200, ALG_Y, ACC) > 150,
   "%d lilac pixels" % count(3200, ALG_Y, ACC))
ok("..and a second tap steps the type, and not the knobs",
   band(5200, ALG_Y) != band(3200, ALG_Y) and band(5200, LEG_Y) == band(3200, LEG_Y))
ok("RESONANCE is in the picture as well as the sound",
   band(9200, ALG_Y) != band(7500, ALG_Y))
ok("HOME puts the diagram and the banner back",
   band(11800, ALG_Y) == band(400, ALG_Y) and band(11800, BANNER_Y) == band(400, BANNER_Y))

# ---- over the spectrum, the curve on the sound, in its own colour; and gone when HOME takes the
# knobs back
ok("over the spectrum, a tap of FX draws the curve on the sound",
   count(13000, ALG_Y, TEAL) > 150, "%d teal pixels" % count(13000, ALG_Y, TEAL))
ok("..and HOME takes it away again", count(14200, ALG_Y, TEAL) == 0,
   "%d teal pixels" % count(14200, ALG_Y, TEAL))

# ---- the page
ok("a hold gives the page: the curve above its numbers", count(15400, PLOT_Y, ACC) > 150,
   "%d lilac pixels" % count(15400, PLOT_Y, ACC))
EGC = 152 + 10
ok("..in three cells, a third of the screen each",
   px(15400, 80, EGC) != (0, 0, 0) and px(15400, 160, EGC) != (0, 0, 0) and
   px(15400, 60, EGC) == (0, 0, 0), "rules at 80 and 160, none at 60")
ok("..and TYPE on the third knob redraws the curve",
   band(15800, PLOT_Y) != band(15400, PLOT_Y) and band(15800, CELL_Y) != band(15400, CELL_Y))
ok("..which has one bank, so its own button stays put",
   band(16200, PLOT_Y) == band(15800, PLOT_Y) and band(16200, CELL_Y) == band(15800, CELL_Y))

# ---- the audio
with open(os.path.join(D, "fx.wav"), "rb") as f:
    w = f.read()
pcm = w[w.index(b"data") + 8:]
FS, N = 44100, 8192
HANN = [0.5 - 0.5 * math.cos(2 * math.pi * i / (N - 1)) for i in range(N)]


def seg(t_ms):
    v = struct.unpack_from("<%dh" % (N * 2), pcm, int(FS * t_ms / 1000) * 4)
    return [v[2 * i] * HANN[i] for i in range(N)]


def db(x, f):
    """the level of the partial near f, in dB of nothing in particular -- only differences count.
    Goertzel, a little either side, because the patch has a touch of vibrato."""
    best = 0.0
    for d in (-0.008, -0.004, 0.0, 0.004, 0.008):
        c = 2 * math.cos(2 * math.pi * f * (1 + d) / FS)
        s1 = s2 = 0.0
        for xi in x:
            s1, s2 = xi + c * s1 - s2, s1
        best = max(best, s1 * s1 + s2 * s2 - c * s1 * s2)
    return 10 * math.log10(best + 1e-9)


LO, HI = 130.81, 1046.5
lv = {}
for name, t in (("off", 1500), ("lp", 3700), ("hp", 5700), ("lp0", 7900), ("res", 9700)):
    x = seg(t)
    lv[name] = (db(x, LO), db(x, HI))


def drop(name, k):
    return lv["off"][k] - lv[name][k]


ok("LOW PASS at 230 Hz keeps the fundamental", abs(drop("lp", 0)) < 1.5,
   "131 Hz %+.1f dB" % -drop("lp", 0))
ok("..and takes the eighth harmonic two octaves down", drop("lp", 1) > 20.0,
   "1046 Hz %+.1f dB" % -drop("lp", 1))
ok("HIGH PASS at 1.86 kHz takes the fundamental", drop("hp", 0) > 35.0,
   "131 Hz %+.1f dB" % -drop("hp", 0))
ok("..and keeps most of the eighth", drop("hp", 1) < 15.0,
   "1046 Hz %+.1f dB" % -drop("hp", 1))
ok("LOW PASS on a partial is 3 dB down there, as it should be", 1.5 < drop("lp0", 1) < 4.5,
   "1046 Hz %+.1f dB" % -drop("lp0", 1))
up = lv["res"][1] - lv["lp0"][1]
ok("..and all the RESONANCE there is stands it up", up > 15.0, "%+.1f dB on RESONANCE 0" % up)
ok("..without touching the fundamental", abs(lv["res"][0] - lv["lp0"][0]) < 1.5,
   "131 Hz %+.1f dB" % (lv["res"][0] - lv["lp0"][0]))


def rms(t0_ms, t1_ms):
    a, b2 = int(FS * t0_ms / 1000) * 4, int(FS * t1_ms / 1000) * 4
    v = struct.unpack_from("<%dh" % ((b2 - a) // 2), pcm, a)
    return (sum(float(s) * s for s in v) / len(v)) ** 0.5


# None of the filter is an operator's, so the black keys stay keys under it.
ok("under the filter's knobs, the black keys still play", rms(10900, 11300) > 300.0,
   "rms %.0f" % rms(10900, 11300))

print("\n%d checks failed" % bad)
sys.exit(1 if bad else 0)
