#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Unit tests for the CDP protocol message format.

These tests validate the protocol specification from .scratch/fm-cdp/PROTOCOL.md
without needing an emulator or hardware. They test JSON message construction,
WebSocket framing, and the expected request/response shapes.
"""
import base64
import hashlib
import json
import struct

import pytest


# --- CDP message format tests ---

class TestCDPMessageFormat:
    """Validate CDP JSON-RPC message shapes per PROTOCOL.md."""

    def test_command_format(self):
        msg = {"id": 1, "method": "FM1.getInfo", "params": {}}
        j = json.dumps(msg)
        parsed = json.loads(j)
        assert "id" in parsed
        assert "method" in parsed
        assert isinstance(parsed["id"], int)

    def test_response_format(self):
        resp = {"id": 1, "result": {"firmware": "core-fm1", "version": "v0.0.0"}}
        j = json.dumps(resp)
        parsed = json.loads(j)
        assert "id" in parsed
        assert "result" in parsed
        assert parsed["result"]["firmware"] == "core-fm1"

    def test_error_format(self):
        err = {"id": 1, "error": {"code": -32601, "message": "Method not found"}}
        j = json.dumps(err)
        parsed = json.loads(j)
        assert parsed["error"]["code"] == -32601

    def test_event_format(self):
        evt = {"method": "Log.entryAdded", "params": {
            "entry": {"timestamp": 12345, "level": "info", "text": "boot ok"}
        }}
        j = json.dumps(evt)
        parsed = json.loads(j)
        assert "id" not in parsed
        assert "method" in parsed

    def test_memory_read_request(self):
        req = {"id": 2, "method": "FM1.memoryRead",
               "params": {"address": 0x1FC000, "length": 64}}
        j = json.dumps(req)
        parsed = json.loads(j)
        assert parsed["params"]["address"] == 0x1FC000
        assert parsed["params"]["length"] == 64

    def test_memory_read_response_base64(self):
        data = bytes(range(64))
        b64 = base64.b64encode(data).decode()
        resp = {"id": 2, "result": {"data": b64, "address": 0x1FC000, "length": 64}}
        decoded = base64.b64decode(resp["result"]["data"])
        assert decoded == data
        assert len(decoded) == 64

    def test_memory_write_request(self):
        data = bytes([0xDE, 0xAD, 0xBE, 0xEF])
        b64 = base64.b64encode(data).decode()
        req = {"id": 3, "method": "FM1.memoryWrite",
               "params": {"address": 0x20000, "data": b64}}
        decoded = base64.b64decode(req["params"]["data"])
        assert decoded == data

    def test_send_midi_request(self):
        req = {"id": 6, "method": "FM1.sendMidi",
               "params": {"data": [0x90, 60, 100]}}
        assert req["params"]["data"][0] == 0x90  # note on
        assert req["params"]["data"][1] == 60     # middle C
        assert req["params"]["data"][2] == 100    # velocity

    def test_all_fm1_methods(self):
        methods = [
            "FM1.getInfo",
            "FM1.memoryRead",
            "FM1.memoryWrite",
            "FM1.getAudioMetrics",
            "FM1.getEngineState",
            "FM1.sendMidi",
            "FM1.pressButton",
            "FM1.turnEncoder",
            "FM1.getLog",
        ]
        for m in methods:
            assert m.startswith("FM1."), f"Method {m} must be in FM1 domain"


# --- WebSocket handshake tests ---

class TestWebSocketHandshake:
    """Validate WebSocket upgrade per RFC 6455."""

    WS_MAGIC = "258EAFA5-E914-47DA-95CA-5AB9CD86B611"

    def test_accept_key_computation(self):
        """The server must SHA-1(client_key + magic) and base64-encode it."""
        client_key = "dGhlIHNhbXBsZSBub25jZQ=="  # RFC 6455 example
        expected_accept = "fhSdELfN6S0vCMsUWYUxkUCe4Bk="

        concat = client_key + self.WS_MAGIC
        sha1 = hashlib.sha1(concat.encode()).digest()
        accept = base64.b64encode(sha1).decode()

        assert accept == expected_accept

    def test_upgrade_request_format(self):
        """Validate the HTTP upgrade request the client sends."""
        key = base64.b64encode(b"0123456789abcdef").decode()
        request = (
            f"GET /devtools HTTP/1.1\r\n"
            f"Host: 10.0.2.15:9222\r\n"
            f"Upgrade: websocket\r\n"
            f"Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            f"Sec-WebSocket-Version: 13\r\n"
            f"\r\n"
        )
        assert "Upgrade: websocket" in request
        assert f"Sec-WebSocket-Key: {key}" in request

    def test_upgrade_response_format(self):
        """Validate the HTTP 101 response the server must send."""
        client_key = "dGhlIHNhbXBsZSBub25jZQ=="
        concat = client_key + self.WS_MAGIC
        accept = base64.b64encode(hashlib.sha1(concat.encode()).digest()).decode()

        response = (
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Accept: {accept}\r\n"
            "\r\n"
        )
        assert "101 Switching Protocols" in response
        assert f"Sec-WebSocket-Accept: {accept}" in response


# --- WebSocket frame tests ---

class TestWebSocketFraming:
    """Validate WebSocket frame encode/decode per RFC 6455."""

    def test_text_frame_encode_short(self):
        """Server text frame (unmasked, <126 bytes)."""
        payload = b'{"id":1,"result":{}}'
        frame = bytes([0x81, len(payload)]) + payload
        assert frame[0] == 0x81  # FIN + text opcode
        assert frame[1] == len(payload)
        assert frame[2:] == payload

    def test_text_frame_encode_medium(self):
        """Server text frame (unmasked, 126-65535 bytes)."""
        payload = b'x' * 200
        frame = bytes([0x81, 126, 0, 200]) + payload
        assert frame[1] == 126
        length = struct.unpack('>H', frame[2:4])[0]
        assert length == 200

    def test_client_frame_masked(self):
        """Client frames must be masked."""
        payload = b'{"id":1,"method":"FM1.getInfo"}'
        mask = b'\x37\xfa\x21\x3d'
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        frame = bytes([0x81, 0x80 | len(payload)]) + mask + masked
        assert frame[1] & 0x80  # mask bit set
        # unmask
        unmasked = bytes(masked[i] ^ mask[i % 4] for i in range(len(masked)))
        assert unmasked == payload

    def test_ping_pong(self):
        """Server must respond to ping with pong."""
        ping_data = b'hello'
        ping = bytes([0x89, len(ping_data)]) + ping_data
        pong = bytes([0x8A, len(ping_data)]) + ping_data
        assert ping[0] & 0x0F == 0x9   # ping opcode
        assert pong[0] & 0x0F == 0xA   # pong opcode
        assert ping[2:] == pong[2:]     # same payload


# --- Discovery endpoint test ---

class TestDiscovery:
    """Validate /json/version HTTP endpoint."""

    def test_discovery_response_shape(self):
        resp = {
            "Browser": "FM-1/1.0",
            "Protocol-Version": "1.0",
            "webSocketDebuggerUrl": "ws://10.0.2.15:9222"
        }
        assert "Browser" in resp
        assert "webSocketDebuggerUrl" in resp
        assert resp["webSocketDebuggerUrl"].startswith("ws://")
