#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""E2E: settings menu (SYS page) renders, tabs switch, values cycle.

Builds felucca_noise (SYS_MENU + USB_MODE + MIDI_CH + BLE), boots in
fm1-emu, long-presses HOME to open settings, and verifies tab switching
and value cycling via encoder interaction + screenshots.
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from e2e_fixtures import E2ETestCase

FWSC_PATH = Path(__file__).resolve().parents[1] / 'build' / 'core-fm1.fwsc'


class TestSysMenu(E2ETestCase):
    """Settings menu page in the emulator."""

    @classmethod
    def setUpClass(cls):
        if not FWSC_PATH.is_file():
            raise unittest.SkipTest('Firmware not built. Run "task build" first.')
        super().setUpClass()

    def _boot_to_home(self, page):
        page.locator('#open').set_input_files(str(FWSC_PATH))
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

    def test_sys_page_opens_via_long_home(self):
        """Long-press HOME opens the SYS (settings) page."""
        page = self.new_emu_page()
        try:
            self._boot_to_home(page)
            self.screenshot(page, 'sys-00-home')

            ctrl = self.controls(page)
            ctrl.press_button('home', hold_ms=600, delay_ms=500)
            page.wait_for_timeout(500)
            self.screenshot(page, 'sys-01-opened')

            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)
        finally:
            self.write_log(page, 'sys-menu-open.log')
            page.close()

    def test_sys_tab_switching(self):
        """ALGO encoder switches tabs in the settings menu."""
        page = self.new_emu_page()
        try:
            self._boot_to_home(page)
            ctrl = self.controls(page)

            # Open settings
            ctrl.press_button('home', hold_ms=600, delay_ms=500)
            page.wait_for_timeout(500)
            self.screenshot(page, 'sys-tabs-00-first')

            # Switch to next tab
            ctrl.rotate_encoder('algorithm', direction=1, steps=1, delay_ms=300)
            page.wait_for_timeout(300)
            self.screenshot(page, 'sys-tabs-01-second')

            # Switch to next tab
            ctrl.rotate_encoder('algorithm', direction=1, steps=1, delay_ms=300)
            page.wait_for_timeout(300)
            self.screenshot(page, 'sys-tabs-02-third')

            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)
        finally:
            self.write_log(page, 'sys-menu-tabs.log')
            page.close()

    def test_sys_value_cycling(self):
        """SELECT encoder cycles the value of the selected row."""
        page = self.new_emu_page()
        try:
            self._boot_to_home(page)
            ctrl = self.controls(page)

            # Open settings
            ctrl.press_button('home', hold_ms=600, delay_ms=500)
            page.wait_for_timeout(500)
            self.screenshot(page, 'sys-value-00-before')

            # Cycle value with SELECT encoder
            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=300)
            page.wait_for_timeout(300)
            self.screenshot(page, 'sys-value-01-after')

            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)
        finally:
            self.write_log(page, 'sys-menu-value.log')
            page.close()

    def test_sys_home_returns(self):
        """Tapping HOME exits settings and returns to previous page."""
        page = self.new_emu_page()
        try:
            self._boot_to_home(page)
            ctrl = self.controls(page)

            # Open settings
            ctrl.press_button('home', hold_ms=600, delay_ms=500)
            page.wait_for_timeout(500)
            self.screenshot(page, 'sys-return-00-settings')

            # Tap HOME to return
            ctrl.press_button('home', delay_ms=500)
            page.wait_for_timeout(500)
            self.screenshot(page, 'sys-return-01-home')

            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)
        finally:
            self.write_log(page, 'sys-menu-return.log')
            page.close()


if __name__ == '__main__':
    unittest.main()
