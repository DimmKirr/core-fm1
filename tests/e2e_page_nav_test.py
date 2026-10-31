#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""E2E: page navigation latency via the select encoder."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from e2e_fixtures import E2ETestCase

FWSC_PATH = Path(__file__).resolve().parents[1] / 'build' / 'core-fm1.fwsc'

# Max acceptable time (ms) for the LCD to show the new page after one encoder click.
# Emulator at 100MHz: typically 30-100ms. Budget allows 2x headroom.
PAGE_NAV_MAX_MS = 200


class TestPageNavLatency(E2ETestCase):
    """Measure page-change latency driven by the select encoder."""

    @classmethod
    def setUpClass(cls):
        if not FWSC_PATH.is_file():
            raise unittest.SkipTest('Firmware not built. Run "task build" first.')
        super().setUpClass()

    def _boot_to_home(self, page):
        """Load firmware and wait for the HOME page to appear."""
        page.locator('#open').set_input_files(str(FWSC_PATH))
        page.wait_for_function(
            'document.getElementById("status").textContent.includes("ready")')
        page.locator('#start').click()
        page.wait_for_function(
            'document.getElementById("status")?.textContent?.includes("instructions/s")',
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

    def _measure_page_change_ms(self, page, direction, label=''):
        """Turn the select encoder one click and return ms until LCD changes.

        Snapshots the footer region (bottom of LCD) before the click, then
        polls at ~10ms until it differs. Returns elapsed ms.
        """
        page.evaluate('''() => {
            const canvas = document.getElementById('panel');
            const ctx = canvas.getContext('2d', {willReadFrequently: true});
            const lx = 370, ly = 42, lw = 480, lh = 480;
            window._navBefore = ctx.getImageData(lx, ly, lw, lh).data.slice();
            window._navT0 = performance.now();
            window._navResult = null;
            function check() {
                const after = ctx.getImageData(lx, ly, lw, lh).data;
                let diff = 0;
                for (let i = 0; i < after.length; i += 16) {
                    if (Math.abs(after[i] - window._navBefore[i]) > 10 ||
                        Math.abs(after[i+1] - window._navBefore[i+1]) > 10 ||
                        Math.abs(after[i+2] - window._navBefore[i+2]) > 10) diff++;
                }
                if (diff > 50) {
                    window._navResult = performance.now() - window._navT0;
                } else if (performance.now() - window._navT0 > 10000) {
                    window._navResult = -1;
                } else {
                    setTimeout(check, 10);
                }
            }
            setTimeout(check, 30);
        }''')
        ctrl = self.controls(page)
        ctrl.rotate_encoder('select', direction=direction, steps=1, delay_ms=0)
        page.wait_for_function('window._navResult !== null', timeout=15_000)
        return page.evaluate('window._navResult')

    def _go_to_page(self, page, button_name):
        """Navigate to a page via button press, wait for redraw."""
        ctrl = self.controls(page)
        ctrl.press_button(button_name, delay_ms=500)

    def test_page_forward_latency(self):
        """A single forward page change via select encoder must be fast."""
        page = self.new_emu_page(mhz=100)
        try:
            self._boot_to_home(page)
            self.screenshot(page, 'HOME-none-initial')

            ms = self._measure_page_change_ms(page, direction=1, label='fwd')
            page.wait_for_timeout(500)
            self.screenshot(page, 'ENV-select-rotateRight')

            print(f'\n  Forward page nav: {ms:.0f}ms')
            self.assertGreater(ms, 0, 'LCD never changed after forward click')
            self.assertLessEqual(
                ms, PAGE_NAV_MAX_MS,
                f'Forward page nav took {ms:.0f}ms (max {PAGE_NAV_MAX_MS}ms)')
        finally:
            self.write_log(page, 'page-nav-fwd.log')
            page.close()

    def test_page_backward_latency(self):
        """A single backward page change via select encoder must be fast."""
        page = self.new_emu_page(mhz=100)
        try:
            self._boot_to_home(page)
            # Go forward one page first, then measure backward
            ctrl = self.controls(page)
            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=500)
            self.screenshot(page, 'ENV-select-rotateRight')

            ms = self._measure_page_change_ms(page, direction=-1, label='bwd')
            page.wait_for_timeout(500)
            self.screenshot(page, 'HOME-select-rotateLeft')

            print(f'\n  Backward page nav: {ms:.0f}ms')
            self.assertGreater(ms, 0, 'LCD never changed after backward click')
            self.assertLessEqual(
                ms, PAGE_NAV_MAX_MS,
                f'Backward page nav took {ms:.0f}ms (max {PAGE_NAV_MAX_MS}ms)')
        finally:
            self.write_log(page, 'page-nav-bwd.log')
            page.close()

    def test_multi_page_sweep_latency(self):
        """Sweep through several pages and verify each transition is fast."""
        page = self.new_emu_page(mhz=100)
        try:
            self._boot_to_home(page)

            timings = []
            for i in range(4):
                ms = self._measure_page_change_ms(page, direction=1,
                                                  label=f'sweep-{i}')
                page.wait_for_timeout(200)
                timings.append(ms)

            avg = sum(timings) / len(timings)
            worst = max(timings)
            print(f'\n  Sweep timings: {[f"{t:.0f}" for t in timings]}ms '
                  f'avg={avg:.0f}ms worst={worst:.0f}ms')
            self.screenshot(page, 'SCL-select-sweep')

            self.assertLessEqual(
                worst, PAGE_NAV_MAX_MS,
                f'Worst sweep nav took {worst:.0f}ms (max {PAGE_NAV_MAX_MS}ms)')
        finally:
            self.write_log(page, 'page-nav-sweep.log')
            page.close()


if __name__ == '__main__':
    unittest.main()
