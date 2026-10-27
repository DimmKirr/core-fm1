#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""RTOS shim: verifies all required symbols are defined (project-original names)."""
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
RTOS_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_rtos.c'

REQUIRED_SYMBOLS = [
    'wifi_buf_alloc', 'wifi_buf_free', 'wifi_buf_clone',
    'wifi_buf_push', 'wifi_buf_trim',
    'wifi_lock_irq', 'wifi_unlock_irq',
    'wifi_defer_init', 'wifi_defer_schedule', 'wifi_defer_cancel',
    'wifi_link_up', 'wifi_link_down', 'wifi_rx_deliver',
    'wifi_heap_alloc', 'wifi_heap_free',
]


def test_rtos_shim_defines_all_symbols():
    if not RTOS_C.exists() or RTOS_C.stat().st_size < 100:
        pytest.skip('wifi_rtos.c not yet implemented')
    text = RTOS_C.read_text()
    missing = [s for s in REQUIRED_SYMBOLS if s not in text]
    assert not missing, f'Missing symbols: {missing}'


def test_buf_pool_uses_static_allocation():
    if not RTOS_C.exists() or RTOS_C.stat().st_size < 100:
        pytest.skip('wifi_rtos.c not yet implemented')
    text = RTOS_C.read_text()
    assert 'wifi_buf_pool' in text, 'wifi_buf must use a static pool'


def test_buf_has_put_and_reserve():
    if not RTOS_C.exists() or RTOS_C.stat().st_size < 100:
        pytest.skip('wifi_rtos.c not yet implemented')
    text = RTOS_C.read_text()
    assert 'wifi_buf_put' in text
    assert 'wifi_buf_reserve' in text


def test_no_linux_kernel_names():
    """Cleanroom check: no Linux kernel API names should appear."""
    if not RTOS_C.exists() or RTOS_C.stat().st_size < 100:
        pytest.skip('wifi_rtos.c not yet implemented')
    text = RTOS_C.read_text()
    forbidden = [
        'sk_buff', 'alloc_skb', 'kfree_skb', 'skb_push', 'skb_pull',
        'skb_put', 'skb_trim', 'skb_reserve', 'skb_clone', 'skb_copy',
        'spinlock_t', 'spin_lock_irqsave', 'spin_unlock_irqrestore',
        'tasklet_struct', 'tasklet_init', 'tasklet_hi_schedule',
        'tasklet_kill', 'tasklet_fn_t',
        'netif_carrier_on', 'netif_carrier_off', 'netif_carrier_ok',
        'netif_start_queue', 'netif_stop_queue', 'netif_wake_queue',
        'netif_rx',
    ]
    found = [s for s in forbidden if s in text]
    assert not found, f'Linux kernel names in cleanroom code: {found}'
