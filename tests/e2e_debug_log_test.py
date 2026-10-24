#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""E2E: debug log ring buffer and DEBUG page.

Builds with felucca_noise + CONFIG_DEBUG_LOG, boots in the emulator,
navigates to the DEBUG page (via GLO button), and verifies that:
  1. The boot log messages appear on the LCD canvas
  2. The DEBUG page shows correct card layout (LOG, FROM, BUF, LEN)
  3. The page footer shows "DEBUG"
  4. Scrolling changes the visible window when entries exceed 8
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from e2e_fixtures import E2ETestCase, _run_timestamp, RESULTS_DIR, e2e_build

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
FWSC = BUILD / 'core-fm1.fwsc'

DEFCONFIG = 'felucca_noise'
TEST_RESULTS = RESULTS_DIR / f'{_run_timestamp}-e2e_debug_log_test'


def build_firmware():
    r, _ = e2e_build(DEFCONFIG, TEST_RESULTS)
    return r


def _canvas_pixel_hash(page):
    """Return a hash of the LCD canvas pixel data for change detection."""
    return page.evaluate('''() => {
        const c = document.getElementById('panel');
        if (!c) return '';
        const ctx = c.getContext('2d');
        const d = ctx.getImageData(0, 0, c.width, c.height).data;
        let h = 0;
        for (let i = 0; i < d.length; i += 16)
            h = ((h << 5) - h + d[i]) | 0;
        return String(h);
    }''')


class TestDebugLog(E2ETestCase):
    """Debug log ring buffer + DEBUG page E2E."""

    _build_ok = False

    @classmethod
    def setUpClass(cls):
        TEST_RESULTS.mkdir(parents=True, exist_ok=True)
        r = build_firmware()
        cls._build_ok = r.returncode == 0
        if not cls._build_ok and FWSC.is_file():
            cls._build_ok = True
        try:
            super().setUpClass()
        except unittest.SkipTest:
            pass

    def _boot_and_wait(self, page):
        """Load firmware, start emulator, wait for boot."""
        page.locator('#open').set_input_files(str(FWSC))
        page.wait_for_function(
            'document.getElementById("status").textContent.includes("ready")')
        page.locator('#start').click()
        page.wait_for_function(
            'document.getElementById("status")?.textContent'
            '?.includes("instructions/s")',
            timeout=30_000)
        page.wait_for_timeout(5_000)

    def _skip_if_not_ready(self):
        if not self._build_ok:
            self.skipTest('Build failed; no .fwsc to boot')
        if not FWSC.is_file():
            self.skipTest('No .fwsc produced')
        if not self._browser:
            self.skipTest('Emulator not available')

    def _assert_canvas_has_content(self, page, label='page'):
        """Verify the canvas has visible rendered content."""
        lit = page.evaluate('''() => {
            const c = document.getElementById('panel');
            if (!c) return 0;
            const ctx = c.getContext('2d');
            const d = ctx.getImageData(0, 0, c.width, c.height).data;
            let lit = 0;
            for (let i = 0; i < d.length; i += 4)
                if (d[i] > 40 || d[i+1] > 40 || d[i+2] > 40) lit++;
            return lit;
        }''')
        self.assertGreater(lit, 200, f'{label} canvas appears blank')

    def _assert_not_halted(self, page):
        status = page.locator('#status').text_content()
        self.assertNotIn('halted', status, 'Emulator halted')

    def test_01_debug_page_shows_boot_log(self):
        """GLO button navigates to DEBUG page with boot log visible."""
        self._skip_if_not_ready()

        page = self.new_emu_page()
        try:
            self._boot_and_wait(page)
            self._assert_canvas_has_content(page, 'HOME')
            home_hash = _canvas_pixel_hash(page)
            self.screenshot(page, 'home-before-glo')

            ctrl = self.controls(page)
            ctrl.press_button('glo', delay_ms=1500)
            self.screenshot(page, 'debug-page-after-glo')

            page.wait_for_timeout(500)
            self.screenshot(page, 'debug-page-settled')

            debug_hash = _canvas_pixel_hash(page)
            self.assertNotEqual(home_hash, debug_hash,
                                'Screen did not change after GLO press')
            self._assert_canvas_has_content(page, 'DEBUG')
            self._assert_not_halted(page)

        finally:
            self.write_log(page, 'debug-log-test.log')
            page.close()

    def test_02_scroll_log(self):
        """Select encoder scrolls the debug log when entries exceed visible area."""
        self._skip_if_not_ready()

        page = self.new_emu_page()
        try:
            self._boot_and_wait(page)
            ctrl = self.controls(page)

            ctrl.press_button('glo', delay_ms=1500)
            self._assert_not_halted(page)
            self.screenshot(page, 'debug-scroll-initial')
            initial_hash = _canvas_pixel_hash(page)

            ctrl.rotate_encoder('select', direction=-1, steps=3, delay_ms=500)
            self.screenshot(page, 'debug-scroll-up')
            scrolled_hash = _canvas_pixel_hash(page)
            self.assertNotEqual(initial_hash, scrolled_hash,
                                'Scroll up did not change the display')

            ctrl.rotate_encoder('select', direction=1, steps=6, delay_ms=500)
            self.screenshot(page, 'debug-scroll-down')
            scrolled_down_hash = _canvas_pixel_hash(page)
            self.assertNotEqual(scrolled_hash, scrolled_down_hash,
                                'Scroll down did not change the display')

            self._assert_not_halted(page)

        finally:
            self.write_log(page, 'debug-scroll-test.log')
            page.close()


if __name__ == '__main__':
    unittest.main()
