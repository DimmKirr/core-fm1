#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the .syx files tests/bank.txt feeds to the host build.

They are made here rather than checked in as real DX7 patches on purpose: FM Lab ships no Yamaha
voice data, and a generated bank is also the only kind a test can make assertions about. Every
voice is a sine except the single-voice dump, which is a two-operator stack, so "did the right
voice arrive in the right slot" is audible as well as readable.

    python3 tests/mksyx.py build/host
"""
import sys
from pathlib import Path


def init_voice():
    """The DX7's INIT VOICE, unpacked (155 bytes). Operators are stored OP6 first."""
    u = bytearray(155)
    for op in range(6):
        o = op * 21
        u[o + 0:o + 4] = bytes([99, 99, 99, 99])        # EG rates: instant
        u[o + 4:o + 8] = bytes([99, 99, 99, 0])         # EG levels
        u[o + 8] = 39                                   # break point C3
        u[o + 16] = 99 if op == 5 else 0                # block 5 is OP1, the only carrier
        u[o + 18] = 1                                   # coarse x1
        u[o + 20] = 7                                   # no detune
    u[126:130] = bytes([99, 99, 99, 99])                # pitch EG rates
    u[130:134] = bytes([50, 50, 50, 50])                # pitch EG levels: flat
    u[134] = 0                                          # algorithm 1
    u[136] = 1                                          # oscillator key sync
    u[137] = 35                                         # LFO speed
    u[141] = 1                                          # LFO key sync
    u[143] = 3                                          # pitch mod sensitivity
    u[144] = 24                                         # transpose: middle C
    u[145:155] = b"INIT VOICE"
    return u


def named(u, name):
    u[145:155] = name.ljust(10)[:10].encode("ascii")
    return u


def pack(u):
    """155 unpacked -> the 128 bytes a .syx carries (dx_pack in firmware/src/dx7_voice.h)."""
    b = bytearray(128)
    for op in range(6):
        o, q = op * 21, op * 17
        b[q:q + 11] = u[o:o + 11]
        b[q + 11] = u[o + 11] | u[o + 12] << 2
        b[q + 12] = u[o + 13] | u[o + 20] << 3
        b[q + 13] = u[o + 14] | u[o + 15] << 2
        b[q + 14] = u[o + 16]
        b[q + 15] = u[o + 17] | u[o + 18] << 1
        b[q + 16] = u[o + 19]
    b[102:110] = u[126:134]
    b[110] = u[134]
    b[111] = u[135] | u[136] << 3
    b[112:116] = u[137:141]
    b[116] = u[141] | u[142] << 1 | u[143] << 4
    b[117] = u[144]
    b[118:128] = u[145:155]
    return bytes(b)


def checksum(data):
    return (-sum(data)) & 0x7F


def bulk(voices):
    body = b"".join(pack(v) for v in voices)
    assert len(body) == 4096
    return b"\xf0\x43\x00\x09\x20\x00" + body + bytes([checksum(body), 0xF7])


def single(u):
    return b"\xf0\x43\x00\x00\x01\x1b" + bytes(u) + bytes([checksum(u), 0xF7])


def test_bank():
    """32 sines, each a different ratio, each knowing its own slot number."""
    out = []
    for k in range(32):
        u = named(init_voice(), "TEST %02d" % (k + 1))
        u[5 * 21 + 18] = 1 + k % 4                      # OP1 coarse: x1 x2 x3 x4, round and round
        out.append(u)
    return out


def one_voice():
    """Two operators: OP2 bending OP1. Obviously not a sine, by ear and in the numbers."""
    u = named(init_voice(), "ONE VOICE")
    u[4 * 21 + 16] = 85                                 # block 4 is OP2: a modulator with some level
    u[4 * 21 + 18] = 3                                  # at three times the pitch
    u[5 * 21 + 5] = 70                                  # and give OP1 somewhere to decay to
    return u


def bright_voice():
    """OP2 at full level on OP1, both x1, no LFO: every harmonic, far past 22 kHz, and nothing else,
    so whatever lands between the harmonics has folded back (tests/alias.txt)."""
    u = named(init_voice(), "BRIGHT")
    u[4 * 21 + 16] = 99
    u[143] = 0                                          # no vibrato to smear the harmonics
    return u


def main(outdir):
    d = Path(outdir)
    d.mkdir(parents=True, exist_ok=True)
    good = bulk(test_bank())
    bad = bytearray(good)
    bad[-2] ^= 0x01                                     # one bit of the checksum: must be refused
    for name, data in (("bank.syx", good), ("voice.syx", single(one_voice())), ("bad.syx", bytes(bad)),
                       ("bright.syx", single(bright_voice())),
                       ("req.syx", b"\xf0\x43\x20\x09\xf7\xf0\x43\x20\x00\xf7")):
        (d / name).write_bytes(data)
        print("%-10s %5d B -> %s" % (name, len(data), d / name))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "build/host")
