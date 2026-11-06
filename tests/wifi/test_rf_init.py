#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Cross-test: RF init register sequence.

Verifies that our GPL wifi_rf.c references the critical register addresses
and that the function structure matches expectations.
"""
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
RF_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_rf.c'


def test_rf_c_references_critical_registers():
    if not RF_C.exists() or RF_C.stat().st_size < 100:
        pytest.skip('wifi_rf.c not yet populated')
    text = RF_C.read_text()
    assert '0x11900' in text or 'FM1_WL_A00' in text, 'Missing WL_A00'
    assert '0x11934' in text or 'FM1_WL_VCO_EN' in text, 'Missing VCO_EN'
    assert '0x11938' in text or 'FM1_WL_VCO_BAND' in text, 'Missing VCO_BAND'
    assert '0x11968' in text or 'FM1_WL_CAL_START' in text, 'Missing CAL_START'


def test_rf_c_has_init_entry():
    if not RF_C.exists() or RF_C.stat().st_size < 100:
        pytest.skip('wifi_rf.c not yet populated')
    text = RF_C.read_text()
    assert 'wifi_rf_init' in text


def test_rf_c_has_vco_scan():
    if not RF_C.exists() or RF_C.stat().st_size < 100:
        pytest.skip('wifi_rf.c not yet populated')
    text = RF_C.read_text()
    assert 'vco_scan' in text.lower()


def test_rf_c_has_channel_set():
    if not RF_C.exists() or RF_C.stat().st_size < 100:
        pytest.skip('wifi_rf.c not yet populated')
    text = RF_C.read_text()
    assert 'wifi_set_channel' in text or 'set_channel' in text
