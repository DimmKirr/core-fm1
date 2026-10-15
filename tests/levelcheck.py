#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check a tests/levels.txt run: the operator boxes against the sound and the knobs.

    python3 tests/levelcheck.py build/host/levels

Every operator is drawn the same way -- an outline that fills from the left -- so what the screen
says about an operator is a count of pixels in a handful of colours, and nothing else on the
diagram uses them. The body of the fill is dim, the level itself is a bright edge on top of it,
and the outline is dimmer still and always there.

The interesting claims are the ones about time: a plucked voice has to empty its boxes as it
decays, a modulator with a shorter envelope has to empty before the carrier it feeds, and letting
go has to take every box with it. None of that can be true unless the drawing really is reading
the engine's per-operator level each frame.

Then the exception. The operator the knobs are on draws its envelope instead of its level, which
is a claim a bar cannot make at all: with nothing sounding anywhere on the screen, that one box
has to have something in it, that something has to reach the bottom right-hand corner, and turning
LEVEL alone has to lower it. Its number has to be gone and everyone else's has to still be there.
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
# and every other operator is lilac, so each kind has its own three. White is the operator's
# number -- the selection ring is white too, but it is drawn outside the box, not in it.
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
    "white":    lambda r, g, b: r >= 250 and g >= 250 and b >= 250,
}
FILL = ("body", "fb_body", "edge", "fb_edge")


class Shot:
    """One screen, read once: where every box is, and what is drawn inside each of them."""

    def __init__(self, path):
        w, px = read(path)
        self.n = {k: 0 for k in INK}
        pix = {}                        # (x, y) -> which kind of ink
        for y in range(PLOT_Y, PLOT_Y + PLOT_H):
            row = (y * w) * 3
            for x in range(W):
                i = row + x * 3
                r, g, b = px[i], px[i + 1], px[i + 2]
                if r == 0 and g == 0 and b == 0:
                    continue
                for k, f in INK.items():
                    if f(r, g, b):
                        self.n[k] += 1
                        pix[(x, y)] = k
                        break
        self.pix = pix
        self.boxes = self._boxes()

    def _boxes(self):
        """A box is a lid and a floor of frame colour at the same x range. Finding them from the
        outline alone keeps what is drawn inside from being mistaken for part of the shape. Two
        boxes stacked in one column share an x range, so the rows are paired off, not spanned."""
        runs = {}
        for (x, y), k in self.pix.items():
            if k in ("frame", "fb_frame"):
                runs.setdefault(y, set()).add(x)
        lids = {}
        for y, xs in runs.items():
            xs = sorted(xs)
            a = 0
            while a < len(xs):
                b = a
                while b + 1 < len(xs) and xs[b + 1] == xs[b] + 1:
                    b += 1
                if b - a + 1 >= 10:     # a lid, not the two side posts of some other row
                    lids.setdefault((xs[a], xs[b]), []).append(y)
                a = b + 1
        out = []
        for (x0, x1), ys in lids.items():
            ys.sort()
            for i in range(0, len(ys) - 1, 2):  # every box draws exactly one lid and one floor
                out.append((x0, x1, ys[i], ys[i + 1]))
        out.sort(key=lambda t: (t[0], -t[2]))   # left column first, bottom operator first
        return out

    def inside(self, i):
        """Every drawn pixel strictly inside box i, as {kind: [(x, y), ...]}."""
        x0, x1, y0, y1 = self.boxes[i]
        got = {}
        for (x, y), k in self.pix.items():
            if x0 < x < x1 and y0 < y < y1:
                got.setdefault(k, []).append((x, y))
        return got

    def fill(self, i=None):
        if i is None:
            return sum(self.n[k] for k in FILL)
        g = self.inside(i)
        return sum(len(g.get(k, ())) for k in FILL)

    def filled(self, i):
        """The filled pixels inside box i, whichever brightness they were drawn at."""
        g = self.inside(i)
        return [p for k in FILL for p in g.get(k, ())]

    def crest(self, i):
        """The highest anything reaches in box i -- the top of the curve, so the level."""
        pts = self.filled(i)
        return min(y for _, y in pts) if pts else None


