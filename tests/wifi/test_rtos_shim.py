#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""RTOS shim: verifies all required Linux compat symbols are defined."""
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
RTOS_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_rtos.c'

REQUIRED_SYMBOLS = [
    'alloc_skb', 'kfree_skb', 'skb_clone', 'skb_push', 'skb_trim',
    'spin_lock_irqsave', 'spin_unlock_irqrestore',
    'tasklet_init', 'tasklet_hi_schedule', 'tasklet_kill',
    'netif_carrier_on', 'netif_carrier_off', 'netif_rx',
    'vmalloc', 'kfree',
]


def test_rtos_shim_defines_all_symbols():
    if not RTOS_C.exists() or RTOS_C.stat().st_size < 100:
        pytest.skip('wifi_rtos.c not yet implemented')
    text = RTOS_C.read_text()
    missing = [s for s in REQUIRED_SYMBOLS if s not in text]
    assert not missing, f'Missing symbols: {missing}'


def test_skb_pool_uses_static_allocation():
    if not RTOS_C.exists() or RTOS_C.stat().st_size < 100:
        pytest.skip('wifi_rtos.c not yet implemented')
    text = RTOS_C.read_text()
    assert 'skb_pool' in text, 'sk_buff must use a static pool'


def test_skb_has_put_and_reserve():
    if not RTOS_C.exists() or RTOS_C.stat().st_size < 100:
        pytest.skip('wifi_rtos.c not yet implemented')
    text = RTOS_C.read_text()
    assert 'skb_put' in text
    assert 'skb_reserve' in text
