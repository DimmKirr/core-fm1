#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Iteration 12l: the vendor's per-channel front-end writes the GPL driver never did
(SPECSHEET §4.2.1 C2-C3, §14 rows 3-4 [M:s]): TX_BAND_CFG band select and TX_PIN_CFG
(LNA/PA/RF switch enables) after every channel switch; BBP R94 and R106 are not written
per channel (§6.8, erratum 5); PBF_MAX_PCNT takes the vendor table value (§4.2.1 B2)."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RF_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_rf.c'
MAC_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_mac.c'
STA_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_sta.c'
SDIO_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_sdio.c'


def _fn_body(src, name):
    m = re.search(r'static\s+\w+\s+' + name + r'\s*\(.*?\)\s*\{(.*?)\n\}', src, re.DOTALL)
    assert m, f'{name}() not found'
    return m.group(1)


def test_channel_switch_no_longer_writes_bbp_r94_or_r106():
    body = _fn_body(RF_C.read_text(), 'wifi_rf_set_channel')
    assert 'wifi_rf_bbp(0x5Eu' not in body, 'R94 (radio priority) is never written by vendor WiFi code'
    assert 'wifi_rf_bbp(0x6Au' not in body, 'R106 stays 4 from PHY init; the vendor never writes it per channel'


def test_front_end_written_after_every_channel_switch():
    src = SDIO_C.read_text()
    for name, off in (('WIFI_TX_PIN_CFG', '0x1328u'), ('WIFI_TX_BAND_CFG', '0x132Cu')):
        assert re.search(rf'#define\s+{name}\s+\(\*\(volatile uint32_t \*\)\(WIFI_MAC_BASE \+ {off}\)\)', src), name
    body = _fn_body(MAC_C.read_text(), 'wifi_mac_front_end')
    assert re.search(r'WIFI_TX_BAND_CFG\s*=\s*\(WIFI_TX_BAND_CFG & ~0x6u\) \| 0x4u', body), 'band select: bits [2:1] cleared, bit 2 set (2.4 GHz)'
    assert re.search(r'WIFI_TX_PIN_CFG\s*=\s*0x00050302u', body), 'PA_PE_G0, LNA_PE_A0/G0, RFTR, TRSW enables'
    assert body.index('WIFI_TX_BAND_CFG') < body.index('WIFI_TX_PIN_CFG'), 'band before pins, as the vendor'
    sta = STA_C.read_text()
    assert sta.count('wifi_rf_set_channel(') == sta.count('wifi_mac_front_end('), 'every channel switch is followed by the front-end writes'
    for m in re.finditer(r'wifi_rf_set_channel\([^;]*\);', sta):
        tail = sta[m.end():m.end() + 120]
        assert 'wifi_mac_front_end(' in tail, 'front-end writes directly after the channel switch'


def test_pbf_max_pcnt_takes_the_vendor_value():
    src = MAC_C.read_text()
    assert re.search(r'0x040Cu\)\s*=\s*0x1F3FBF9Fu', src), 'PBF_MAX_PCNT vendor value (queue-2 page limit 0xBF)'
    assert '0x1F3F7F9F' not in src


def test_scan_logs_front_end_and_gate_readbacks():
    sta = STA_C.read_text()
    for key in ('"wifi: txpin="', '"wifi: txband="', '"wifi: pbfmax="', '"wifi: txgate="'):
        assert key in sta, f'missing scan-time log key {key}'
