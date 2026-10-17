#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The filter on the spectrum screen, in pixels and in samples; and the effects, in pixels.

    python3 tests/fxcheck.py build/host/fx

That the filter filters, by the amounts a filter of its kind has to. One steady note -- ORGAN on
middle C, partials at 131, 262, 523 and 1046 Hz -- is measured with the filter off and then through
each setting, so the voice cancels out and what is left is the filter: LOW PASS at 230 Hz keeps the
fundamental and takes about 26 dB off the 1046 two octaves up; HIGH PASS at 1.86 kHz does the
opposite; and RESONANCE at the top stands the harmonic under the cutoff up by about 24 dB.

That it lives on the spectrum screen: its knobs under it, its curve over the sound, flat when it is
off, and TYPE going round. And that FX is the effects now: a tap gives the four MIXes, HOME takes
them off, and a hold gives the page, opening where it was last left.
"""
import sys, os, struct, math

D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "host")
BANNER_Y = (18, 50)                     # HOME's banner, which says what the knobs have become
LEG_Y = (208, 222)
TITLE_Y = (18, 44)                      # the banner's big line, without what a button just said                      # what the four knobs are, under the four knobs
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


def rows(ms, ys, rgb):
    b = band(ms, ys)
    return {ys[0] + i // (240 * 3) for i in range(0, len(b), 3) if tuple(b[i:i + 3]) == rgb}


def px(ms, x, y):
    i = (y * 240 + x) * 3
    return tuple(ppm(ms)[i:i + 3])


# ---- the spectrum screen
ok("HOME once: the filter's knobs and banner, over the spectrum",
   band(1000, ALG_Y) != band(400, ALG_Y) and band(1000, LEG_Y) != band(400, LEG_Y) and
   band(1000, BANNER_Y) != band(400, BANNER_Y), "picture, knobs and banner all changed")
ok("..with the filter's curve drawn on it", count(1000, ALG_Y, TEAL) > 150,
   "%d teal pixels" % count(1000, ALG_Y, TEAL))
ok("..and it starts off: the curve is one flat line", len(rows(1000, ALG_Y, TEAL)) == 1,
   "on %d rows" % len(rows(1000, ALG_Y, TEAL)))
ok("..until LOW PASS bends it", len(rows(3200, ALG_Y, TEAL)) > 10,
   "on %d rows" % len(rows(3200, ALG_Y, TEAL)))
ok("..and TYPE steps on, not the knobs",
   band(5200, ALG_Y) != band(3200, ALG_Y) and band(5200, LEG_Y) == band(3200, LEG_Y))
ok("TYPE goes round, past HIGH PASS to OFF: flat again", len(rows(6900, ALG_Y, TEAL)) == 1,
   "on %d rows" % len(rows(6900, ALG_Y, TEAL)))
ok("RESONANCE is in the picture as well as the sound",
   band(9200, ALG_Y) != band(7500, ALG_Y))
ok("the wave screen has no curve", count(11800, ALG_Y, TEAL) == 0,
   "%d teal pixels" % count(11800, ALG_Y, TEAL))
ok("..and the diagram after it is the diagram",
   band(12200, ALG_Y) == band(400, ALG_Y) and band(12200, BANNER_Y) == band(400, BANNER_Y) and
   band(12200, LEG_Y) == band(400, LEG_Y))

# ---- FX
ok("a tap of FX: a banner and the effect's knobs, over the same diagram",
   band(12600, BANNER_Y) != band(400, BANNER_Y) and band(12600, LEG_Y) != band(400, LEG_Y)
   and band(12600, ALG_Y) == band(400, ALG_Y))
ok("..all four at once, so a second tap changes nothing", band(13000, TITLE_Y) == band(12600, TITLE_Y)
   and band(13000, LEG_Y) == band(12600, LEG_Y))
ok("..and nor does SELECT", band(13400, TITLE_Y) == band(12600, TITLE_Y)
   and band(13400, LEG_Y) == band(12600, LEG_Y))
ok("..and HOME puts the knobs back",
   band(13800, TITLE_Y) == band(400, TITLE_Y) and band(13800, LEG_Y) == band(400, LEG_Y))
PAGE_Y = (0, 240)
pages = [band(ms, PAGE_Y) for ms in (14800, 15200, 15600)]
ok("a hold gives the page, and FX walks its four effects",
   len(set(pages)) == 3 and band(14800, PAGE_Y) != band(13800, PAGE_Y))
ok("..and the next hold opens where the last left off", band(16300, PAGE_Y) == band(15600, PAGE_Y))
ok("..and the knobs come back at the page's", band(16800, TITLE_Y) == band(13400, TITLE_Y)
   and band(16800, LEG_Y) == band(13400, LEG_Y))

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
