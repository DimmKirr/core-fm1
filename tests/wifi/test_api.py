#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""WiFi public API tests."""
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
API_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_api.c'


def test_api_surface():
    if not API_C.exists() or API_C.stat().st_size < 100:
        pytest.skip('wifi_api.c not yet implemented')
    text = API_C.read_text()
    required = ['wifi_init', 'wifi_scan', 'wifi_connect', 'wifi_disconnect',
                'wifi_get_state', 'wifi_get_ip']
    missing = [f for f in required if f not in text]
    assert not missing, f'Missing API functions: {missing}'


def test_api_has_poll():
    if not API_C.exists() or API_C.stat().st_size < 100:
        pytest.skip('wifi_api.c not yet implemented')
    text = API_C.read_text()
    assert 'wifi_poll' in text


def test_wifi_connect_derives_pmk_before_auth():
    """wifi_connect must derive PMK before starting authentication."""
    src = (ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_api.c').read_text()
    fn_start = src.index('wifi_connect(')
    fn_end = src.index('\n}', fn_start) + 2
    fn = src[fn_start:fn_end]

    pmk_pos = max(
        fn.find('wifi_wpa_init('),
        fn.find('wifi_wpa_derive_pmk('),
    )
    auth_pos = fn.find('wifi_sta_start_connect(')
    assert pmk_pos >= 0, 'wifi_connect must call wpa_init or wpa_derive_pmk'
    assert auth_pos >= 0, 'wifi_connect must call sta_start_connect'
    assert pmk_pos < auth_pos, \
        'PMK derivation must happen BEFORE start_connect (auth)'


def test_wifi_cmd_queue_exists():
    """WiFi module must define a command queue for cross-task communication."""
    cmd_path = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_cmd.c'
    assert cmd_path.exists(), 'wifi_cmd.c must exist'
    src = cmd_path.read_text()
    assert 'wifi_cmd_t' in src
    assert 'WIFI_CMD_CONNECT' in src
    assert 'wifi_cmd_enqueue' in src
    assert 'wifi_cmd_poll' in src


def test_wifi_task_entry_exists():
    """wifi_api.c must define wifi_task() as the scheduler entry point."""
    src = (ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_api.c').read_text()
    assert 'wifi_task(void)' in src
