#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check a tests/env.txt run: ADSR, and the eight bytes it is really writing.

    python3 tests/envcheck.py build/host/env

The ENVELOPE page says the same envelope twice -- four knobs that every synth has, all four on one
bank in a row of cells that line up with the knobs, then the four raw segments the DX7 stores. Two views of one set of bytes are worth having only if they
cannot disagree, so this drives the ADSR knobs to known places and then walks out to the raw banks
to read what landed.

Three of the four are turned round, because a DX7 rate counts upwards as it gets faster and every
knob labelled ATTACK counts upwards as it gets slower. That is the claim the sums test: the attack
bar and the rate 1 bar have to add up to a full-width bar, not match it.

And two of them write a byte you did not ask for. DECAY writes rate 3 with rate 2, SUSTAIN writes
level 2 with level 3; without that a decay drawn in two stages would only half obey the knob. The
test reads both halves of each pair off the banks that show them apart.
"""
import sys
from pathlib import Path

ROW_Y = (149, 181)                      # the bar under each of ENVELOPE's two raw rows
BAR_X, BAR_W = 6, 228
CELL_Y, CELL_X, CELL_W = 179, 3, 54     # and under each of the four ADSR cells, 60 pixels apart
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
        """the two bar widths on this screen, in pixels"""
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
        """the whole screen, for the banks that only have to match or differ"""
        return read(stem.with_name(stem.name + f"-{ms}.ppm"))[1]

    def cells(ms):
        """the four ADSR bar widths on this screen, in pixels"""
        w, px = read(stem.with_name(stem.name + f"-{ms}.ppm"))
        out = []
        for e in range(4):
            n = 0
            for x in range(e * 60 + CELL_X, e * 60 + CELL_X + CELL_W):
                i = (CELL_Y * w + x) * 3
                if tuple(px[i:i + 3]) == ACC:
                    n += 1
            out.append(n)
        return out

    def want(v, full=BAR_W):
        return full * v // 99

    adsr = cells(1800)
    r1, l1 = bars(2700)
    r2, l2 = bars(3600)
    r3, l3 = bars(4500)
    r4, l4 = bars(5400)

    # All four fit on one bank, a cell to a knob, and each landed where the script drove it.
    check("ADSR is four cells in one row", len([c for c in adsr if c]) == 4,
          " ".join(str(c) for c in adsr))
    for name, got, v in (("attack", adsr[0], 59), ("decay", adsr[1], 25),
                         ("sustain", adsr[2], 70), ("release", adsr[3], 45)):
        check(f"{name} is where the script put it", abs(got - want(v, CELL_W)) <= 1,
              f"{got} pixels, want {want(v, CELL_W)}")

    # Turned round: 99 minus the time on the page is the rate in the byte. The raw bars are four
    # times the resolution of the cells, so the claim is made against them.
    for name, v, got in (("attack", 59, r1), ("decay", 25, r2), ("release", 45, r4)):
        check(f"{name} is the rate counted backwards", abs(got - want(99 - v)) <= 1,
              f"{got} pixels, want {want(99 - v)} for rate {99 - v}")

    # Sustain is a level and is not turned round.
    check("sustain is the level itself, not backwards", abs(l3 - want(70)) <= 1,
          f"{l3} pixels, want {want(70)}")

    # The bytes neither knob was pointed at.
    check("decay wrote the second stage with the first", r2 == r3, f"rate 2 {r2}, rate 3 {r3}")
    check("sustain wrote the level it decays to", l2 == l3, f"level 2 {l2}, level 3 {l3}")
    check("so the two decay stages are one slope", r2 == r3 and l2 == l3)

    # Levels 1 and 4 are nobody's business but the raw page's: a patch that peaks below full, or
    # releases to something above silence, goes on doing it.
    check("level 1 was left alone", l1 == want(99), f"{l1} pixels")
    check("level 4 was left alone", l4 == 0, f"{l4} pixels")

    # Walking all five banks and coming back has to be a round trip.
    check("five banks and round to the start again", cells(6300) == adsr,
          f"{cells(6300)} against {adsr}")

    # A tap of the button whose page you are on is the next bank, not a trip to HOME. Proven by
    # reaching the same bank both ways and comparing every pixel: if the tap had focused the knobs
    # on HOME, or stayed put, or landed on a different bank, the two screens could not match.
    check("a tap on ENVELOPE walks the banks", full(7400) == full(9800))
    check("and it is a different bank", full(7400) != full(6300))

    print(f"\n{bad} checks failed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "build/host/env"))
