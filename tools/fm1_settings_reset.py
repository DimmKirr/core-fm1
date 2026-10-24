#!/usr/bin/env python3
"""Reset FM1 settings to factory defaults over USB MIDI SysEx.

Sends F0 7D 46 4C 02 F7. The firmware erases saved settings,
writes defaults, and reboots.
"""
import sys
import time
import mido

RESET_CMD = [0xF0, 0x7D, 0x46, 0x4C, 0x7F, 0xF7]
KNOWN_NAMES = ["Core FM1", "Felucca", "FM-1"]


def find_port(hint=None):
    names = hint and [hint] or KNOWN_NAMES
    for port in mido.get_output_names():
        for n in names:
            if n.lower() in port.lower():
                return port
    return None


def main():
    hint = sys.argv[1] if len(sys.argv) > 1 else None
    port_name = find_port(hint)
    if not port_name:
        print(f"No MIDI port matching '{hint or KNOWN_NAMES}'. Available:")
        for p in mido.get_output_names():
            print(f"  {p}")
        sys.exit(1)

    print(f"Using: {port_name}")
    print("Sending settings reset command...")
    with mido.open_output(port_name) as port:
        port.send(mido.Message("sysex", data=RESET_CMD[1:-1]))
    print("Sent. Device will reboot with default settings.")


if __name__ == "__main__":
    main()
