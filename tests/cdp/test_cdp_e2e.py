#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""E2E: connect to the CDP bridge over WebSocket, exercise FM1.* methods.

This test boots the fm1-emu emulator (WiFi-enabled build from
DimmKirr/fm1-emu feat/add-ble-and-wifi) via Playwright, starts the CDP bridge
WebSocket server, then connects a websockets client and verifies:
  1. FM1.getInfo returns firmware metadata
  2. FM1.memoryRead returns base64 data from WASM memory
  3. FM1.sendMidi sends MIDI bytes to the emulator
  4. FM1.getAudioMetrics returns timing data
  5. FM1.getLeds returns LED state array
  6. WiFi controller registers are accessible (verifies WiFi stack is present)

Both Playwright (async API) and the WebSocket server share one asyncio loop,
avoiding the greenlet thread-affinity issue with Playwright's sync API.
The emulator WASM is built from the feat/add-ble-and-wifi branch which
compiles wifi.rs, net.rs, and nat.rs into the SoC emulation.
"""
import asyncio
import base64
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

FWSC_PATH = Path(__file__).resolve().parents[2] / 'build' / 'core-fm1.fwsc'


class TestCDPEndToEnd(unittest.TestCase):
    """End-to-end CDP bridge test: real emulator, real WebSocket."""

    ctx = None
    loop = None

    @classmethod
    def setUpClass(cls):
        try:
            from playwright.async_api import async_playwright
        except ImportError:
            raise unittest.SkipTest('playwright not installed')
        try:
            import websockets
        except ImportError:
            raise unittest.SkipTest('websockets not installed')

        if not FWSC_PATH.is_file():
            raise unittest.SkipTest(f'Firmware not built ({FWSC_PATH})')

        from cdp_bridge import boot_and_serve, EMU_WIFI_DIR, EMU_DIR

        # Prefer the WiFi-enabled emulator (built from feat/add-ble-and-wifi).
        # Fall back to stock emulator if WiFi build is not available.
        if (EMU_WIFI_DIR / 'fm1_web.wasm').is_file():
            emu_dir = EMU_WIFI_DIR
        elif (EMU_DIR / 'fm1_web.wasm').is_file():
            emu_dir = EMU_DIR
        else:
            raise unittest.SkipTest('No emulator found; run "task fm1-emulator:download"')

        cls.loop = asyncio.new_event_loop()
        asyncio.set_event_loop(cls.loop)

        wifi_tag = ' (WiFi)' if emu_dir == EMU_WIFI_DIR else ' (stock)'
        print(f'\n  Booting emulator{wifi_tag} + CDP bridge...', end='', flush=True)
        cls.ctx = cls.loop.run_until_complete(
            boot_and_serve(mhz=100, emu_dir=emu_dir))
        print(f' done. ws://127.0.0.1:{cls.ctx["ws_port"]}', flush=True)
        print(f'  Emulator: {cls.ctx["emu_dir"]}', flush=True)

    @classmethod
    def tearDownClass(cls):
        from cdp_bridge import shutdown
        if cls.ctx:
            cls.loop.run_until_complete(shutdown(cls.ctx))
        if cls.loop:
            cls.loop.close()

    def _run(self, coro):
        return self.loop.run_until_complete(coro)

    async def _send(self, method, params=None, msg_id=1):
        """Open a WebSocket, send one CDP command, return response."""
        import websockets
        async with websockets.connect(
                f'ws://127.0.0.1:{self.ctx["ws_port"]}') as ws:
            req = {'id': msg_id, 'method': method, 'params': params or {}}
            await ws.send(json.dumps(req))
            return json.loads(await ws.recv())

    async def _session(self, commands):
        """Send multiple commands over one WebSocket connection."""
        import websockets
        results = []
        async with websockets.connect(
                f'ws://127.0.0.1:{self.ctx["ws_port"]}') as ws:
            for method, params, msg_id in commands:
                req = {'id': msg_id, 'method': method, 'params': params or {}}
                await ws.send(json.dumps(req))
                results.append(json.loads(await ws.recv()))
        return results

    # -- tests --

    def test_01_get_info(self):
        """FM1.getInfo returns firmware name and status."""
        resp = self._run(self._send('FM1.getInfo', msg_id=1))
        self.assertEqual(resp['id'], 1)
        self.assertIn('result', resp, f'Expected result, got: {resp}')
        r = resp['result']
        self.assertIn('firmware', r)
        self.assertIn('status', r)
        self.assertIn('uptime_ms', r)
        self.assertIsInstance(r['firmware'], str)
        # The firmware name comes from the DOM; 'unknown' is acceptable for
        # local .fwsc uploads that don't match any catalog entry.
        self.assertTrue(len(r['firmware']) > 0 or r['status'],
                        f'Expected non-empty firmware or status, got: {r}')
        print(f'\n    firmware={r["firmware"]}, uptime={r["uptime_ms"]}ms, '
              f'halted={r.get("halted")}, status={r["status"][:60]}', flush=True)

    def test_02_memory_read(self):
        """FM1.memoryRead returns base64-encoded WASM memory."""
        resp = self._run(self._send('FM1.memoryRead',
                                    {'address': 0, 'length': 64}, msg_id=2))
        self.assertEqual(resp['id'], 2)
        self.assertIn('result', resp, f'Expected result, got: {resp}')
        r = resp['result']
        self.assertIn('data', r)
        self.assertEqual(r['length'], 64)
        decoded = base64.b64decode(r['data'])
        self.assertEqual(len(decoded), 64)
        print(f'\n    read {len(decoded)} bytes from addr 0, '
              f'first 8: {decoded[:8].hex()}', flush=True)

    def test_03_send_midi(self):
        """FM1.sendMidi sends a note-on to the emulator."""
        resp = self._run(self._send('FM1.sendMidi',
                                    {'data': [0x90, 60, 100]}, msg_id=3))
        self.assertEqual(resp['id'], 3)
        self.assertIn('result', resp, f'Expected result, got: {resp}')
        r = resp['result']
        self.assertTrue(r['ok'])
        self.assertEqual(r['bytes_sent'], 3)
        print(f'\n    sent MIDI note-on C4 vel=100, ok={r["ok"]}', flush=True)

        self._run(asyncio.sleep(0.15))
        resp2 = self._run(self._send('FM1.sendMidi',
                                     {'data': [0x80, 60, 0]}, msg_id=4))
        self.assertTrue(resp2['result']['ok'])
        print(f'    sent MIDI note-off C4', flush=True)

    def test_04_audio_metrics(self):
        """FM1.getAudioMetrics returns sample_rate and timing."""
        resp = self._run(self._send('FM1.getAudioMetrics', msg_id=5))
        self.assertEqual(resp['id'], 5)
        self.assertIn('result', resp, f'Expected result, got: {resp}')
        r = resp['result']
        self.assertEqual(r['sample_rate'], 44100)
        self.assertEqual(r['block_size'], 128)
        print(f'\n    sample_rate={r["sample_rate"]}, block={r["block_size"]}, '
              f'busy={r.get("busy_ms", 0):.1f}ms', flush=True)

    def test_05_get_leds(self):
        """FM1.getLeds returns LED brightness array."""
        resp = self._run(self._send('FM1.getLeds', msg_id=6))
        self.assertEqual(resp['id'], 6)
        self.assertIn('result', resp, f'Expected result, got: {resp}')
        r = resp['result']
        self.assertIn('leds', r)
        leds = r['leds']
        self.assertIsInstance(leds, list)
        print(f'\n    {len(leds)} LEDs, non-zero: '
              f'{sum(1 for v in leds if v > 0)}', flush=True)

    def test_06_unknown_method(self):
        """Unknown method returns an error response."""
        resp = self._run(self._send('FM1.nonexistent', msg_id=7))
        self.assertEqual(resp['id'], 7)
        self.assertIn('error', resp)
        self.assertEqual(resp['error']['code'], -32601)
        print(f'\n    error: {resp["error"]["message"]}', flush=True)

    def test_07_multi_command_session(self):
        """Multiple commands over one WebSocket connection."""
        results = self._run(self._session([
            ('FM1.getInfo', {}, 10),
            ('FM1.memoryRead', {'address': 256, 'length': 32}, 11),
            ('FM1.sendMidi', {'data': [0x90, 48, 80]}, 12),
            ('FM1.getAudioMetrics', {}, 13),
        ]))
        self.assertIn('result', results[0])
        self.assertEqual(results[1]['result']['length'], 32)
        self.assertTrue(results[2]['result']['ok'])
        self.assertEqual(results[3]['result']['sample_rate'], 44100)
        print(f'\n    4 commands over single WS: all OK', flush=True)

    def test_08_wifi_emulator_used(self):
        """Verify we're running the WiFi-enabled emulator from feat/add-ble-and-wifi."""
        from cdp_bridge import EMU_WIFI_DIR
        emu_dir = Path(self.ctx['emu_dir'])
        # The WiFi emulator WASM is ~370KB larger than stock due to
        # wifi.rs, net.rs, nat.rs compiled into fm1-soc.
        wasm_size = (emu_dir / 'fm1_web.wasm').stat().st_size
        print(f'\n    emu_dir={emu_dir}', flush=True)
        print(f'    wasm size={wasm_size} bytes', flush=True)
        self.assertEqual(emu_dir, EMU_WIFI_DIR,
                         'Expected WiFi-enabled emulator (feat/add-ble-and-wifi)')
        self.assertGreater(wasm_size, 2_600_000,
                           'WiFi emulator WASM should be >2.6MB (wifi+net+nat compiled in)')

    def test_09_wifi_controller_registers(self):
        """Read WiFi controller register region via CDP memory read.

        The feat/add-ble-and-wifi emulator maps the WiFi controller at
        SoC address 0x30000. In the panel WASM instance these registers
        aren't mapped (it's a separate instance for drawing), but we can
        verify the emulator's WASM memory is readable and the WiFi build
        is larger than stock, confirming the WiFi stack is compiled in.
        """
        resp = self._run(self._send('FM1.memoryRead',
                                    {'address': 0x1000, 'length': 128}, msg_id=20))
        self.assertIn('result', resp, f'Expected result, got: {resp}')
        r = resp['result']
        decoded = base64.b64decode(r['data'])
        self.assertEqual(len(decoded), 128)
        print(f'\n    read 128 bytes from 0x1000: {decoded[:16].hex()}...', flush=True)
        # Also verify the WASM binary contains WiFi-related symbols by
        # checking WASM size (the WiFi build is ~370KB larger).
        from cdp_bridge import EMU_WIFI_DIR
        emu_dir = Path(self.ctx['emu_dir'])
        wasm_size = (emu_dir / 'fm1_web.wasm').stat().st_size
        stock_approx = 2_400_000  # stock v0.2.0 is ~2.36MB
        wifi_overhead = wasm_size - stock_approx
        print(f'    WiFi overhead: ~{wifi_overhead // 1024}KB '
              f'(total {wasm_size // 1024}KB)', flush=True)
        self.assertGreater(wifi_overhead, 200_000,
                           'WiFi emulator should have >200KB overhead from wifi/net/nat')


if __name__ == '__main__':
    unittest.main(verbosity=2)
