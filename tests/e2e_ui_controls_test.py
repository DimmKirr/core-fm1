#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""E2E: comprehensive UI controls test.

Loads the felucca_noise firmware (all pages enabled: HOME, ENV, LFO, FX,
SCL, EDIT, ARP, SEQ, MIXER, VOICE, ABOUT, SYS, FILES, BLE, DEBUG) and
systematically verifies every control path works without crash.

Tests mirror the Felucca control map (cfm1_controls.h):
  - Button taps navigate to the correct page
  - SELECT encoder cycles through all pages
  - Long-HOME opens the SYS (settings) page
  - ALGO encoder switches SYS tabs
  - PRESET encoder scrolls SYS rows
  - SELECT encoder cycles SYS values
  - KNOB1-4 adjust parameters on parameter pages
  - OCT+/- buttons work on HOME page
  - HOME tap always returns to HOME

Each test screenshots before/after and asserts no halt.
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from e2e_fixtures import E2ETestCase, _run_timestamp, RESULTS_DIR

ROOT = Path(__file__).resolve().parents[1]
FWSC = ROOT / 'build' / 'core-fm1.fwsc'

# Felucca control map: button -> page name (from cfm1_controls.h)
BUTTON_NAV = {
    'fx':   'FX',
    'scl':  'SCL',
    'env':  'ENV',
    'lfo':  'LFO',
    'edit': 'EDIT',
    'home': 'HOME',
    'arp':  'ARP',
    'seq':  'SEQ',
    'glo':  'DEBUG',   # GLO -> DEBUG when CONFIG_DEBUG_LOG
}

# Full page order (felucca_noise: SYS_MENU + UDISK + BLE + DEBUG_LOG)
ALL_PAGES = [
    'HOME', 'ENV', 'LFO', 'FX', 'SCL', 'EDIT', 'ARP', 'SEQ',
    'MIXER', 'VOICE', 'ABOUT', 'SYS', 'FILES', 'BLE', 'DEBUG',
]

# SYS menu tabs (felucca_noise: MIDI, SYSTEM, BLE)
SYS_TABS = ['MIDI', 'SYSTEM', 'BLE']


