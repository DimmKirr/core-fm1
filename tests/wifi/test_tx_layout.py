#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Source-level checks for the WDMA TX image and readiness gate (SPECSHEET §7.11.5,
revised 2026-11-04 [M:s]):

    staging: [u32 MPDU+22 (not transferred)][u32 queue word][TXWI 16][frame, unpadded][2 zero][4 zero]
    transfer: WDMA_TX_ADR = &queue word, WDMA_TX_LEN = MPDU + 26
    queue word: bit 0 WIV, bits [2:1] QSEL = 2 -> 5 for management, 4 for data

The emulator has no WiFi MAC, so these pin the arithmetic in wifi_sdio_tx() and the TXWI
word1 choices that mirror the vendor (software sequence numbers, block-ack window 7).
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SDIO_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_sdio.c'


def _fn_body(src, name):
    m = re.search(r'static\s+\w+\s+' + name + r'\s*\(.*?\)\s*\{(.*?)\n\}',
                  src, re.DOTALL)
    assert m, f'{name}() not found'
    return m.group(1)


def test_tx_image_starts_at_the_queue_word_with_mpdu_plus_26():
    body = _fn_body(SDIO_C.read_text(), 'wifi_sdio_tx')
    assert re.search(r'&wifi_tx_buf\[0\]\s*=\s*len\s*\+\s*22u', body), 'software length word = MPDU + 22 (not transferred)'
    assert re.search(r'&wifi_tx_buf\[4\]\s*=\s*\(\(frame\[0\] & 0x0Cu\) == 0u\) \? 5u : 4u', body), \
        'queue word: 5 (WIV, QSEL 2) for management, 4 for data'
    assert re.search(r'WDMA_TX_ADR\s*=\s*\(uint32_t\)&wifi_tx_buf\[4\]', body), 'DMA source is the queue word'
    assert re.search(r'WDMA_TX_LEN\s*=\s*len\s*\+\s*26u', body), 'transfer length = MPDU + 26'
    assert not re.search(r'\bpadded\s*=', body), 'no padding arithmetic: the frame goes out unpadded'
    assert re.search(r'for \(; i < len \+ 6u; i\+\+\)', body), 'six trailing bytes (2 + 4) follow the unpadded frame'


def test_tx_readiness_gate_is_read_and_logged_before_the_transfer():
    src = SDIO_C.read_text()
    for name, off in (('WIFI_TXQ_STA', '0x0434u'), ('WIFI_PBF_DBG', '0x043Cu'), ('WIFI_PBF_MAX_PCNT', '0x040Cu')):
        assert re.search(rf'#define\s+{name}\s+\(\*\(volatile uint32_t \*\)\(WIFI_MAC_BASE \+ {off}\)\)', src), name
    body = _fn_body(src, 'wifi_sdio_tx')
    gate = body[body.index('TX readiness gate'):body.index('wifi_sdio_claim()')]
    for reg in ('WIFI_TXRXQ_PCNT', 'WIFI_TXQ_STA', 'WIFI_PBF_DBG', 'WIFI_PBF_MAX_PCNT'):
        assert reg in gate, f'{reg} must be read before the transfer'
    assert '(1u << 18)' in gate and '0xE0u' in gate, 'bit 18 of 0x434 and [7:5] of 0x43C decide readiness'
    assert 'tx_gate' in gate and 'tx_gate_dbg' in gate, 'result kept for the STA log'
    assert 'return -1' not in gate, 'the gate is diagnostic for now: log, then transmit anyway'


def test_tx_buffer_size_covers_headers_and_trailer():
    src = SDIO_C.read_text()
    m = re.search(r'#define\s+WIFI_TX_HDR\s+(\d+)u', src)
    assert m and int(m.group(1)) == 8, 'length word and queue word precede the TXWI'
    m = re.search(r'#define\s+WIFI_TX_BUF_SIZE\s+\(?([^\n]+?)\)?\s*(?:/\*.*)?$', src, re.MULTILINE)
    assert m, 'WIFI_TX_BUF_SIZE not found'
    expr = m.group(1).replace('WIFI_TX_HDR', '8').replace('WIFI_TX_MAX_FRAME', '1600').replace('u', '')
    assert eval(expr) >= 8 + 16 + 1600 + 6, f'buffer too small: {expr}'


def test_txwi_mirrors_vendor_word1():
    body = _fn_body(SDIO_C.read_text(), 'wifi_build_txwi')
    assert 'TXWI_W1_NSEQ' not in body.split('word1')[1].split(';')[0], \
        'vendor uses software sequence numbers: NSEQ must be clear'
    assert 'TXWI_W1_BW_WIN_SHIFT' in body and '7u << TXWI_W1_BW_WIN_SHIFT' in body
    assert 'TXWI_W0_DENSITY_SHIFT' in body and '5u << TXWI_W0_DENSITY_SHIFT' in body
