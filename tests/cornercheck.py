#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check a tests/corners.txt run: the battery, and which of HOME's screens you are on.

    python3 tests/cornercheck.py build/host/corners

The battery is three bars in a cell, and a bar is a 3x5 block, so how full it is reads as a pixel
count inside the outline: forty-five, thirty, fifteen, nothing. The script walks the raw ADC counts
down through all three thresholds and this reads the bars back, which is the only part of a
measured threshold a host can check at all -- whether 591 really is a full battery is a question
for a voltmeter, but whether 591 draws three bars is a question for this.

The colour is checked by its hue rather than by its number, so the test says what it means: the
outline is grey while there is charge in it, amber on the last bar, red on none.

The pips are the other corner, and the case worth a test is the one a signature cache gets wrong.
Every page change clears the whole screen, so the corners have to notice they have been wiped and
come back. Going to a page and returning is exactly that, twice.
"""
import sys
from pathlib import Path

BATT = (7, 22, 5, 10)                   # inside the cell: the bars, without the outline or the nub
LID = (10, 3)                           # a pixel on the top of the outline, for the colour
PIPS = (210, 240, 0, 16)                # the right-hand corner
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
    cache = {}

    def px(ms):
        if ms not in cache:
            f = stem.with_name(stem.name + f"-{ms}.ppm")
            if not f.exists():
                raise SystemExit(f"no screen at {ms} ms beside {stem}")
            cache[ms] = read(f)
        return cache[ms]

    def at(ms, x, y):
        w, b = px(ms)
        i = (y * w + x) * 3
        return b[i], b[i + 1], b[i + 2]

    def bars(ms):
        """how many pixels of bar are inside the cell: a bar is 3 x 5"""
        x0, x1, y0, y1 = BATT
        return sum(at(ms, x, y) != (0, 0, 0)
                   for y in range(y0, y1) for x in range(x0, x1))

    def pips(ms):
        """the lit pip's x, and how many dim ones there are beside it"""
        x0, x1, y0, y1 = PIPS
        lit, dim = [], []
        for y in range(y0, y1):
            for x in range(x0, x1):
                r, g, b = at(ms, x, y)
                if b > 200 and r > 150:
                    lit.append(x)
                elif 0 < max(r, g, b) < 110:
                    dim.append(x)
        return lit, dim

    # Three bars, then two, then one, then none, at 800, 570, 540 and 500 raw counts. A bar is
    # fifteen pixels, so these are the only four numbers the cell can hold.
    for ms, want in ((200, 3), (2000, 2), (2300, 1), (2600, 0)):
        check(f"{want} bars of battery", bars(ms) == want * 15, f"{bars(ms)} px, want {want * 15}")

    # And the outline says the same thing again in colour, for the two that matter.
    r, g, b = at(200, *LID)
    check("a battery with charge in it is grey", max(r, g, b) - min(r, g, b) < 24, f"{(r, g, b)}")
    r, g, b = at(2300, *LID)
    check("the last bar turns it amber", r > g > b and g - b > 50, f"{(r, g, b)}")
    r, g, b = at(2600, *LID)
    check("and an empty one turns it red", r > 200 and g < 130 and abs(g - b) < 24, f"{(r, g, b)}")

    # Two pips on HOME, one of them lit, and pressing HOME moves which.
    lit, dim = pips(200)
    check("HOME has two pips, one of them lit", len(lit) == 36 and len(dim) == 36,
          f"{len(lit)} lit, {len(dim)} dim")
    first = min(lit)
    lit2, dim2 = pips(600)
    check("and pressing HOME lights the other one",
          len(lit2) == 36 and min(lit2) > first, f"x {first} -> {min(lit2) if lit2 else None}")

    # A page has no pips, because its title already says which bank it is on.
    lit3, dim3 = pips(1700)
    check("a parameter page has no pips", not lit3 and not dim3,
          f"{len(lit3)} lit, {len(dim3)} dim")

    # The corners are cached on a signature, and a page change clears the screen out from under
    # them. Both of these are the picture surviving that.
    check("the battery survives leaving HOME", bars(1700) == 45, f"{bars(1700)} px")
    lit4, dim4 = pips(3200)
    check("and the pips come back on returning to it",
          len(lit4) == 36 and len(dim4) == 36, f"{len(lit4)} lit, {len(dim4)} dim")
    check("with the battery full again", bars(3200) == 45, f"{bars(3200)} px")

    print(f"\n{bad} checks failed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "build/host/corners"))
