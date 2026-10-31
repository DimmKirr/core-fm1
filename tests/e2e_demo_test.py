#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""E2E: DEMO view kitchen-sink UI showcase.

Long-FX opens VIEW_DEMO. SELECT cycles 5 screens (CARDS, LIST, GRAPH, TEXT,
EDIT). K1-K4 adjust card values. Piano keys update the note display.
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from e2e_fixtures import E2ETestCase

FWSC = Path(__file__).resolve().parents[1] / 'build' / 'core-fm1.fwsc'

LCD_REGION = (370, 42, 480, 480)
LCD_MIN_PIXELS = 500


class TestDemoView(E2ETestCase):
    """Exercise every screen of the DEMO view."""

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

    def _assert_alive(self, page):
        status = page.locator('#status').text_content()
        self.assertNotIn('halted', status, f'Emulator halted: {status}')

    def _verify_page(self, page, name):
        self.screenshot(page, name)
        pixels = self._lcd_pixel_count(page)
        self.assertGreater(pixels, LCD_MIN_PIXELS,
                           f'{name}: LCD too empty ({pixels} non-black pixels)')
        self._assert_alive(page)
        print(f'  {name}: {pixels} pixels OK')

    def _open_demo(self, page):
        """Long-press FX to open the DEMO view."""
        ctrl = self.controls(page)
        ctrl.press_button('fx', hold_ms=600, delay_ms=500)
        page.wait_for_timeout(500)

    # ------------------------------------------------------------------

    def test_01_demo_opens_via_long_fx(self):
        """Long-FX opens the DEMO view (CARDS screen)."""
        page = self._boot()
        try:
            self._open_demo(page)
            self._verify_page(page, 'DEMO-cards-initial')
        finally:
            self.write_log(page, 'demo-open.log')
            page.close()

    def test_02_demo_all_screens(self):
        """SELECT cycles through all 5 demo screens."""
        page = self._boot()
        try:
            ctrl = self.controls(page)
            self._open_demo(page)
            self._verify_page(page, 'DEMO-screen0-cards')

            names = ['cards', 'list', 'graph', 'text', 'edit']
            for i in range(1, 5):
                ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=200)
                page.wait_for_timeout(500)
                self._verify_page(page, f'DEMO-screen{i}-{names[i]}')
        finally:
            self.write_log(page, 'demo-screens.log')
            page.close()

    def test_03_demo_list_scroll(self):
        """K1 encoder scrolls the list screen."""
        page = self._boot()
        try:
            ctrl = self.controls(page)
            self._open_demo(page)

            # go to LIST screen (screen 1)
            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=200)
            page.wait_for_timeout(500)
            self._verify_page(page, 'DEMO-list-top')

            # scroll down several items via K1
            for _ in range(8):
                ctrl.rotate_encoder('knob1', direction=1, steps=1, delay_ms=100)
            page.wait_for_timeout(300)
            self._verify_page(page, 'DEMO-list-scrolled')

            # scroll back up
            for _ in range(8):
                ctrl.rotate_encoder('knob1', direction=-1, steps=1, delay_ms=100)
            page.wait_for_timeout(300)
            self._verify_page(page, 'DEMO-list-back-top')
        finally:
            self.write_log(page, 'demo-list.log')
            page.close()

    def test_04_demo_graph_changes_on_interaction(self):
        """GRAPH screen content changes after K1 encoder interaction."""
        page = self._boot()
        try:
            ctrl = self.controls(page)
            self._open_demo(page)

            # go to GRAPH screen (screen 2)
            ctrl.rotate_encoder('select', direction=1, steps=2, delay_ms=200)
            page.wait_for_timeout(500)
            self._verify_page(page, 'DEMO-graph-frame1')

            # capture pixels before interaction (sample every 4th byte for density)
            lx, ly, lw, lh = LCD_REGION
            snap1 = page.evaluate(f'''() => {{
                const c = document.getElementById('panel');
                return Array.from(
                    c.getContext('2d').getImageData({lx}, {ly}, {lw}, {lh}).data
                        .filter((_, i) => i % 4 === 0));
            }}''')

            # trigger redraws via K1 encoder (changes card value + advances frame)
            for _ in range(5):
                ctrl.rotate_encoder('knob1', direction=1, steps=1, delay_ms=100)
            page.wait_for_timeout(500)

            snap2 = page.evaluate(f'''() => {{
                const c = document.getElementById('panel');
                return Array.from(
                    c.getContext('2d').getImageData({lx}, {ly}, {lw}, {lh}).data
                        .filter((_, i) => i % 4 === 0));
            }}''')
            self._verify_page(page, 'DEMO-graph-frame2')

            diff = sum(1 for a, b in zip(snap1, snap2) if abs(a - b) > 10)
            print(f'  Graph diff after interaction: {diff} pixels changed')
            self.assertGreater(diff, 10, 'Graph screen did not change after interaction')
        finally:
            self.write_log(page, 'demo-graph.log')
            page.close()

    def test_05_demo_piano_keys(self):
        """Piano keys update the note display on CARDS screen."""
        page = self._boot()
        try:
            ctrl = self.controls(page)
            self._open_demo(page)
            self._verify_page(page, 'DEMO-cards-before-note')

            # play a note
            ctrl.play_note('c4', hold_ms=200, delay_ms=300)
            self._verify_page(page, 'DEMO-cards-after-note')
        finally:
            self.write_log(page, 'demo-keys.log')
            page.close()

    def test_06_demo_screen_wrap(self):
        """SELECT wraps from last screen back to first."""
        page = self._boot()
        try:
            ctrl = self.controls(page)
            self._open_demo(page)

            # go to EDIT (screen 4), then one more -> wraps to CARDS (screen 0)
            ctrl.rotate_encoder('select', direction=1, steps=4, delay_ms=200)
            page.wait_for_timeout(300)
            self._verify_page(page, 'DEMO-edit-before-wrap')

            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=200)
            page.wait_for_timeout(500)
            self._verify_page(page, 'DEMO-cards-after-wrap')

            # also test backward wrap: from CARDS, go back -> EDIT
            ctrl.rotate_encoder('select', direction=-1, steps=1, delay_ms=200)
            page.wait_for_timeout(500)
            self._verify_page(page, 'DEMO-edit-backward-wrap')
        finally:
            self.write_log(page, 'demo-wrap.log')
            page.close()

    def test_07_demo_return_to_home(self):
        """Pressing HOME from DEMO returns to HOME view."""
        page = self._boot()
        try:
            ctrl = self.controls(page)
            self._open_demo(page)
            self._verify_page(page, 'DEMO-before-home')

            ctrl.press_button('home', delay_ms=400)
            page.wait_for_timeout(500)
            self._verify_page(page, 'HOME-after-demo')
        finally:
            self.write_log(page, 'demo-return.log')
            page.close()


if __name__ == '__main__':
    unittest.main()
