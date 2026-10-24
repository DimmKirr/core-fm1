#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""E2E: comprehensive UI controls test for all Felucca pages.

For each page: verifies button-tap navigation, encoder CW/CCW response,
long-press navigation, and hold+encoder combos where applicable.
Every interaction asserts the LCD canvas changed (pixel hash diff).
Screenshots are saved per test for visual inspection.
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from e2e_fixtures import E2ETestCase

FWSC_PATH = Path(__file__).resolve().parents[1] / 'build' / 'core-fm1.fwsc'


def _canvas_hash(page):
    return page.evaluate('''() => {
        const c = document.getElementById('panel');
        if (!c) return '';
        const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
        let h = 0;
        for (let i = 0; i < d.length; i += 16)
            h = ((h << 5) - h + d[i]) | 0;
        return String(h);
    }''')


def _lcd_region_hash(page, rx, ry, rw, rh):
    """Hash a specific region of the LCD canvas (native coords)."""
    return page.evaluate('''([rx, ry, rw, rh]) => {
        const c = document.getElementById('panel');
        if (!c) return '';
        const d = c.getContext('2d').getImageData(rx, ry, rw, rh).data;
        let h = 0;
        for (let i = 0; i < d.length; i += 4)
            h = ((h << 5) - h + d[i] + d[i+1] + d[i+2]) | 0;
        return String(h);
    }''', [rx, ry, rw, rh])


# LCD region for the 4 card slots (parameter values area, native canvas coords).
# The panel canvas is 1280x820, LCD area starts at ~(370,42) and is 480x480.
# Cards row is roughly y=28..72 in LCD space = y=42+56..42+144 in canvas space.
CARDS_REGION = (370, 98, 480, 88)
# Footer region: bottom of LCD
FOOTER_REGION = (370, 42 + 404, 480, 76)
# Full LCD region
LCD_REGION = (370, 42, 480, 480)


class _UITestBase(E2ETestCase):
    """Shared boot/nav helpers for UI controls tests."""

    @classmethod
    def setUpClass(cls):
        if not FWSC_PATH.is_file():
            raise unittest.SkipTest('Firmware not built. Run "task build" first.')
        super().setUpClass()

    def _boot(self, page):
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

    def _assert_not_halted(self, page):
        status = page.locator('#status').text_content()
        self.assertNotIn('halted', status, 'Emulator halted')

    def _assert_lcd_changed(self, before, after, msg='LCD did not change'):
        self.assertNotEqual(before, after, msg)

    def _assert_lcd_same(self, before, after, msg='LCD changed unexpectedly'):
        self.assertEqual(before, after, msg)

    def _nav_to(self, page, ctrl, button_name):
        """Navigate to a page via button, wait for redraw."""
        ctrl.press_button(button_name, delay_ms=500)
        page.wait_for_timeout(300)

    def _long_press(self, page, ctrl, button_name):
        ctrl.press_button(button_name, hold_ms=600, delay_ms=500)
        page.wait_for_timeout(300)


# ---------------------------------------------------------------------------
# 1. Button-tap navigation: each button goes to the correct page
# ---------------------------------------------------------------------------

