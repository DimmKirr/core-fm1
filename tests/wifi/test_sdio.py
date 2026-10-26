#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""SDIO bus driver structure tests."""
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SDIO_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_sdio.c'


def test_sdio_has_cmd52_cmd53():
    if not SDIO_C.exists() or SDIO_C.stat().st_size < 100:
        pytest.skip('wifi_sdio.c not yet implemented')
    text = SDIO_C.read_text()
    has_cmd52 = 'cmd52' in text.lower() or 'io_rw_direct' in text.lower()
    has_cmd53 = 'cmd53' in text.lower() or 'io_rw_extended' in text.lower()
    assert has_cmd52, 'Missing CMD52 implementation'
    assert has_cmd53, 'Missing CMD53 implementation'


def test_sdio_has_dma_init():
    if not SDIO_C.exists() or SDIO_C.stat().st_size < 100:
        pytest.skip('wifi_sdio.c not yet implemented')
    text = SDIO_C.read_text()
    assert 'dma' in text.lower()


def test_sdio_has_init():
    if not SDIO_C.exists() or SDIO_C.stat().st_size < 100:
        pytest.skip('wifi_sdio.c not yet implemented')
    text = SDIO_C.read_text()
    assert 'wifi_sdio_init' in text
