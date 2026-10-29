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


def test_sdio_tx_no_delay_in_critical_section():
    """IRQ-disable regions in wifi_sdio_tx must not contain busy-wait loops.

    The audio DMA deadline is 2.9ms.  If fm1_delay_us() is called inside
    a wifi_sdio_claim()/wifi_sdio_release() region that also contains a
    for or while loop, the total IRQ-off time can exceed that deadline and
    cause audio underruns.  Descriptor-programming critical sections must
    be short register writes only.
    """
    import re

    if not SDIO_C.exists() or SDIO_C.stat().st_size < 100:
        pytest.skip('wifi_sdio.c not yet implemented')
    src = SDIO_C.read_text()

    # Extract the wifi_sdio_tx function body
    fn_match = re.search(
        r'static\s+int\s+wifi_sdio_tx\s*\(.*?\)\s*\{(.*?)\n\}',
        src, re.DOTALL)
    assert fn_match, 'wifi_sdio_tx() not found'
    body = fn_match.group(1)

    # Find all claim/release regions within the function body.
    # A "region" is text between wifi_sdio_claim() and wifi_sdio_release().
    regions = re.findall(
        r'wifi_sdio_claim\s*\(\s*\)(.*?)wifi_sdio_release\s*\(\s*\)',
        body, re.DOTALL)
    assert regions, 'No wifi_sdio_claim/release regions found in wifi_sdio_tx'

    for i, region in enumerate(regions):
        has_loop = bool(re.search(r'\b(for|while)\s*\(', region))
        has_delay = bool(re.search(r'fm1_delay_us\s*\(', region))
        # A delay inside a critical section is acceptable for a short fixed
        # wait (e.g. 2us DMA reset settle) as long as there is no loop
        # that could multiply it into milliseconds.
        assert not (has_loop and has_delay), (
            f'claim/release region {i} contains both a busy-wait loop '
            f'and fm1_delay_us -- this can exceed the 2.9ms audio deadline'
        )
