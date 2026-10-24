#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""E2E: stuck notes and engine-switch bleed via audio frequency analysis.

Uses WebAudio AnalyserNode to check whether note frequencies persist after
release or engine switch. Two failure modes tested:
  1. Note-off does not silence the voice (stuck note)
  2. Switching engines leaves the old engine's voice audible (engine bleed)

Litmus tests verify the analyser pipeline itself: a held note must show
its fundamental frequency above the noise floor.
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from e2e_fixtures import E2ETestCase

FWSC_PATH = Path(__file__).resolve().parents[1] / 'build' / 'core-fm1.fwsc'

# Frequency constants (Hz) for the test notes.
# C2 (MIDI 36) after two oct-down from the default C4 key.
FREQ_C2 = 65.41
# C5 (MIDI 72) via the C5 key at default octave.
FREQ_C5 = 523.25

# dB threshold: power above this means the frequency is "present".
# AnalyserNode returns dB where -Infinity is silence, 0 is max.
PRESENT_DB = -60
SILENT_DB = -80

# Audio capture JS: monkey-patches AudioNode.connect to grab the context
# and worklet node reference before the emulator's main.js runs.
AUDIO_INTERCEPT_SCRIPT = """
(() => {
    const _origConnect = AudioNode.prototype.connect;
    AudioNode.prototype.connect = function(dest) {
        if (dest instanceof AudioDestinationNode) {
            window._fm1Ctx = dest.context;
            window._fm1Node = this;
        }
        return _origConnect.apply(this, arguments);
    };
})();
"""

# JS to set up the AnalyserNode (call after emulator audio is running).
ANALYSER_SETUP_JS = """() => {
    if (window._fm1Analyser) return true;
    const ac = window._fm1Ctx;
    const node = window._fm1Node;
    if (!ac || !node) return false;
    const a = ac.createAnalyser();
    a.fftSize = 8192;
    a.smoothingTimeConstant = 0;
    node.connect(a);
    window._fm1Analyser = a;
    return true;
}"""

# JS to read power (dB) at a given frequency from the AnalyserNode.
READ_FREQ_POWER_JS = """(freq) => {
    const a = window._fm1Analyser;
    if (!a) return -Infinity;
    const ac = window._fm1Ctx;
    const data = new Float32Array(a.frequencyBinCount);
    a.getFloatFrequencyData(data);
    const binWidth = ac.sampleRate / a.fftSize;
    const bin = Math.round(freq / binWidth);
    // Average over a few neighbouring bins for robustness.
    let peak = -Infinity;
    for (let i = Math.max(0, bin - 2); i <= Math.min(data.length - 1, bin + 2); i++) {
        if (data[i] > peak) peak = data[i];
    }
    return peak;
}"""

# JS to read the full spectrum (for diagnostics).
READ_SPECTRUM_JS = """() => {
    const a = window._fm1Analyser;
    if (!a) return null;
    const data = new Float32Array(a.frequencyBinCount);
    a.getFloatFrequencyData(data);
    // Return first 200 bins (~1kHz at 44100/8192 resolution).
    return Array.from(data.slice(0, 200));
}"""


