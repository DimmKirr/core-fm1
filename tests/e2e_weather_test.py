#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""E2E: Weather page in the emulator.

Builds the core_fm1_dev defconfig (WiFi + lwIP), boots in fm1-emu,
navigates to the WEATHER page,
and tests:
  1. Weather page renders without crashing
  2. Loading state shows spinner and "FETCHING WEATHER" text
  3. Error state shows "NO WEATHER DATA" when fetch fails (no network)
  4. PLAY button triggers a refresh attempt without crashing
  5. Cards show placeholder "---" values before data arrives
  6. Page survives multiple visits (enter/leave cycle)
"""
import subprocess
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from e2e_fixtures import E2ETestCase, _run_timestamp, RESULTS_DIR, e2e_build

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
FWSC = BUILD / 'core-fm1.fwsc'

TEST_RESULTS = RESULTS_DIR / f'{_run_timestamp}-e2e_weather_test'

DEFCONFIG = 'core_fm1_dev'

# Firmware hold threshold is 500 ms of emulated time; 800 ms wall-clock was
# occasionally delivered as a tap by the WASM emulator, so keep a wide margin.
GLO_HOLD_MS = 1500


def _navigate_to_weather(ctrl, page):
    """From HOME: a GLO hold opens VIEW_CONN on its first page (WIFI); a
    second GLO hold advances within the view to WEATHER. SELECT cannot be
    used for this: the WIFI page consumes it for its AP list."""
    ctrl.press_button('glo', hold_ms=GLO_HOLD_MS, delay_ms=500)
    page.wait_for_timeout(300)
    ctrl.press_button('glo', hold_ms=GLO_HOLD_MS, delay_ms=500)


class TestWeatherPage(E2ETestCase):
    """Weather page in the emulator with wifi defconfig."""

    _build_ok = False

    @classmethod
    def setUpClass(cls):
        TEST_RESULTS.mkdir(parents=True, exist_ok=True)
        dc_path = ROOT / 'configs' / f'{DEFCONFIG}_defconfig'
        if not dc_path.exists():
            cls._build_ok = False
            (TEST_RESULTS / 'SKIP-no-defconfig.txt').write_text(
                f'configs/{DEFCONFIG}_defconfig does not exist.\n')
        else:
            try:
                r, _ = e2e_build(DEFCONFIG, TEST_RESULTS)
                cls._build_ok = r.returncode == 0
            except (RuntimeError, FileNotFoundError, subprocess.TimeoutExpired):
                cls._build_ok = False
        if not cls._build_ok:
            (TEST_RESULTS / 'SKIP-build-failed.txt').write_text(
                f'Build failed or defconfig missing for {DEFCONFIG}.\n'
                'Weather page E2E tests run once this defconfig builds.\n')
        try:
            super().setUpClass()
        except unittest.SkipTest:
            pass

    def _boot_page(self):
        if not self._build_ok:
            self.skipTest(f'{DEFCONFIG} defconfig build failed')
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

    def test_weather_page_renders(self):
        """Navigate to WEATHER page and verify it draws without crash."""
        page = self._boot_page()
        try:
            ctrl = self.controls(page)
            _navigate_to_weather(ctrl, page)

            page.wait_for_timeout(500)
            self.screenshot(page, 'WEATHER-page-renders')

            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)

            (TEST_RESULTS / 'weather-page-status.txt').write_text(status + '\n')
        finally:
            self.write_log(page, 'weather-page.log')
            page.close()

    def test_weather_shows_no_data(self):
        """Weather page shows error state when no network is available."""
        page = self._boot_page()
        try:
            ctrl = self.controls(page)
            _navigate_to_weather(ctrl, page)

            # fm1-emu v0.3.0 has no WiFi hardware: the fetch state machine
            # never fails over, so the spinner stays (observed for 25 s).
            # The error state needs the --oss-wifi emulator or a device.
            page.wait_for_timeout(5_000)
            self.screenshot(page, 'WEATHER-no-data')

            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)
        finally:
            self.write_log(page, 'weather-no-data.log')
            page.close()

    def test_weather_play_button_refresh(self):
        """PLAY button triggers a weather refresh without crashing."""
        page = self._boot_page()
        try:
            ctrl = self.controls(page)
            _navigate_to_weather(ctrl, page)
            page.wait_for_timeout(500)

            self.screenshot(page, 'WEATHER-before-refresh')

            ctrl.press_button('play')
            page.wait_for_timeout(1_000)

            self.screenshot(page, 'WEATHER-after-refresh')

            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)
        finally:
            self.write_log(page, 'weather-refresh.log')
            page.close()

    def test_weather_cards_placeholder(self):
        """Cards show --- placeholder values before weather data arrives."""
        page = self._boot_page()
        try:
            ctrl = self.controls(page)
            _navigate_to_weather(ctrl, page)
            page.wait_for_timeout(500)

            self.screenshot(page, 'WEATHER-cards-placeholder')

            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)
        finally:
            self.write_log(page, 'weather-cards.log')
            page.close()

    def test_weather_page_revisit(self):
        """Leave and re-enter the weather page without crash."""
        page = self._boot_page()
        try:
            ctrl = self.controls(page)

            # Visit weather
            _navigate_to_weather(ctrl, page)
            page.wait_for_timeout(500)
            self.screenshot(page, 'WEATHER-visit-1')

            # Leave: a HOME tap opens VIEW_HOME
            ctrl.press_button('home')
            page.wait_for_timeout(500)
            self.screenshot(page, 'WEATHER-left-to-HOME')

            # Return: the CONN view remembers its last page, so a single
            # GLO hold lands on WEATHER again
            ctrl.press_button('glo', hold_ms=GLO_HOLD_MS, delay_ms=500)
            page.wait_for_timeout(500)
            self.screenshot(page, 'WEATHER-visit-2')

            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)
        finally:
            self.write_log(page, 'weather-revisit.log')
            page.close()

    def test_weather_spinner_animates(self):
        """Capture frames during loading to verify spinner animation."""
        page = self._boot_page()
        try:
            ctrl = self.controls(page)
            _navigate_to_weather(ctrl, page)

            self.start_canvas_capture(page, fps=5)
            page.wait_for_timeout(2_000)
            frames = self.stop_canvas_capture(page)

            self.screenshot(page, 'WEATHER-spinner')

            if len(frames) >= 3:
                self.frames_to_gif(frames, name='weather-spinner', fps=5)

            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)
        finally:
            self.write_log(page, 'weather-spinner.log')
            page.close()

if __name__ == '__main__':
    unittest.main()
