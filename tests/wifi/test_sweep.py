#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Register sweep diff (plan test 2, task step 11): read whole blocks on both sides in the
same `0x%08x = 0x%08x` line format and diff them, to catch a register the 62-register
comparison never looked at."""
import re
import struct
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import fm1log  # noqa: E402
import hbdeploy  # noqa: E402

DAP_C = ROOT / 'firmware' / 'src' / 'debug' / 'debug_dap.c'
DEBUG_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_debug.c'
TASKFILE = ROOT / 'Taskfile.yml'
VENDOR_HTTP = ROOT / '.scratch' / 'examples' / 'fm1-base' / 'examples' / 'wifi' / 'debug_http.c'
WINDOWS = [(0x10000, 0x11000), (0x14000, 0x15000), (0x30000, 0x32000)]  # 0x40000 is the flash controller (SPECSHEET erratum 3): never swept


def _fn_body(src, name):
    m = re.search(r'static\s+\w+\s+' + name + r'\s*\(.*?\)\s*\{(.*?)\n\}', src, re.DOTALL)
    assert m, f'{name}() not found'
    return m.group(1)


def test_dap_dispatches_sweep_into_a_reply_that_fits_the_frame():
    src = DAP_C.read_text()
    assert re.search(r'#define\s+DBG_D_SWEEP\s+0x28u', src)
    assert 'case DBG_D_SWEEP' in src and 'wifi_sweep_fill(' in src
    size = int(re.search(r'static uint8_t sb\[(\d+)\];', src).group(1))
    assert size <= 256 and (size - 8) % 4 == 0, 'status+pad+base header, then whole words, within one DAP reply'


def test_sweep_fill_limits_windows_and_guards_the_gated_spaces():
    body = _fn_body(DEBUG_C.read_text(), 'wifi_sweep_fill')
    for lo, hi in WINDOWS:
        assert f'0x{lo:05X}u' in body and f'0x{hi:05X}u' in body, f'window {lo:#x}-{hi:#x} must be allowed'
    assert body.index('wifi_sdio_state.initialized') < body.index('0x30000u'), \
        'MAC space only after the clock tree is up (reads fault before it)'
    assert '0x40000u' not in body, 'the 0x40000 block is the flash controller, never swept'
    assert '(max - 8u) / 4u' in body, 'count clamped to what the reply buffer holds'


class SweepDevice:
    """Answers D_SWEEP like wifi_sweep_fill: [0] status, [4..7] base, [8..] words (addr ^ 0xA5A5A5A5)."""

    def __init__(self, max_words=60):
        self.max_words = max_words
        self.calls = []

    def ask(self, cmd, payload=b''):
        assert cmd == fm1log.D_SWEEP
        base, count = struct.unpack('<IH', payload)
        self.calls.append((base, count))
        if not any(lo <= base and base + 4 * count <= hi for lo, hi in WINDOWS):
            return b'\x0a'
        count = min(count, self.max_words)
        words = [((base + 4 * i) ^ 0xA5A5A5A5) & 0xFFFFFFFF for i in range(count)]
        return bytes(4) + struct.pack('<I', base) + struct.pack(f'<{count}I', *words)


def test_sweep_read_walks_the_block_in_reply_sized_chunks():
    dev = SweepDevice()
    regs = fm1log.sweep_read(dev, 0x30000, 130)
    assert [c[1] for c in dev.calls] == [60, 60, 10]
    assert len(regs) == 130 and regs[0x30000] == 0x30000 ^ 0xA5A5A5A5 and regs[0x30000 + 4 * 129] == (0x30000 + 516) ^ 0xA5A5A5A5


def test_sweep_read_refuses_outside_the_windows():
    with pytest.raises(RuntimeError):
        fm1log.sweep_read(SweepDevice(), 0x20000, 4)


def test_sweep_text_matches_the_vendor_line_format():
    text = fm1log.sweep_text({0x30F04: 0x37, 0x30000: 0xDEADBEEF})
    assert text == '0x00030000 = 0xdeadbeef\n0x00030f04 = 0x00000037\n'
    args = fm1log.build_parser().parse_args(['--sweep', '0x30000', '2048'])
    assert args.sweep == [0x30000, 2048]


def test_taskfile_sweeps_the_four_windows_into_one_file():
    text = TASKFILE.read_text()
    task = re.search(r'^  debug:sweep:(.*?)(?=^  \S)', text, re.M | re.S)
    assert task, 'debug:sweep task missing'
    body = task.group(1)
    for lo, hi in WINDOWS:
        assert f'0x{lo:x} {(hi - lo) // 4}' in body, f'window {lo:#x} with {(hi - lo) // 4} words'
    assert '.scratch/debug/sweep.log' in body


def test_vendor_endpoint_serves_the_same_format():
    if not VENDOR_HTTP.exists():
        pytest.skip('fm1-base research copy not present')
    src = VENDOR_HTTP.read_text()
    assert '/sweep?base=' in src and 'sweep_page' in src
    body = re.search(r'static void sweep_page\(.*?\)\s*\{(.*?)\n\}', src, re.DOTALL).group(1)
    assert 'P("0x"); P8(' in body and 'P(" = 0x"); P8(' in body, 'same addr = value lines as the MMIO section'
    assert '2048' in body, 'request size clamp'
