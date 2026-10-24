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
        elapsed_ms = page.evaluate('''(direction) => {
            return new Promise(resolve => {
                const canvas = document.getElementById('panel');
                const ctx = canvas.getContext('2d', {willReadFrequently: true});
                // Footer region on the LCD (bottom 80px of the 480px LCD area)
                const lx = 370, ly = 42 + 400, lw = 480, lh = 80;
                const before = ctx.getImageData(lx, ly, lw, lh).data.slice();
                const t0 = performance.now();
                // Send the encoder click via keyboard
                const key = direction > 0 ? 'ArrowRight' : 'ArrowLeft';
                canvas.tabIndex = 0;
                canvas.focus({preventScroll: true});
                canvas.dispatchEvent(new KeyboardEvent('keydown', {key, bubbles: true}));
                setTimeout(() => {
                    canvas.dispatchEvent(new KeyboardEvent('keyup', {key, bubbles: true}));
                }, 50);
                // Poll for LCD change
                function check() {
                    const after = ctx.getImageData(lx, ly, lw, lh).data;
                    let diff = 0;
                    for (let i = 0; i < after.length; i += 16) {
                        if (Math.abs(after[i] - before[i]) > 10 ||
                            Math.abs(after[i+1] - before[i+1]) > 10 ||
                            Math.abs(after[i+2] - before[i+2]) > 10) diff++;
                    }
                    if (diff > 50) {
                        resolve(performance.now() - t0);
                    } else if (performance.now() - t0 > 10000) {
                        resolve(-1); // timeout
                    } else {
                        setTimeout(check, 10);
                    }
                }
                setTimeout(check, 30);
            });
        }''', direction)
        return elapsed_ms

    def _go_to_page(self, page, button_name):
        """Navigate to a page via button press, wait for redraw."""
        ctrl = self.controls(page)
        ctrl.press_button(button_name, delay_ms=500)

    def test_page_forward_latency(self):
        """A single forward page change via select encoder must be fast."""
        page = self.new_emu_page(mhz=100)
        try:
            self._boot_to_home(page)
            # Go to ENV page (button press) where knob 0 is not consumed
            self._go_to_page(page, 'env')
            page.wait_for_timeout(500)
            self.screenshot(page, 'ENV-env-shortPress')

            ms = self._measure_page_change_ms(page, direction=1, label='fwd')
            page.wait_for_timeout(500)
            self.screenshot(page, 'LFO-select-rotateRight')

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
            # Go to LFO page, then backward to ENV
            self._go_to_page(page, 'lfo')
            page.wait_for_timeout(500)
            self.screenshot(page, 'LFO-lfo-shortPress')

            ms = self._measure_page_change_ms(page, direction=-1, label='bwd')
            page.wait_for_timeout(500)
            self.screenshot(page, 'ENV-select-rotateLeft')

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
            # Go to ENV, then sweep forward through LFO, FX, SCL, EDIT
            self._go_to_page(page, 'env')
            page.wait_for_timeout(500)

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
            self.screenshot(page, 'EDIT-select-rotateRight')

            self.assertLessEqual(
                worst, PAGE_NAV_MAX_MS,
                f'Worst sweep nav took {worst:.0f}ms (max {PAGE_NAV_MAX_MS}ms)')
        finally:
            self.write_log(page, 'page-nav-sweep.log')
            page.close()


if __name__ == '__main__':
    unittest.main()