class TestUIControls(E2ETestCase):
    """Comprehensive UI controls verification."""

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

    # ------------------------------------------------------------------
    # Test 1: SELECT encoder sweeps through all pages
    # ------------------------------------------------------------------
    def test_01_select_encoder_sweeps_all_pages(self):
        """SELECT encoder visits every registered page without crash."""
        page = self._boot()
        try:
            ctrl = self.controls(page)
            self.screenshot(page, 'HOME-none-initial')

            for i in range(1, len(ALL_PAGES)):
                ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=200)
                page.wait_for_timeout(300)
                self._assert_alive(page)
                self.screenshot(page, f'{ALL_PAGES[i]}-select-rotateRight')

            # Wrap around back to HOME
            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=200)
            page.wait_for_timeout(300)
            self._assert_alive(page)
            self.screenshot(page, 'HOME-select-rotateRight')

            # Sweep backward through all
            for i in range(len(ALL_PAGES) - 1, -1, -1):
                ctrl.rotate_encoder('select', direction=-1, steps=1, delay_ms=200)
                page.wait_for_timeout(200)
                self._assert_alive(page)

            self.screenshot(page, 'HOME-select-rotateLeft')
        finally:
            self.write_log(page, 'select-sweep.log')
            page.close()

    # ------------------------------------------------------------------
    # Test 2: Every button navigates to its mapped page
    # ------------------------------------------------------------------
    def test_02_button_navigation(self):
        """Each button tap navigates to the correct page."""
        page = self._boot()
        try:
            ctrl = self.controls(page)

            for btn, expected_page in BUTTON_NAV.items():
                ctrl.press_button(btn, delay_ms=400)
                page.wait_for_timeout(300)
                self._assert_alive(page)
                self.screenshot(page, f'{expected_page}-{btn}-shortPress')

                # Return to HOME for next test
                if btn != 'home':
                    ctrl.press_button('home', delay_ms=400)
                    page.wait_for_timeout(200)
        finally:
            self.write_log(page, 'button-nav.log')
            page.close()

    # ------------------------------------------------------------------
    # Test 3: Long-HOME opens SYS, HOME tap returns
    # ------------------------------------------------------------------
    def test_03_long_home_opens_sys(self):
        """Long-press HOME opens settings, tap HOME returns."""
        page = self._boot()
        try:
            ctrl = self.controls(page)
            self.screenshot(page, 'HOME-none-initial')

            # Long-press HOME
            ctrl.press_button('home', hold_ms=600, delay_ms=500)
            page.wait_for_timeout(500)
            self._assert_alive(page)
            self.screenshot(page, 'SYS-home-longPress')

            # Tap HOME to return
            ctrl.press_button('home', delay_ms=500)
            page.wait_for_timeout(500)
            self._assert_alive(page)
            self.screenshot(page, 'HOME-home-shortPress')
        finally:
            self.write_log(page, 'long-home.log')
            page.close()

    # ------------------------------------------------------------------
    # Test 4: SYS page full interaction (tabs, rows, values)
    # ------------------------------------------------------------------
    def test_04_sys_menu_full_interaction(self):
        """SYS: ALGO switches tabs, PRESET scrolls rows, SELECT cycles values."""
        page = self._boot()
        try:
            ctrl = self.controls(page)

            # Open SYS
            ctrl.press_button('home', hold_ms=600, delay_ms=500)
            page.wait_for_timeout(500)
            self.screenshot(page, 'SYS-home-longPress')

            # Cycle through all tabs with ALGO encoder
            for i, tab in enumerate(SYS_TABS[1:], 1):
                ctrl.rotate_encoder('algorithm', direction=1, steps=1, delay_ms=300)
                page.wait_for_timeout(300)
                self._assert_alive(page)
                self.screenshot(page, 'SYS-algorithm-rotateRight')

            # Go back to first tab
            for _ in range(len(SYS_TABS) - 1):
                ctrl.rotate_encoder('algorithm', direction=-1, steps=1, delay_ms=200)
                page.wait_for_timeout(200)

            # Cycle value on MIDI tab (MIDI CH: 1->2->3)
            self.screenshot(page, 'SYS-algorithm-rotateLeft')
            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=300)
            page.wait_for_timeout(300)
            self.screenshot(page, 'SYS-select-rotateRight')
            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=300)
            page.wait_for_timeout(300)
            self.screenshot(page, 'SYS-select-rotateRight')

            # Cycle value backward
            ctrl.rotate_encoder('select', direction=-1, steps=1, delay_ms=300)
            page.wait_for_timeout(300)
            self.screenshot(page, 'SYS-select-rotateLeft')

            # Switch to SYSTEM tab, cycle USB MODE (MIDI->OTG)
            ctrl.rotate_encoder('algorithm', direction=1, steps=1, delay_ms=300)
            page.wait_for_timeout(300)
            self.screenshot(page, 'SYS-algorithm-rotateRight')
            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=300)
            page.wait_for_timeout(300)
            self.screenshot(page, 'SYS-select-rotateRight')
            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=300)
            page.wait_for_timeout(300)
            self.screenshot(page, 'SYS-select-rotateRight')

            # Switch to BLE tab, toggle BLUETOOTH (ON->OFF->ON)
            ctrl.rotate_encoder('algorithm', direction=1, steps=1, delay_ms=300)
            page.wait_for_timeout(300)
            self.screenshot(page, 'SYS-algorithm-rotateRight')
            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=300)
            page.wait_for_timeout(300)
            self.screenshot(page, 'SYS-select-rotateRight')
            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=300)
            page.wait_for_timeout(300)
            self.screenshot(page, 'SYS-select-rotateRight')

            self._assert_alive(page)
        finally:
            self.write_log(page, 'sys-full.log')
            page.close()

    # ------------------------------------------------------------------
    # Test 5: KNOB1-4 on parameter pages don't crash
    # ------------------------------------------------------------------
    def test_05_knobs_on_parameter_pages(self):
        """KNOB1-4 adjustments on every parameter page cause no crash."""
        page = self._boot()
        try:
            ctrl = self.controls(page)
            knob_names = ['knob1', 'knob2', 'knob3', 'knob4']
            test_pages = {
                'env': 'ENV', 'lfo': 'LFO', 'fx': 'FX',
                'scl': 'SCL', 'edit': 'EDIT', 'arp': 'ARP',
            }

            for btn, pname in test_pages.items():
                ctrl.press_button(btn, delay_ms=400)
                page.wait_for_timeout(300)

                # Turn each knob CW and CCW
                for knob in knob_names:
                    ctrl.rotate_encoder(knob, direction=1, steps=2, delay_ms=100)
                    ctrl.rotate_encoder(knob, direction=-1, steps=1, delay_ms=100)

                page.wait_for_timeout(200)
                self._assert_alive(page)
                self.screenshot(page, f'{pname}-knob4-rotateLeft')

            # Also test ALGO encoder on HOME (switches engine algorithm)
            ctrl.press_button('home', delay_ms=400)
            page.wait_for_timeout(300)
            ctrl.rotate_encoder('algorithm', direction=1, steps=1, delay_ms=300)
            page.wait_for_timeout(300)
            self._assert_alive(page)
            self.screenshot(page, 'HOME-algorithm-rotateRight')

            # PRESET encoder on HOME (switches preset)
            ctrl.rotate_encoder('presets', direction=1, steps=1, delay_ms=300)
            page.wait_for_timeout(300)
            self._assert_alive(page)
            self.screenshot(page, 'HOME-presets-rotateRight')
        finally:
            self.write_log(page, 'knobs.log')
            page.close()

    # ------------------------------------------------------------------
    # Test 6: OCT+/- buttons on HOME
    # ------------------------------------------------------------------
    def test_06_octave_buttons(self):
        """OCT+ and OCT- buttons work on HOME page."""
        page = self._boot()
        try:
            ctrl = self.controls(page)
            self.screenshot(page, 'HOME-none-initial')

            ctrl.press_button('oct+', delay_ms=400)
            page.wait_for_timeout(300)
            self._assert_alive(page)
            self.screenshot(page, 'HOME-octPlus-shortPress')

            ctrl.press_button('oct+', delay_ms=400)
            page.wait_for_timeout(300)
            self.screenshot(page, 'HOME-octPlus-shortPress')

            ctrl.press_button('oct-', delay_ms=400)
            page.wait_for_timeout(300)
            self._assert_alive(page)
            self.screenshot(page, 'HOME-octMinus-shortPress')
        finally:
            self.write_log(page, 'octave.log')
            page.close()

    # ------------------------------------------------------------------
    # Test 7: MIXER and VOICE pages with knobs
    # ------------------------------------------------------------------
    def test_07_mixer_voice_pages(self):
        """MIXER and VOICE pages respond to knobs without crash."""
        page = self._boot()
        try:
            ctrl = self.controls(page)

            # Navigate to MIXER (no direct button, use SELECT encoder)
            # HOME(0) -> ... -> MIXER(8): 8 clicks
            for _ in range(8):
                ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=150)
            page.wait_for_timeout(300)
            self._assert_alive(page)
            self.screenshot(page, 'MIXER-select-rotateRight')

            # Turn knobs on MIXER
            for knob in ['knob1', 'knob2', 'knob3', 'knob4']:
                ctrl.rotate_encoder(knob, direction=1, steps=2, delay_ms=100)
            page.wait_for_timeout(200)
            self._assert_alive(page)
            self.screenshot(page, 'MIXER-knob4-rotateRight')

            # One more click to VOICE
            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=200)
            page.wait_for_timeout(300)
            self._assert_alive(page)
            self.screenshot(page, 'VOICE-select-rotateRight')

            for knob in ['knob1', 'knob2', 'knob3', 'knob4']:
                ctrl.rotate_encoder(knob, direction=1, steps=2, delay_ms=100)
            page.wait_for_timeout(200)
            self._assert_alive(page)
            self.screenshot(page, 'VOICE-knob4-rotateRight')
        finally:
            self.write_log(page, 'mixer-voice.log')
            page.close()

    # ------------------------------------------------------------------
    # Test 8: PLAY and REC buttons (no nav, but must not crash)
    # ------------------------------------------------------------------
    def test_08_play_rec_buttons(self):
        """PLAY and REC buttons don't crash on any page."""
        page = self._boot()
        try:
            ctrl = self.controls(page)

            # Press on HOME
            ctrl.press_button('play', delay_ms=300)
            self._assert_alive(page)
            ctrl.press_button('rec', delay_ms=300)
            self._assert_alive(page)

            # Press on SEQ page
            ctrl.press_button('seq', delay_ms=400)
            page.wait_for_timeout(300)
            ctrl.press_button('play', delay_ms=300)
            self._assert_alive(page)
            ctrl.press_button('rec', delay_ms=300)
            self._assert_alive(page)
            self.screenshot(page, 'SEQ-rec-shortPress')

            # Press on ARP page
            ctrl.press_button('arp', delay_ms=400)
            page.wait_for_timeout(300)
            ctrl.press_button('play', delay_ms=300)
            self._assert_alive(page)
            self.screenshot(page, 'ARP-play-shortPress')
        finally:
            self.write_log(page, 'play-rec.log')
            page.close()

    # ------------------------------------------------------------------
    # Test 9: SAVE button (no nav in Felucca, must not crash)
    # ------------------------------------------------------------------
    def test_09_save_button(self):
        """SAVE button doesn't navigate or crash."""
        page = self._boot()
        try:
            ctrl = self.controls(page)
            ctrl.press_button('save', delay_ms=400)
            page.wait_for_timeout(300)
            self._assert_alive(page)
            self.screenshot(page, 'HOME-save-shortPress')
        finally:
            self.write_log(page, 'save.log')
            page.close()

    # ------------------------------------------------------------------
    # Test 10: Keyboard notes on HOME
    # ------------------------------------------------------------------
    def test_10_keyboard_notes(self):
        """Playing notes on HOME page triggers audio without crash."""
        page = self._boot()
        try:
            ctrl = self.controls(page)

            ctrl.play_note('c4', hold_ms=200, delay_ms=100)
            ctrl.play_note('e4', hold_ms=200, delay_ms=100)
            ctrl.play_note('g4', hold_ms=200, delay_ms=100)
            page.wait_for_timeout(300)
            self._assert_alive(page)
            self.screenshot(page, 'HOME-g4-noteOn')
        finally:
            self.write_log(page, 'notes.log')
            page.close()


if __name__ == '__main__':
    unittest.main()
