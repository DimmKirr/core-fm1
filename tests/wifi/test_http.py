#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""HTTP client protocol format tests.

Tests the protocol logic (request building, status parsing, header/body
separation, Content-Length parsing) by reimplementing the key helpers in
Python to validate the C algorithms.
"""
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
HTTP_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_http.c'


# ---- Python reimplementations of the C helpers -------------------------


def http_parse_status(data: bytes) -> int:
    """Parse HTTP status code from status line, matching the C impl."""
    i = 0
    while i < len(data) and data[i:i+1] != b' ':
        i += 1
    if i >= len(data):
        return -1
    i += 1  # skip space
    if i + 3 > len(data):
        return -1
    return (data[i] - ord('0')) * 100 + (data[i+1] - ord('0')) * 10 + (data[i+2] - ord('0'))


def http_find_body(data: bytes) -> int:
    """Find \\r\\n\\r\\n separator, return offset of body start (0 = not found)."""
    if len(data) < 4:
        return 0
    for i in range(len(data) - 3):
        if data[i:i+4] == b'\r\n\r\n':
            return i + 4
    return 0


def http_parse_content_length(hdr: bytes) -> tuple[int | None, bool]:
    """Parse Content-Length from header bytes (case-insensitive).
    Returns (value, found)."""
    needle = b'content-length:'
    lower = hdr.lower()
    idx = lower.find(needle)
    if idx < 0:
        return (0, False)
    j = idx + len(needle)
    while j < len(hdr) and hdr[j:j+1] == b' ':
        j += 1
    val = 0
    while j < len(hdr) and ord('0') <= hdr[j] <= ord('9'):
        val = val * 10 + (hdr[j] - ord('0'))
        j += 1
    return (val, True)


def build_get_request(host: str, path: str) -> str:
    """Build the GET request string matching the C implementation."""
    return f'GET {path} HTTP/1.1\r\nHost: {host}\r\nConnection: close\r\n\r\n'


# ---- Tests: request building -------------------------------------------


class TestRequestBuilding:
    def test_basic_get(self):
        req = build_get_request('example.com', '/data')
        assert req.startswith('GET /data HTTP/1.1\r\n')
        assert 'Host: example.com\r\n' in req
        assert 'Connection: close\r\n' in req
        assert req.endswith('\r\n\r\n')

    def test_path_with_query(self):
        req = build_get_request('api.open-meteo.com',
                                '/v1/forecast?latitude=52.52&longitude=13.41')
        assert 'GET /v1/forecast?latitude=52.52&longitude=13.41 HTTP/1.1' in req
        assert 'Host: api.open-meteo.com' in req

    def test_root_path(self):
        req = build_get_request('ip-api.com', '/')
        assert 'GET / HTTP/1.1' in req


# ---- Tests: status parsing ---------------------------------------------


class TestStatusParsing:
    def test_200_ok(self):
        assert http_parse_status(b'HTTP/1.1 200 OK') == 200

    def test_404_not_found(self):
        assert http_parse_status(b'HTTP/1.1 404 Not Found') == 404

    def test_301_redirect(self):
        assert http_parse_status(b'HTTP/1.1 301 Moved Permanently') == 301

    def test_500_server_error(self):
        assert http_parse_status(b'HTTP/1.0 500 Internal Server Error') == 500

    def test_truncated(self):
        assert http_parse_status(b'HTTP') == -1

    def test_no_space(self):
        assert http_parse_status(b'INVALID') == -1

    def test_short_after_space(self):
        assert http_parse_status(b'HTTP/1.1 20') == -1


# ---- Tests: header/body separation ------------------------------------


class TestHeaderBodySeparation:
    def test_simple_response(self):
        data = b'HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello'
        off = http_find_body(data)
        assert off > 0
        assert data[off:] == b'hello'

    def test_no_body_separator(self):
        data = b'HTTP/1.1 200 OK\r\nContent-Length: 5\r\n'
        assert http_find_body(data) == 0

    def test_empty_body(self):
        data = b'HTTP/1.1 200 OK\r\n\r\n'
        off = http_find_body(data)
        assert off == len(data)

    def test_too_short(self):
        assert http_find_body(b'abc') == 0

    def test_multiple_headers(self):
        data = (b'HTTP/1.1 200 OK\r\n'
                b'Content-Type: application/json\r\n'
                b'Content-Length: 2\r\n'
                b'\r\n'
                b'{}')
        off = http_find_body(data)
        assert data[off:] == b'{}'


# ---- Tests: Content-Length parsing -------------------------------------


class TestContentLength:
    def test_standard(self):
        hdr = b'HTTP/1.1 200 OK\r\nContent-Length: 42\r\n\r\n'
        val, found = http_parse_content_length(hdr)
        assert found is True
        assert val == 42

    def test_no_space_after_colon(self):
        hdr = b'Content-Length:100\r\n\r\n'
        val, found = http_parse_content_length(hdr)
        assert found is True
        assert val == 100

    def test_missing(self):
        hdr = b'HTTP/1.1 200 OK\r\nServer: nginx\r\n\r\n'
        val, found = http_parse_content_length(hdr)
        assert found is False

    def test_case_insensitive(self):
        hdr = b'content-length: 256\r\n\r\n'
        val, found = http_parse_content_length(hdr)
        assert found is True
        assert val == 256

    def test_zero(self):
        hdr = b'Content-Length: 0\r\n\r\n'
        val, found = http_parse_content_length(hdr)
        assert found is True
        assert val == 0


# ---- Tests: source file existence --------------------------------------


class TestSourceFile:
    def test_http_file_exists(self):
        assert HTTP_C.exists(), 'wifi_http.c not found'

    def test_has_state_machine(self):
        text = HTTP_C.read_text()
        for state in ['HTTP_IDLE', 'HTTP_DNS_WAIT', 'HTTP_CONNECTING',
                       'HTTP_RECV_HEADERS', 'HTTP_RECV_BODY']:
            assert state in text, f'Missing state: {state}'

    def test_has_public_api(self):
        text = HTTP_C.read_text()
        for fn in ['http_get', 'http_busy', 'http_poll']:
            assert fn in text, f'Missing function: {fn}'

    def test_has_stubs(self):
        text = HTTP_C.read_text()
        assert '!CONFIG_WIFI_LWIP' in text, 'Missing stub guard'
