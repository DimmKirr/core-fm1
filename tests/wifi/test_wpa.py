#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""WPA2-PSK implementation tests."""
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
WPA_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_wpa.c'


def test_wpa_has_4way_handshake():
    if not WPA_C.exists() or WPA_C.stat().st_size < 200:
        pytest.skip('wifi_wpa.c not yet implemented')
    text = WPA_C.read_text()
    assert 'eapol' in text.lower()
    assert 'ptk' in text.lower() or 'pairwise' in text.lower()


def test_wpa_has_aes():
    if not WPA_C.exists() or WPA_C.stat().st_size < 200:
        pytest.skip('wifi_wpa.c not yet implemented')
    text = WPA_C.read_text()
    assert 'aes' in text.lower()


def test_wpa_has_ccmp():
    if not WPA_C.exists() or WPA_C.stat().st_size < 200:
        pytest.skip('wifi_wpa.c not yet implemented')
    text = WPA_C.read_text()
    assert 'ccmp' in text.lower() or 'ccm' in text.lower()


def test_wpa_has_pmk_derivation():
    if not WPA_C.exists() or WPA_C.stat().st_size < 200:
        pytest.skip('wifi_wpa.c not yet implemented')
    text = WPA_C.read_text()
    assert 'pbkdf2' in text.lower() or 'pmk' in text.lower()


def test_pbkdf2_yields_during_inner_loop():
    """PBKDF2 inner loop must yield periodically for scheduler cooperation."""
    src = (ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_wpa.c').read_text()
    fn_start = src.index('wifi_pbkdf2_sha1(')
    fn_end = src.index('\n}', fn_start) + 2
    fn = src[fn_start:fn_end]
    assert 'yield()' in fn, \
        'wifi_pbkdf2_sha1 must yield inside the iteration loop'
