#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Iteration 12o: the first real reception (12n) stalled the CPU until the watchdog reset it.
The vendor's receive contract (SPECSHEET §7.11.6 steps 1-7, §7.11.9 [M:s]): the interrupt
handler masks the source (WF_CON4 0x14050 bit 0) and wakes the receive task; the receive
path drains WDMA and re-opens the mask when its buffer is re-posted. Our handler never masked
and our drain loop was unbounded."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SDIO_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_sdio.c'


def _fn_body(src, name):
    m = re.search(r'static\s+[\w ]+\s+' + name + r'\s*\([^;{]*?\)\s*\{(.*?)\n\}', src, re.DOTALL)
    assert m, f'{name}() not found'
    return m.group(1)


def test_isr_masks_the_source_before_flagging():
    src = SDIO_C.read_text()
    m = re.search(r'void fm1_wifi_irq\(void\)\s*\{(.*?)\n\}', src, re.DOTALL)
    assert m, 'fm1_wifi_irq() not found'
    body = m.group(1)
    assert re.search(r'WIFI_SOC_IRQ_EN\s*&=\s*~1u', body), 'mask WF_CON4 (0x14050) bit 0 in the handler, as the vendor does'
    assert 'WIFI_MAC_IRQ_EN' not in body, 'the vendor handler touches no MAC register'
    assert body.index('&= ~1u') < body.index('wifi_irq_pending = 1'), 'mask first, then flag'


def test_receive_drain_is_bounded_and_reopens_the_mask():
    body = _fn_body(SDIO_C.read_text(), 'wifi_sdio_poll_irq')
    assert 'while (ctl & WDMA_CTL_RX_AVAIL)' not in body, 'no unbounded drain'
    assert re.search(r'for \(n = 0; n < 8u && \(ctl & WDMA_CTL_RX_AVAIL\); n\+\+\)', body), 'at most 8 frames per poll'
    tail = body[body.rindex('WDMA_CTL_RX_AVAIL'):]
    assert re.search(r'WIFI_SOC_IRQ_EN\s*\|=\s*1u', tail), 're-open the interrupt mask after draining'
    assert '"wifi: rx first"' in body, 'log the first received frame'
