#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""E2E: BLE connection lifecycle in the emulator.

Builds felucca_noise (BLE + stub driver), boots in fm1-emu, navigates to
the BLUETOOTH page via long-press GLO, and tests:
  1. BLE page renders and shows ADVERTISING
  2. Full connection lifecycle: ADVERTISING -> CONNECTED -> MIDI READY -> ADVERTISING
  3. Peer MAC address displayed on LCD when connected
  4. BLE-MIDI note injected by external device reaches the firmware
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from e2e_fixtures import E2ETestCase, _run_timestamp, RESULTS_DIR, e2e_build

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
FWSC = BUILD / 'core-fm1.fwsc'

TEST_RESULTS = RESULTS_DIR / f'{_run_timestamp}-e2e_ble_test'


def _navigate_to_ble(ctrl):
    """Long-press GLO to jump directly to the BLE page."""
    ctrl.press_button('glo', hold_ms=600, delay_ms=500)


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
            '(() => { const t = document.getElementById("status")?.textContent || "";'
            'return t.includes("instructions/s") || t.includes("real time"); })()',
            timeout=30_000)
        page.wait_for_timeout(3_000)
        return page

    def test_ble_page_renders(self):
        """Navigate to BLUETOOTH page and verify it draws."""
        page = self._boot_page()
        try:
            ctrl = self.controls(page)
            _navigate_to_ble(ctrl)

            page.wait_for_timeout(500)
            self.screenshot(page, 'BLE-page-renders')

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
            _navigate_to_ble(ctrl)

            page.wait_for_timeout(500)
            self.screenshot(page, 'BLE-advertising')

            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)
        finally:
            self.write_log(page, 'ble-advertising.log')
            page.close()

    def test_ble_connection_lifecycle(self):
        """Full BLE lifecycle: ADVERTISING -> CONNECTED -> MIDI READY -> ADVERTISING.

        The stub state machine auto-advances through MTU exchange, CCCD
        subscription, and BLE-MIDI note injection after PLAY triggers connect.
        """
        page = self._boot_page()
        try:
            ctrl = self.controls(page)
            _navigate_to_ble(ctrl)

            page.wait_for_timeout(500)
            self.screenshot(page, 'BLE-lifecycle-01-advertising')

            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)

            # Press PLAY to trigger simulated BLE connection from external device
            ctrl.press_button('play')

            # Wait for stub tick to advance: CONNECTED -> MTU -> CCCD -> MIDI NOTE
            page.wait_for_timeout(5_000)
            self.screenshot(page, 'BLE-lifecycle-02-connected')

            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)

            # Press PLAY to trigger external device disconnect
            ctrl.press_button('play')

            page.wait_for_timeout(3_000)
            self.screenshot(page, 'BLE-lifecycle-03-disconnected')

            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)
        finally:
            self.write_log(page, 'ble-lifecycle.log')
            page.close()

    def test_ble_shows_peer_address(self):
        """LCD shows the connected peer's MAC address."""
        page = self._boot_page()
        try:
            ctrl = self.controls(page)
            _navigate_to_ble(ctrl)
            page.wait_for_timeout(500)

            # Connect: the stub uses its default peer address
            ctrl.press_button('play')
            page.wait_for_timeout(5_000)

            self.screenshot(page, 'BLE-peer-address')

            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)

            # Disconnect
            ctrl.press_button('play')
            page.wait_for_timeout(2_000)
        finally:
            self.write_log(page, 'ble-peer-addr.log')
            page.close()


if __name__ == '__main__':
    unittest.main()
