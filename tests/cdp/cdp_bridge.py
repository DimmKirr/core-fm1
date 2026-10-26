#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""CDP bridge: WebSocket server backed by fm1-emu via Playwright (async).

Boots the WASM emulator, then exposes FM1.* CDP methods over WebSocket.
Each method calls page.evaluate() to read emulator state or send commands.
Both Playwright and the WebSocket server share one asyncio event loop.
"""
import asyncio
import base64
import json
import http.server
import os
import socket
import threading
from pathlib import Path

import websockets


ROOT = Path(__file__).resolve().parents[2]
EMU_DIR = Path(os.environ.get('FM1_WEB_DIR', Path.home() / '.fm1-emulator' / 'web'))
EMU_WIFI_DIR = Path(os.environ.get(
    'FM1_WIFI_WEB_DIR', Path.home() / '.fm1-emulator' / 'web-wifi'))
FWSC_PATH = ROOT / 'build' / 'core-fm1.fwsc'

if 'PLAYWRIGHT_BROWSERS_PATH' not in os.environ:
    _pw_cache = Path.home() / '.cache' / 'ms-playwright'
    if _pw_cache.is_dir():
        os.environ['PLAYWRIGHT_BROWSERS_PATH'] = str(_pw_cache)


def _make_handler(directory):
    class _H(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *a, **kw):
            super().__init__(*a, directory=str(directory), **kw)
        def end_headers(self):
            self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
            self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
            super().end_headers()
        def log_message(self, *a):
            pass
    return _H


def _free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


class CDPBridge:
    """Manages the emulator page and handles CDP method dispatch."""

    def __init__(self, page):
        self.page = page

    async def dispatch(self, method, params):
        if method == 'FM1.getInfo':
            return await self._get_info()
        elif method == 'FM1.memoryRead':
            return await self._mem_read(params)
        elif method == 'FM1.getAudioMetrics':
            return await self._audio_metrics()
        elif method == 'FM1.getEngineState':
            return await self._engine_state()
        elif method == 'FM1.sendMidi':
            return await self._send_midi(params)
        elif method == 'FM1.pressButton':
            return await self._press_button(params)
        elif method == 'FM1.turnEncoder':
            return await self._turn_encoder(params)
        elif method == 'FM1.getLeds':
            return await self._get_leds()
        elif method == 'Target.getTargetInfo':
            return {'targetInfo': {
                'targetId': 'fm1-0', 'type': 'page',
                'title': 'FM-1', 'url': 'fm1://localhost'}}
        else:
            raise ValueError(f'Unknown method: {method}')

    async def _get_info(self):
        return await self.page.evaluate('''() => {
            const s = document.getElementById('status')?.textContent || '';
            // fw is module-scoped; parse firmware name from the <select> or status
            const sel = document.getElementById('fw');
            const fwName = sel?.selectedOptions?.[0]?.textContent?.trim()
                || s.split(':')[0]?.trim() || 'unknown';
            return {
                firmware: fwName,
                status: s,
                uptime_ms: typeof frame !== 'undefined' && frame ? (frame.ms || 0) : 0,
                halted: typeof frame !== 'undefined' && frame ? !!frame.halted : false,
            };
        }''')

    async def _mem_read(self, params):
        addr = params.get('address', 0)
        length = min(params.get('length', 64), 4096)
        return await self.page.evaluate(f'''() => {{
            // _cdp_wasm[0] is the panel WASM instance captured by our init script
            const inst = window._cdp_wasm?.[0];
            if (!inst?.exports?.memory) return {{ data: "", address: {addr}, length: 0, error: "no wasm instance" }};
            const buf = inst.exports.memory.buffer;
            const maxAddr = buf.byteLength;
            const addr = {addr};
            const len = Math.min({length}, maxAddr - addr);
            if (addr < 0 || addr >= maxAddr) return {{ data: "", address: addr, length: 0 }};
            const bytes = new Uint8Array(buf, addr, len);
            let binary = '';
            for (let i = 0; i < bytes.length; i++) binary += String.fromCharCode(bytes[i]);
            return {{ data: btoa(binary), address: addr, length: len }};
        }}''')

    async def _audio_metrics(self):
        return await self.page.evaluate('''() => {
            if (typeof frame === 'undefined' || !frame) return {
                sample_rate: 44100, block_size: 128, busy_ms: 0, worst_ms: 0, quanta: 0
            };
            return {
                sample_rate: 44100,
                block_size: 128,
                busy_ms: frame.busy || 0,
                worst_ms: frame.worst || 0,
                quanta: frame.quanta || 0,
                short: frame.short || 0,
            };
        }''')

    async def _engine_state(self):
        return await self.page.evaluate('''() => ({
            engine_index: 0,
            halted: typeof frame !== 'undefined' && frame ? !!frame.halted : false,
            ms: typeof frame !== 'undefined' && frame ? (frame.ms || 0) : 0,
        })''')

    async def _send_midi(self, params):
        data = params.get('data', [])
        if not data or len(data) < 1:
            raise ValueError('data must be a non-empty array of MIDI bytes')
        data_json = json.dumps(data)
        await self.page.evaluate(f'''() => {{
            const n = typeof node !== 'undefined' ? node : null;
            if (n && n.port) n.port.postMessage({{type:"midi",bytes:{data_json}}});
        }}''')
        return {'ok': True, 'bytes_sent': len(data)}

    async def _press_button(self, params):
        key_id = params.get('id', 0)
        await self.page.evaluate(f'''() => {{
            const n = typeof node !== 'undefined' ? node : null;
            if (n && n.port) {{
                n.port.postMessage({{type:"key",id:{key_id},down:true}});
                setTimeout(() => n.port.postMessage({{type:"key",id:{key_id},down:false}}), 100);
            }}
        }}''')
        return {'ok': True, 'key': key_id}

    async def _turn_encoder(self, params):
        enc = params.get('encoder', 0)
        clicks = params.get('clicks', 1)
        await self.page.evaluate(f'''() => {{
            const n = typeof node !== 'undefined' ? node : null;
            if (n && n.port) n.port.postMessage({{type:"enc",enc:{enc},clicks:{clicks}}});
        }}''')
        return {'ok': True, 'encoder': enc, 'clicks': clicks}

    async def _get_leds(self):
        return await self.page.evaluate('''() => {
            if (typeof frame === 'undefined' || !frame || !frame.leds) return { leds: [] };
            return { leds: Array.from(frame.leds) };
        }''')


async def _handle_ws(bridge, ws):
    """Handle one WebSocket connection with CDP JSON-RPC."""
    async for raw in ws:
        try:
            msg = json.loads(raw)
        except json.JSONDecodeError:
            await ws.send(json.dumps({
                'error': {'code': -32700, 'message': 'Parse error'}}))
            continue

        msg_id = msg.get('id')
        method = msg.get('method', '')
        params = msg.get('params', {})

        try:
            result = await bridge.dispatch(method, params)
            resp = {'id': msg_id, 'result': result}
        except ValueError as e:
            resp = {'id': msg_id, 'error': {'code': -32601, 'message': str(e)}}
        except Exception as e:
            resp = {'id': msg_id, 'error': {'code': -32603, 'message': str(e)}}

        await ws.send(json.dumps(resp))


def _start_http_server(directory, port):
    """Start a COOP/COEP HTTP server in a daemon thread."""
    server = http.server.HTTPServer(('127.0.0.1', port), _make_handler(directory))
    t = threading.Thread(target=server.serve_forever, daemon=True)
    t.start()
    return server


async def boot_and_serve(fwsc_path=None, mhz=10, ws_port=0, chromium_path=None,
                         emu_dir=None):
    """Async entry point: boot emulator, start WS server, return context.

    Args:
        emu_dir: path to emulator web directory (defaults to EMU_DIR).
                 Set to EMU_WIFI_DIR to use the WiFi-enabled emulator.

    Returns a dict with keys: page, browser, pw, http_server, ws_server, ws_port, emu_dir.
    The caller's event loop must keep running for the WS server to serve.
    """
    import shutil
    from playwright.async_api import async_playwright

    fwsc = Path(fwsc_path) if fwsc_path else FWSC_PATH
    if emu_dir is None:
        emu_dir = EMU_DIR
    emu_dir = Path(emu_dir)
    if not fwsc.is_file():
        raise FileNotFoundError(f'Firmware not found: {fwsc}')
    if not (emu_dir / 'fm1_web.wasm').is_file():
        raise FileNotFoundError(f'Emulator not found in {emu_dir}')

    http_port = _free_port()
    http_server = _start_http_server(emu_dir, http_port)
    emu_url = f'http://127.0.0.1:{http_port}/?fw=local&mhz={mhz}&debug=1'

    pw = await async_playwright().start()
    launch_kw = dict(headless=True,
                     args=['--autoplay-policy=no-user-gesture-required'])
    chromium = chromium_path or os.environ.get('CHROMIUM_PATH') or shutil.which('chromium')
    if chromium and os.path.isfile(chromium):
        launch_kw['executable_path'] = chromium
    browser = await pw.chromium.launch(**launch_kw)
    page = await browser.new_page()

    # Hook WebAssembly.instantiate BEFORE main.js loads so we can capture
    # the panel WASM instance (module-scoped `ui` is otherwise inaccessible).
    await page.add_init_script('''
        window._cdp_wasm = [];
        const _origInstantiate = WebAssembly.instantiate;
        WebAssembly.instantiate = async function(source, imports) {
            const result = await _origInstantiate.call(this, source, imports);
            const inst = result.instance || result;
            window._cdp_wasm.push(inst);
            return result;
        };
    ''')

    await page.goto(emu_url)
    await page.evaluate("async () => { await import('./main.js'); }")

    await page.locator('#open').set_input_files(str(fwsc))
    await page.wait_for_function(
        'document.getElementById("status").textContent.includes("ready")',
        timeout=30_000)
    await page.locator('#start').click()
    # Wait for emulator running: stock shows "instructions/s", WiFi build shows "real time"
    await page.wait_for_function(
        '''() => {
            const s = document.getElementById("status")?.textContent || "";
            return s.includes("instructions/s") || s.includes("real time");
        }''',
        timeout=120_000)

    bridge = CDPBridge(page)
    if ws_port == 0:
        ws_port = _free_port()

    async def handler(ws):
        await _handle_ws(bridge, ws)

    ws_server = await websockets.serve(handler, '127.0.0.1', ws_port)

    return {
        'page': page,
        'browser': browser,
        'pw': pw,
        'http_server': http_server,
        'ws_server': ws_server,
        'ws_port': ws_port,
        'emu_dir': str(emu_dir),
    }


async def shutdown(ctx):
    """Tear down everything returned by boot_and_serve."""
    if ctx.get('ws_server'):
        ctx['ws_server'].close()
        await ctx['ws_server'].wait_closed()
    if ctx.get('browser'):
        await ctx['browser'].close()
    if ctx.get('pw'):
        await ctx['pw'].stop()
    if ctx.get('http_server'):
        ctx['http_server'].shutdown()
