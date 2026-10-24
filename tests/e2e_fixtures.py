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

import subprocess
import sys

from fm1_controls import FM1Controls

ROOT = Path(__file__).resolve().parents[1]
EMU_DIR = Path(os.environ.get('FM1_WEB_DIR', Path.home() / '.fm1-emulator' / 'web'))
RESULTS_DIR = Path(__file__).resolve().parent / 'results'
CHROMIUM_PATH = os.environ.get('CHROMIUM_PATH') or shutil.which('chromium')

if 'PLAYWRIGHT_BROWSERS_PATH' not in os.environ:
    _pw_cache = Path.home() / '.cache' / 'ms-playwright'
    if _pw_cache.is_dir():
        os.environ['PLAYWRIGHT_BROWSERS_PATH'] = str(_pw_cache)


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


def e2e_genconfig(defconfig, results_dir):
    """Run genconfig with --outdir into the test results directory.

    Returns the gen_dir path containing .config, config.h, manifest.json.
    """
    gen_dir = results_dir / 'config'
    gen_dir.mkdir(parents=True, exist_ok=True)
    r = subprocess.run(
        [sys.executable, str(ROOT / 'tools' / 'genconfig.py'),
         '--defconfig', defconfig, '--outdir', str(gen_dir)],
        capture_output=True, text=True, cwd=str(ROOT), timeout=60)
    if r.returncode != 0:
        raise RuntimeError(f'genconfig failed:\n{r.stderr}')
    return gen_dir


def e2e_build(defconfig, results_dir, gen_dir=None):
    """Run genconfig + build, storing config in results_dir/config/.

    Returns (subprocess result, gen_dir path).
    """
    if gen_dir is None:
        gen_dir = e2e_genconfig(defconfig, results_dir)
    env = {**os.environ, 'CFM1_GEN_DIR': str(gen_dir)}
    r = subprocess.run(
        [sys.executable, str(ROOT / 'tools' / 'build.py'),
         '--defconfig', defconfig],
        capture_output=True, text=True, cwd=str(ROOT), timeout=300, env=env)
    (results_dir / 'build.log').write_text(
        f'--- stdout ---\n{r.stdout}\n--- stderr ---\n{r.stderr}\n'
        f'--- exit {r.returncode} ---\n')
    return r, gen_dir


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

    def new_emu_page(self, mhz=None):
        """Open the fm1-emu WASM emulator in a fresh browser page."""
        page = self._browser.new_page()
        page._console_logs = []
        page.on('console', lambda msg: page._console_logs.append(
            f'[{msg.type}] {msg.text}'))
        page.on('pageerror', lambda err: page._console_logs.append(
            f'[pageerror] {err}'))
        url = self.emu_url
        if mhz is not None:
            url = url.replace('mhz=100', f'mhz={mhz}')
        page.goto(url)
        page.evaluate("async () => { await import('./main.js'); }")
        return page

    def start_canvas_capture(self, page, fps=10):
        """Start capturing canvas#panel frames via requestAnimationFrame."""
        page.evaluate('''(fps) => {
            const canvas = document.getElementById('panel');
            window._capturedFrames = [];
            window._captureRunning = true;
            const minInterval = 1000 / fps;
            let last = 0;
            function grab(ts) {
                if (!window._captureRunning) return;
                if (ts - last >= minInterval) {
                    window._capturedFrames.push(canvas.toDataURL('image/png'));
                    last = ts;
                }
                requestAnimationFrame(grab);
            }
            requestAnimationFrame(grab);
        }''', fps)

    def stop_canvas_capture(self, page):
        """Stop frame capture and return the data URLs."""
        return page.evaluate('''() => {
            window._captureRunning = false;
            const frames = window._capturedFrames;
            window._capturedFrames = [];
            return frames;
        }''')

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
        ts = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H%M%S.%f')[:-3] + 'Z'
        filename = f'{ts}-{screen_name}.png'
        page.locator('canvas#panel').screenshot(path=str(screenshot_dir / filename))

    def frames_to_gif(self, frames, name='boot', fps=10, scale=480):
        """Convert captured canvas data-URL frames to an optimised GIF."""
        import base64
        import subprocess
        results = self._results_dir()
        frame_dir = results / 'frames'
        frame_dir.mkdir(parents=True, exist_ok=True)
        for i, data_url in enumerate(frames):
            raw = base64.b64decode(data_url.split(',', 1)[1])
            (frame_dir / f'{i:05d}.png').write_bytes(raw)
        gif_path = results / f'{name}.gif'
        palette = frame_dir / '_palette.png'
        filters = f'fps={fps},scale={scale}:-1:flags=lanczos'
        inp = str(frame_dir / '%05d.png')
        subprocess.run([
            'ffmpeg', '-y', '-framerate', str(fps), '-i', inp,
            '-vf', f'{filters},palettegen=stats_mode=diff',
            str(palette),
        ], check=True, capture_output=True)
        subprocess.run([
            'ffmpeg', '-y', '-framerate', str(fps), '-i', inp,
            '-i', str(palette),
            '-lavfi', f'{filters}[x];[x][1:v]paletteuse=dither=bayer:bayer_scale=3',
            str(gif_path),
        ], check=True, capture_output=True)
        import shutil
        shutil.rmtree(frame_dir)
        return gif_path