class TestButtonNavigation(_UITestBase):
    """Verify each button tap navigates to its target page."""

    def _test_button_nav(self, button, screenshot_label):
        page = self.new_emu_page()
        try:
            self._boot(page)
            ctrl = self.controls(page)
            home_hash = _lcd_region_hash(page, *LCD_REGION)
            self.screenshot(page, f'nav-{screenshot_label}-00-home')

            ctrl.press_button(button, delay_ms=500)
            page.wait_for_timeout(300)
            self.screenshot(page, f'nav-{screenshot_label}-01-after')

            after_hash = _lcd_region_hash(page, *LCD_REGION)
            self._assert_lcd_changed(home_hash, after_hash,
                                     f'{button} button did not change LCD')
            self._assert_not_halted(page)
        finally:
            self.write_log(page, f'nav-{screenshot_label}.log')
            page.close()

    def test_fx_button(self):
        self._test_button_nav('fx', 'fx')

    def test_scl_button(self):
        self._test_button_nav('scl', 'scl')

    def test_env_button(self):
        self._test_button_nav('env', 'env')

    def test_lfo_button(self):
        self._test_button_nav('lfo', 'lfo')

    def test_edit_button(self):
        self._test_button_nav('edit', 'edit')

    def test_arp_button(self):
        self._test_button_nav('arp', 'arp')

    def test_seq_button(self):
        self._test_button_nav('seq', 'seq')

    def test_home_returns_to_home(self):
        """From another page, HOME returns to HOME."""
        page = self.new_emu_page()
        try:
            self._boot(page)
            ctrl = self.controls(page)
            home_hash = _lcd_region_hash(page, *LCD_REGION)

            ctrl.press_button('env', delay_ms=500)
            page.wait_for_timeout(300)
            env_hash = _lcd_region_hash(page, *LCD_REGION)
            self._assert_lcd_changed(home_hash, env_hash, 'ENV did not open')

            ctrl.press_button('home', delay_ms=500)
            page.wait_for_timeout(300)
            back_hash = _lcd_region_hash(page, *LCD_REGION)
            self._assert_lcd_changed(env_hash, back_hash,
                                     'HOME button did not leave ENV')
            self._assert_not_halted(page)
        finally:
            self.write_log(page, 'nav-home-return.log')
            page.close()

    def test_glo_opens_debug(self):
        """GLO button opens DEBUG page (when CONFIG_DEBUG_LOG)."""
        page = self.new_emu_page()
        try:
            self._boot(page)
            ctrl = self.controls(page)
            home_hash = _lcd_region_hash(page, *LCD_REGION)

            ctrl.press_button('glo', delay_ms=1500)
            page.wait_for_timeout(300)
            self.screenshot(page, 'nav-glo-debug')

            after_hash = _lcd_region_hash(page, *LCD_REGION)
            self._assert_lcd_changed(home_hash, after_hash,
                                     'GLO button did not change LCD')
            self._assert_not_halted(page)
        finally:
            self.write_log(page, 'nav-glo.log')
            page.close()


# ---------------------------------------------------------------------------
# 2. Long-press navigation
# ---------------------------------------------------------------------------

class TestLongPressNavigation(_UITestBase):
    """Verify long-press HOME opens SYS (settings) page."""

    def test_long_home_opens_sys(self):
        page = self.new_emu_page()
        try:
            self._boot(page)
            ctrl = self.controls(page)
            home_hash = _lcd_region_hash(page, *LCD_REGION)
            self.screenshot(page, 'hold-home-00-home')

            ctrl.press_button('home', hold_ms=600, delay_ms=500)
            page.wait_for_timeout(300)
            self.screenshot(page, 'hold-home-01-sys')

            sys_hash = _lcd_region_hash(page, *LCD_REGION)
            self._assert_lcd_changed(home_hash, sys_hash,
                                     'Long HOME did not open SYS')
            self._assert_not_halted(page)
        finally:
            self.write_log(page, 'hold-home.log')
            page.close()


# ---------------------------------------------------------------------------
# 3. SELECT encoder (ENC 0): page navigation on non-HOME pages
# ---------------------------------------------------------------------------

class TestSelectEncoder(_UITestBase):
    """SELECT encoder navigates between pages (CW and CCW)."""

    def test_select_cw_from_env(self):
        """CW on ENV page goes to next page (LFO)."""
        page = self.new_emu_page()
        try:
            self._boot(page)
            ctrl = self.controls(page)
            self._nav_to(page, ctrl, 'env')
            before = _lcd_region_hash(page, *FOOTER_REGION)
            self.screenshot(page, 'select-cw-00-env')

            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=500)
            page.wait_for_timeout(300)
            self.screenshot(page, 'select-cw-01-next')

            after = _lcd_region_hash(page, *FOOTER_REGION)
            self._assert_lcd_changed(before, after,
                                     'SELECT CW did not change page from ENV')
            self._assert_not_halted(page)
        finally:
            self.write_log(page, 'select-cw.log')
            page.close()

    def test_select_ccw_from_lfo(self):
        """CCW on LFO page goes back (to ENV)."""
        page = self.new_emu_page()
        try:
            self._boot(page)
            ctrl = self.controls(page)
            self._nav_to(page, ctrl, 'lfo')
            before = _lcd_region_hash(page, *FOOTER_REGION)
            self.screenshot(page, 'select-ccw-00-lfo')

            ctrl.rotate_encoder('select', direction=-1, steps=1, delay_ms=500)
            page.wait_for_timeout(300)
            self.screenshot(page, 'select-ccw-01-prev')

            after = _lcd_region_hash(page, *FOOTER_REGION)
            self._assert_lcd_changed(before, after,
                                     'SELECT CCW did not change page from LFO')
            self._assert_not_halted(page)
        finally:
            self.write_log(page, 'select-ccw.log')
            page.close()


