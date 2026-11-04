#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""CONFIG_WIFI_DEBUG_WLCTL0_PROBE (iteration 12i): write WL_CTL0 (0x14000) = 0
once after boot, with the running-indicator rows before and after, a one-shot
marker in NOINIT so a crash does not repeat, and a scan afterwards.
"""
import re
import sys
from pathlib import Path

from kconfiglib import Kconfig, BOOL, expr_str

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import fm1log  # noqa: E402

API_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_api.c'
SDIO_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_sdio.c'
DAP_C = ROOT / 'firmware' / 'src' / 'debug' / 'debug_dap.c'
DEBUG_H = ROOT / 'firmware' / 'adapters' / 'cfm1_debug.h'
DEV_DEFCONFIG = ROOT / 'configs' / 'core_fm1_dev_defconfig'
TASKFILE = ROOT / 'Taskfile.yml'


def _fn_body(src, name):
    m = re.search(r'static\s+\w+\s+' + name + r'\s*\(.*?\)\s*\{(.*?)\n\}', src, re.DOTALL)
    assert m, f'{name}() not found'
    return m.group(1)


def test_kconfig_symbol_needs_wifi_and_dap():
    kconf = Kconfig(str(ROOT / 'Kconfig'), warn=False)
    sym = kconf.syms['WIFI_DEBUG_WLCTL0_PROBE']
    assert sym.type == BOOL
    deps = expr_str(sym.direct_dep)
    assert 'WIFI' in deps and 'DEBUG_DAP' in deps, deps


def test_probe_runs_from_wifi_poll_only_when_the_host_asks():
    """No timer: a fault halts the unit on the FAULT screen (fm1_fault feeds the
    watchdog forever), so the probe must never re-run on its own after a power-cycle."""
    src = API_C.read_text()
    assert '#ifdef CONFIG_WIFI_DEBUG_WLCTL0_PROBE' in src
    poll = _fn_body(src, 'wifi_poll')
    assert 'wifi_wlctl0_probe_poll()' in poll
    body = _fn_body(src, 'wifi_wlctl0_probe_poll')
    assert 'wifi_wl0_probe_req' in body and 'wifi_sta_now_ms' not in body
    req = _fn_body(src, 'wifi_wl0_probe_request')
    assert 'wifi_wl0_probe_req = 1' in req


def test_dap_command_requests_the_probe():
    src = DAP_C.read_text()
    assert re.search(r'#define\s+DBG_D_WL0\s+0x29u', src)
    assert 'case DBG_D_WL0' in src and 'wifi_wl0_probe_request(' in src
    assert '#ifdef CONFIG_WIFI_DEBUG_WLCTL0_PROBE' in src


def test_fm1log_wl0_sends_the_request_then_follows():
    sent = []

    class Dev:
        def ask(self, cmd, payload=b''):
            sent.append((cmd, payload))
            return b'\x00'

    assert fm1log.wl0_request(Dev()) is True
    assert sent == [(0x29, b'\x00\x00\x00\x00')]
    assert fm1log.build_parser().parse_args(['--wl0']).wl0 is True
    assert not hasattr(fm1log, 'wl0_early_cycle'), '12p: the early-release mode is gone with the boot-time assert'


def test_retry_mode_rewrites_the_refused_registers_on_the_released_block():
    """12k: the writes refused in 12e/12h were attempted on a held block. Mode 2 retries them
    after the release, reads each back, brackets with rows, then scans."""
    api = API_C.read_text()
    req = _fn_body(api, 'wifi_wl0_probe_request')
    assert 'wifi_wl0_retry_req = 1' in req
    body = _fn_body(api, 'wifi_wl0_retry_poll')
    seq = ['wifi_ind_log("pre2", 1)', '0x14050u', '(1u << 23)', '0x14060u', '0x14058u = 0x009F827Du',
           'WDMA_CTL = 0x00010037u', '"wifi: r2 sirq="', '"wifi: r2 cfg60="', '"wifi: r2 cfg58="',
           '"wifi: r2 wctl="', 'wifi_ind_log("pst2", 1)', 'wifi_scan(0)']
    idx = [body.index(x) for x in seq]
    assert idx == sorted(idx), 'retry steps out of order'
    poll = _fn_body(api, 'wifi_poll')
    assert 'wifi_wl0_retry_poll()' in poll


def test_fm1log_retry_flag_sends_mode_two():
    sent = []

    class Dev:
        def ask(self, cmd, payload=b''):
            sent.append((cmd, payload))
            return b'\x00'

    assert fm1log.wl0_request(Dev(), mode=2) is True
    assert sent == [(0x29, b'\x02\x00\x00\x00')]
    assert fm1log.build_parser().parse_args(['--wl0', '--retry']).retry is True


def test_no_early_release_left_in_firmware():
    sdio = SDIO_C.read_text()
    assert 'early release' not in sdio and 'probe & 4u' not in sdio, 'early-release block removed (12p)'
    api = API_C.read_text()
    assert 'wifi_wl0_reboot_req' not in api and 'probe |= 4u' not in api, 'mode 1 removed'
