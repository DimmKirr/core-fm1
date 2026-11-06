#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Iteration 12m: channel selection the vendor's way (SPECSHEET §4.2.1 C1, §4.2 0x1405C,
§5 FEQ bank patch, §5.2 bank-limit scan, §14 rows 5-7 [M:s 2026-11-04]).

- The only per-channel RF write is the channel code (channel + 101) into 0x1405C [7:0].
- FEQ LUT entries 0-127 carry the VCO bank for their divider in word 1 [15:9], from a 48-entry
  bank-limit table built by the scan of §5.2.
- LUT load and auto control run again after the MAC/PHY resets, so no reset follows them.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RF_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_rf.c'
API_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_api.c'
STA_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_sta.c'


def _fn_body(src, name):
    m = re.search(r'static\s+[\w ]+\s+' + name + r'\s*\([^;{]*?\)\s*\{(.*?)\n\}', src, re.DOTALL)  # skips prototypes
    assert m, f'{name}() not found'
    return m.group(1)


def test_bank_probe_is_one_pll_restart_and_one_comparator_read():
    body = _fn_body(RF_C.read_text(), 'wifi_rf_bank_probe')
    assert '(0x7Fu << 19)' in body and '(0xFFu << 5)' in body, 'bank in 0x11938 [25:19], divider in 0x1193C [12:5]'
    delays = re.findall(r'fm1_delay_us\((\d+)\)', body)
    assert delays == ['5', '5', '100'], f'PLL restart timing 5/5/100 us, got {delays}'
    assert 'for (i = 0; i < 8u; i++)' in body and '0x11978u) = 1' in body, 'eight strobes then 0'
    assert '(1u << 18)' in body and '(1u << 17)' in body, 'code 1 above (bit 18), code 2 below (bit 17)'
    assert 'while' not in body, 'no unbounded waits in boot-time RF code'


def test_bank_scan_follows_the_documented_procedure_and_is_bounded():
    src = RF_C.read_text()
    body = _fn_body(src, 'wifi_rf_bank_scan')
    assert '{7, 23, 39, 55, 71, 87, 103, 119}' in body, 'mid-bank candidates'
    assert '73u' in body and '171u' in body, 'divider 73 for the search, sweep ceiling 171'
    assert '(prev_hi - 3u + lo) / 2u' in body, 'limit[i] = (previous high - 3 + low) / 2'
    assert 'while' not in body and 'for (;;)' not in body, 'bounded loops only'
    assert body.count('(1u << 24)') >= 2, 'detector 0x11940 bit 24 on at the start, off at the end'
    assert re.search(r'static uint8_t wifi_rf_bank_limit\[48\]', src)
    lookup = _fn_body(src, 'wifi_rf_bank_for')
    assert 'wifi_rf_bank_limit[47]' in lookup and '47u' in lookup


def test_lut_load_patches_the_feq_bank_field():
    body = _fn_body(RF_C.read_text(), 'wifi_rf_lut_load')
    assert re.search(r'if \(i < 128u\)', body), 'FEQ entries 0-127'
    assert '(w1 >> 21) & 0xFFu' in body, 'divider from word 1 [28:21]'
    assert 'wifi_rf_bank_for(' in body and '<< 9' in body, 'bank OR-ed into word 1 [15:9]'


def test_rf_init_wifi_mode_then_calibrate_then_arm():
    """wifi_rf_wifi_mode() sets the 24 MHz constant block and runs the bank scan
    internally (§11.2.9); calibrate follows; arm is the re-armable tail."""
    rf = RF_C.read_text()
    init = _fn_body(rf, 'wifi_rf_init')
    assert 'wifi_rf_wifi_mode();' in init and 'wifi_rf_arm();' in init
    assert init.index('wifi_rf_wifi_mode();') < init.index('wifi_rf_calibrate();') < init.index('wifi_rf_arm();')
    wm = _fn_body(rf, 'wifi_rf_wifi_mode')
    assert 'wifi_rf_bank_scan();' in wm, 'bank scan runs inside wifi_mode after the PLL block'
    arm = _fn_body(rf, 'wifi_rf_arm')
    for step in ('wifi_rf_lut_load();', 'wifi_rf_auto_control();', 'wifi_cal_rf_mode(0);'):
        assert step in arm, f'{step} belongs to the re-armable tail'
    assert 'wifi_rf_lut_load();' not in init.replace('wifi_rf_arm();', ''), 'the tail lives only in wifi_rf_arm()'
    api = _fn_body(API_C.read_text(), 'wifi_init')
    assert api.index('wifi_mac_init();') < api.index('wifi_rf_arm();') < api.index('wifi_ind_log("mac", 1)'), \
        'LUT load and auto control run again after the last MAC/PHY reset (§14 row 7)'


def test_channel_switch_is_the_channel_code_only():
    body = _fn_body(RF_C.read_text(), 'wifi_rf_set_channel')
    assert re.search(r'0x1405Cu\)\s*=\s*\(v & ~0xFFu\) \| \(\(uint32_t\)channel \+ 101u\)', body), 'channel + 101 into WF_CON7 [7:0]'
    for gone in ('FM1_WL_VCO_FINE', 'FM1_WL_VCO_EN', 'wifi_rf_bbp('):
        assert gone not in body, f'{gone} is not part of the vendor channel step'


def test_scan_logs_rx_counters_and_pll_state_on_every_channel():
    sta = STA_C.read_text()
    for key in ('"wifi: rx0="', '"wifi: rx1="', '"wifi: pll="', '"wifi: fine="', '"wifi: wcon7="'):
        assert key in sta, f'missing per-channel key {key}'
    dwell = sta[sta.index('"wifi: scan rx="'):sta.index('"wifi: mac_rx0="')]
    assert '"wifi: rx0="' in dwell and '"wifi: pll="' in dwell, 'per-channel keys precede the channel 1-2 detail block'


def test_bank_scan_runs_with_the_control_loops_off_and_logs_raw_comparator_words():
    """The vendor scans with 0x11950 zeroed and before auto control.
    wifi_rf_wifi_mode() sets the 24 MHz constant block then runs the bank scan;
    wifi_rf_arm() re-arms auto control and PA trims afterwards."""
    src = RF_C.read_text()
    body = _fn_body(src, 'wifi_rf_bank_scan')
    first_probe = body.index('wifi_rf_bank_probe(')
    head = body[:first_probe]
    assert '0x11950u) = 0' in head, 'control word zeroed before the first measurement'
    assert '~((1u << 26) | (1u << 27))' in head, 'auto-control enables cleared before the first measurement'
    assert '"wifi: rf bank cmp73="' in body and '"wifi: rf bank cmp121="' in body, 'raw comparator words logged'
    wm = _fn_body(src, 'wifi_rf_wifi_mode')
    assert wm.index('0x11934u') < wm.index('wifi_rf_bank_scan();'), \
        '24 MHz PLL words applied before the bank scan in wifi_mode()'
    arm = _fn_body(src, 'wifi_rf_arm')
    assert 'wifi_rf_pa_trims();' in arm, 'PA trims applied in the arm tail'
    assert 'wifi_rf_auto_control();' in arm, 'auto control re-armed in the arm tail'