class TestStuckNotes(E2ETestCase):
    """Verify notes stop sounding after release and engine switch."""

    @classmethod
    def setUpClass(cls):
        if not FWSC_PATH.is_file():
            raise unittest.SkipTest('Firmware not built. Run "task build" first.')
        super().setUpClass()

    def _open_with_audio_intercept(self, mhz=100):
        """Open emulator page with audio interception enabled."""
        page = self._browser.new_page()
        page._console_logs = []
        page.on('console', lambda msg: page._console_logs.append(
            f'[{msg.type}] {msg.text}'))
        page.on('pageerror', lambda err: page._console_logs.append(
            f'[pageerror] {err}'))
        page.add_init_script(AUDIO_INTERCEPT_SCRIPT)
        url = self.emu_url.replace('mhz=100', f'mhz={mhz}')
        page.goto(url)
        page.evaluate("async () => { await import('./main.js'); }")
        return page

    def _boot_and_setup_audio(self, page):
        """Load firmware, boot, wait for audio, set up analyser."""
        page.locator('#open').set_input_files(str(FWSC_PATH))
        page.wait_for_function(
            'document.getElementById("status").textContent.includes("ready")')
        page.locator('#start').click()
        page.wait_for_function(
            'document.getElementById("status")?.textContent?.includes("instructions/s")',
            timeout=30_000)
        # Wait for LCD content (firmware booted).
        page.wait_for_function('''() => {
            const c = document.getElementById('panel');
            const p = c.getContext('2d').getImageData(370, 42, 480, 480).data;
            let n = 0;
            for (let i = 0; i < p.length; i += 4)
                if (p[i] > 30 || p[i+1] > 30 || p[i+2] > 30) n++;
            return n > 500;
        }''', timeout=120_000)
        page.wait_for_timeout(2000)
        # Set up analyser node.
        ok = page.evaluate(ANALYSER_SETUP_JS)
        self.assertTrue(ok, 'Failed to set up audio analyser (no AudioContext?)')
        # Set master volume to max.
        ctrl = self.controls(page)
        ctrl.set_master_volume(1023)
        page.wait_for_timeout(200)

    def _freq_power(self, page, freq):
        """Return the peak power (dB) near a frequency."""
        return page.evaluate(READ_FREQ_POWER_JS, freq)

    def _octave_down(self, page, steps=1):
        """Press oct- button to shift keyboard octave down."""
        ctrl = self.controls(page)
        for _ in range(steps):
            ctrl.press_button('oct-', delay_ms=200)

    def _octave_up(self, page, steps=1):
        """Press oct+ button to shift keyboard octave up."""
        ctrl = self.controls(page)
        for _ in range(steps):
            ctrl.press_button('oct+', delay_ms=200)

    # ---- Litmus tests: verify the analyser pipeline works ----

    def test_01_litmus_low_note(self):
        """Litmus: a held C2 note shows power at ~65Hz."""
        page = self._open_with_audio_intercept()
        try:
            self._boot_and_setup_audio(page)
            ctrl = self.controls(page)
            # Shift octave down twice: C4 key -> C2.
            self._octave_down(page, 2)
            page.wait_for_timeout(300)
            # Hold C4 key (which now plays C2).
            ctrl._focus_panel()
            page.keyboard.down('a')  # C4 key
            page.wait_for_timeout(500)
            power = self._freq_power(page, FREQ_C2)
            page.keyboard.up('a')
            page.wait_for_timeout(100)
            self.screenshot(page, 'HOME-c2-hold')
            print(f'\n  Litmus C2 held: {power:.1f} dB (threshold {PRESENT_DB})')
            self.assertGreater(
                power, PRESENT_DB,
                f'C2 not audible while held ({power:.1f} dB < {PRESENT_DB} dB)')
        finally:
            self.write_log(page, 'litmus-low.log')
            page.close()

    def test_02_litmus_high_note(self):
        """Litmus: a held C5 note shows power at ~523Hz."""
        page = self._open_with_audio_intercept()
        try:
            self._boot_and_setup_audio(page)
            ctrl = self.controls(page)
            # C5 key at default octave.
            ctrl._focus_panel()
            page.keyboard.down('k')  # C5 key
            page.wait_for_timeout(500)
            power = self._freq_power(page, FREQ_C5)
            page.keyboard.up('k')
            page.wait_for_timeout(100)
            self.screenshot(page, 'HOME-c5-hold')
            print(f'\n  Litmus C5 held: {power:.1f} dB (threshold {PRESENT_DB})')
            self.assertGreater(
                power, PRESENT_DB,
                f'C5 not audible while held ({power:.1f} dB < {PRESENT_DB} dB)')
        finally:
            self.write_log(page, 'litmus-high.log')
            page.close()

    # ---- Stuck note test ----

    def test_03_note_release_silence(self):
        """After releasing a note, its frequency must go silent."""
        page = self._open_with_audio_intercept()
        try:
            self._boot_and_setup_audio(page)
            ctrl = self.controls(page)
            self._octave_down(page, 2)
            page.wait_for_timeout(300)
            # Play C2 for 500ms, then release.
            ctrl._focus_panel()
            page.keyboard.down('a')  # C2
            page.wait_for_timeout(500)
            held_power = self._freq_power(page, FREQ_C2)
            print(f'\n  C2 held: {held_power:.1f} dB')
            page.keyboard.up('a')
            # Wait for analyser buffer to flush (fftSize/sampleRate ~ 186ms)
            # plus release envelope time.
            page.wait_for_timeout(500)
            released_power = self._freq_power(page, FREQ_C2)
            self.screenshot(page, 'HOME-c2-noteOff')
            print(f'  C2 after release+500ms: {released_power:.1f} dB '
                  f'(threshold {SILENT_DB})')
            self.assertLess(
                released_power, SILENT_DB,
                f'Stuck note: C2 still audible after release '
                f'({released_power:.1f} dB >= {SILENT_DB} dB)')
        finally:
            self.write_log(page, 'note-release.log')
            page.close()

    # ---- Engine switch bleed test ----

    def test_04_engine_switch_no_bleed(self):
        """Switching engines must silence the old engine's notes."""
        page = self._open_with_audio_intercept()
        try:
            self._boot_and_setup_audio(page)
            ctrl = self.controls(page)
            self._octave_down(page, 2)
            page.wait_for_timeout(300)
            # Play C2 on the current engine and hold it.
            ctrl._focus_panel()
            page.keyboard.down('a')  # C2
            page.wait_for_timeout(500)
            held_power = self._freq_power(page, FREQ_C2)
            print(f'\n  C2 held on engine A: {held_power:.1f} dB')
            # Release the note first (the bug is it stays audible).
            page.keyboard.up('a')
            page.wait_for_timeout(100)
            # Switch engine via the algorithm encoder (clockwise).
            ctrl.rotate_encoder('algorithm', direction=1, steps=1)
            page.wait_for_timeout(500)
            # Check C2 is gone after engine switch.
            bleed_power = self._freq_power(page, FREQ_C2)
            self.screenshot(page, 'HOME-algorithm-rotateRight')
            print(f'  C2 after release + engine switch: {bleed_power:.1f} dB '
                  f'(threshold {SILENT_DB})')
            # Now play C5 on the new engine to confirm it works.
            self._octave_up(page, 2)  # Back to default octave.
            page.wait_for_timeout(200)
            ctrl._focus_panel()
            page.keyboard.down('k')  # C5
            page.wait_for_timeout(500)
            new_power = self._freq_power(page, FREQ_C5)
            page.keyboard.up('k')
            print(f'  C5 on engine B: {new_power:.1f} dB')
            self.assertLess(
                bleed_power, SILENT_DB,
                f'Engine bleed: C2 still audible after release + engine switch '
                f'({bleed_power:.1f} dB >= {SILENT_DB} dB)')
        finally:
            self.write_log(page, 'engine-switch.log')
            page.close()


if __name__ == '__main__':
    unittest.main()
