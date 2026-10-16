#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check a tests/focus.txt run: a tap and a hold are two different buttons.

    python3 tests/focuscheck.py build/host/focus

Tapped, EDIT and ENV stay on HOME and put one operator under the four knobs. Held, they open their
page. The two have to be told apart cleanly, which means proving three separate things.

That a tap does not open the page: HOME's drawing is still on the screen afterwards, and the thing
that did change is the legend and the banner.

That the knobs really moved: they are driven to numbers while focused, the page is then opened the
long way, and the bars are read there. A bar is a byte scaled to a width in pixels, so "level 50"
is a prediction of 115 pixels. The two operators are given different numbers, so a knob wired to
the wrong one fails rather than passing by luck.

And that the black keys stopped playing: every OP key pressed here must send no MIDI, while a
white key pressed in the same breath must send its note as usual.
"""
import re
import sys
from pathlib import Path

LEG_Y, LEG_H, CELL = 208, 16, 60        # the knob legend, four cells across the bottom
HEAD_Y, HEAD_H = 18, 32                 # the banner, which names the focus when there is one
DIAG_Y, DIAG_H = 90, 80                 # the operator boxes, which a tap must leave alone
ROW_Y = (81, 113, 145, 177)             # the bar under each of OPERATOR's four rows
EG_ROW_Y = (149, 181)                   # and under each of ENVELOPE's two raw rows
CELL_Y, CELL_X, CELL_W = 179, 3, 54     # and under each of the four ADSR cells
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

    def legend(ms):
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

    def run(ms, y, x0, n):
        w, px = shot(ms)
        return sum(1 for x in range(x0, x0 + n) if tuple(px[(y * w + x) * 3:][:3]) == ACC)

    def bars(ms, rows=ROW_Y):
        return [run(ms, y, BAR_X, BAR_W) for y in rows]

    def adsr(ms):
        return [run(ms, CELL_Y, e * CELL + CELL_X, CELL_W) for e in range(4)]

    def want(v, full=BAR_W):
        return full * v // 99

    # ---- a tap does not open a page
    check("a tap of EDIT leaves the diagram alone",
          band(500, DIAG_Y, DIAG_H) == band(100, DIAG_Y, DIAG_H))
    check("but renames the banner", band(500, HEAD_Y, HEAD_H) != band(100, HEAD_Y, HEAD_H))
    check("and the four knobs with it", legend(500) != legend(100))
    check("all four knobs are named, the fourth the wave", all(ink(c) > 0 for c in legend(500)),
          " ".join(str(ink(c)) for c in legend(500)))

    # ---- an operator key moves the knobs to another operator, and the banner says so
    check("an operator key renames the banner",
          band(1900, HEAD_Y, HEAD_H) != band(500, HEAD_Y, HEAD_H))
    check("without touching the knob names", legend(1900) == legend(500))

    # ---- the knobs really were on those two operators, read off the page afterwards
    check("the operator the key picked has the level it was given",
          bars(3000)[0] == 0, f"{bars(3000)[0]} pixels, want 0")
    check("and the one before it kept its own", bars(3500)[0] == want(50),
          f"{bars(3500)[0]} pixels, want {want(50)}")
    check("so the two knobs were not the same knob", bars(3000)[0] != bars(3500)[0])

    # ---- ENV focuses the envelope instead, and the number survives the trip to the page
    check("a tap of ENV names four different knobs", legend(3900) != legend(500))
    check("all four of them", all(ink(c) > 0 for c in legend(3900)),
          " ".join(str(ink(c)) for c in legend(3900)))
    check("the attack knob put 40 on the ADSR bank", adsr(5300)[0] == want(40, CELL_W),
          f"{adsr(5300)[0]} pixels, want {want(40, CELL_W)}")
    check("which is rate 1 of 59 on the raw bank", bars(6400, EG_ROW_Y)[0] == want(59),
          f"{bars(6400, EG_ROW_Y)[0]} pixels, want {want(59)}")

    # ---- and HOME hands the knobs back
    check("HOME takes the focus off again", legend(7400) == legend(100))
    check("which is not what it was a moment before", legend(7000) != legend(7400))

    # ---- the keyboard, meanwhile
    log = stem.with_suffix(".log")
    midi = re.findall(r"MIDI out ([0-9a-f]{2}) (\d+)", log.read_text()) if log.exists() else None
    if midi is None:
        check("the operator keys sent nothing", False, "no log to read")
    else:
        notes = [(a, b) for a, b in midi if a in ("90", "80")]
        check("the operator keys sent nothing", len(notes) == 2,
              " ".join(a + " " + b for a, b in notes))
        check("and the white key still played", notes and notes[0] == ("90", "53"),
              " ".join(a + " " + b for a, b in notes))

    print(f"\n{bad} checks failed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "build/host/focus"))
