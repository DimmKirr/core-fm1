#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Rank what fills RAM (.data + .bss) from the linker map.

    uv run python tools/ram_top.py [build/core_fm1.map] [-n 20]

`task flash:size-check` (run by `task flash`) prints the top entries so the 96 KB region is
never a surprise. Input sections named `.L_MergedGlobals.N` are clang's merged small globals of
one translation unit; they have no single owner.
"""
import re
import sys
from collections import namedtuple
from pathlib import Path

Row = namedtuple('Row', 'section name size obj')

_OUT = re.compile(r'^\.(\w[\w.]*)')
_NAME_ONLY = re.compile(r'^ (\.\S+)\s*$')
_SIZE_LINE = re.compile(r'^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)')
_ONE_LINE = re.compile(r'^ (\.\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)')


def parse(text):
    """Input sections of the .data and .bss output sections, largest first, zero-size dropped."""
    lines = text.splitlines()
    sec = None
    rows = []
    i = 0
    while i < len(lines):
        line = lines[i]
        m = _OUT.match(line)
        if m:
            sec = m.group(1)
        if sec in ('bss', 'data'):
            m2 = _NAME_ONLY.match(line)
            if m2 and i + 1 < len(lines):
                m3 = _SIZE_LINE.match(lines[i + 1])
                if m3:
                    rows.append(Row(sec, m2.group(1), int(m3.group(2), 16), Path(m3.group(3)).name))
                    i += 2
                    continue
            m4 = _ONE_LINE.match(line)
            if m4:
                rows.append(Row(sec, m4.group(1), int(m4.group(3), 16), Path(m4.group(4)).name))
        i += 1
    rows = [r for r in rows if r.size > 0]
    return sorted(rows, key=lambda r: -r.size)


def table(rows, top=20):
    total = sum(r.size for r in rows)
    out = [f".data+.bss: {total} bytes in {len(rows)} input sections; top {min(top, len(rows))}:"]
    for r in rows[:top]:
        out.append(f"  {r.size:>7}  {r.name:<40} {r.obj}")
    return "\n".join(out)


def main(argv):
    path = 'build/core_fm1.map'
    top = 20
    args = list(argv)
    if '-n' in args:
        i = args.index('-n')
        top = int(args[i + 1])
        del args[i:i + 2]
    if args:
        path = args[0]
    print(table(parse(Path(path).read_text(errors='replace')), top=top))


if __name__ == '__main__':
    main(sys.argv[1:])
