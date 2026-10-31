#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""E2E: visit every view (page + modal) and capture a screenshot.

Navigation model: each button opens a VIEW; SELECT encoder cycles pages
within that view. Multi-page views:
  VIEW_GLO (glo btn):         MIXER, VOICE (GLO->DEBUG only with CONFIG_DEBUG_LOG)
  VIEW_CONN (long-glo):       BLE, WEATHER, WIFI
  VIEW_SETTINGS (long-home):  SYS, ABOUT, DEBUG
  VIEW_SAVE (save btn):       FILES

Single-page views: HOME, ENV, LFO, FX, SCL, EDIT, ARP, SEQ
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from e2e_fixtures import E2ETestCase

ROOT = Path(__file__).resolve().parents[1]
FWSC = ROOT / 'build' / 'core-fm1.fwsc'

LCD_MIN_PIXELS = 500
LCD_REGION = (370, 42, 480, 480)


class TestAllViews(E2ETestCase):
    """Visit every view and confirm it renders with meaningful content."""

    @classmethod
    def setUpClass(cls):
        if not FWSC.is_file():
            raise unittest.SkipTest('Firmware not built. Run "task build" first.')
        super().setUpClass()

    def _boot(self):
        page = self.new_emu_page(mhz=100)
        page.locator('#open').set_input_files(str(FWSC))
        page.wait_for_function(
            'document.getElementById("status").textContent.includes("ready")')
        page.locator('#start').click()
        page.wait_for_function(
            'document.getElementById("status")?.textContent'
            '?.includes("instructions/s")',
            timeout=30_000)
        page.wait_for_function('''() => {
            const c = document.getElementById('panel');
            const p = c.getContext('2d').getImageData(370, 42, 480, 480).data;
            let n = 0;
            for (let i = 0; i < p.length; i += 4)
                if (p[i] > 30 || p[i+1] > 30 || p[i+2] > 30) n++;
            return n > 500;
        }''', timeout=120_000)
        page.wait_for_timeout(2000)
        return page

    def _assert_alive(self, page):
        status = page.locator('#status').text_content()
        self.assertNotIn('halted', status, f'Emulator halted: {status}')

    def _lcd_pixel_count(self, page):
        lx, ly, lw, lh = LCD_REGION
        return page.evaluate(f'''() => {{
            const c = document.getElementById('panel');
            const p = c.getContext('2d').getImageData({lx}, {ly}, {lw}, {lh}).data;
            let n = 0;
            for (let i = 0; i < p.length; i += 4)
                if (p[i] > 30 || p[i+1] > 30 || p[i+2] > 30) n++;
            return n;
        }}''')

    def _verify_page(self, page, name, label):
        self.screenshot(page, f'{name}-{label}')
        pixels = self._lcd_pixel_count(page)
        self.assertGreater(pixels, LCD_MIN_PIXELS,
                           f'{name}: LCD too empty ({pixels} non-black pixels)')
        self._assert_alive(page)
        print(f'  {name}: {pixels} pixels OK')

    # ------------------------------------------------------------------
    # Single-page views: each accessible via button tap from HOME
    # ------------------------------------------------------------------

    def test_01_home_page(self):
        """HOME page renders with engine display."""
        page = self._boot()
        try:
            self._verify_page(page, 'HOME', 'initial')
        finally:
            self.write_log(page, 'views-home.log')
            page.close()

    def test_02_button_pages(self):
        """Each button-accessible page renders with content."""
        page = self._boot()
        try:
            ctrl = self.controls(page)
            for btn, name in [('env', 'ENV'), ('lfo', 'LFO'), ('fx', 'FX'),
                              ('scl', 'SCL'), ('edit', 'EDIT'), ('arp', 'ARP'),
                              ('seq', 'SEQ')]:
                ctrl.press_button(btn, delay_ms=400)
                page.wait_for_timeout(500)
                self._verify_page(page, name, f'button-{btn}')
                ctrl.press_button('home', delay_ms=300)
                page.wait_for_timeout(200)
        finally:
            self.write_log(page, 'views-button-pages.log')
            page.close()

    # ------------------------------------------------------------------
    # VIEW_GLO: GLO button opens GLO view; SELECT cycles MIXER/VOICE
    # (with CONFIG_DEBUG_LOG, GLO tap may go to DEBUG instead)
    # ------------------------------------------------------------------

    def test_03_glo_view_pages(self):
        """GLO button view: MIXER and VOICE via SELECT cycle."""
        page = self._boot()
        try:
            ctrl = self.controls(page)
            ctrl.press_button('glo', delay_ms=400)
            page.wait_for_timeout(500)
            self._verify_page(page, 'GLO-page1', 'glo-tap')

            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=200)
            page.wait_for_timeout(500)
            self._verify_page(page, 'GLO-page2', 'select-cycle')
        finally:
            self.write_log(page, 'views-glo.log')
            page.close()

    # ------------------------------------------------------------------
    # VIEW_SAVE: SAVE button opens FILES page
    # ------------------------------------------------------------------

    def test_04_files_page(self):
        """SAVE button opens FILES view."""
        page = self._boot()
        try:
            ctrl = self.controls(page)
            ctrl.press_button('save', delay_ms=400)
            page.wait_for_timeout(500)
            self._verify_page(page, 'FILES', 'save-button')
        finally:
            self.write_log(page, 'views-files.log')
            page.close()

    # ------------------------------------------------------------------
    # VIEW_SETTINGS: long-HOME opens SYS; SELECT cycles SYS/ABOUT/DEBUG
    # ------------------------------------------------------------------

    def test_05_settings_view_pages(self):
        """Long-HOME opens SETTINGS: SYS, ABOUT, DEBUG via SELECT."""
        page = self._boot()
        try:
            ctrl = self.controls(page)
            ctrl.press_button('home', hold_ms=600, delay_ms=500)
            page.wait_for_timeout(500)
            self._verify_page(page, 'SYS', 'long-home')

            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=200)
            page.wait_for_timeout(500)
            self._verify_page(page, 'SETTINGS-page2', 'select-1')

            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=200)
            page.wait_for_timeout(500)
            self._verify_page(page, 'SETTINGS-page3', 'select-2')

            ctrl.press_button('home', delay_ms=400)
            page.wait_for_timeout(300)
            self._assert_alive(page)
        finally:
            self.write_log(page, 'views-settings.log')
            page.close()

    # ------------------------------------------------------------------
    # VIEW_CONN: long-GLO opens BLE; SELECT cycles BLE/WEATHER/WIFI
    # ------------------------------------------------------------------

    def test_06_conn_view_pages(self):
        """Long-GLO opens CONN view: BLE (+ WEATHER/WIFI if enabled)."""
        page = self._boot()
        try:
            ctrl = self.controls(page)
            ctrl.press_button('glo', hold_ms=600, delay_ms=500)
            page.wait_for_timeout(500)
            self._verify_page(page, 'CONN-page1', 'long-glo')

            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=200)
            page.wait_for_timeout(500)
            self._verify_page(page, 'CONN-page2', 'select-1')
        finally:
            self.write_log(page, 'views-conn.log')
            page.close()

    # ------------------------------------------------------------------
    # SYS modal tabs: ALGO encoder switches between MIDI/SYSTEM/BLE tabs
    # ------------------------------------------------------------------

    def test_07_sys_tabs(self):
        """SYS page tab switching via ALGO encoder."""
        page = self._boot()
        try:
            ctrl = self.controls(page)
            ctrl.press_button('home', hold_ms=600, delay_ms=500)
            page.wait_for_timeout(500)
            self._verify_page(page, 'SYS-tab0', 'initial')

            for tab_idx in range(1, 3):
                ctrl.rotate_encoder('algorithm', direction=1, steps=1, delay_ms=200)
                page.wait_for_timeout(400)
                self._verify_page(page, f'SYS-tab{tab_idx}', f'algo-{tab_idx}')
        finally:
            self.write_log(page, 'views-sys-tabs.log')
            page.close()

    # ------------------------------------------------------------------
    # HOME engine cycling via SELECT encoder
    # ------------------------------------------------------------------

    def test_08_home_engine_cycling(self):
        """SELECT on HOME cycles engines (not pages), screenshot each."""
        page = self._boot()
        try:
            ctrl = self.controls(page)
            self._verify_page(page, 'HOME-engine-0', 'initial')

            for i in range(1, 6):
                ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=200)
                page.wait_for_timeout(300)
                self._verify_page(page, f'HOME-engine-{i}', 'select')
        finally:
            self.write_log(page, 'views-engines.log')
            page.close()

    # ------------------------------------------------------------------
    # Rapid view switching stress test
    # ------------------------------------------------------------------

    def test_09_rapid_view_switching(self):
        """Rapidly switch between views via buttons without crash."""
        page = self._boot()
        try:
            ctrl = self.controls(page)
            buttons = ['env', 'lfo', 'fx', 'scl', 'edit', 'arp', 'seq',
                       'glo', 'home']
            for _ in range(3):
                for btn in buttons:
                    ctrl.press_button(btn, delay_ms=80)
                    page.wait_for_timeout(100)
            self._assert_alive(page)
            self.screenshot(page, 'rapid-view-switch-done')
        finally:
            self.write_log(page, 'views-rapid.log')
            page.close()


if __name__ == '__main__':
    unittest.main()
