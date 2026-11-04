#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Source-level checks for the behavioural WiFi readbacks (plan Phase 1, Task 2-3).

The emulator has no WiFi MAC, so these verify the C sources carry the
registers and DAP log keys the vendor comparison depends on, that the
queue-count read happens while the frame can still be queued, and that
the scan-time log block no longer carries readbacks already proven equal
to the vendor (iteration 11).
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SDIO_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_sdio.c'
STA_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_sta.c'
LOG_H = ROOT / 'firmware' / 'adapters' / 'cfm1_log.h'

# RT2800 offsets from the public Linux header (rt2800.h, GPL-2.0+)
DIAG_REGS = {
    'WIFI_TXRXQ_PCNT': '0x0438u',
    'WIFI_CH_IDLE_STA': '0x1130u',
    'WIFI_CH_BUSY_STA': '0x1134u',
    'WIFI_CH_BUSY_SEC': '0x1138u',
    'WIFI_MAC_STATUS': '0x1200u',
    'WIFI_TX_STA_FIFO': '0x1718u',
}

NEW_KEYS = ['wifi: qpcnt=', 'wifi: mstat=', 'wifi: tsfifo=',
            'wifi: ch_idle=', 'wifi: ch_busy=', 'wifi: ch_bsec=']

# Verified equal to the vendor in iteration 11: no longer logged per scan.
PRUNED_KEYS = ['wifi: con0=', 'wifi: con1=', 'wifi: con3=', 'wifi: con5=',
               'wifi: con6=', 'wifi: pa0=', 'wifi: pa1=', 'wifi: pa2=',
               'wifi: xosc=', 'wifi: pll_f=', 'wifi: pll_c=', 'wifi: con20=',
               'wifi: ana1=', 'wifi: a64=', 'wifi: a5c=', 'wifi: a60=',
               'wifi: a10=', 'wifi: a20=', 'wifi: cca72=', 'wifi: cca79=',
               'wifi: mac_lo=', 'wifi: mac_hi=', 'wifi: lrate=',
               'wifi: mxlen=', 'wifi: brate=', 'wifi: wrst=', 'wifi: irq328=',
               'wifi: mac308=', 'wifi: mac30c=']


def _fn_body(src, name):
    m = re.search(r'static\s+\w+\s+' + name + r'\s*\(.*?\)\s*\{(.*?)\n\}',
                  src, re.DOTALL)
    assert m, f'{name}() not found'
    return m.group(1)


def test_sdio_defines_diag_registers():
    src = SDIO_C.read_text()
    for name, off in DIAG_REGS.items():
        pat = rf'#define\s+{name}\s+\(\*\(volatile uint32_t \*\)\(WIFI_MAC_BASE \+ {off}\)\)'
        assert re.search(pat, src), f'{name} must map to WIFI_MAC_BASE + {off}'


def test_behavioural_keys_logged():
    src = SDIO_C.read_text() + STA_C.read_text()
    for key in NEW_KEYS:
        assert f'"{key}"' in src, f'missing DAP log key {key}'


def test_qpcnt_read_before_tx_ack_and_outside_log():
    """TXRXQ_PCNT is sampled after DMA completion but before the TX ack,
    and logged only after the IRQ-off region (no logging while claimed)."""
    body = _fn_body(SDIO_C.read_text(), 'wifi_sdio_tx')
    region = re.search(r'wifi_sdio_claim\s*\(\s*\)(.*?)wifi_sdio_release\s*\(\s*\)',
                       body, re.DOTALL).group(1)
    assert 'WIFI_TXRXQ_PCNT' in region, 'queue count must be read inside the TX region'
    assert region.index('WIFI_TXRXQ_PCNT') < region.rindex('WDMA_CTL_TX_ACK'), \
        'queue count must be read before the final TX ack'
    assert 'cfm1_log' not in region, 'no logging inside the IRQ-off region'
    assert 'tx_qpcnt' in region, 'queue count must be kept in wifi_sdio_state for the STA layer to log'
    assert '"wifi: qpcnt="' in STA_C.read_text(), 'STA layer logs qpcnt next to the probe TX result'


def test_bbp_snapshot_present_and_rows_fit_log_line():
    src = SDIO_C.read_text()
    body = _fn_body(src, 'wifi_bbp_snapshot')
    for prefix in ('bbpR', 'bbpC', 'bbpD'):
        assert prefix in body, f'snapshot must emit {prefix} rows'
    # 7-char prefix + 16 bytes as 32 hex chars + NUL must fit the smallest line
    smallest = min(int(v) for v in re.findall(r'CFM1_LOG_LINE_LEN\s+(\d+)',
                                              LOG_H.read_text()))
    assert 7 + 32 + 1 <= smallest, 'hex row does not fit CFM1_LOG_LINE_LEN'
    assert 'wifi_bbp_snapshot()' in STA_C.read_text(), 'snapshot must be taken during scan'


def test_scan_block_pruned_of_verified_readbacks():
    src = STA_C.read_text()
    left = [k for k in PRUNED_KEYS if f'"{k}"' in src]
    assert not left, f'readbacks already verified equal to vendor still logged: {left}'
