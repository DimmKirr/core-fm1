#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The arpeggiator and the sequencer, in samples.

    python3 tests/playcheck.py build/host/play

INIT VOICE is one sine, so each note is found by where the sound starts and named by its pitch.
The arpeggiator has to step through the held chord a sixteenth at a time (125 ms at 120 BPM), in
the pattern and over the octaves asked for, leave a gap after each note, stop with the keys, and
keep going without them once latched. REC from stopped has to click a bar in -- the downbeat
higher -- then take one bar, snapped to the sixteenth, and loop it; REC while it plays has to add
to it for one time round; CLICK has to tick the quarters; a hold of REC has to clear it.
"""
import sys, os, struct

D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "host")
FS = 44100
bad = 0


def ok(name, good, detail=""):
    global bad
    if not good:
        bad += 1
    print("%-54s %s    %s" % (name, "ok  " if good else "WRONG", detail))


with open(os.path.join(D, "play.wav"), "rb") as f:
    w = f.read()
pcm = w[w.index(b"data") + 8:]
L = struct.unpack("<%dh" % (len(pcm) // 2), pcm)[0::2]
PEAK = max(abs(s) for s in L[:FS * 8])          # a note's, before there is any click


def at(ms):
    return int(FS * ms / 1000)


def rms(t0, t1):
    v = L[at(t0):at(t1)]
    return (sum(float(s) * s for s in v) / max(1, len(v))) ** 0.5


def onset(t0, t1):
    """the first ms in [t0, t1) the sound comes up out of silence, or None"""
    th = PEAK * 0.1
    for i in range(at(t0), at(t1)):
        if abs(L[i]) > th and rms(i * 1000 / FS - 8, i * 1000 / FS - 1) < th * 0.3:
            return i * 1000.0 / FS
    return None


def crossings(t0, t1):
    xs = []
    for i in range(at(t0), at(t1)):
        a, b = L[i - 1], L[i]
        if a < 0 <= b:
            xs.append(i - 1 + (-a) / float(b - a))
    return xs


def pitch(t0, t1):
    xs = crossings(t0, t1)
    return (len(xs) - 1) * FS / (xs[-1] - xs[0]) if len(xs) > 2 else 0.0


C, E, G, A = 261.63, 329.63, 392.00, 440.00
NAME = {C: "C", E: "E", G: "G", A: "A", 2 * C: "C'", 2 * E: "E'", 2 * G: "G'"}


def run(title, t0, notes, step=125.0, gate=0.6, steps=None):
    """notes due one a step from t0 (or after each of steps): each where it should be, at its pitch"""
    got, sung = [], []
    steps = steps or [step] * len(notes)
    for n, f in enumerate(notes):
        due = t0 + sum(steps[:n])
        t = onset(due - 40, due + 40)
        got.append(t)
        p = pitch(t + 5, t + min(70, steps[n] * gate - 3)) if t is not None else 0.0
        sung.append(min(NAME, key=lambda g: abs(g - p)) if p else None)
        if t is None or abs(p - f) > f * 0.01:
            ok("%s: note %d" % (title, n), False, "due %d ms, %s: found %s, %.1f Hz" %
               (due, NAME[f], "%.0f ms" % t if t is not None else "nothing", p))
    ok(title, None not in sung and [NAME[s] for s in sung] == [NAME[f] for f in notes],
       " ".join(NAME[s] if s else "?" for s in sung))
    ts = [t for t in got if t is not None]
    d = [b - a for a, b in zip(ts, ts[1:])]
    if d:
        ok("  ... one every %s ms" % "/".join("%d" % x for x in sorted(set(steps))),
           all(abs(x - y) < 3 for x, y in zip(d, steps)),
           "%s ms apart, the first %+.0f ms off its key" % (" ".join("%.0f" % x for x in d), ts[0] - t0))
    if len(ts) == len(steps):
        g = [rms(t + y * gate + 10, t + y * gate + 30) for t, y in zip(ts[:-1], steps)]
        s = [rms(t + y * gate - 25, t + y * gate - 5) for t, y in zip(ts[:-1], steps)]
        ok("  ... sounding for %d %%, then a gap" % (gate * 100), max(g) < PEAK * 0.05 and min(s) > PEAK * 0.3,
           "quietest held %.0f, loudest gap %.0f, of %d" % (min(s), max(g), PEAK))


run("ARP UP, C E G held", 1000, [C, E, G] * 4)
ok("  ... and nothing once the keys go", rms(2560, 2700) < PEAK * 0.01, "%.0f" % rms(2560, 2700))
run("OCTAVE 2", 2800, [C, E, G, 2 * C, 2 * E, 2 * G, C, E])
run("DOWN, two octaves", 4100, [2 * G, 2 * E, 2 * C, G, E, C])
run("LATCH: C E played and let go", 5900, [C, E] * 4 + [C])
ok("  ... G alone replaces the chord", all(
    abs(pitch(t + 10, t + 70) - G) < 4 for t in (7150, 7275, 7400)),
   "%.1f %.1f %.1f Hz" % tuple(pitch(t + 10, t + 70) for t in (7150, 7275, 7400)))
ok("  ... and ARP turns it off", rms(7700, 7950) < PEAK * 0.01, "%.0f" % rms(7700, 7950))

# the count-in: four clicks from where the first one is found, the first one higher
T0 = None
for i in range(at(8100), at(8400)):
    if abs(L[i]) > 1000:
        T0 = i * 1000.0 / FS
        break
ok("REC: a bar of count-in", T0 is not None and all(
    rms(T0 + 500 * k, T0 + 500 * k + 15) > 1000 and rms(T0 + 500 * k + 30, T0 + 500 * k + 480) < 50
    for k in range(4)), "from %s ms" % ("%.0f" % T0 if T0 else "?"))
if T0 is None:
    sys.exit(1)
hz = [len(crossings(T0 + 500 * k, T0 + 500 * k + 15)) / 0.015 for k in range(4)]
ok("  ... the downbeat higher", hz[0] > 1.6 * max(hz[1:]), "%.0f, then %.0f %.0f %.0f Hz" % tuple(hz))
D1 = T0 + 2000                                  # the downbeat


def bar(title, b, notes):
    bad0, det = 0, []
    for off, f in notes:
        t = onset(b + off - 15, b + off + 15)
        p = pitch(t + 10, t + 70) if t is not None else 0.0
        det.append("%s@%s" % (NAME[f], "%+.0f" % (t - b - off) if t is not None else "?"))
        bad0 += t is None or abs(p - f) > f * 0.01
    ok(title, not bad0, " ".join(det))


ONE = [(0, C), (500, G), (1000, E)]
bar("  ... then one bar, snapped and looping", D1 + 2000, ONE)
bar("  ... and again", D1 + 4000, ONE)
ok("  ... C as long as it was held: two steps", rms(D1 + 2200, D1 + 2230) > PEAK * 0.2 and
   rms(D1 + 2300, D1 + 2480) < PEAK * 0.01, "%.0f then %.0f" % (rms(D1 + 2200, D1 + 2230), rms(D1 + 2300, D1 + 2480)))
ok("  ... and no click with CLICK off", rms(D1 + 3500, D1 + 3520) < 20, "%.0f" % rms(D1 + 3500, D1 + 3520))
bar("REC while playing: the bar still there", D1 + 6000, ONE)
f = pitch(D1 + 6770, D1 + 6840)                 # G ends as it starts: found by pitch, not by onset
ok("  ... and the high C over it, on its eighth", abs(f - 2 * C) < 3, "%.1f Hz" % f)
P0 = onset(18550, 18700)
ok("PLAY: from the top", P0 is not None, "%.0f ms" % (P0 or 0))
if P0:
    ok("  ... the low G after the overdub not in it", rms(P0 + 1640, P0 + 1700) < PEAK * 0.01,
       "%.0f" % rms(P0 + 1640, P0 + 1700))
    ok("  ... and CLICK ticks the quarters", rms(P0 + 1500, P0 + 1515) > 1000 and
       rms(P0 + 1530, P0 + 1600) < 50, "%.0f" % rms(P0 + 1500, P0 + 1515))
ok("REC held: cleared, and PLAY has nothing", rms(20800, 22000) < PEAK * 0.01, "%.0f" % rms(20800, 22000))

run("ARP 2/2: RATE 1/8, GATE 30 %", 22500, [C, E, G, C], step=250.0, gate=0.3)
run("SWING 75 %", 23800, [C, E, G, C, E], steps=[375.0, 125.0] * 2 + [375.0], gate=0.3)
run("AS PLAYED: G, C, E", 25300, [G, C, E, G, C, E], step=250.0, gate=0.3)

f = pitch(28720, 28790)
ok("STEPS: a step landed on, stopped, is heard", abs(f - G) < 3 and rms(29000, 29150) < PEAK * 0.01,
   "%.1f Hz, then %.0f" % (f, rms(29000, 29150)))
P1 = onset(29150, 29350)
ok("  ... PLAY loops what was put in", P1 is not None, "%.0f ms" % (P1 or 0))
if P1:
    bar("  ... C, A where G was, E", P1, [(0, C), (500, A), (1000, E)])
    ok("  ... E four steps long", rms(P1 + 1400, P1 + 1480) > PEAK * 0.2 and
       rms(P1 + 1560, P1 + 1900) < PEAK * 0.01, "%.0f then %.0f" % (rms(P1 + 1400, P1 + 1480), rms(P1 + 1560, P1 + 1900)))
    bar("  ... A taken off while playing: C and E", P1 + 2000, [(0, C), (1000, E)])
    ok("  ... and nothing where A was", rms(P1 + 2500, P1 + 2900) < PEAK * 0.01, "%.0f" % rms(P1 + 2500, P1 + 2900))


def ppm(ms):
    with open(os.path.join(D, "play-%d.ppm" % ms), "rb") as f:
        d = f.read()
    return d[-240 * 240 * 3:]


a, b, c = ppm(600), ppm(800), ppm(21500)
ok("SEQ and ARP each show their own knobs", a[200 * 720:] != b[200 * 720:] and a[200 * 720:] == c[200 * 720:])
ok("  ... and ARP a second four", ppm(22200)[200 * 720:] != b[200 * 720:])


def px(img, x, y):
    i = (y * 240 + x) * 3
    return tuple(img[i:i + 3])


def cell(img, i):
    """what step i shows: its middle, the cursor's frame, and the playhead's bar"""
    x, y = 6 + (i & 15) * 14 + (i >> 4 & 3) * 0 + (i & 15) // 4 * 2, 60 + (i >> 4) * 30
    m = px(img, x + 6, y + 7)
    kind = "start" if m[2] > 200 and m[0] > 150 else "held" if m[2] > 90 else "empty"
    return kind, px(img, x - 1, y + 7) == (255, 255, 255), px(img, x + 6, y + 17)[0] > 200


def grid(img):
    return [cell(img, i) for i in range(16)]


g0, g1, g2 = grid(ppm(27700)), grid(ppm(29000)), grid(ppm(34200))
ok("STEPS: empty, the cursor on step 1", all(k == "empty" for k, _, _ in g0) and
   [c for _, c, _ in g0] == [True] + [False] * 15)
want = ["start", "empty", "empty", "empty", "start", "empty", "empty", "empty",
        "start", "held", "held", "held", "empty", "empty", "empty", "empty"]
ok("  ... C, A, and E held three more", [k for k, _, _ in g1] == want, " ".join(k[0] for k, _, _ in g1))
ok("  ... the cursor on step 5", [c for _, c, _ in g1] == [i == 4 for i in range(16)])
ok("  ... no playhead, stopped", not any(p for _, _, p in g1 + g2))
gp = grid(ppm(30060))
ph = [i for i, (_, _, p) in enumerate(gp) if p]
ok("  ... and one, playing", len(ph) == 1, "on step %s" % ",".join(str(i + 1) for i in ph))
ok("  ... A gone from the grid", [k for k, _, _ in g2][4] == "empty")
ok("  ... BARS on K3: a second row", px(ppm(34200), 12, 97) == (0, 0, 0) and px(ppm(34400), 12, 97) != (0, 0, 0),
   "%s then %s" % (px(ppm(34200), 12, 97), px(ppm(34400), 12, 97)))

sys.exit(1 if bad else 0)
