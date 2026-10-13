#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Decode retained-ram.bin: the 512 bytes of .noinit at 0x01C7C000 that fm1-recover saves from a unit in UBOOT.

  tools/fm1_retained_ram.py retained-ram.bin [more.bin ...]

Layout of Melodee 1.3.x (app.ld NOINIT, link order): fm1_crash, melodee_dbg, bootguard, then fm1_core1_bar
(dual core only) and oc_guard. The loader leaves this RAM alone (loader.ld), so a unit that entered UBOOT
through the boot guard still holds what its last boots left here.
"""
import struct
import sys

CRASH = ('magic', 'count', 'vec', 'pc', 'rets', 'emu', 'dbg', 'sp', 'psr', 'icfg', 'uptime_ms',
         'etm0', 'etm1', 'etm2', 'etm3', 'early')
DBG = ('magic', 'halves', 'max_us', 'nested', 'in_audio', 'late', 'timer_irqs', 'ui_frames', 'last_us', 'cpu_q8',
       'boots', 'stage', 'page', 'home', 'prev_stage', 'prev_page', 'prev_home', 'prev_rst', 'prev_frames')
RST = ('power-on', 'VDDIO low', 'watchdog', 'VCM', 'long press', '1.2 V', 'soft (P33)')
MAGIC = {'crash': 0x43525348, 'dbg': 0x44424731, 'bootguard': 0x42475244, 'bar': 0x52414231, 'oc': 0x4F43474B}


def words(b, off, names):
    return dict(zip(names, struct.unpack_from('<%dI' % len(names), b, off)))


def rst(v):
    return ', '.join(n for i, n in enumerate(RST) if v >> i & 1) or 'none'


def decode(b):
    if len(b) < 0xB0:
        raise SystemExit('expected 512 bytes from 0x01C7C000')
    out = []
    c = words(b, 0x00, CRASH)
    if c['magic'] == MAGIC['crash']:
        out.append('crash      count %(count)d  vec %(vec)d  pc %(pc)08x  rets %(rets)08x  dbg %(dbg)08x  '
                   'emu %(emu)08x  %(uptime_ms)d ms after boot  early %(early)d' % c)
        out.append('           etm %08x %08x %08x %08x' % (c['etm0'], c['etm1'], c['etm2'], c['etm3']))
    else:
        out.append('crash      no record (no CPU exception since power-on)')
    d = words(b, 0x40, DBG)
    if d['magic'] == MAGIC['dbg']:
        out.append('boots      %(boots)d past the splash; the last one: reset by ' % d + rst(d['prev_rst']) +
                   ', main loop stage %(stage)d, %(ui_frames)d UI frames' % d)
    else:
        out.append('boots      no record (no boot reached the splash since power-on)')
    g = words(b, 0x8C, ('magic', 'failed', 'pending'))
    out.append('bootguard  ' + ('failed %(failed)d pending %(pending)d' % g if g['magic'] == MAGIC['bootguard']
                                else 'no record'))
    # dual core: fm1_core1_bar at 0x98, oc_guard at 0xA0; single core: oc_guard at 0x98. The bar's magic is written
    # only once CPU1 was barred (power-on garbage before), so the layout follows oc_guard's own magic
    single = struct.unpack_from('<I', b, 0x98)[0] == MAGIC['oc']
    if not single:
        bar = words(b, 0x98, ('magic', 'barred'))
        out.append('core1 bar  ' + ('barred %(barred)d' % bar if bar['magic'] == MAGIC['bar'] else 'not set since power-on'))
    oc = words(b, 0x98 if single else 0xA0, ('magic', 'live'))
    out.append('oc_guard   ' + ('live %d (%s layout)' % (oc['live'], 'single-core' if single else 'dual-core')
                                if oc['magic'] == MAGIC['oc'] else 'no record (before 1.3.0?)'))
    return '\n'.join(out)


if __name__ == '__main__':
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    for path in sys.argv[1:]:
        print('==', path)
        with open(path, 'rb') as f:
            print(decode(f.read()))