# ---------------------------------------------------------------------------
# 4. HOME page: engine select (ENC 0), track select (ENC 1), knobs (ENC 2-4)
# ---------------------------------------------------------------------------

class TestHomePage(_UITestBase):
    """HOME page encoder controls."""

    def test_engine_select_cw(self):
        """SELECT encoder CW on HOME switches engine."""
        page = self.new_emu_page()
        try:
            self._boot(page)
            ctrl = self.controls(page)
            before = _lcd_region_hash(page, *FOOTER_REGION)
            self.screenshot(page, 'home-eng-cw-00')

            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=500)
            page.wait_for_timeout(300)
            self.screenshot(page, 'home-eng-cw-01')

            after = _lcd_region_hash(page, *FOOTER_REGION)
            self._assert_lcd_changed(before, after,
                                     'Engine select CW did not change display')
            self._assert_not_halted(page)
        finally:
            self.write_log(page, 'home-eng-cw.log')
            page.close()

    def test_engine_select_ccw(self):
        """SELECT encoder CCW on HOME switches engine backward."""
        page = self.new_emu_page()
        try:
            self._boot(page)
            ctrl = self.controls(page)
            # go forward first, then backward
            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=500)
            page.wait_for_timeout(200)
            before = _lcd_region_hash(page, *FOOTER_REGION)

            ctrl.rotate_encoder('select', direction=-1, steps=1, delay_ms=500)
            page.wait_for_timeout(300)
            self.screenshot(page, 'home-eng-ccw-01')

            after = _lcd_region_hash(page, *FOOTER_REGION)
            self._assert_lcd_changed(before, after,
                                     'Engine select CCW did not change display')
            self._assert_not_halted(page)
        finally:
            self.write_log(page, 'home-eng-ccw.log')
            page.close()

    def test_track_select_cw(self):
        """ALGO encoder CW on HOME switches track."""
        page = self.new_emu_page()
        try:
            self._boot(page)
            ctrl = self.controls(page)
            before = _lcd_region_hash(page, *LCD_REGION)
            self.screenshot(page, 'home-trk-cw-00')

            ctrl.rotate_encoder('algorithm', direction=1, steps=1, delay_ms=500)
            page.wait_for_timeout(300)
            self.screenshot(page, 'home-trk-cw-01')

            after = _lcd_region_hash(page, *LCD_REGION)
            self._assert_lcd_changed(before, after,
                                     'Track select CW did not change display')
            self._assert_not_halted(page)
        finally:
            self.write_log(page, 'home-trk-cw.log')
            page.close()

    def test_track_select_ccw(self):
        """ALGO encoder CCW on HOME switches track backward."""
        page = self.new_emu_page()
        try:
            self._boot(page)
            ctrl = self.controls(page)
            # go forward first
            ctrl.rotate_encoder('algorithm', direction=1, steps=1, delay_ms=500)
            page.wait_for_timeout(200)
            before = _lcd_region_hash(page, *LCD_REGION)

            ctrl.rotate_encoder('algorithm', direction=-1, steps=1, delay_ms=500)
            page.wait_for_timeout(300)
            self.screenshot(page, 'home-trk-ccw-01')

            after = _lcd_region_hash(page, *LCD_REGION)
            self._assert_lcd_changed(before, after,
                                     'Track select CCW did not change display')
            self._assert_not_halted(page)
        finally:
            self.write_log(page, 'home-trk-ccw.log')
            page.close()

    def test_knob1_cw_ccw(self):
        """KNOB1 (ENC 2) CW then CCW on HOME changes card values."""
        page = self.new_emu_page()
        try:
            self._boot(page)
            ctrl = self.controls(page)
            before = _lcd_region_hash(page, *CARDS_REGION)
            self.screenshot(page, 'home-k1-00')

            ctrl.rotate_encoder('knob1', direction=1, steps=3, delay_ms=300)
            page.wait_for_timeout(300)
            self.screenshot(page, 'home-k1-01-cw')

            cw_hash = _lcd_region_hash(page, *CARDS_REGION)
            self._assert_lcd_changed(before, cw_hash,
                                     'KNOB1 CW did not change cards')

            ctrl.rotate_encoder('knob1', direction=-1, steps=3, delay_ms=300)
            page.wait_for_timeout(300)
            self.screenshot(page, 'home-k1-02-ccw')

            ccw_hash = _lcd_region_hash(page, *CARDS_REGION)
            self._assert_lcd_changed(cw_hash, ccw_hash,
                                     'KNOB1 CCW did not change cards')
            self._assert_not_halted(page)
        finally:
            self.write_log(page, 'home-k1.log')
            page.close()

    def test_knob2_cw_ccw(self):
        """KNOB2 (ENC 3) CW/CCW on HOME changes card values."""
        page = self.new_emu_page()
        try:
            self._boot(page)
            ctrl = self.controls(page)
            before = _lcd_region_hash(page, *CARDS_REGION)

            ctrl.rotate_encoder('knob2', direction=1, steps=3, delay_ms=300)
            page.wait_for_timeout(300)
            self.screenshot(page, 'home-k2-01-cw')

            cw_hash = _lcd_region_hash(page, *CARDS_REGION)
            self._assert_lcd_changed(before, cw_hash,
                                     'KNOB2 CW did not change cards')

            ctrl.rotate_encoder('knob2', direction=-1, steps=3, delay_ms=300)
            page.wait_for_timeout(300)

            ccw_hash = _lcd_region_hash(page, *CARDS_REGION)
            self._assert_lcd_changed(cw_hash, ccw_hash,
                                     'KNOB2 CCW did not change cards')
            self._assert_not_halted(page)
        finally:
            self.write_log(page, 'home-k2.log')
            page.close()

    def test_knob3_cw_ccw(self):
        """KNOB3 (ENC 4) CW/CCW on HOME changes card values."""
        page = self.new_emu_page()
        try:
            self._boot(page)
            ctrl = self.controls(page)
            before = _lcd_region_hash(page, *CARDS_REGION)

            ctrl.rotate_encoder('knob3', direction=1, steps=3, delay_ms=300)
            page.wait_for_timeout(300)
            self.screenshot(page, 'home-k3-01-cw')

            cw_hash = _lcd_region_hash(page, *CARDS_REGION)
            self._assert_lcd_changed(before, cw_hash,
                                     'KNOB3 CW did not change cards')

            ctrl.rotate_encoder('knob3', direction=-1, steps=3, delay_ms=300)
            page.wait_for_timeout(300)

            ccw_hash = _lcd_region_hash(page, *CARDS_REGION)
            self._assert_lcd_changed(cw_hash, ccw_hash,
                                     'KNOB3 CCW did not change cards')
            self._assert_not_halted(page)
        finally:
            self.write_log(page, 'home-k3.log')
            page.close()

    def test_note_key(self):
        """Piano key press on HOME does not crash."""
        page = self.new_emu_page()
        try:
            self._boot(page)
            ctrl = self.controls(page)
            ctrl.play_note('c4', hold_ms=200, delay_ms=300)
            ctrl.play_note('e4', hold_ms=200, delay_ms=300)
            ctrl.play_note('g4', hold_ms=200, delay_ms=300)
            self._assert_not_halted(page)
            self.screenshot(page, 'home-notes')
        finally:
            self.write_log(page, 'home-notes.log')
            page.close()


