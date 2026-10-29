#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify SDIO timeout logging is rate-limited and DAP SysEx is suppressed.

These are source-level checks: they read the C files and verify the patterns
are present, since the emulator doesn't reproduce SDIO timeouts (no WiFi HW).
"""
import re
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SDIO_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_sdio.c'
USB_C = ROOT / 'firmware' / 'src' / 'midi' / 'usb.c'


def test_sdio_timeout_rate_limited():
    """TX timeout must increment error_count instead of logging on every failure."""
    src = SDIO_C.read_text()

    fn_match = re.search(
        r'static\s+int\s+wifi_sdio_tx\s*\(.*?\)\s*\{(.*?)\n\}',
        src, re.DOTALL)
    assert fn_match, 'wifi_sdio_tx() not found'
    body = fn_match.group(1)

    assert 'error_count' in body, \
        'wifi_sdio_tx() must track errors via error_count'


def test_dap_sysex_not_logged():
    """SX< log line must skip DAP protocol frames (7D 48 42)."""
    src = USB_C.read_text()

    sx_idx = src.index('SX< ')
    sx_log_region = src[max(0, sx_idx - 400):sx_idx + 200]

    assert '0x7Du' in sx_log_region or '0x7D' in sx_log_region, \
        'No DAP manufacturer ID check before SX< log line'
    assert '0x48' in sx_log_region, \
        'No 0x48 (H) check before SX< log line'
    assert '0x42' in sx_log_region, \
        'No 0x42 (B) check before SX< log line'
    assert 'CONFIG_DEBUG_DAP' in sx_log_region, \
        'DAP SysEx suppression must be under CONFIG_DEBUG_DAP guard'
