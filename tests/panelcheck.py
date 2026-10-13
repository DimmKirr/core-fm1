#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check a tests/panel.txt run: what the panel sent, and what the screen did.

    python3 tests/panelcheck.py build/host/panel

Reads <prefix>.log (the run's own output) and the screens beside it. The screens are compared
byte for byte rather than looked at: the claims here are all of the form "this is the same screen
as that one" or "this is a different one", which is exactly what a comparison can settle.
"""
import sys
from pathlib import Path

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
    want = [(0x90, 53, 100), (0x80, 53, 0),      # the white key on the OPERATOR page
            (0x90, 66, 100), (0x80, 66, 0),      # the black key, back on HOME
            (0x90, 53, 100), (0x80, 53, 0)]      # held across a page change
    check("the operator keys sent nothing", len(sent) == len(want), f"{len(sent)} messages, want {len(want)}")
    check("the keys that are keys still played", sent == want,
          " ".join("%02X%d" % (s, d1) for s, d1, _ in sent))
    on = [m for m in sent if m[0] == 0x90]
    off = [m for m in sent if m[0] == 0x80]
    check("every note that started was stopped",
          len(on) == len(off) and all(a[1] == b[1] for a, b in zip(on, off)))

    def screen(ms):
        for ext in (".ppm", ".png"):
            f = p.with_name(p.name + f"-{ms}" + ext)
            if f.exists():
                return f.read_bytes()
        raise SystemExit(f"no screen at {ms} ms beside {prefix}")

    s = {ms: screen(ms) for ms in (400, 800, 1200, 1600, 2000, 2400, 2800, 3200, 3800, 4400)}

    walked = [s[400], s[800], s[1200], s[1600]]
    check("OPERATOR has four banks, all different", len(set(walked)) == 4)
    check("the fifth press wrapped to the first", s[2000] == s[400])
    check("ENVELOPE walked a bank of its own", s[2400] != s[2800])
    check("ENVELOPE is not OPERATOR", s[2400] != s[400])
    check("coming back to a page starts it over", s[3200] == s[400])
    check("a black key picked another operator", s[3800] != s[3200])
    check("and the first black key picked OP1 back", s[4400] == s[3200])

    print(f"\n{bad} checks failed")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "build/host/panel"))