def main(prefix):
    p = Path(prefix)

    def shot(ms):
        f = p.with_name(p.name + f"-{ms}.ppm")
        if not f.exists():
            raise SystemExit(f"no screen at {ms} ms beside {prefix}")
        return Shot(f)

    rest = shot(400)
    held = [shot(820), shot(1400), shot(2400)]
    gone = shot(3400)
    muted = shot(4100)
    packed = shot(5100)
    loud, quiet = shot(6200), shot(6700)

    # The boxes are drawn at all, and the operator in the feedback loop gets its own warm colour
    # so that it reads differently from the rest whatever its level happens to be.
    check("six operators, six boxes", len(rest.boxes) == 6, f"{len(rest.boxes)} found")
    warm = rest.n["fb_frame"] + rest.n["fb_body"] + rest.n["fb_edge"]
    check("the feedback operator is drawn warm", warm > 40, f"{warm} px")
    check("a box with nothing playing is empty", rest.fill() == 0, f"{rest.fill()} px")

    # The fill is a dim body under a bright edge, one edge per operator that is sounding.
    check("holding a note fills the boxes", held[0].fill() > 200, f"{held[0].fill()} px")
    check("every fill carries a level edge",
          held[0].n["edge"] + held[0].n["fb_edge"] > 40,
          f"{held[0].n['edge'] + held[0].n['fb_edge']} px")

    # The claim: a plucked voice empties its boxes as it decays.
    f = [s.fill() for s in held]
    check("the fill falls while the note decays", f[0] > f[1] > f[2], f"{f[0]} -> {f[1]} -> {f[2]} px")

    # And letting go takes all of it, while leaving the boxes themselves on the screen.
    check("letting go empties them", gone.fill() == 0, f"{gone.fill()} px")
    check("the empty boxes are still drawn", gone.n["frame"] > 100, f"{gone.n['frame']} px")

    # The feedback bracket is drawn by the box, so the live redraw cannot clip it any more.
    check("the feedback bracket survives the live redraw", gone.n["bracket"] >= 12,
          f"{gone.n['bracket']} px")

    # A switched-off operator: struck through, and no fill of its own.
    check("a switched-off operator is struck through", muted.n["strike"] >= 8,
          f"{muted.n['strike']} px")

    # Six carriers side by side, the tightest the boxes ever pack. Each box's live redraw is
    # clamped to its own column, so a cut across them finds six separate boxes all the same width.
    # If one redraw reached into the next, two would have merged or one would be short a side.
    wide = {x1 - x0 for x0, x1, _, _ in packed.boxes}
    check("six boxes abreast stay six separate boxes", len(packed.boxes) == 6,
          f"{len(packed.boxes)} found")
    check("and none of them has been eaten into", len(wide) == 1, f"widths {sorted(wide)}")

    # Now the exception. Nothing is sounding, so every bar is empty -- and exactly one box is not
    # a bar. Box 0 is the bottom of the left-hand column, which under algorithm 1 is operator one,
    # which is where SELECT starts.
    drawn = [i for i in range(len(loud.boxes)) if loud.fill(i) > 0]
    check("editing one operator draws one curve", drawn == [0], f"boxes {drawn}")
    check("and the other five stay empty bars",
          sum(loud.fill(i) for i in range(1, 6)) == 0)

    # Solid, not a line: every column that has any of the curve in it has all of it, down to the
    # floor. A hollow outline would leave gaps underneath.
    x0, x1, y0, y1 = loud.boxes[0]
    cols = {}
    for x, y in loud.filled(0):
        cols.setdefault(x, []).append(y)
    check("the curve is filled in solid, not outlined",
          bool(cols) and all(sorted(v) == list(range(min(v), y1)) for v in cols.values()),
          f"{len(cols)} columns")
    check("and it lands in the bottom right-hand corner", (x1 - 1, y1 - 1) in loud.filled(0))

    # It gives up its number, because the white ring round it has already said which it is.
    check("the operator being edited drops its number",
          len(loud.inside(0).get("white", ())) == 0)
    check("and the others keep theirs",
          all(len(loud.inside(i).get("white", ())) > 0 for i in range(1, 6)))

    # And the reason that box stopped being a bar. Nothing is sounding in either shot -- so a bar
    # would have been empty in both -- and LEVEL alone has to lower the whole curve.
    check("nothing is sounding either side of the turn",
          loud.fill(1) == 0 and quiet.fill(1) == 0)
    a, b = loud.crest(0), quiet.crest(0)
    check("turning LEVEL down lowers the curve", a is not None and b is not None and b > a,
          f"crest y {a} -> {b}")

    print(f"\n{bad} checks failed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "build/host/levels"))
