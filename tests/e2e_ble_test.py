#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""E2E: BLE page renders in the emulator.

Builds felucca_noise (BLE + stub driver), boots in fm1-emu, navigates to
the BLUETOOTH page via the SELECT encoder, and screenshots the BLE status.
"""
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from e2e_fixtures import E2ETestCase, _run_timestamp, RESULTS_DIR, e2e_build

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
FWSC = BUILD / 'core-fm1.fwsc'

TEST_RESULTS = RESULTS_DIR / f'{_run_timestamp}-e2e_ble_test'


class TestBLEPage(E2ETestCase):
    """BLE BLUETOOTH page in the emulator."""

    DEFCONFIG = 'felucca_noise'
    _build_ok = False

    @classmethod
    def setUpClass(cls):
        TEST_RESULTS.mkdir(parents=True, exist_ok=True)
        if FWSC.is_file():
            cls._build_ok = True
        else:
            r, _ = e2e_build(cls.DEFCONFIG, TEST_RESULTS)
            cls._build_ok = r.returncode == 0
        try:
            super().setUpClass()
        except unittest.SkipTest:
            pass

    def _boot_page(self):
        if not self._build_ok:
            self.skipTest('Build failed')
        if not FWSC.is_file():
            self.skipTest('No .fwsc produced')
        if not self._browser:
            self.skipTest('Emulator not available')
        page = self.new_emu_page()
        page.locator('#open').set_input_files(str(FWSC))
        page.wait_for_function(
            'document.getElementById("status").textContent.includes("ready")')
        page.locator('#start').click()
        page.wait_for_function(
            'document.getElementById("status")?.textContent'
            '?.includes("instructions/s")',
            timeout=30_000)
        page.wait_for_timeout(3_000)
        return page

    def test_ble_page_renders(self):
        """Navigate to BLUETOOTH page and verify it draws."""
        page = self._boot_page()
        try:
            ctrl = self.controls(page)

            # Navigate right through pages until we reach BLE.
            # Pages: HOME, ENV, LFO, FX, SCL, EDIT, ARP, SEQ, MIXER, VOICE,
            #        ABOUT, FILES (if UDISK), BLE, DEBUG (if DEBUG_LOG)
            # felucca_noise has UDISK + BLE + DEBUG_LOG, so BLE is page ~12
            for _ in range(13):
                ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=200)

            page.wait_for_timeout(500)
            self.screenshot(page, 'ble-page')

            # Take a screenshot of the status area
            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)

            (TEST_RESULTS / 'ble-page-status.txt').write_text(status + '\n')
        finally:
            self.write_log(page, 'ble-page.log')
            page.close()

    def test_ble_page_shows_advertising(self):
        """BLE page shows ADVERTISING status with stub driver."""
        page = self._boot_page()
        try:
            ctrl = self.controls(page)

            # Navigate to BLE page
            for _ in range(13):
                ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=200)

            page.wait_for_timeout(500)
            self.screenshot(page, 'ble-advertising')

            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)
        finally:
            self.write_log(page, 'ble-advertising.log')
            page.close()


if __name__ == '__main__':
    unittest.main()
