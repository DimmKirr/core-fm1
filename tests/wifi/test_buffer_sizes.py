#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify WiFi stack buffers are static (not stack-local) for scheduler safety.

The cooperative scheduler gives WiFi a 4KB task stack.  These four buffers
total ~6KB and would overflow it instantly.  Moving them to static (.bss)
is safe because only the WiFi task accesses them and cooperative scheduling
means only one task runs at a time.
"""
import re
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
DATA_C = ROOT / "firmware" / "src" / "wifi" / "wifi_data.c"
LWIP_C = ROOT / "firmware" / "src" / "wifi" / "wifi_lwip.c"


# ---- helpers ----

def _read(path: Path) -> str:
    if not path.exists() or path.stat().st_size < 200:
        pytest.skip(f"{path.name} not yet implemented")
    return path.read_text()


def _extract_function(source: str, name: str) -> str:
    """Return the body of a static C function by name (greedy brace match)."""
    pattern = rf"static\s+\S+\s+{re.escape(name)}\s*\([^)]*\)\s*\{{"
    m = re.search(pattern, source)
    if not m:
        pytest.fail(f"Function {name} not found in source")
    start = m.end() - 1  # opening brace
    depth = 0
    for i, ch in enumerate(source[start:], start=start):
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return source[start : i + 1]
    pytest.fail(f"Could not find end of function {name}")


# ---- wifi_data.c: no stack-local frame buffers ----

class TestWifiDataTxNoStackBuffer:
    def test_no_stack_frame_array(self):
        body = _extract_function(_read(DATA_C), "wifi_data_tx")
        assert "uint8_t frame[" not in body, \
            "wifi_data_tx still has a stack-local frame[] buffer"

    def test_uses_static_pointer(self):
        body = _extract_function(_read(DATA_C), "wifi_data_tx")
        assert "wifi_tx_frame" in body, \
            "wifi_data_tx should reference the static wifi_tx_frame buffer"


class TestWifiDataRxNoStackBuffers:
    def test_no_stack_buf_array(self):
        body = _extract_function(_read(DATA_C), "wifi_data_rx")
        assert "uint8_t buf[WIFI_DATA_MAX" not in body, \
            "wifi_data_rx still has a stack-local buf[] buffer"

    def test_no_stack_eth_array(self):
        body = _extract_function(_read(DATA_C), "wifi_data_rx")
        assert "uint8_t eth[WIFI_ETH_HDR" not in body, \
            "wifi_data_rx still has a stack-local eth[] buffer"

    def test_uses_static_rx_frame(self):
        body = _extract_function(_read(DATA_C), "wifi_data_rx")
        assert "wifi_rx_frame" in body, \
            "wifi_data_rx should reference the static wifi_rx_frame buffer"

    def test_uses_static_rx_eth(self):
        body = _extract_function(_read(DATA_C), "wifi_data_rx")
        assert "wifi_rx_eth" in body, \
            "wifi_data_rx should reference the static wifi_rx_eth buffer"


# ---- wifi_lwip.c: no stack-local buffer ----

class TestWifiLwipLinkoutputNoStackBuffer:
    def test_no_stack_buf_array(self):
        body = _extract_function(_read(LWIP_C), "wifi_lwip_linkoutput")
        assert "uint8_t buf[WIFI_LWIP_MTU" not in body, \
            "wifi_lwip_linkoutput still has a stack-local buf[] buffer"

    def test_uses_static_tx_eth(self):
        body = _extract_function(_read(LWIP_C), "wifi_lwip_linkoutput")
        assert "wifi_tx_eth" in body, \
            "wifi_lwip_linkoutput should reference the static wifi_tx_eth buffer"


# ---- static buffer declarations exist at file scope ----

class TestStaticBufferDeclarations:
    def test_wifi_tx_frame_static(self):
        src = _read(DATA_C)
        assert "static uint8_t wifi_tx_frame" in src, \
            "Missing static wifi_tx_frame declaration in wifi_data.c"

    def test_wifi_rx_frame_static(self):
        src = _read(DATA_C)
        assert "static uint8_t wifi_rx_frame" in src, \
            "Missing static wifi_rx_frame declaration in wifi_data.c"

    def test_wifi_rx_eth_static(self):
        src = _read(DATA_C)
        assert "static uint8_t wifi_rx_eth" in src, \
            "Missing static wifi_rx_eth declaration in wifi_data.c"

    def test_wifi_tx_eth_static(self):
        src = _read(LWIP_C)
        assert "static uint8_t wifi_tx_eth" in src, \
            "Missing static wifi_tx_eth declaration in wifi_lwip.c"
