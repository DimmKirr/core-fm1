#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check a tests/edit.txt run: RATIO, and the switch that puts the rest of the parameters back.

    python3 tests/editcheck.py build/host/edit

Every parameter row draws a bar under its value whose width is the amount, so a screenshot of a
page is four numbers. That is what this reads. It makes the claims arithmetic rather than "the
screen changed": the ratio steps one entry at a time, retraces exactly, stops at both ends, and
switches to counting frequency steps in FIXED mode.

The claim worth proving is the last one -- that RATIO is not a new thing stored anywhere, only the
COARSE and FINE bytes said differently. SHOW ALL puts those two back on the page, so the test
drives RATIO to each end of its list and then goes and reads the bytes it wrote.
"""
import sys
from pathlib import Path

ROW_Y = (81, 113, 145, 177)             # the bar under each of OPERATOR's four rows
BAR_X, BAR_W, W = 6, 228, 240
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

    def bars(ms):
        """the four bar widths on this screen, in pixels"""
        w, px = read(stem.with_name(stem.name + f"-{ms}.ppm"))
        out = []
        for y in ROW_Y:
            n = 0
            for x in range(BAR_X, BAR_X + BAR_W):
                i = (y * w + x) * 3
                if tuple(px[i:i + 3]) == ACC:
                    n += 1
            out.append(n)
        return out

    def full(ms):
        """the whole screen, for the banks that only have to differ"""
        return read(stem.with_name(stem.name + f"-{ms}.ppm"))[1]

    r = {ms: bars(ms)[1] for ms in (1100, 1500, 1900, 2300, 2700, 3100,
                                    3500, 3900, 4300, 4700, 5100, 5500, 6100, 10900)}

    # One entry per click. The widths are a list index scaled to 228 pixels, so consecutive entries
    # differ by about six and the steps have to be even as well as upward.
    up = [r[1500], r[1900], r[2300]]
    check("the ratio steps up one entry a click", up[0] < up[1] < up[2],
          f"{r[1100]} -> {up}")
    check("and the steps are the same size", abs((up[1] - up[0]) - (up[2] - up[1])) <= 1,
          f"{up[1] - up[0]} then {up[2] - up[1]}")
    check("the first click lands on the list", r[1500] != r[1100])

    # Coming back down has to arrive at the same entries, not half a step off them.
    check("turning back down retraces it", r[2700] == r[1900] and r[3100] == r[1500],
          f"{r[2700]} = {r[1900]}, {r[3100]} = {r[1500]}")

    # x0.50 is the first entry and x31.00 the last: the bar is empty at one end and full at the
    # other, and another click at either end does nothing at all.
    check("the bottom of the list is an empty bar", r[3500] == 0, f"{r[3500]} px")
    check("and it stops there", r[3900] == 0)
    check("the top of the list is a full bar", r[4300] == BAR_W, f"{r[4300]} px")
    check("and it stops there too", r[4700] == BAR_W)

    # FIXED counts four hundred frequency steps instead of thirty-seven entries, so coarse 31 --
    # of which the engine reads only the low two bits -- lands three decades up, at 1 kHz.
    check("FIXED reads the bytes as a frequency", r[5100] == round(BAR_W * 300 / 399),
          f"{r[5100]} px, want {round(BAR_W * 300 / 399)}")
    check("and the knob walks the steps", r[5500] == round(BAR_W * 350 / 399),
          f"{r[5500]} px, want {round(BAR_W * 350 / 399)}")

    # Four banks of four, all different, and the fifth press back to the first.
    walked = [full(6100), full(7200), full(8300), full(9400)]
    check("OPERATOR has four banks", len(set(walked)) == 4)
    check("and the fifth press wraps", full(10500) == full(6100))

    # The claim: RATIO is the COARSE and FINE bytes, which are the next bank along.
    b_low, b_high = bars(7200), bars(12000)
    check("x0.50 is coarse 0 and fine 0", b_low[0] == 0 and b_low[1] == 0, f"{b_low[:2]}")
    check("and the empty RATIO bar agreed", r[6100] == 0, f"{r[6100]} px")
    check("x31.00 is coarse 31 and fine 0", b_high[0] == BAR_W and b_high[1] == 0, f"{b_high[:2]}")
    check("and the full RATIO bar agreed", r[10900] == BAR_W, f"{r[10900]} px")

    # A tap of the button whose page you are on is the next bank, not a trip to HOME. Proven by
    # reaching the same bank both ways and comparing every pixel: if the tap had focused the knobs
    # on HOME, or stayed put, or landed on a different bank, the two screens could not match.
    check("a tap on OPERATOR walks the banks", full(13000) == full(16700))
    check("and it is a different bank", full(13000) != full(12000))

    print(f"\n{bad} checks failed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "build/host/edit"))
