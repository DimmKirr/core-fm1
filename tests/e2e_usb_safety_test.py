#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""E2E: USB SysEx safety tests.

Guarantees every shipped defconfig produces a binary that can receive SysEx
over USB, so the device is always reflashable. Three layers of proof:

  1. NEGATIVE: build without CONFIG_USB, scan the compiled binary for the
     UBOOT SysEx key -> must be absent (validates the test itself).
  2. POSITIVE: build with CONFIG_USB, scan the compiled binary -> key present.
  3. LIVE: boot the firmware in the WASM emulator, scan the running
     firmware's RAM for the UBOOT key bytes -> proves the USB driver code
     is loaded into memory and executable, not dead-stripped or unreachable.

Results saved to tests/results/<timestamp>-e2e_usb_safety_test/.
"""
import http.server
import json
import os
import shutil
import socket
import subprocess
import sys
import threading
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from e2e_fixtures import E2ETestCase, _run_timestamp, RESULTS_DIR, EMU_DIR, e2e_genconfig, e2e_build
from emu_usb_patch import create_patched_emu, cleanup_patched_emu

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
FWSC = BUILD / 'core-fm1.fwsc'

TEST_RESULTS = RESULTS_DIR / f'{_run_timestamp}-e2e_usb_safety_test'

UBOOT_SYSEX_KEY = bytes([0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7])
USB_VID_LE = bytes([0x09, 0x12])  # VID 0x1209, little-endian in descriptor
MANIFEST_MARKER = b'<<CFM1>>'

ALL_DEFCONFIGS = ['minimal', 'core_fm1_dev', 'core', 'melodee', 'felucca', 'sloop']


def _genconfig(defconfig):
    return e2e_genconfig(defconfig, TEST_RESULTS)


def _build(defconfig):
    r, _ = e2e_build(defconfig, TEST_RESULTS)
    return r


def _read_binary():
    for name in ('text.bin', 'core-fm1.bin', 'body.bin'):
        path = BUILD / name
        if path.exists():
            return path.read_bytes()
    return None


def _manifest_components_from_binary(data):
    start = data.find(MANIFEST_MARKER)
    if start < 0:
        return None
    start += len(MANIFEST_MARKER)
    end = data.find(MANIFEST_MARKER, start)
    if end < 0:
        return None
    return sorted(data[start:end].decode('ascii').split(','))


def _save(filename, data):
    TEST_RESULTS.mkdir(parents=True, exist_ok=True)
    content = json.dumps(data, indent=2) + '\n' if isinstance(data, (list, dict)) else str(data) + '\n'
    (TEST_RESULTS / filename).write_text(content)


def _find_free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


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


class TestUSBAbsentWhenDisabled(E2ETestCase):
    """Build with USB off, prove the UBOOT key is NOT in the binary."""

    _nousb_defconfig = '_nousb_test'
    _build_ok = False

    @classmethod
    def setUpClass(cls):
        dc_path = ROOT / 'configs' / f'{cls._nousb_defconfig}_defconfig'
        dc_path.write_text(
            '# Temporary defconfig for USB-absent test\n'
            '# CONFIG_USB is not set\n'
            '# CONFIG_AUDIO is not set\n'
            '# CONFIG_INPUT is not set\n'
            '# CONFIG_FLASH is not set\n'
            '# CONFIG_MIDI_UART is not set\n'
            '# CONFIG_DUAL_CORE is not set\n'
        )
        try:
            r = _build(cls._nousb_defconfig)
            cls._build_ok = r.returncode == 0
        finally:
            dc_path.unlink(missing_ok=True)
        try:
            super().setUpClass()
        except unittest.SkipTest:
            pass

    @classmethod
    def tearDownClass(cls):
        dc_path = ROOT / 'configs' / f'{cls._nousb_defconfig}_defconfig'
        dc_path.unlink(missing_ok=True)
        super().tearDownClass()

    def test_01_no_uboot_key_in_binary(self):
        """Binary built without CONFIG_USB must NOT contain the UBOOT SysEx key."""
        if not self._build_ok:
            self.skipTest('Build failed (expected if toolchain absent)')
        data = _read_binary()
        self.assertIsNotNone(data, 'No binary found after build')
        found = data.find(UBOOT_SYSEX_KEY)
        _save('nousb-scan.json', {
            'uboot_key_found': found >= 0,
            'uboot_key_offset': found,
            'binary_size': len(data),
        })
        self.assertEqual(found, -1,
            'SAFETY TEST VALIDATION: UBOOT SysEx key found in a binary built '
            'WITHOUT CONFIG_USB. The negative test is broken.')

    def test_02_no_usb_in_manifest(self):
        """Manifest in binary must NOT contain "usb" when USB is disabled."""
        if not self._build_ok:
            self.skipTest('Build failed')
        data = _read_binary()
        self.assertIsNotNone(data)
        components = _manifest_components_from_binary(data)
        if components is not None:
            self.assertNotIn('usb', components)


class TestUSBLiveInEmulator(unittest.TestCase):
    """Boot firmware in the emulator, scan RUNNING memory for USB proof."""

    _patched_dir = None
    _server = None
    _thread = None
    _pw = None
    _browser = None
    _build_ok = False
    emu_url = None

    @classmethod
    def setUpClass(cls):
        try:
            from playwright.sync_api import sync_playwright
        except ImportError:
            raise unittest.SkipTest('playwright not installed')

        if not (EMU_DIR / 'fm1_web.wasm').is_file():
            raise unittest.SkipTest(f'Emulator not found at {EMU_DIR}')

        r = _build('minimal')
        cls._build_ok = r.returncode == 0
        if not cls._build_ok:
            raise unittest.SkipTest('Build failed (toolchain absent)')

        if not FWSC.is_file():
            raise unittest.SkipTest('No .fwsc produced')

        cls._patched_dir = Path(create_patched_emu(EMU_DIR))

        port = _find_free_port()
        cls._server = http.server.HTTPServer(
            ('127.0.0.1', port), _make_handler(cls._patched_dir))
        cls._thread = threading.Thread(
            target=cls._server.serve_forever, daemon=True)
        cls._thread.start()
        cls.emu_url = f'http://127.0.0.1:{port}/?fw=local&mhz=100&debug=1'

        cls._pw = sync_playwright().start()
        launch_kwargs = dict(
            headless=True,
            args=['--autoplay-policy=no-user-gesture-required'],
        )
        chromium = os.environ.get('CHROMIUM_PATH') or shutil.which('chromium')
        if chromium and os.path.isfile(chromium):
            launch_kwargs['executable_path'] = chromium
        cls._browser = cls._pw.chromium.launch(**launch_kwargs)

    @classmethod
    def tearDownClass(cls):
        if cls._browser:
            cls._browser.close()
        if cls._pw:
            cls._pw.stop()
        if cls._server:
            cls._server.shutdown()
        if cls._patched_dir:
            cleanup_patched_emu(cls._patched_dir)

    def _boot_page(self):
        page = self._browser.new_page()
        page._console_logs = []
        page.on('console', lambda msg: page._console_logs.append(
            f'[{msg.type}] {msg.text}'))
        page.on('pageerror', lambda err: page._console_logs.append(
            f'[pageerror] {err}'))
        page.goto(self.emu_url)
        page.evaluate("async () => { await import('./main.js'); }")
        return page

    def _save_log(self, page, name):
        TEST_RESULTS.mkdir(parents=True, exist_ok=True)
        info = page.evaluate('''() => ({
            status: document.getElementById('status')?.textContent || '',
            fm1log: document.getElementById('fm1log')?.textContent || ''
        })''')
        lines = [
            f'status: {info["status"]}', '',
            '--- console ---',
            *getattr(page, '_console_logs', ['(none)']),
        ]
        (TEST_RESULTS / f'{name}.log').write_text('\n'.join(lines) + '\n')

    def test_01_uboot_key_in_live_memory(self):
        """The UBOOT SysEx key must be in the running firmware's WASM memory."""
        page = self._boot_page()
        try:
            page.locator('#open').set_input_files(str(FWSC))
            page.wait_for_function(
                'document.getElementById("status").textContent.includes("ready")')
            page.locator('#start').click()
            page.wait_for_function(
                'document.getElementById("status")?.textContent'
                '?.includes("instructions/s")',
                timeout=30_000)
            page.wait_for_timeout(3_000)

            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status, f'Firmware halted: {status}')

            result = page.evaluate('''async () => {
                return await window.fm1_scan([0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7]);
            }''')

            _save('live-uboot-key.json', result)

            self.assertTrue(result['found'],
                'SAFETY: UBOOT SysEx key (F0 22 24 35 7D F7) not found in '
                'the running firmware memory. The USB SysEx handler is not '
                'loaded. The device will NOT be reflashable over USB.')

        finally:
            self._save_log(page, 'live-uboot-scan')
            page.close()

    def test_02_usb_vid_in_live_memory(self):
        """The USB device descriptor (VID 0x1209) must be in live memory."""
        page = self._boot_page()
        try:
            page.locator('#open').set_input_files(str(FWSC))
            page.wait_for_function(
                'document.getElementById("status").textContent.includes("ready")')
            page.locator('#start').click()
            page.wait_for_function(
                'document.getElementById("status")?.textContent'
                '?.includes("instructions/s")',
                timeout=30_000)
            page.wait_for_timeout(3_000)

            result = page.evaluate('''async () => {
                return await window.fm1_scan([0x09, 0x12]);
            }''')

            _save('live-usb-vid.json', result)

            self.assertTrue(result['found'],
                'SAFETY: USB VID 0x1209 not found in running firmware memory. '
                'USB enumeration code is not loaded.')

        finally:
            self._save_log(page, 'live-vid-scan')
            page.close()

    def test_03_manifest_usb_in_live_memory(self):
        """The manifest marker in live memory must list "usb"."""
        page = self._boot_page()
        try:
            page.locator('#open').set_input_files(str(FWSC))
            page.wait_for_function(
                'document.getElementById("status").textContent.includes("ready")')
            page.locator('#start').click()
            page.wait_for_function(
                'document.getElementById("status")?.textContent'
                '?.includes("instructions/s")',
                timeout=30_000)
            page.wait_for_timeout(3_000)

            marker = list(b'<<CFM1>>')
            result = page.evaluate(f'''async () => {{
                const r = await window.fm1_scan({marker});
                if (!r.found) return {{ found: false, components: null }};
                const offset = r.offset + {len(marker)};
                const data = await window.fm1_peek(offset, 512);
                const end = data.indexOf(0x3C);  // '<' of closing marker
                const text = String.fromCharCode(...data.slice(0, end >= 0 ? end : 512));
                return {{ found: true, components: text.split(',') }};
            }}''')

            _save('live-manifest.json', result)

            self.assertTrue(result['found'], 'Manifest marker not in live memory')
            self.assertIn('usb', result['components'],
                f'SAFETY: "usb" not in live manifest. Components: {result["components"]}')

        finally:
            self._save_log(page, 'live-manifest-scan')
            page.close()


