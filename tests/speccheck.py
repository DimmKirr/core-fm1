#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check a tests/spectrum.txt run: the analyser against the arithmetic.

    python3 tests/speccheck.py build/host/spectrum

The screen is read rather than compared. A bar's top is drawn in white and nothing else on that
screen is, so the white pixels give the height of every column, and the heights say where the
partials are. C4 is 261.63 Hz, so at a span of 5 kHz its fundamental belongs at column 12.6 and
its harmonics at 25.1 and 37.7 -- which is a claim about FM, about the axis, and about the
transform all at once, and it is only true if all three are right.
"""
import sys
from pathlib import Path

PLOT_Y, PLOT_H, W = 64, 136, 240        # where draw_spectrum() blits
C4 = 261.626
bad = 0


def check(what, ok, note=""):
    global bad
    if not ok:
        bad += 1
    print(f"{what:<48} {'ok' if ok else 'WRONG':<5} {note}")


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


def main(prefix):
    p = Path(prefix)

    def shot(ms):
        f = p.with_name(p.name + f"-{ms}.ppm")
        if not f.exists():
            raise SystemExit(f"no screen at {ms} ms beside {prefix}")
        return read(f)

    def columns(ms, want):
        """Height of each column, by colour: "cap" is a bar's white top, "peak" the lilac mark."""
        w, px = shot(ms)
        out = {}
        for x in range(W):
            for y in range(PLOT_Y, PLOT_Y + PLOT_H):
                r, g, b = px[(y * w + x) * 3:(y * w + x) * 3 + 3]
                white = r > 240 and g > 240 and b > 240
                lilac = 160 < r < 200 and 120 < g < 160 and b > 230
                if white if want == "cap" else lilac:
                    out[x] = PLOT_Y + PLOT_H - y
                    break
        return out

    def peak(cols):
        return max(cols, key=lambda x: cols[x]) if cols else -1

    raw = {ms: shot(ms)[1] for ms in (200, 600, 4800)}
    check("the second press of HOME is another screen", raw[200] != raw[600])
    check("and a third comes back to the diagram", raw[4800] == raw[200])

    quiet = columns(600, "cap")
    check("nothing playing draws no bars", not quiet, f"{len(quiet)} columns")

    five, ten, back = columns(1400, "cap"), columns(2200, "cap"), columns(3000, "cap")
    check("a note held draws bars", len(five) > 10, f"{len(five)} columns")

    # The fundamental, and then the harmonics, each looked for only near where it should be: the
    # tallest column in that neighbourhood has to be the one the arithmetic names.
    for k in (1, 2, 3):
        want = k * C4 * W / 5000.0
        near = {x: h for x, h in five.items() if abs(x - want) <= 4}
        x = peak(near)
        check(f"harmonic {k} is at column {want:.1f}", near and abs(x - want) <= 1.5 and near[x] >= 20,
              f"tallest at {x}, {near.get(x, 0)} px")

    want = C4 * W / 10000.0
    x = peak({x: h for x, h in ten.items() if abs(x - want) <= 4})
    check("twice the span puts it half as far across", abs(x - want) <= 1.5, f"column {x}, want {want:.1f}")
    check("and turning back gives the first picture again", peak(back) == peak(five),
          f"{peak(back)} against {peak(five)}")

    tail, gone = columns(3600, "cap"), columns(4400, "cap")
    tall = max(tail.values(), default=0)
    check("letting go takes the bars down with it", tall * 3 < five[peak(five)],
          f"{tall} px against {five[peak(five)]}")
    check("and a moment later there is nothing left", not gone, f"{len(gone)} columns")
    # The mark the fundamental left behind: above nothing at all, below where the bar reached, and
    # lower again a moment later. It is a memory of the sound, and it is supposed to fade.
    fx = peak(five)
    a, b = columns(3600, "peak"), columns(4400, "peak")
    check("the fundamental's peak outlives its bar", a.get(fx, 0) > 10,
          f"{a.get(fx, 0)} px at column {fx}, where the bar reached {five[fx]}")
    check("and it is coming down", 0 < b.get(fx, 0) < a.get(fx, 0),
          f"{b.get(fx, 0)} px, was {a.get(fx, 0)}")

    print(f"\n{bad} checks failed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "build/host/spectrum"))
