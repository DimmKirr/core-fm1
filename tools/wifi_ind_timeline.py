#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Decode the iteration-12i running-indicator rows from a DAP log (task debug:ind).

Rows (firmware/src/wifi/wifi_sdio.c wifi_ind_log):
    A tag:<8 hex WL_CTL0><8 hex WL_ID><8 hex WL_CLK4><8 hex CFG58>
    B tag:<8 hex CFG60><8 hex CFG90><8 hex WDMA_CTL><8 hex MAC_SYS_CTRL>
Prints one line per sample with the five indicator bits (vendor target in the header)
and the raw words, in log order. Usage: wifi_ind_timeline.py <follow.log|dump.log|wl0.log>
"""
import re
import sys

ROW = re.compile(r'\b([AB]) ([A-Za-z0-9]{1,4}):([0-9A-Fa-f]{32})\b')
VENDOR = {'wl0': 0x0, 'id29': 1, 'clk4_23': 1, 'c58': 0x009F827D, 'c60_23': 1, 'c90': 0x23CC, 'wd16': 1}


def parse(text):
    """Return samples in order: list of (tag, dict of words)."""
    samples, pending = [], {}
    for m in ROW.finditer(text):
        row, tag, hx = m.group(1), m.group(2), m.group(3)
        words = [int(hx[i:i + 8], 16) for i in range(0, 32, 8)]
        if row == 'A':
            pending[tag] = dict(zip(('wl0', 'id', 'clk4', 'c58'), words))
        else:
            d = pending.pop(tag, {})
            d.update(zip(('c60', 'c90', 'wdma', 'macsys'), words))
            samples.append((tag, d))
    return samples


def bit(v, n):
    return (v >> n) & 1


def table(samples):
    out = ['{:<5} {:>8} {:>4} {:>7} {:>8} {:>6} {:>6} {:>4} {:>8} {:>6}'.format(
        'tag', 'WL_CTL0', 'ID29', 'CLK4.23', 'CFG58', 'C60.23', 'CFG90', 'WD16', 'WDMA_CTL', 'MACSYS'),
        '{:<5} {:>8} {:>4} {:>7} {:>8} {:>6} {:>6} {:>4} {:>8} {:>6}'.format(
        'vendr', '0', '1', '1', '9F827D', '1', '23CC', '1', '10037', 'C')]
    for tag, d in samples:
        if 'id' not in d or 'c60' not in d:
            out.append(f'{tag:<5} (incomplete row pair)')
            continue
        out.append('{:<5} {:>8X} {:>4} {:>7} {:>8X} {:>6} {:>6X} {:>4} {:>8X} {:>6X}'.format(
            tag, d['wl0'], bit(d['id'], 29), bit(d['clk4'], 23), d['c58'], bit(d['c60'], 23),
            d['c90'], bit(d['wdma'], 16), d['wdma'], d['macsys']))
    return '\n'.join(out)


if __name__ == '__main__':
    text = open(sys.argv[1], errors='replace').read()
    s = parse(text)
    print(table(s))
    print(f'\n{len(s)} samples')