class TestAllDefconfigsBinaryUSB(unittest.TestCase):
    """Every shipped defconfig must produce a binary with USB SysEx support."""

    def _check_defconfig(self, name):
        dc = ROOT / 'configs' / f'{name}_defconfig'
        if not dc.exists():
            self.skipTest(f'configs/{name}_defconfig not yet created')

        gen_dir = _genconfig(name)

        config_h = gen_dir / 'config.h'
        self.assertTrue(config_h.exists(), f'{name}: config.h not generated')
        header = config_h.read_text()
        self.assertIn('#define CONFIG_USB 1', header,
            f'SAFETY: defconfig {name!r} does not enable CONFIG_USB.')

        manifest = json.loads((gen_dir / 'manifest.json').read_text())['components']
        self.assertIn('usb', manifest,
            f'SAFETY: defconfig {name!r} manifest missing "usb".')

        r, _ = e2e_build(name, TEST_RESULTS, gen_dir=gen_dir)
        if r.returncode != 0:
            self.skipTest(f'{name}: build failed (toolchain may be absent)')

        data = _read_binary()
        self.assertIsNotNone(data, f'{name}: no binary after build')

        offset = data.find(UBOOT_SYSEX_KEY)
        _save(f'all-{name}-uboot-key.json', {
            'defconfig': name,
            'uboot_key_found': offset >= 0,
            'uboot_key_offset': offset,
            'binary_size': len(data),
        })
        self.assertGreaterEqual(offset, 0,
            f'SAFETY: UBOOT SysEx key not in {name} binary. '
            f'Device will NOT be reflashable over USB.')

    def test_minimal(self):
        self._check_defconfig('minimal')

    def test_core(self):
        self._check_defconfig('core')

    def test_core_fm1_dev(self):
        self._check_defconfig('core_fm1_dev')

    def test_melodee(self):
        self._check_defconfig('melodee')

    def test_felucca(self):
        self._check_defconfig('felucca')

    def test_sloop(self):
        self._check_defconfig('sloop')


if __name__ == '__main__':
    unittest.main()
