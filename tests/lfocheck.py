#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The LFO, in pixels and in samples.

    python3 tests/lfocheck.py build/host/lfo

Three claims.

That a depth and a sensitivity are a pair and neither is worth anything alone. The same note is
played three times -- depth only, both, sensitivity only -- and the outer two are divided into the
first to cancel the patch's own decay, which leaves the LFO's gain and nothing else. Only the
middle one may move.

That the page is every LFO number there is one of, and the tap is the one there are six of. The
black keys go on playing on every bank of the page, because nothing on it belongs to an operator;
under a tap of LFO they point instead, because the one knob there does. A key that points makes no
sound, so that is read out of the audio rather than off the screen. GLOBAL swaps the tap's one
knob for the LFO itself, and an OP key is the way back.

And that the picture is of the LFO rather than a decoration: changing the wave changes it, and
changing it back makes it pixel for pixel what it was.
"""
import sys, os, struct

D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "host")
TITLE_Y = (24, 38)                      # the page title line, which names the bank
BANNER_Y = (18, 50)                     # HOME's banner, which says what the knobs have become
LEG_Y = (208, 222)                      # what the four knobs are, under the four knobs
PLOT_Y = (44, 148)                      # the LFO's picture on its own page
ALG_Y = (64, 200)                       # the diagram's room on HOME, which the picture borrows
ROW_Y = (81, 113, 145, 177)             # the bar under each of OPERATOR's four rows
BAR_X, BAR_W = 6, 228
ACC = (180, 141, 255)                   # C_ACC, lilac, after 565 and back
bad = 0


def ok(name, good, detail=""):
    global bad
    if not good:
        bad += 1
    print("%-54s %s    %s" % (name, "ok  " if good else "WRONG", detail))


def ppm(ms):
    with open(os.path.join(D, "lfo-%d.ppm" % ms), "rb") as f:
        d = f.read()
    i, n = 0, 0
    while n < 4:                        # P6, w, h, maxval
        while d[i:i + 1].isspace():
            i += 1
        if d[i:i + 1] == b"#":
            while d[i:i + 1] != b"\n":
                i += 1
            continue
        while not d[i:i + 1].isspace():
            i += 1
        n += 1
    return d[i + 1:]


def band(ms, ys):
    """one horizontal strip of a screen, full width"""
    return ppm(ms)[ys[0] * 240 * 3:ys[1] * 240 * 3]


def lit(ms, ys):
    b = band(ms, ys)
    return sum(1 for i in range(0, len(b), 3) if b[i] or b[i + 1] or b[i + 2])


def bars(ms):
    """the four bar widths on this screen, in pixels"""
    px = ppm(ms)
    out = []
    for y in ROW_Y:
        n = 0
        for x in range(BAR_X, BAR_X + BAR_W):
            i = (y * 240 + x) * 3
            if tuple(px[i:i + 3]) == ACC:
                n += 1
        out.append(n)
    return out


def px(ms, x, y):
    i = (y * 240 + x) * 3
    return tuple(ppm(ms)[i:i + 3])


# ---- the page: a picture over three cells, on each of its three banks
ok("the page draws the LFO above its numbers", lit(1400, PLOT_Y) > 500,
   "%d lit pixels" % lit(1400, PLOT_Y))
# Three to a bank, a third of the screen each, so the rules between the cells are at 80 and 160 --
# and not at 60, where four would put the first of them.
EGC = 152 + 10
ok("..in three cells, a third of the screen each",
   px(1400, 80, EGC) != (0, 0, 0) and px(1400, 160, EGC) != (0, 0, 0) and
   px(1400, 60, EGC) == (0, 0, 0), "rules at 80 and 160, none at 60")
ok("..and a tap of its own button walks to the next bank",
   band(4900, TITLE_Y) != band(1400, TITLE_Y))
ok("..which keeps the picture, having only changed the numbers",
   lit(4900, PLOT_Y) > 500 and band(4900, (152, 184)) != band(1400, (152, 184)))

# The picture is drawn out of the engine's own LFO arithmetic, so the wave byte has to move it --
# a square has corners a triangle has not. Changing it back has to give the same pixels, which is
# what says the picture is a function of the bytes and not of what happened on the way here.
ok("the wave it is drawing is the wave that is set",
   band(6300, PLOT_Y) != band(6700, PLOT_Y),
   "%d lit pixels square, %d triangle" % (lit(6300, PLOT_Y), lit(6700, PLOT_Y)))
ok("..and turning it back draws what it drew before",
   band(6000, PLOT_Y) == band(6700, PLOT_Y))

# ---- a tap of LFO on HOME: one knob, and the banner naming whose it is
ok("a tap of LFO leaves the diagram where it is", lit(7900, ALG_Y) > 500,
   "%d lit pixels in the diagram's room" % lit(7900, ALG_Y))
ok("..and the banner names the operator whose knob it is",
   band(8300, BANNER_Y) != band(7900, BANNER_Y))
ok("..and says the same one again on the way back",
   band(8700, BANNER_Y) == band(7900, BANNER_Y))

# ---- GLOBAL: the LFO itself, where the one operator's share was
ok("GLOBAL changes what the knobs are",
   band(11700, LEG_Y) != band(7900, LEG_Y))
ok("..and says so",
   band(11700, BANNER_Y) != band(7900, BANNER_Y))
ok("..and puts the LFO's picture where the diagram was",
   band(11700, ALG_Y) != band(7900, ALG_Y) and lit(11700, ALG_Y) > 300,
   "%d lit pixels" % lit(11700, ALG_Y))
# There is no operator to step under GLOBAL, so a second tap of LFO steps the wave, and the
# picture in the diagram's room is what says so.
ok("..where a second tap of LFO steps the wave instead",
   band(12500, ALG_Y) != band(12100, ALG_Y) and lit(12500, ALG_Y) > 300 and
   band(12500, LEG_Y) == band(12100, LEG_Y), "the picture changed, the knobs did not")
ok("..and an OP key puts it all back",
   band(15100, BANNER_Y) == band(7900, BANNER_Y) and
   band(15100, ALG_Y) == band(7900, ALG_Y))

# That one knob writes an operator's own byte, so OPERATOR 2/4 -- COARSE FINE V SENS A M SENS --
# has to be waiting with it. Its max is 3, so 3 fills the bar.
b = bars(17100)
ok("the knob under the tap is the operator's own byte", b[3] == BAR_W,
   "A M SENS %d px of %d" % (b[3], BAR_W))

# ---- the audio
with open(os.path.join(D, "lfo.wav"), "rb") as f:
    w = f.read()
pcm = w[w.index(b"data") + 8:]
FS = 44100


def rms(t0_ms, t1_ms):
    a, b2 = int(FS * t0_ms / 1000) * 4, int(FS * t1_ms / 1000) * 4
    n = (b2 - a) // 4
    if n <= 0:
        return 0.0
    v = struct.unpack_from("<%dh" % (n * 2), pcm, a)
    return (sum(float(x) * x for x in v) / len(v)) ** 0.5


# Nothing on the LFO page belongs to one operator, so the black keys stay keys on both banks. The
# moment one knob does belong to one -- the tap, and nowhere else -- they point, and a key that
# points makes no sound at all. GLOBAL points as well, or there would be no way back off it.
ok("a black key on the page is still a key, on bank 2/3", rms(3700, 4300) > 300.0,
   "rms %.0f" % rms(3700, 4300))
ok("..and on bank 3/3", rms(5100, 5700) > 300.0,
   "rms %.0f" % rms(5100, 5700))
ok("..but under a tap of LFO it points instead", rms(8100, 8300) == 0.0,
   "rms %.1f" % rms(8100, 8300))
ok("..and still points under GLOBAL, which is the way back", rms(14300, 14500) == 0.0,
   "rms %.1f" % rms(14300, 14500))

# The three notes, ten milliseconds at a time. Dividing the second and third by the first cancels
# the patch's own decay and leaves the LFO's gain, so a flat line means "nothing is modulating
# this" however loud it is. The LFO is about 8 Hz here, so 600 ms is four or five cycles of it.
#
# Two windows that size are not a fair comparison on their own, because a window that short also
# hears the partials beat against each other, and that comes and goes by a tenth from one to the
# next whether or not there is an LFO. Averaging three of them -- 30 ms, a fifth of a cycle --
# costs the LFO almost nothing and takes most of that away, which is what makes the two notes tell
# themselves apart by seventy times rather than by a hair. A whole cycle hides the LFO completely,
# so there is a floor under this as well as a ceiling.
N, STEP, SMOOTH = 60, 10, 3


def ratio(t):
    out = []
    for i in range(N):
        a = rms(2400 + i * STEP, 2400 + (i + 1) * STEP)
        b2 = rms(t + i * STEP, t + (i + 1) * STEP)
        out.append(b2 / a if a > 0 else 0.0)
    return [sum(out[i:i + SMOOTH]) / SMOOTH for i in range(len(out) - SMOOTH + 1)]


both, sens = ratio(9600), ratio(13000)
sw_both = max(both) / min(both)
sw_sens = max(sens) / min(sens)
ok("the note everything is measured against is there", rms(2400, 3000) > 300.0,
   "depth up, nobody listening: rms %.0f" % rms(2400, 3000))
ok("with both of them up, the sound moves", sw_both > 4.0,
   "down to %.0f%% of itself and back, a swing of %.1f" % (min(both) / max(both) * 100, sw_both))
ok("with the sensitivity up and no depth, it stops moving", sw_sens < 1.12,
   "a swing of %.2f against %.1f" % (sw_sens, sw_both))
# ..though it is not quite the same note. Dexed's sensitivity curve does not start at zero, so an
# operator that is listening gives up about a tenth of its level whether or not anything is being
# said to it. That is the engine being faithful, and is worth pinning down so it cannot drift.
ok("..at a steady nine tenths, which is the curve's own floor",
   0.80 < sum(sens) / len(sens) < 0.95, "%.3f of the first note" % (sum(sens) / len(sens)))

print("\n%d checks failed" % bad)
sys.exit(1 if bad else 0)