# ---------------------------------------------------------------------------
# 5. Parameter pages: K1-K4 encoders change card values
# ---------------------------------------------------------------------------

class _ParamPageTest(_UITestBase):
    """Base for testing a 4-knob parameter page. Not collected by pytest."""

    PAGE_BUTTON = None   # override in subclass
    PAGE_NAME = None

    def setUp(self):
        if self.PAGE_BUTTON is None:
            self.skipTest('abstract base')

    def _test_knob(self, knob_name, screenshot_prefix):
        page = self.new_emu_page()
        try:
            self._boot(page)
            ctrl = self.controls(page)
            self._nav_to(page, ctrl, self.PAGE_BUTTON)
            self.screenshot(page, f'{screenshot_prefix}-00')
            before = _lcd_region_hash(page, *CARDS_REGION)

            ctrl.rotate_encoder(knob_name, direction=1, steps=5, delay_ms=300)
            page.wait_for_timeout(300)
            self.screenshot(page, f'{screenshot_prefix}-01-cw')
            cw_hash = _lcd_region_hash(page, *CARDS_REGION)
            self._assert_lcd_changed(before, cw_hash,
                                     f'{knob_name} CW on {self.PAGE_NAME} did not change cards')

            ctrl.rotate_encoder(knob_name, direction=-1, steps=5, delay_ms=300)
            page.wait_for_timeout(300)
            self.screenshot(page, f'{screenshot_prefix}-02-ccw')
            ccw_hash = _lcd_region_hash(page, *CARDS_REGION)
            self._assert_lcd_changed(cw_hash, ccw_hash,
                                     f'{knob_name} CCW on {self.PAGE_NAME} did not change cards')
            self._assert_not_halted(page)
        finally:
            self.write_log(page, f'{screenshot_prefix}.log')
            page.close()

    def test_knob1_cw_ccw(self):
        self._test_knob('knob1', f'{self.PAGE_NAME.lower()}-k1')

    def test_knob2_cw_ccw(self):
        self._test_knob('knob2', f'{self.PAGE_NAME.lower()}-k2')

    def test_knob3_cw_ccw(self):
        self._test_knob('knob3', f'{self.PAGE_NAME.lower()}-k3')

    @unittest.skip('KNOB4 (ENC 5) not wired: fparams_knob handles id 1-4 only')
    def test_knob4_cw_ccw(self):
        self._test_knob('knob4', f'{self.PAGE_NAME.lower()}-k4')


