#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""CONFIG_WIFI_DEBUG_AUTOSCAN: scan without the panel so a power-cycle alone
produces the scan diagnostics for `task debug:dump` / `task debug:follow`."""
import re
from pathlib import Path

from kconfiglib import Kconfig, BOOL, expr_str

ROOT = Path(__file__).resolve().parents[2]
API_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_api.c'
DEV_DEFCONFIG = ROOT / 'configs' / 'core_fm1_dev_defconfig'


def test_kconfig_symbol_is_debug_only():
    kconf = Kconfig(str(ROOT / 'Kconfig'), warn=False)
    sym = kconf.syms['WIFI_DEBUG_AUTOSCAN']
    assert sym.type == BOOL
    deps = expr_str(sym.direct_dep)
    assert 'WIFI' in deps and 'DEBUG_LOG' in deps, f'must depend on WIFI and DEBUG_LOG, got {deps}'


def test_autoscan_runs_from_wifi_poll_when_idle():
    src = API_C.read_text()
    assert '#ifdef CONFIG_WIFI_DEBUG_AUTOSCAN' in src
    body = re.search(r'static void wifi_autoscan_poll\(void\)\s*\{(.*?)\n\}', src, re.DOTALL)
    assert body, 'wifi_autoscan_poll() missing'
    assert 'WIFI_STATE_IDLE' in body.group(1), 'must only start a scan from IDLE'
    assert 'wifi_scan(' in body.group(1)
    poll = re.search(r'static void wifi_poll\(void\)\s*\{(.*?)\n\}', src, re.DOTALL).group(1)
    assert 'wifi_autoscan_poll()' in poll, 'must run from wifi_poll (both scheduler and main-loop builds)'


def test_dev_defconfig_lists_autoscan():
    """Present in the dev profile so it can be toggled; off by default."""
    text = DEV_DEFCONFIG.read_text()
    assert 'CONFIG_WIFI_DEBUG_AUTOSCAN' in text
    assert 'CONFIG_WIFI_DEBUG_AUTOSCAN=y' not in text, 'auto-scan is a bring-up aid, keep it off by default'
