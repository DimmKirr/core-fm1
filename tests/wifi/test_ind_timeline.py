#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Iteration 12i: the five read-only "MAC/BBP running" indicators are sampled at
every init step boundary so a transient assert (block started, then killed by a
later step) cannot hide. Source-level checks: the emulator has no WiFi block.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SDIO_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_sdio.c'
STA_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_sta.c'
API_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_api.c'
LOG_H = ROOT / 'firmware' / 'adapters' / 'cfm1_log.h'

SOC_WORDS = ['0x14000u', '0x1400Cu', '0x14050u', '0x14058u', '0x14060u', '0x14090u']


def _fn_body(src, name):
    m = re.search(r'static\s+\w+\s+' + name + r'\s*\(.*?\)\s*\{(.*?)\n\}', src, re.DOTALL)
    assert m, f'{name}() not found'
    return m.group(1)


def _ordered(text, *needles):
    idx = []
    for n in needles:
        assert n in text, f'missing: {n}'
        idx.append(text.index(n))
    assert idx == sorted(idx), f'out of order: {needles}'


def test_ind_log_reads_soc_words_and_mac_words_behind_guard():
    body = _fn_body(SDIO_C.read_text(), 'wifi_ind_log')
    for w in SOC_WORDS:
        assert w in body, f'{w} must be sampled'
    assert re.search(r'mac_ok\s*\?\s*WDMA_CTL\s*:\s*0u', body), 'WDMA_CTL only when the MAC space is readable'
    assert re.search(r'mac_ok\s*\?\s*WIFI_MAC_SYS_CTRL\s*:\s*0u', body), 'MAC_SYS_CTRL only when readable'


def test_ind_row_fits_the_smallest_log_line_without_spaces():
    """Row = 1 id + space + 4-char tag + ':' + 32 hex digits = 39 chars + NUL."""
    smallest = min(int(v) for v in re.findall(r'CFM1_LOG_LINE_LEN\s+(\d+)', LOG_H.read_text()))
    assert 1 + 1 + 4 + 1 + 32 + 1 <= smallest
    body = _fn_body(SDIO_C.read_text(), 'wifi_ind_row')
    assert 'cfm1_log_hex' not in body, 'cfm1_log_hex inserts spaces; rows must be packed'
    assert 'cfm1_log(line)' in body


def test_samples_bracket_every_init_step_in_order():
    src = SDIO_C.read_text()
    init = _fn_body(src, 'wifi_sdio_init')
    assert '0x14000u' not in init, 'the vendor never writes WL_CTL0 on a WiFi-only start (SPECSHEET §14 row 10, 12p)'
    _ordered(init,
             '0x10008u', 'wifi_ind_log("gate", 1)',
             'wifi_hw_reset();', 'wifi_ind_log("rst", 1)',
             'wdma_init();', 'wifi_ind_log("wdma", 1)',
             'wifi_phy_hw_init();', 'wifi_ind_log("phy", 1)',
             'wifi_cal_bbp_apply();', 'wifi_ind_log("cal", 1)',
             'WIFI_SOC_IRQ_EN |=', 'wifi_ind_log("irq", 1)')
    _ordered(_fn_body(src, 'wifi_hw_reset'),
             'MAC_RESET_CSR | MAC_RESET_BBP', 'wifi_ind_log("inrs", 1)', 'WIFI_MAC_SYS_CTRL = 0;')
    _ordered(_fn_body(src, 'wifi_phy_hw_init'),
             'wifi_phy_mac_rst();', 'wifi_ind_log("pmrs", 1)',
             'wifi_phy_rst();', 'wifi_ind_log("prst", 1)')
    api = API_C.read_text()
    _ordered(_fn_body(api, 'wifi_init'),
             'wifi_ind_arm();', 'wifi_rf_init();', 'wifi_ind_log("rf", 0)',
             'wifi_sdio_init();', 'wifi_mac_init();', 'wifi_ind_log("mac", 1)', 'wifi_ind_done();')
    assert 'wifi_ind_log("pre"' not in api, 'no SoC-block reads before the clock tree is up (unproven on hardware)'
    assert 'wifi_ind_log("scan", 1)' in STA_C.read_text()


def test_init_sampling_skipped_on_the_boot_after_a_crash():
    src = SDIO_C.read_text()
    arm = _fn_body(src, 'wifi_ind_arm')
    assert re.search(r'cfm1_dbg_nv\.probe\s*&\s*2u', arm) and 'wifi_ind_off = 1' in arm
    assert 'cfm1_dbg_nv.probe |= 2u' in arm
    assert 'cfm1_dbg_nv.probe &= ~2u' in _fn_body(src, 'wifi_ind_done')
    log = _fn_body(src, 'wifi_ind_log')
    assert log.index('wifi_ind_off') < log.index('0x14000u'), 'guard before the first read'


def test_tags_unique_and_at_most_four_chars():
    src = SDIO_C.read_text() + STA_C.read_text() + API_C.read_text()
    tags = re.findall(r'wifi_ind_log\("([^"]+)",\s*[01]\)', src)
    assert len(tags) >= 13, tags
    assert len(set(tags)) == len(tags), f'duplicate tags: {tags}'
    assert all(len(t) <= 4 for t in tags), tags


def test_experiment_flags_folded_into_permanent_init():
    src = SDIO_C.read_text()
    assert 'WIFI_EXPERIMENT_' not in src, 'experiment flags must be gone'
    for refused in ('0x1400Cu |= (1u << 29)', '0x14058u = 0x009F827Du', '0x14060u |= (1u << 23)',
                    '0x14008u = 0x0000FFFFu'):
        assert refused not in src, f'refused/hazard write still present: {refused}'
    init = _fn_body(src, 'wifi_sdio_init')
    assert '(1u << 10)' in init and '0x10010u' in init, 'SYS_CLK2 bit 10 select is permanent'
    assert '~(1u << 12)' in init and '0x10008u' in init, 'SYS_CLK1 bit 12 clear is permanent'
    assert 'WIFI_SOC_IRQ_EN |= 1u;' in init, 'bit 23 is read-only; only bit 0 is written'
    assert '(1u << 23)' not in init
