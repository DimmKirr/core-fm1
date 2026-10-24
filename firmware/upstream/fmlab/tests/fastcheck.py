#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check a tests/fast.txt run: a click is worth five, and SEL held is the fine-adjust shift.

    python3 tests/fastcheck.py build/host/fast

Every parameter row draws a bar whose width is the value, so four clicks of a knob is a number
this can do arithmetic on. That makes the claim exact rather than "it moved less": four clicks
move LEVEL by twenty, and the same four clicks with SEL held move it by four.

Only the 0..99 bytes are worth five a click. Everything else is a list of choices rather than a
range, and the proof of that is next door: tests/edit.txt steps RATIO one entry a click with SEL
nowhere near it, and would fail outright if the coarse step had been applied to a list.

The rest is about SEL keeping its old job without the two of them colliding. The names in the
legend go bright while it is down, so the shift is visible rather than something you have to
remember. Letting go after turning a knob does nothing -- the turn spoke for the press -- which is
the case that matters, because otherwise reaching for a precise value would land you on a
different bank every time. Letting go without turning one is still the plain tap it always was.
"""
import sys
from pathlib import Path

ROW_Y, BAR_X, BAR_W, W = 81, 6, 228, 240    # the bar under OPERATOR's first row, which is LEVEL
PAGE_Y = (70, 200)                          # the rows, with the legend and the status bar left out
LEGEND_Y, LEGEND_H = 208, 16
ACC = (180, 141, 255)                       # C_ACC, lilac, after 565 and back
bad = 0


def check(what, ok, note=""):
    global bad
    if not ok:
        bad += 1
    print(f"{what:<52} {'ok' if ok else 'WRONG':<5} {note}")


def read(path):
    b = path.read_bytes()
    i = fields = w = 0                      # P6 <w> <h> <max>, then the pixels
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

    def px(ms):
        if ms not in shots:
            f = stem.with_name(stem.name + f"-{ms}.ppm")
            if not f.exists():
                raise SystemExit(f"no screen at {ms} ms beside {stem}")
            shots[ms] = read(f)
        return shots[ms]

    def bar(ms):
        """LEVEL as a width in pixels"""
        w, b = px(ms)
        n = 0
        for x in range(BAR_X, BAR_X + BAR_W):
            i = (ROW_Y * w + x) * 3
            if tuple(b[i:i + 3]) == ACC:
                n += 1
        return n

    def rows(ms):
        """the parameter rows, as bytes, to say whether the bank under them changed"""
        w, b = px(ms)
        return b[PAGE_Y[0] * w * 3:PAGE_Y[1] * w * 3]

    def legend_lit(ms):
        """how much of the knob legend is drawn in the accent colour"""
        w, b = px(ms)
        n = 0
        for y in range(LEGEND_Y, LEGEND_Y + LEGEND_H):
            for x in range(W):
                i = (y * w + x) * 3
                if tuple(b[i:i + 3]) == ACC:
                    n += 1
        return n

    # The sums. LEVEL runs 0..99 across 228 pixels, so a click is a shade over two pixels: four
    # and twenty are nowhere near each other, and both are read off the same bar.
    check("LEVEL starts from the bottom of its range", bar(900) == 0, f"{bar(900)} px")
    coarse = bar(1100)
    check("four clicks move it twenty", abs(coarse - round(20 * BAR_W / 99)) <= 2,
          f"{coarse} px, twenty is about {round(20 * BAR_W / 99)}")
    check("and SEL puts it back to the bottom", bar(1400) == 0, f"{bar(1400)} px")
    fine = bar(1600)
    check("the same four clicks with SEL held move it four",
          abs(fine - round(4 * BAR_W / 99)) <= 2,
          f"{fine} px, four is about {round(4 * BAR_W / 99)}")
    check("which is a fifth as far, not merely less",
          abs(coarse - 5 * fine) <= 3, f"{coarse} px against 5 x {fine}")

    # Visible while it is happening, rather than something to remember.
    cool, lit = legend_lit(900), legend_lit(1400)
    check("the knob legend lights up while SEL is held", lit > cool + 40, f"{cool} px -> {lit} px")
    check("and goes out again when it is let go", legend_lit(2300) == cool,
          f"{legend_lit(2300)} px")

    # The collision that had to be designed away: a turn under SEL spends the press.
    check("letting SEL go after a turn does nothing at all",
          rows(2300) == rows(1600))
    # And the shortcut it has always been, on a tap with no turn in it.
    check("and SELECT steps the bank", rows(2600) != rows(2300))

    print(f"\n{bad} checks failed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "build/host/fast"))