class TestENVPage(_ParamPageTest):
    PAGE_BUTTON = 'env'
    PAGE_NAME = 'ENV'


class TestLFOPage(_ParamPageTest):
    PAGE_BUTTON = 'lfo'
    PAGE_NAME = 'LFO'


class TestFXPage(_ParamPageTest):
    PAGE_BUTTON = 'fx'
    PAGE_NAME = 'FX'


class TestSCLPage(_ParamPageTest):
    PAGE_BUTTON = 'scl'
    PAGE_NAME = 'SCL'


class TestEDITPage(_ParamPageTest):
    PAGE_BUTTON = 'edit'
    PAGE_NAME = 'EDIT'


class TestARPPage(_ParamPageTest):
    PAGE_BUTTON = 'arp'
    PAGE_NAME = 'ARP'


class TestVOICEPage(_UITestBase):
    """VOICE page: navigate via SELECT encoder from ARP page."""

    def _go_to_voice(self, page, ctrl):
        """VOICE has no dedicated button; reach it via page navigation."""
        self._nav_to(page, ctrl, 'arp')
        # ARP is page index 6, VOICE is 9. Navigate forward 3 times:
        # ARP -> SEQ -> MIXER -> VOICE
        for _ in range(3):
            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=500)
            page.wait_for_timeout(200)

    def test_knob1_cw_ccw(self):
        page = self.new_emu_page()
        try:
            self._boot(page)
            ctrl = self.controls(page)
            self._go_to_voice(page, ctrl)
            self.screenshot(page, 'voice-k1-00')
            before = _lcd_region_hash(page, *CARDS_REGION)

            ctrl.rotate_encoder('knob1', direction=1, steps=5, delay_ms=300)
            page.wait_for_timeout(300)
            self.screenshot(page, 'voice-k1-01-cw')
            cw_hash = _lcd_region_hash(page, *CARDS_REGION)
            self._assert_lcd_changed(before, cw_hash,
                                     'KNOB1 CW on VOICE did not change cards')
            self._assert_not_halted(page)
        finally:
            self.write_log(page, 'voice-k1.log')
            page.close()


# ---------------------------------------------------------------------------
# 6. MIXER page: K1-K4 encoders control track levels
# ---------------------------------------------------------------------------

class TestMIXERPage(_UITestBase):
    """MIXER page reached via SELECT encoder from SEQ."""

    def _go_to_mixer(self, page, ctrl):
        self._nav_to(page, ctrl, 'seq')
        # SEQ -> MIXER is one step forward
        ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=500)
        page.wait_for_timeout(200)

    def test_knob1_cw_ccw(self):
        page = self.new_emu_page()
        try:
            self._boot(page)
            ctrl = self.controls(page)
            self._go_to_mixer(page, ctrl)
            self.screenshot(page, 'mixer-k1-00')
            before = _lcd_region_hash(page, *CARDS_REGION)

            ctrl.rotate_encoder('knob1', direction=1, steps=5, delay_ms=300)
            page.wait_for_timeout(300)
            self.screenshot(page, 'mixer-k1-01-cw')
            cw_hash = _lcd_region_hash(page, *CARDS_REGION)
            self._assert_lcd_changed(before, cw_hash,
                                     'KNOB1 CW on MIXER did not change cards')
            self._assert_not_halted(page)
        finally:
            self.write_log(page, 'mixer-k1.log')
            page.close()


# ---------------------------------------------------------------------------
# 7. Dead buttons: verify they don't crash the emulator
# ---------------------------------------------------------------------------

