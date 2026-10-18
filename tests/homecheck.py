#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check a tests/home.txt run: the four knobs on HOME.

    python3 tests/homecheck.py build/host/home

HOME draws no parameter rows, so a knob turned here leaves no number on the screen to read. The
test proves the wiring from the far end instead: turn the knob, walk to the EDIT page, and measure
the bar that draws the byte. A bar is a byte scaled to 228 pixels, so "thirty steps down" is a
prediction of 69 pixels and not a vague claim that something changed.

The legend is the other half. Four knobs have to be named on both HOME screens, SELECT has to step
through four sets and come back, and the sets that reach past OP6 have to leave the last cell blank
rather than name something that is not there -- the third is FEEDBACK, which is measured off the
GLOBAL page the same way, and drawn on the diagram as a bracket as bright as it is high.
"""
import sys
from pathlib import Path

LEG_Y, LEG_H, CELL = 208, 16, 60        # the knob legend, four cells across the bottom
VOICE_Y, VOICE_H = 40, 24               # the line HOME flashes messages on
ALG_Y, ALG_H = 64, 136                  # the diagram
ROW_Y = (81, 113, 145, 177)             # the bar under each of OPERATOR's four rows
BAR_X, BAR_W = 6, 228
ACC = (180, 141, 255)                   # C_ACC, lilac, after 565 and back
bad = 0


def check(what, ok, note=""):
    global bad
    if not ok:
        bad += 1
    print(f"{what:<52} {'ok' if ok else 'WRONG':<5} {note}")


def read(path):
    b = path.read_bytes()
    i = fields = w = 0                  # P6 <w> <h> <max>, then the pixels
    while fields < 4:
        while b[i:i + 1].isspace():
            i += 1
        if b[i:i + 1] == b"#":
            while b[i:i + 1] not in (b"\n", b""):
                i += 1
            continue
        s = i
        while not b[i:i + 1].isspace():
            i += 1
        if fields == 1:
            w = int(b[s:i])
        fields += 1
    return w, b[i + 1:]


def main(stem):
    stem = Path(stem)
    shots = {}

    def shot(ms):
        if ms not in shots:
            shots[ms] = read(stem.with_name(stem.name + f"-{ms}.ppm"))
        return shots[ms]

    def band(ms, y0, h):
        w, px = shot(ms)
        return px[y0 * w * 3:(y0 + h) * w * 3]

    def cells(ms):
        """the four legend cells of this screen, as bytes"""
        w, px = shot(ms)
        out = []
        for e in range(4):
            c = b""
            for y in range(LEG_Y, LEG_Y + LEG_H):
                i = (y * w + e * CELL + 1) * 3   # past the grey rule that divides the cells
                c += px[i:i + (CELL - 1) * 3]
            out.append(c)
        return out

    def ink(cell):
        return sum(1 for i in range(0, len(cell), 3) if cell[i:i + 3] != b"\0\0\0")

    def bars(ms):
        """the four bar widths on this screen, in pixels"""
        w, px = shot(ms)
        out = []
        for y in ROW_Y:
            n = 0
            for x in range(BAR_X, BAR_X + BAR_W):
                i = (y * w + x) * 3
                if tuple(px[i:i + 3]) == ACC:
                    n += 1
            out.append(n)
        return out

    # The legend. Four sets, stepped by SELECT, and the fourth step is back where it started.
    sets = {ms: cells(ms) for ms in (200, 600, 1000, 1400, 1800, 2200)}
    check("HOME names all four knobs", all(ink(c) > 0 for c in sets[200]),
          " ".join(str(ink(c)) for c in sets[200]))
    seen = [sets[ms] for ms in (200, 600, 1000, 1400)]
    check("SELECT steps through four different sets",
          len({bytes(b"".join(s)) for s in seen}) == 4)
    check("and the fourth step wraps back to the first", sets[1800] == sets[200])
    check("the set that runs past OP6 names FEEDBACK third, the fourth blank",
          ink(sets[600][0]) > 0 and ink(sets[600][1]) > 0
          and ink(sets[600][2]) > 0 and ink(sets[600][3]) == 0,
          " ".join(str(ink(c)) for c in sets[600]))
    check("the analyser has the filter's four knobs instead", sets[2200] != sets[200]
          and all(ink(c) > 0 for c in sets[2200]), " ".join(str(ink(c)) for c in sets[2200]))

    # KNOB1 is OP1's level. Thirty steps of a byte whose bar is 228 pixels wide for 99.
    before, after = bars(3700), bars(5400)
    want = round(30 * BAR_W / 99.0)
    check("a HOME knob moves the operator's own byte", before[0] - after[0] != 0,
          f"{before[0]} -> {after[0]} pixels")
    check("and it moved it by exactly thirty steps", abs((before[0] - after[0]) - want) <= 2,
          f"{before[0] - after[0]} pixels, want {want}")
    check("turning the level left the ratio alone", before[1] == after[1],
          f"{before[1]} against {after[1]}")

    # With no note held the boxes show nothing, so the voice line has to say what moved.
    check("and HOME says what it did", band(4300, VOICE_Y, VOICE_H) != band(200, VOICE_Y, VOICE_H))

    # The third set puts the ratio under the same knob, and now the other bar is the one that moves.
    last = bars(7300)
    check("the ratio set moves the ratio instead", last[1] > after[1],
          f"{after[1]} -> {last[1]} pixels")
    check("and leaves the level where the first set put it", last[0] == after[0],
          f"{after[0]} against {last[0]}")

    # FEEDBACK, from HOME. Five clicks up from the bottom is 5 of 7, on GLOBAL's second row.
    check("the LVL 5/6 set is the one with FEEDBACK", cells(7900) == sets[600])
    fb = bars(9700)[1]
    want = round(5 * BAR_W / 7.0)
    check("HOME's third knob is FEEDBACK, a click a step", abs(fb - want) <= 2,
          f"{fb} pixels, want {want}")
    check("and the bracket brightens as it goes up",
          band(8300, ALG_Y, ALG_H) != band(8700, ALG_Y, ALG_H))
    check("and HOME says what it did", band(8700, VOICE_Y, VOICE_H) != band(7900, VOICE_Y, VOICE_H))

    print(f"\n{bad} checks failed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "build/host/home"))
