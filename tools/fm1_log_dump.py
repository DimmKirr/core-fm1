#!/usr/bin/env python3
"""Dump the debug log from FM1 over USB MIDI SysEx.

Usage:
    python tools/fm1_log_dump.py              # auto-detect FM1 port
    python tools/fm1_log_dump.py "FM-1"       # match port by name substring

Sends F0 7D 46 4C 01 F7, waits for the reply, prints timestamped log lines.
Requires: pip install mido python-rtmidi
"""
import sys
import time
import threading
import mido

QUERY = [0xF0, 0x7D, 0x46, 0x4C, 0x7E, 0xF7]
HDR = bytes([0x7D, 0x46, 0x4C, 0x7E])
TIMEOUT = 5.0
KNOWN_NAMES = ["Core FM1", "Felucca", "FM-1"]


def find_ports(hint=None):
    names = hint and [hint] or KNOWN_NAMES
    in_name = out_name = None
    for port in mido.get_input_names():
        for n in names:
            if n.lower() in port.lower():
                in_name = port
                break
        if in_name:
            break
    for port in mido.get_output_names():
        for n in names:
            if n.lower() in port.lower():
                out_name = port
                break
        if out_name:
            break
    return in_name, out_name


def main():
    hint = sys.argv[1] if len(sys.argv) > 1 else None
    in_name, out_name = find_ports(hint)
    if not in_name or not out_name:
        print(f"No MIDI port matching '{hint or KNOWN_NAMES}'. Available:")
        print(f"  IN:  {mido.get_input_names()}")
        print(f"  OUT: {mido.get_output_names()}")
        sys.exit(1)

    print(f"IN:  {in_name}")
    print(f"OUT: {out_name}")

    result = []
    got_reply = threading.Event()

    def on_msg(msg):
        if msg.type == "sysex":
            data = bytes(msg.data)
            if data[:4] == HDR:
                result.append(data[4:])
                got_reply.set()

    inp = mido.open_input(in_name, callback=on_msg)
    out = mido.open_output(out_name)

    out.send(mido.Message("sysex", data=QUERY[1:-1]))
    print("Sent log dump request, waiting...")

    if got_reply.wait(timeout=TIMEOUT):
        body = result[0]
        text = bytes(b for b in body if b < 0x80).decode("ascii", errors="replace")
        for line in text.split("\n"):
            if line.strip():
                ts = line[:7]
                msg_text = line[8:] if len(line) > 8 else ""
                mm, ss, ms = ts[:2], ts[2:4], ts[4:7]
                print(f"{mm}:{ss}.{ms}  {msg_text}")
    else:
        print("Timeout: no log dump reply received.")
        inp.close()
        out.close()
        sys.exit(1)

    inp.close()
    out.close()


if __name__ == "__main__":
    main()
