#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""STA state machine tests."""
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
STA_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_sta.c'


def test_sta_states_defined():
    if not STA_C.exists() or STA_C.stat().st_size < 200:
        pytest.skip('wifi_sta.c not yet implemented')
    text = STA_C.read_text()
    states = ['IDLE', 'SCANNING', 'AUTH', 'ASSOC', 'CONNECTED']
    found = [s for s in states if s in text]
    assert len(found) >= 4, f'Missing STA states, found: {found}'


def test_sta_has_scan_connect():
    if not STA_C.exists() or STA_C.stat().st_size < 200:
        pytest.skip('wifi_sta.c not yet implemented')
    text = STA_C.read_text()
    assert 'scan' in text.lower()
    assert 'connect' in text.lower()
    assert 'disconnect' in text.lower()


def test_sta_has_auth_assoc_handlers():
    if not STA_C.exists() or STA_C.stat().st_size < 200:
        pytest.skip('wifi_sta.c not yet implemented')
    text = STA_C.read_text()
    assert 'auth_rsp' in text.lower() or 'on_auth' in text.lower()
    assert 'assoc_rsp' in text.lower() or 'on_assoc' in text.lower()
