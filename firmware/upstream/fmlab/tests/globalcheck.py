#!/usr/bin/env python3
"""The global envelope, in pixels and in samples.

Three claims. That the GLOBAL key really does replace the envelope on the screen -- the title,
the four cells and the graph all change, and the page loses its other banks. That the four cells
read real times rather than raw bytes. And that the thing is actually in the audio path: the same
chord, played before and after, must arrive differently.

And a fourth: that it starts switched out, and that a tap of ENV on it switches it in and out again,
on HOME and on its page, keeping its knobs -- out, the chord is the patch's own, release and all.
"""
import sys, os, struct

D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "host")
bad = 0


def ok(name, good, detail=""):
    global bad
    if not good:
        bad += 1
    print("%-52s %s    %s" % (name, "ok  " if good else "WRONG", detail))


def ppm(ms):
    with open(os.path.join(D, "global-%d.ppm" % ms), "rb") as f:
        d = f.read()
    i, n = 0, 0
    while n < 4:                                # P6, w, h, maxval
        while d[i:i + 1].isspace():
            i += 1
        if d[i:i + 1] == b"#":
            while d[i:i + 1] != b"\n":
                i += 1
            continue
        while not d[i:i + 1].isspace():
            i += 1
        n += 1
    i += 1
    return d[i:]


def px(b, x, y):
    o = (y * 240 + x) * 3
    return b[o], b[o + 1], b[o + 2]


ACC = (180, 141, 255)


def count(b, box, c, tol=20):
    x0, x1, y0, y1 = box
    n = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            p = px(b, x, y)
            if all(abs(p[k] - c[k]) <= tol for k in range(3)):
                n += 1
    return n


EG_Y, EG_H = 44, 104                            # the graph, on the bank that is all four knobs
CELLS = (0, 240, 152, 182)                      # the row of four ADSR cells
TITLE = (0, 240, 24, 40)                        # the page title


# ---- 1. the key swaps what the page is about ------------------------------------------------
before = ppm(2400)
after = ppm(2800)
ok("the GLOBAL key repaints the title",
   bytes(before[TITLE[2] * 720:TITLE[3] * 720]) != bytes(after[TITLE[2] * 720:TITLE[3] * 720]),
   "title band differs")
ok("..and the four cells with it",
   bytes(before[CELLS[2] * 720:CELLS[3] * 720]) != bytes(after[CELLS[2] * 720:CELLS[3] * 720]),
   "cell band differs")

# The operator page says 1/5; the global one has four parameters and so has no bank line at all.
# The title band is the place to see it, and it is enough that it got shorter.
def band_ink(b, box):
    return count(b, box, ACC, 40)


ok("the page loses its other banks",
   band_ink(after, TITLE) < band_ink(before, TITLE),
   "%d lit against %d" % (band_ink(after, TITLE), band_ink(before, TITLE)))


# ---- 2. the graph is the global envelope, and it moves when the knobs do ----------------------
GRAPH = (0, 240, EG_Y, EG_Y + EG_H)
flat = ppm(2800)                                # attack 0: straight up at the left edge
sloped = ppm(3200)                              # attack 50: a real slope
dropped = ppm(3600)                             # sustain 49: and a real decay


def first_col(b):                               # leftmost column of the graph with any accent in it
    for x in range(240):
        if count(b, (x, x + 1, EG_Y, EG_Y + EG_H), ACC, 40):
            return x
    return 240


def top_row(b):                                 # highest row the envelope reaches
    for y in range(EG_Y, EG_Y + EG_H):
        if count(b, (0, 240, y, y + 1), ACC, 40):
            return y
    return EG_Y + EG_H


ok("attack 0 is a vertical edge at the left",
   first_col(flat) <= 6, "first lit column %d" % first_col(flat))
def peak_col(b):                                # where the envelope first reaches the top
    for x in range(240):
        if count(b, (x, x + 1, EG_Y, EG_Y + 6), ACC, 40):
            return x
    return 240


ok("attack 50 is a slope, so the peak moves right",
   peak_col(sloped) > peak_col(flat) + 20,
   "peak at %d against %d" % (peak_col(sloped), peak_col(flat)))
ok("sustain 99 holds the envelope at the top",
   top_row(sloped) <= EG_Y + 4, "top row %d" % top_row(sloped))
