#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Shared fixtures for Playwright E2E tests against the fm1-emu WASM emulator."""
import http.server
import os
import socket
import shutil
import threading
import unittest
from datetime import datetime, timezone
from pathlib import Path

from fm1_controls import FM1Controls

EMU_DIR = Path(os.environ.get('FM1_WEB_DIR', Path.home() / '.fm1-emulator' / 'web'))
RESULTS_DIR = Path(__file__).resolve().parent / 'results'
CHROMIUM_PATH = os.environ.get('CHROMIUM_PATH') or shutil.which('chromium')


def _make_handler(directory):
    class _Handler(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(directory), **kwargs)
        def end_headers(self):
            self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
            self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
            super().end_headers()
        def log_message(self, fmt, *args):
            pass
    return _Handler


def _find_free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


_run_timestamp = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H%M%SZ')


class E2ETestCase(unittest.TestCase):
    """Base class: starts a static file server + Playwright browser for fm1-emu."""

    _server = None
    _thread = None
    _pw = None
    _browser = None
    emu_url = None

    @classmethod
    def setUpClass(cls):
        try:
            from playwright.sync_api import sync_playwright
        except ImportError:
            raise unittest.SkipTest('playwright not installed (uv add playwright)')

        if not (EMU_DIR / 'fm1_web.wasm').is_file():
            raise unittest.SkipTest(
                f'FM1_WEB_DIR not set or missing ({EMU_DIR}). '
                'Run "task fm1-emulator:download" first.')

        port = _find_free_port()
        cls._server = http.server.HTTPServer(
            ('127.0.0.1', port), _make_handler(EMU_DIR))
        cls._thread = threading.Thread(
            target=cls._server.serve_forever, daemon=True)
        cls._thread.start()
        cls.emu_url = f'http://127.0.0.1:{port}/?fw=local&mhz=100&debug=1'

        cls._pw = sync_playwright().start()
        launch_kwargs = dict(
            headless=True,
            args=['--autoplay-policy=no-user-gesture-required'],
        )
        if CHROMIUM_PATH and os.path.isfile(CHROMIUM_PATH):
            launch_kwargs['executable_path'] = CHROMIUM_PATH
        cls._browser = cls._pw.chromium.launch(**launch_kwargs)

    @classmethod
    def tearDownClass(cls):
        if cls._browser:
            cls._browser.close()
        if cls._pw:
            cls._pw.stop()
        if cls._server:
            cls._server.shutdown()

    def new_emu_page(self):
        """Open the fm1-emu WASM emulator in a fresh browser page."""
        page = self._browser.new_page()
        page._console_logs = []
        page.on('console', lambda msg: page._console_logs.append(
            f'[{msg.type}] {msg.text}'))
        page.on('pageerror', lambda err: page._console_logs.append(
            f'[pageerror] {err}'))
        page.goto(self.emu_url)
        # The module awaits WASM initialization before attaching input handlers.
        page.evaluate("async () => { await import('./main.js'); }")
        return page

    def write_log(self, page, filename='fm1-emulator.log'):
        results = self._results_dir()
        results.mkdir(parents=True, exist_ok=True)
        info = page.evaluate('''() => {
            const r = {};
            r.status = document.getElementById('status')?.textContent || '';
            r.fm1log = document.getElementById('fm1log')?.textContent || '';
            return r;
        }''')
        lines = [
            f'status: {info["status"]}',
            '',
            '--- #fm1log ---',
            info.get('fm1log', '(empty)'),
            '',
            f'--- JS console ({len(page._console_logs)} messages) ---',
        ]
        lines.extend(page._console_logs or ['(no console output)'])
        (results / filename).write_text('\n'.join(lines) + '\n')

    def _results_dir(self):
        test_module = self.__class__.__module__.rsplit('.', 1)[-1]
        return RESULTS_DIR / f'{_run_timestamp}-{test_module}'

    # -- FM-1 panel controls (keyboard mapping from fm1-emu) --

    @staticmethod
    def controls(page):
        return FM1Controls(page)

    @staticmethod
    def push(page, key, delay_ms=300):
        FM1Controls(page).press_key(key, delay_ms=delay_ms)

    @staticmethod
    def long_push(page, key, hold_ms=900, delay_ms=500):
        FM1Controls(page).press_key(key, hold_ms=hold_ms, delay_ms=delay_ms)

    @staticmethod
    def send_enc(page, physical_enc, clicks, delay_ms=300):
        FM1Controls(page).send_encoder(physical_enc, clicks, delay_ms=delay_ms)

    def screenshot(self, page, screen_name):
        """Save only the emulator panel canvas beside this run's logs."""
        screenshot_dir = self._results_dir() / 'screenshots'
        screenshot_dir.mkdir(parents=True, exist_ok=True)
        filename = f'{screen_name}.png'
        page.locator('canvas#panel').screenshot(path=str(screenshot_dir / filename))
