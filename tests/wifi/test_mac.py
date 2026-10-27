#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""802.11 MAC frame handling tests."""
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
MAC_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_mac.c'


def test_mac_has_frame_types():
    if not MAC_C.exists() or MAC_C.stat().st_size < 200:
        pytest.skip('wifi_mac.c not yet implemented')
    text = MAC_C.read_text().lower()
    frame_types = ['beacon', 'probe', 'auth', 'assoc', 'data']
    found = [f for f in frame_types if f in text]
    assert len(found) >= 4, f'Missing frame types, found only: {found}'


def test_mac_has_tx_rx():
    if not MAC_C.exists() or MAC_C.stat().st_size < 200:
        pytest.skip('wifi_mac.c not yet implemented')
    text = MAC_C.read_text()
    assert 'wifi_mac_tx' in text or 'mac_tx' in text
    assert 'wifi_mac_rx' in text or 'mac_rx' in text


def test_mac_has_beacon_parser():
    if not MAC_C.exists() or MAC_C.stat().st_size < 200:
        pytest.skip('wifi_mac.c not yet implemented')
    text = MAC_C.read_text()
    assert 'parse_beacon' in text.lower()


def test_mac_has_bss_table():
    if not MAC_C.exists() or MAC_C.stat().st_size < 200:
        pytest.skip('wifi_mac.c not yet implemented')
    text = MAC_C.read_text()
    assert 'wifi_bss' in text