# The held stretch is the one dim-blue run on the graph, and dropping the sustain is what moves it
# down. Where along the width it sits depends on the three times, so the claim is about its height.
HELD = (90, 95, 120)
ok("sustain 49 drops the held stretch to the middle",
   count(dropped, (0, 240, EG_Y + 40, EG_Y + 70), HELD, 40) > 10,
   "%d pixels of held line in the middle band"
   % count(dropped, (0, 240, EG_Y + 40, EG_Y + 70), HELD, 40))
ok("..and it was at the top before",
   count(sloped, (0, 240, EG_Y + 40, EG_Y + 70), HELD, 40) == 0,
   "%d pixels of held line in the middle band"
   % count(sloped, (0, 240, EG_Y + 40, EG_Y + 70), HELD, 40))


# RELEASE 0 and RELEASE 99 are the same setting, so they have to read the same. The cell is the
# whole claim: if one of them said 0MS the picture below it would be a cliff rather than a flat.
relcell = (183, 237, 152, 182)
down = count(ppm(2880), relcell, (255, 255, 255), 60)
up = count(after, relcell, (255, 255, 255), 60)
ok("release off the bottom of the knob is the same OFF as off the top",
   down == up and up > 20, "%d pixels against %d" % (down, up))


# ---- 3. the cells say times, not bytes --------------------------------------------------------
# ATTACK 50 is 45 ms, and "45MS" is four glyphs where "50" is two. The first cell is 54 px wide and
# the value is centred in it, so more ink means a longer word, which is the whole claim.
c0 = (3, 57, 156, 176)
ok("the first cell says more than a two-digit byte",
   count(sloped, c0, (255, 255, 255), 60) > count(flat, c0, (255, 255, 255), 60),
   "%d pixels against %d" % (count(sloped, c0, (255, 255, 255), 60),
                             count(flat, c0, (255, 255, 255), 60)))


# ---- 4. an OP key takes it back, and the tap route gets there too ----------------------------
# The operator it goes back to is OP2, not the OP1 it started on, so the cells cannot be compared
# with the first shot. What can be said is that they are no longer the global envelope's, and that
# the page has its other four banks back -- which the bank line in the title is the proof of.
back = ppm(4000)
ok("an OP key puts the operator back",
   bytes(back[CELLS[2] * 720:CELLS[3] * 720]) != bytes(after[CELLS[2] * 720:CELLS[3] * 720]),
   "cells are not the global envelope's")
ok("..with the banks it had",
   band_ink(back, TITLE) > band_ink(after, TITLE),
   "%d lit against %d" % (band_ink(back, TITLE), band_ink(after, TITLE)))

home_op = ppm(4600)
home_glo = ppm(5000)
BANNER = (0, 240, 18, 50)
DIAG = (0, 240, 64, 200)


def ink(b, box):                                # anything at all that is not the background
    x0, x1, y0, y1 = box
    return sum(1 for y in range(y0, y1) for x in range(x0, x1)
               if sum(px(b, x, y)) > 60)


ok("a tap of ENV still shows the diagram",
   ink(home_op, DIAG) > 400, "%d pixels of diagram" % ink(home_op, DIAG))
ok("the GLOBAL key renames the banner",
   bytes(home_glo[18 * 720:50 * 720]) != bytes(home_op[18 * 720:50 * 720]), "banner differs")
# GLOBAL is not in the algorithm, so the diagram has nothing to say about it and gives up its room
# to the shape the knobs are actually moving. The held stretch is the colour that proves which of
# the two pictures is on the screen: nothing in the diagram uses it.
ok("..and gives the diagram's room to the envelope",
   count(home_glo, DIAG, HELD, 8) > 10 and count(home_op, DIAG, HELD, 8) == 0,
   "%d pixels of held line, against %d before"
   % (count(home_glo, DIAG, HELD, 8), count(home_op, DIAG, HELD, 8)))
# The curve inside a box and the white ring around it mark the operator the knobs are on. GLOBAL
# is not an operator, so both have to go: otherwise the screen names one thing and points at
# another. The curve is the plain claim -- its fill is a colour nothing else on the diagram uses.
CURVE = (82, 65, 140)
ok("..but stops drawing a curve in a box",
   count(home_glo, DIAG, CURVE, 20) == 0,
   "%d pixels of curve, against %d before"
   % (count(home_glo, DIAG, CURVE, 20), count(home_op, DIAG, CURVE, 20)))
