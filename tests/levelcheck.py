#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check a tests/levels.txt run: the operator boxes against the envelopes behind them.

    python3 tests/levelcheck.py build/host/levels

Every operator is drawn the same way now -- an outline that fills from the left -- so what the
screen says about an operator is a count of pixels in three colours, and nothing else on the
diagram uses them. The body of the fill is dim, the level itself is a bright edge on top of it,
and the outline is dimmer still and always there.

The interesting claims are the ones about time: a plucked voice has to empty its boxes as it
decays, a modulator with a shorter envelope has to empty before the carrier it feeds, and letting
go has to take every box with it. None of that can be true unless the drawing really is reading
the engine's per-operator level each frame.
"""
import sys
from pathlib import Path

PLOT_Y, PLOT_H, W = 64, 136, 240        # where draw_alg() blits the diagram
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


# The colours alg_box() draws with, after 565 and back. An operator in the feedback loop is warm
# and every other operator is lilac, so each kind has its own three.
def _near(v, lo, hi):
    return lo <= v <= hi


INK = {
    "edge":     lambda r, g, b: _near(r, 135, 160) and _near(g, 110, 132) and b >= 245,
    "body":     lambda r, g, b: _near(r, 72, 92) and _near(g, 55, 74) and _near(b, 128, 150),
    "frame":    lambda r, g, b: _near(r, 48, 66) and _near(g, 40, 56) and _near(b, 96, 116),
    "fb_edge":  lambda r, g, b: r >= 245 and _near(g, 140, 158) and _near(b, 82, 98),
    "fb_body":  lambda r, g, b: _near(r, 130, 150) and _near(g, 74, 90) and _near(b, 42, 58),
    "fb_frame": lambda r, g, b: _near(r, 96, 118) and _near(g, 52, 70) and _near(b, 24, 42),
    "bracket":  lambda r, g, b: _near(r, 195, 215) and _near(g, 140, 158) and _near(b, 82, 98),
    "strike":   lambda r, g, b: _near(r, 180, 200) and _near(g, 78, 94) and _near(b, 82, 98),
}


def boxes_across(rows):
    """The runs a horizontal cut finds, taking the busiest row as the cut: one run per box."""
    y = max(rows, key=lambda k: len(rows[k]))
    runs = []
    for x in rows[y]:
        if runs and x == runs[-1][1] + 1:
            runs[-1][1] = x
        else:
            runs.append([x, x])
    return [(a, b - a + 1) for a, b in runs]


def main(prefix):
    p = Path(prefix)

    def ink(ms):
        """How many pixels of each kind the diagram is drawn in, and the fill extents per column."""
        f = p.with_name(p.name + f"-{ms}.ppm")
        if not f.exists():
            raise SystemExit(f"no screen at {ms} ms beside {prefix}")
        w, px = read(f)
        n = {k: 0 for k in INK}
        cols = {k: set() for k in INK}
        rows = {}
        for y in range(PLOT_Y, PLOT_Y + PLOT_H):
            row = (y * w) * 3
            here = []
            for x in range(W):
                i = row + x * 3
                r, g, b = px[i], px[i + 1], px[i + 2]
                if r == 0 and g == 0 and b == 0:
                    continue
                for k, f_ in INK.items():
                    if f_(r, g, b):
                        n[k] += 1
                        cols[k].add(x)
                        here.append(x)
                        break
            rows[y] = here
        return n, cols, rows


    held_a, _, _ = ink(450)
    held_b, _, _ = ink(900)
    held_c, _, _ = ink(1700)
    gone, _, gone_rows = ink(3400)
    muted, _, _ = ink(4000)
    packed, packed_c, packed_rows = ink(5000)

    # The boxes are drawn at all, and the operator in the feedback loop gets its own warm colour
    # so that it reads differently from the rest whatever its level happens to be.
    check("the boxes are outlined", held_a["frame"] > 100, f"{held_a['frame']} px")
    warm = held_a["fb_frame"] + held_a["fb_body"] + held_a["fb_edge"]
    check("the feedback operator is drawn warm", warm > 60, f"{warm} px")

    # The fill is a dim body under a bright edge, one edge per operator that is sounding.
    fill_a = held_a["body"] + held_a["fb_body"]
    check("holding a note fills the boxes", fill_a > 200, f"{fill_a} px")
    check("every fill carries a level edge", held_a["edge"] + held_a["fb_edge"] > 40,
          f"{held_a['edge'] + held_a['fb_edge']} px")

    # The claim: a plucked voice empties its boxes as it decays.
    fill_b = held_b["body"] + held_b["fb_body"]
    fill_c = held_c["body"] + held_c["fb_body"]
    check("the fill falls while the note decays", fill_a > fill_b > fill_c,
          f"{fill_a} -> {fill_b} -> {fill_c} px")

    # And letting go takes all of it, while leaving the boxes themselves on the screen.
    fill_g = gone["body"] + gone["fb_body"] + gone["edge"] + gone["fb_edge"]
    check("letting go empties them", fill_g == 0, f"{fill_g} px")
    check("the empty boxes are still drawn", gone["frame"] > 100, f"{gone['frame']} px")

    # A carrier is no longer a solid block -- the fill means loudness now, not routing. If a
    # carrier were still drawn filled, the silent screen could not be empty of body pixels.
    check("a silent carrier is an outline, not a block", gone["body"] == 0)

    # The feedback bracket is drawn by the box, so the live redraw cannot clip it any more.
    check("the feedback bracket survives the live redraw", gone["bracket"] >= 12,
          f"{gone['bracket']} px")

    # A switched-off operator: struck through, and no fill of its own.
    check("a switched-off operator is struck through", muted["strike"] >= 8,
          f"{muted['strike']} px")

    # Six carriers side by side. Each box's live patch is clamped to its own column, so every one
    # of them still has its outline: six boxes' worth of frame, not five.
    # Six carriers side by side, the tightest the boxes ever pack. Each box's live redraw is
    # clamped to its own column, so a cut across them finds six separate boxes all the same width.
    # If one redraw reached into the next, two would have merged or one would be short a side.
    runs = boxes_across(packed_rows)
    check("six boxes abreast stay six separate boxes", len(runs) == 6, f"{len(runs)} found")
    check("and none of them has been eaten into",
          len({w for _, w in runs}) == 1, f"widths {sorted({w for _, w in runs})}")

    print(f"\n{bad} checks failed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "build/host/levels"))
