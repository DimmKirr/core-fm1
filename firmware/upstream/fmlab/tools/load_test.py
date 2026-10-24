#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Load the FM-1 over USB-MIDI and watch the CPU meter on its screen.

Holds a chord of N notes so you can read `CPU` (the smoothed load) and `PK` (the worst single
block since the last SEL press) off the device. Both are a percentage of the 5.80 ms the audio
ISR has to render 256 frames at 44.1 kHz; over 100 % means it cannot keep up.

  python tools/load_test.py              # 1, 2, 4, 6, 8 notes, 10 s each
  python tools/load_test.py 8 20         # 8 notes for 20 s
Pick a voice that sustains with all six operators (6 BRASS) for the worst case.
"""
import sys
import time

import mido

NOTES = [48, 52, 55, 60, 64, 67, 72, 76]          # C3 E3 G3 C4 E4 G4 C5 E5


def chord(port, n, secs):
    print(f"\n=== {n} note{'s' if n != 1 else ''} -- hold, read CPU and PK ===", flush=True)
    for m in NOTES[:n]:
        port.send(mido.Message("note_on", note=m, velocity=100))
    for left in range(secs, 0, -1):
        print(f"\r    {left:2d} s ", end="", flush=True)
        time.sleep(1)
    for m in NOTES[:n]:
        port.send(mido.Message("note_off", note=m))
    print("\r    released", flush=True)
    time.sleep(1.5)


def main():
    name = next((p for p in mido.get_output_names() if "FM-1" in p or "Felucca" in p), None)
    if not name:
        sys.exit(f"no FM-1 in {mido.get_output_names()}")
    stages = [(int(sys.argv[1]), int(sys.argv[2]) if len(sys.argv) > 2 else 10)] if len(sys.argv) > 1 \
        else [(n, 10) for n in (1, 2, 4, 6, 8)]
    print(f"port: {name}")
    with mido.open_output(name) as port:
        try:
            for n, secs in stages:
                chord(port, n, secs)
        finally:
            for m in NOTES:                        # never leave a note stuck
                port.send(mido.Message("note_off", note=m))
            port.send(mido.Message("control_change", control=123, value=0))
    print("\ndone")


if __name__ == "__main__":
    main()