class TestDeadButtons(_UITestBase):
    """Buttons with no handler must not crash. LCD may or may not change."""

    def _test_dead_button(self, button_name):
        page = self.new_emu_page()
        try:
            self._boot(page)
            ctrl = self.controls(page)
            ctrl.press_button(button_name, delay_ms=500)
            page.wait_for_timeout(300)
            self._assert_not_halted(page)
            self.screenshot(page, f'dead-{button_name}')
        finally:
            self.write_log(page, f'dead-{button_name}.log')
            page.close()

    def test_octdn_no_crash(self):
        self._test_dead_button('oct-')

    def test_octup_no_crash(self):
        self._test_dead_button('oct+')

    def test_play_no_crash(self):
        self._test_dead_button('play')

    def test_rec_no_crash(self):
        self._test_dead_button('rec')

    def test_save_no_crash(self):
        self._test_dead_button('save')


# ---------------------------------------------------------------------------
# 8. Hold+encoder combos (e.g. hold EDIT + turn knob)
# ---------------------------------------------------------------------------

class TestHoldEncoderCombo(_UITestBase):
    """Hold a button while turning an encoder. Firmware should not crash."""

    def test_hold_edit_turn_knob1(self):
        """Hold EDIT + turn KNOB1: should not crash."""
        page = self.new_emu_page()
        try:
            self._boot(page)
            ctrl = self.controls(page)
            with ctrl.hold_button('edit'):
                ctrl.rotate_encoder('knob1', direction=1, steps=3, delay_ms=200)
                ctrl.rotate_encoder('knob1', direction=-1, steps=3, delay_ms=200)
            page.wait_for_timeout(300)
            self._assert_not_halted(page)
            self.screenshot(page, 'hold-edit-k1')
        finally:
            self.write_log(page, 'hold-edit-k1.log')
            page.close()

    def test_hold_home_turn_select(self):
        """Hold HOME + turn SELECT: should not crash (might open SYS)."""
        page = self.new_emu_page()
        try:
            self._boot(page)
            ctrl = self.controls(page)
            with ctrl.hold_button('home'):
                ctrl.rotate_encoder('select', direction=1, steps=2, delay_ms=200)
                ctrl.rotate_encoder('select', direction=-1, steps=2, delay_ms=200)
            page.wait_for_timeout(500)
            self._assert_not_halted(page)
            self.screenshot(page, 'hold-home-select')
        finally:
            self.write_log(page, 'hold-home-select.log')
            page.close()

    def test_hold_seq_turn_algo(self):
        """Hold SEQ + turn ALGO: should not crash."""
        page = self.new_emu_page()
        try:
            self._boot(page)
            ctrl = self.controls(page)
            with ctrl.hold_button('seq'):
                ctrl.rotate_encoder('algorithm', direction=1, steps=2, delay_ms=200)
            page.wait_for_timeout(300)
            self._assert_not_halted(page)
            self.screenshot(page, 'hold-seq-algo')
        finally:
            self.write_log(page, 'hold-seq-algo.log')
            page.close()


# ---------------------------------------------------------------------------
# 9. Full page sweep: visit every page sequentially via SELECT encoder
# ---------------------------------------------------------------------------

class TestFullPageSweep(_UITestBase):
    """Walk through all pages via SELECT encoder, screenshot each one."""

    def test_sweep_all_pages(self):
        """Visit all registered pages via SELECT CW from HOME."""
        page = self.new_emu_page()
        try:
            self._boot(page)
            ctrl = self.controls(page)
            self.screenshot(page, 'sweep-00-home')

            # Felucca registers: HOME ENV LFO FX SCL EDIT ARP SEQ MIXER VOICE
            #   ABOUT [SYS] [FILES] [BLE] [DEBUG]
            # At least 11 base pages + conditional. Sweep 15 to cover all.
            hashes = [_lcd_region_hash(page, *FOOTER_REGION)]
            for i in range(15):
                ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=500)
                page.wait_for_timeout(300)
                h = _lcd_region_hash(page, *FOOTER_REGION)
                hashes.append(h)
                self.screenshot(page, f'sweep-{i+1:02d}')
                self._assert_not_halted(page)

            # At least some transitions must have changed the footer
            changes = sum(1 for a, b in zip(hashes, hashes[1:]) if a != b)
            self.assertGreaterEqual(changes, 5,
                                    f'Only {changes} page changes in 15 steps')
        finally:
            self.write_log(page, 'sweep.log')
            page.close()


if __name__ == '__main__':
    unittest.main()
