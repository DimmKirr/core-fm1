#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check a tests/pitch.txt run: the PIT key, and the pitch envelope it opens.

    python3 tests/pitchcheck.py build/host/pitch

The claim worth proving is the one you hear. INIT VOICE is one sine with a flat pitch envelope, so
a held note is the key's own pitch; LEVEL 3 at +12.0 -- the level held while the key is down --
has to put the same note an octave up, by its zero crossings. Pressing PIT itself has to sound
nothing, because there it points rather than plays.

The rest is the screen: PIT changes what the title and the legend say, SELECT steps the second
pair of segments, the picture follows LEVEL 3 and shows a marker while a note is held, a hold of
ENV opens ENVELOPE on the same envelope, a tap of ENV switches it out and back in, GLOBAL reads back the byte that was written, and from
the OPERATOR page PIT lands on the same screen the hold did -- with OP1 going back to OP1's.
"""
import struct
import sys
from pathlib import Path

FS = 44100
TITLE_Y, TITLE_H = 18, 32
GRAPH_Y, GRAPH_H = 64, 136              # where the diagram was
EG_Y, EG_H = 44, 104                    # the ENVELOPE page's picture
LEG_Y, LEG_H, CELL = 208, 16, 60
ROW_Y = (81, 113, 145, 177)             # the bar under each of a page's four rows
BAR_X, BAR_W = 6, 228
ACC = (180, 141, 255)
WHITE = (255, 255, 255)
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

    def count(ms, y0, h, rgb):
        b = band(ms, y0, h)
        return sum(1 for i in range(0, len(b), 3) if tuple(b[i:i + 3]) == rgb)

    def cells(ms):
        w, px = shot(ms)
        out = []
        for e in range(4):
            c = b""
            for y in range(LEG_Y, LEG_Y + LEG_H):
                i = (y * w + e * CELL + 1) * 3
                c += px[i:i + (CELL - 1) * 3]
            out.append(c)
        return out

    def ink(cell):
        return sum(1 for i in range(0, len(cell), 3) if cell[i:i + 3] != b"\0\0\0")

    def bar(ms, row):
        w, px = shot(ms)
        y = ROW_Y[row]
        return sum(1 for x in range(BAR_X, BAR_X + BAR_W)
                   if tuple(px[(y * w + x) * 3:(y * w + x) * 3 + 3]) == ACC)

    wav = stem.with_name(stem.name + ".wav").read_bytes()
    pcm = wav[wav.index(b"data") + 8:]
    L = struct.unpack("<%dh" % (len(pcm) // 2), pcm)[0::2]

    def at(ms):
        return int(FS * ms / 1000)

    def rms(t0, t1):
        v = L[at(t0):at(t1)]
        return (sum(float(s) * s for s in v) / max(1, len(v))) ** 0.5

    def pitch(t0, t1):
        xs = []
        for i in range(at(t0), at(t1)):
            a, b = L[i - 1], L[i]
            if a < 0 <= b:
                xs.append(i - 1 + (-a) / float(b - a))
        return (len(xs) - 1) * FS / (xs[-1] - xs[0]) if len(xs) > 2 else 0.0

    # Heard.
    f0, f1 = pitch(900, 1300), pitch(4900, 5500)
    check("the note sounds at the key's own pitch", 250 < f0 < 275, f"{f0:.1f} Hz")
    check("PIT points: it sounds nothing", rms(2250, 2750) < 50, f"rms {rms(2250, 2750):.0f}")
    check("LEVEL 3 at +12.0 holds it an octave up", abs(f1 / f0 - 2.0) < 0.01 if f0 else False,
          f"{f1:.1f} Hz, x{f1 / f0 if f0 else 0:.3f}")

    # Seen, on HOME.
    check("PIT renames the knobs", band(2800, TITLE_Y, TITLE_H) != band(2000, TITLE_Y, TITLE_H))
    check("and names all four", all(ink(c) > 0 for c in cells(2800)),
          " ".join(str(ink(c)) for c in cells(2800)))
    check("and draws the envelope where the diagram was",
          count(2800, GRAPH_Y, GRAPH_H, ACC) > 20 and band(2800, GRAPH_Y, GRAPH_H) != band(2000, GRAPH_Y, GRAPH_H))
    check("SELECT has the other two segments", cells(3400) != cells(2800)
          and band(3400, GRAPH_Y, GRAPH_H) != band(2800, GRAPH_Y, GRAPH_H))
    check("the picture follows LEVEL 3", band(4400, GRAPH_Y, GRAPH_H) != band(3400, GRAPH_Y, GRAPH_H))
    check("and marks where a held note has got to",
          count(4400, GRAPH_Y, GRAPH_H, WHITE) == 0 and count(5200, GRAPH_Y, GRAPH_H, WHITE) > 0,
          f"{count(4400, GRAPH_Y, GRAPH_H, WHITE)} -> {count(5200, GRAPH_Y, GRAPH_H, WHITE)} px")

    # The page, and the bytes.
    check("a hold of ENV opens ENVELOPE on it, SELECT its next bank", shot(7600) != shot(6800))
    f2 = pitch(8100, 8500)
    check("a tap of ENV switches it out: the key's pitch", abs(f2 / f0 - 1.0) < 0.01 if f0 else False,
          f"{f2:.1f} Hz")
    check("and says so, in grey", count(8400, EG_Y, EG_H, WHITE) > 30
          and count(8400, EG_Y, EG_H, ACC) == 0, f"{count(8400, EG_Y, EG_H, WHITE)} px white")
    check("a second tap has it back as it was", band(9200, EG_Y, EG_H) == band(7600, EG_Y, EG_H))
    want = round(82 * BAR_W / 99)
    check("GLOBAL reads back P LEVEL 3 = 82", abs(bar(10400, 2) - want) <= 2,
          f"{bar(10400, 2)} px, want {want}")
    check("PIT from OPERATOR lands on the same screen", shot(12000) == shot(6800))
    check("and OP1 goes back to an operator's", shot(12800) != shot(12000))

    print(f"\n{bad} checks failed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "build/host/pitch"))
