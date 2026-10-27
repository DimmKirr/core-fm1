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