# The diagram is gone, so every white pixel in it goes with it bar the envelope's live marker.
ok("..and takes the ring with it",
   count(home_glo, DIAG, (255, 255, 255), 20) < count(home_op, DIAG, (255, 255, 255), 20) * 3 // 5,
   "%d white pixels, against %d before"
   % (count(home_glo, DIAG, (255, 255, 255), 20), count(home_op, DIAG, (255, 255, 255), 20)))


# ---- 5. and it is in the sound --------------------------------------------------------------
with open(os.path.join(D, "global.wav"), "rb") as f:
    w = f.read()
i = w.index(b"data") + 8
pcm = w[i:]
FS = 44100


def rms(t0_ms, t1_ms):
    a, b = int(FS * t0_ms / 1000) * 4, int(FS * t1_ms / 1000) * 4
    n = (b - a) // 4
    if n <= 0:
        return 0.0
    v = struct.unpack_from("<%dh" % (n * 2), pcm, a)
    return (sum(float(x) * x for x in v) / len(v)) ** 0.5


# The first chord is at 200 ms with the envelope doing nothing; the second is at 5200 ms under a
# 424 ms attack. Thirty milliseconds in, the second one cannot have arrived yet; by 424 it has.
plain = rms(210, 240)
faded = rms(5210, 5240)
ok("the chord with no envelope is there at once", plain > 300, "rms %.0f" % plain)
ok("the one under a 424 ms attack is not", faded < plain / 3.0,
   "rms %.0f against %.0f" % (faded, plain))
ok("..but it has arrived by the end of it", rms(5610, 5660) > faded * 3.0,
   "rms %.0f" % rms(5610, 5660))
ok("and the sustain is 49, so it is quieter than the first chord was",
   rms(6100, 6300) < rms(700, 900), "rms %.0f against %.0f" % (rms(6100, 6300), rms(700, 900)))

# The third chord, with RELEASE at 921 ms. EPIANO 1's own release is over in a third of a second,
# so the only way the note can still be sounding half a second after the key comes up is if the
# operators were held in sustain underneath the fade rather than let go -- which is the claim.
# And it has to end when the fade does, or something is holding a voice open that should not be.
ok("the patch's own release is finished half a second after the key",
   rms(1900, 2000) < 20, "rms %.0f" % rms(1900, 2000))
ok("..but the global one is still going, because the operators were held",
   rms(8700, 8800) > 200.0,
   "rms %.0f at the same offset, against %.0f" % (rms(8700, 8800), rms(1900, 2000)))
ok("..and it stops when the fade reaches the bottom",
   rms(9150, 9350) < 20, "rms %.0f" % rms(9150, 9350))

ok("it starts switched out", count(ppm(2690), GRAPH, ACC) == 0,
   "%d lilac pixels" % count(ppm(2690), GRAPH, ACC))

# ---- 6. and it switches out ------------------------------------------------------------------
# A tap of ENV on the global envelope takes it out of the sound without touching its knobs. Out,
# the chord is the patch's own: there at once, as loud as the first one, and let go when the key
# is -- so it is finished half a second later, which with RELEASE at 852 ms it would not be.
ok("a tap of ENV on it switches it out, and says so in the picture",
   band_ink(ppm(9700), DIAG) != band_ink(ppm(7500), DIAG) and count(ppm(9700), DIAG, ACC) == 0,
   "%d lilac pixels left" % count(ppm(9700), DIAG, ACC))
ok("..with the knobs where they were", ppm(9700)[208 * 720:222 * 720] == ppm(7500)[208 * 720:222 * 720])
ok("..and the chord is the patch's own: there at once",
   abs(rms(9810, 9840) / plain - 1.0) < 0.1, "rms %.0f against %.0f" % (rms(9810, 9840), plain))
ok("..and let go with the key", rms(10900, 11000) < 20, "rms %.0f" % rms(10900, 11000))
ok("a tap more puts the same shape back", ppm(11600)[64 * 720:200 * 720] == ppm(7500)[64 * 720:200 * 720])
ok("..and the global release with it", rms(12900, 13000) > 200.0, "rms %.0f" % rms(12900, 13000))
ok("on the ENVELOPE page, a tap of ENV switches it out too",
   count(ppm(14400), GRAPH, ACC) == 0 and count(ppm(14000), GRAPH, ACC) > 100,
   "%d lilac pixels, from %d" % (count(ppm(14400), GRAPH, ACC), count(ppm(14000), GRAPH, ACC)))
ok("..and in again, as it was above what the button just said", ppm(14800)[:185 * 720] == ppm(14000)[:185 * 720])

print("\n%d checks failed" % bad)
sys.exit(1 if bad else 0)
