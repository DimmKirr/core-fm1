#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Unit tests for WiFi Kconfig integration."""
import subprocess
import sys
from pathlib import Path

import pytest
from kconfiglib import Kconfig, BOOL, expr_str

ROOT = Path(__file__).resolve().parents[1]
KCONFIG = ROOT / 'Kconfig'


@pytest.fixture(autouse=True)
def skip_if_no_kconfig():
    if not KCONFIG.exists():
        pytest.skip('Kconfig not yet created')


@pytest.fixture
def kconf():
    return Kconfig(str(KCONFIG), warn=False)


def test_wifi_kconfig_parses(kconf):
    w = kconf.syms['WIFI']
    assert w.type == BOOL, f'WIFI must be bool, got {w.type}'


def test_wifi_mutually_exclusive_with_ble(kconf):
    w = kconf.syms['WIFI']
    dep_str = expr_str(w.direct_dep)
    assert 'BLE' in dep_str, f'WIFI must depend on !BLE, got: {dep_str}'


def test_wifi_sub_options_depend_on_wifi(kconf):
    for name in ['WIFI_LWIP', 'WIFI_WPA', 'WIFI_MIDI']:
        s = kconf.syms[name]
        dep = expr_str(s.direct_dep)
        assert 'WIFI' in dep, f'{name} must depend on WIFI, got: {dep}'


def test_wifi_defconfig_generates_header():
    genconfig = ROOT / 'tools' / 'genconfig.py'
    if not genconfig.exists():
        pytest.skip('tools/genconfig.py not found')
    import tempfile
    with tempfile.TemporaryDirectory() as tmpdir:
        r = subprocess.run(
            [sys.executable, str(genconfig), '--defconfig', 'wifi', '--outdir', tmpdir],
            capture_output=True, text=True, cwd=str(ROOT))
        assert r.returncode == 0, r.stderr
        header = Path(tmpdir) / 'config.h'
        text = header.read_text()
        assert '#define CONFIG_WIFI 1' in text
        assert '#define CONFIG_WIFI_WPA 1' in text
        assert '#define CONFIG_WIFI_LWIP 1' in text
