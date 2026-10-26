#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Cross-test: RF init register sequence.

Verifies that our GPL wifi_rf.c references the critical register addresses
from the decompiled IR and that the function structure matches expectations.
"""
import json
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
IR_DIR = ROOT / '.scratch' / 'examples' / 'wifi' / 'ir' / 'wl_rf_common'
RF_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_rf.c'


def test_ir_files_available():
    assert IR_DIR.exists(), 'Decompiled IR not available'
    assert (IR_DIR / 'wf_rf_init.c.ll').exists()


def test_rf_init_function_count():
    idx_path = ROOT / '.scratch' / 'examples' / 'wifi' / 'function_index.json'
    if not idx_path.exists():
        pytest.skip('function_index.json not available')
    idx = json.loads(idx_path.read_text())
    rf_init_funcs = [f for f in idx['functions']
                     if f['kind'] == 'define'
                     and f.get('object', '') == 'wf_rf_init.c']
    assert len(rf_init_funcs) >= 15, f'Expected ~21 RF init functions, got {len(rf_init_funcs)}'


def test_rf_c_references_critical_registers():
    if not RF_C.exists() or RF_C.stat().st_size < 100:
        pytest.skip('wifi_rf.c not yet populated')
    text = RF_C.read_text()
    assert '0x11900' in text or 'FM1_WF_CON0' in text, 'Missing WL_CON0'
    assert '0x11934' in text or 'FM1_WF_VCO_EN' in text, 'Missing VCO_EN'
    assert '0x11938' in text or 'FM1_WF_VCO_BAND' in text, 'Missing VCO_BAND'
    assert '0x11968' in text or 'FM1_WF_CAL_START' in text, 'Missing CAL_START'


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
