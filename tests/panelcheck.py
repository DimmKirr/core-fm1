#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check a tests/panel.txt run: what the panel sent, and what the screen did.

    python3 tests/panelcheck.py build/host/panel

Reads <prefix>.log (the run's own output) and the screens beside it. The screens are compared
byte for byte rather than looked at: the claims here are all of the form "this is the same screen
as that one" or "this is a different one", which is exactly what a comparison can settle.
"""
import struct
import sys
from pathlib import Path

CROP_H = 240                        # the whole screen: the drifting CPU meter is on GLOBAL only now,
                                    # and nothing here goes there, so the knob legend gets compared too
bad = 0


def check(what, ok, note=""):
    global bad
    if not ok:
        bad += 1
    print(f"{what:<46} {'ok' if ok else 'WRONG':<5} {note}")


def main(prefix):
    p = Path(prefix)
    log = p.with_suffix(".log").read_text(errors="replace").splitlines()

    sent = []
    for line in log:
        i = line.find("MIDI out ")
        if i >= 0:
            st, d1, d2 = line[i + 9:].split()[:3]
            sent.append((int(st, 16), int(d1), int(d2)))

    # F3 is key 0 and the keyboard starts at 53, so F3 is 53 and F#4 (key 13) is 66.
    chord = [60, 64, 67]                         # keys 7, 11 and 14
    want = [(0x90, 53, 100), (0x80, 53, 0),      # the white key on the OPERATOR page
            (0x90, 66, 100), (0x80, 66, 0),      # the black key, back on HOME
            (0x90, 53, 100), (0x80, 53, 0)]      # held across a page change
    for _ in range(2):                           # the same chord in mono and then in poly
        want += [(0x90, n, 100) for n in chord] + [(0x80, n, 0) for n in chord]
    want += [(0x90, 60, 100), (0x90, 62, 100),   # the slur: 62 arrives with 60 still down
             (0x80, 60, 0), (0x80, 62, 0),
             (0x90, 62, 100), (0x80, 62, 0),     # and 62 again, on its own
             (0x90, 60, 100), (0x90, 62, 100),   # the fall-back: 62 let go, 60 still down
             (0x80, 62, 0), (0x80, 60, 0)]
    check("the operator keys sent nothing", len(sent) == len(want), f"{len(sent)} messages, want {len(want)}")
    check("the keys that are keys still played", sent == want,
          " ".join("%02X%d" % (s, d1) for s, d1, _ in sent))
    on = [m for m in sent if m[0] == 0x90]
    off = [m for m in sent if m[0] == 0x80]
    check("every note that started was stopped",
          len(on) == len(off) and sorted(a[1] for a in on) == sorted(b[1] for b in off))

    def screen(ms):
        """The screen, read out of the PPM the run wrote.

        The CPU meter and its running peak drift with whatever the machine happened to be doing, so
        a screen carrying them could not be compared at all; it lives on GLOBAL now, and nothing
        here goes to GLOBAL, so the whole screen is fair game -- including the strip along the
        bottom naming what the four knobs do, which has to walk with the banks like everything
        else. (This runs before the PPMs are turned into PNGs, so there is only the one format.)
        """
        f = p.with_name(p.name + f"-{ms}.ppm")
        if not f.exists():
            raise SystemExit(f"no screen at {ms} ms beside {prefix}")
        b = f.read_bytes()
        i, fields, w = 0, 0, 0                  # P6 <w> <h> <max>, then the pixels
        while fields < 4:
            while b[i:i + 1].isspace():
                i += 1
            if b[i:i + 1] == b"#":
                while b[i:i + 1] not in (b"\n", b""):
                    i += 1
                continue
            start = i
            while not b[i:i + 1].isspace():
                i += 1
            if fields == 1:
                w = int(b[start:i])
            fields += 1
        return b[i + 1:i + 1 + w * CROP_H * 3]

    s = {ms: screen(ms) for ms in (400, 800, 1200, 1600, 2000, 2400, 2800, 3200, 3800, 4400)}

    walked = [s[400], s[800], s[1200], s[1600]]
    check("OPERATOR has four banks, all different", len(set(walked)) == 4)
    check("the fifth press wrapped to the first", s[2000] == s[400])
    check("ENVELOPE walked a bank of its own", s[2400] != s[2800])
    check("ENVELOPE is not OPERATOR", s[2400] != s[400])
    check("coming back to a page starts it over", s[3200] == s[400])
    check("a black key picked another operator", s[3800] != s[3200])
    check("and the first black key picked OP1 back", s[4400] == s[3200])

    # One voice against eight: the chord is the same three keys either way, so the only thing that
    # can account for the difference is how many of them the engine let through.
    raw = p.with_suffix(".wav").read_bytes()
    i = 12
    while i < len(raw):
        cid, sz = struct.unpack_from("<4sI", raw, i)
        if cid == b"data":
            data = raw[i + 8:i + 8 + sz]
            break
        i += 8 + sz + (sz & 1)
    ch = struct.unpack_from("<H", raw, 22)[0]
    pcm = struct.unpack("<%dh" % (len(data) // 2), data)

    def peak(t0, t1):
        a, b = int(t0 * 44100 / 1000) * ch, int(t1 * 44100 / 1000) * ch
        return max(abs(v) for v in pcm[a:b]) if b > a else 0

    one, three = peak(6850, 7350), peak(8050, 8550)
    check("a chord in mono is one note", three and one * 100 // three < 60,
          f"{one} against {three} ({one * 100 // max(three, 1)}%)")
    check("and poly still plays all three", three > 1000, str(three))

    # The first note has had a second to die away by the time the second is pressed. A slur carries
    # on from there; a note struck on its own starts again.
    slur, struck = peak(10450, 10650), peak(11150, 11350)
    check("a slurred note gets no new attack", struck and slur * 100 // struck < 25,
          f"{slur} against {struck} ({slur * 100 // max(struck, 1)}%)")
    check("and a note struck on its own does", struck > 1000, str(struck))

    # Letting go of the newer note drops back to the one still held, not to silence; letting go of
    # that one is the end of it.
    back, gone = peak(12700, 13100), peak(13600, 13950)
    check("the older note is still there to fall back to", back > 300, str(back))
    check("and the last key up is the end of it", back and gone * 100 // back < 10,
          f"{gone} against {back}")

    print(f"\n{bad} checks failed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "build/host/panel"))
