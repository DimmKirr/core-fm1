#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""E2E: cooperative scheduler acceptance tests (Phase 4, Task 16).

Verifies that the multi-task scheduler (sched.c) keeps all tasks running
without crashes, audio dropouts, or UI freezes.  Tests that need real WiFi
hardware are specced as skip-stubs until hardware is available.

Build: core_fm1_dev defconfig (includes CONFIG_SCHED, CONFIG_DEBUG).
"""
import base64
import json
import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from e2e_fixtures import E2ETestCase, _run_timestamp, RESULTS_DIR, e2e_build

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
FWSC = Path(os.environ['FM1_FWSC']) if 'FM1_FWSC' in os.environ else BUILD / 'core-fm1.fwsc'
DEFCONFIG = 'core_fm1_dev'
TEST_RESULTS = RESULTS_DIR / f'{_run_timestamp}-e2e_sched_test'

JS_HOOK_AUDIO_CONTEXT = '''() => {
    const OrigAC = window.AudioContext;
    const origConnect = AudioNode.prototype.connect;
    window.AudioContext = function(...args) {
        const ac = new OrigAC(...args);
        window.__fm1_ac = ac;
        const analyser = ac.createAnalyser();
        analyser.fftSize = 2048;
        analyser.smoothingTimeConstant = 0;
        window.__fm1_analyser = analyser;
        const recorder = ac.createScriptProcessor(4096, 2, 2);
        const sink = ac.createMediaStreamDestination();
        window.__fm1_chunks = [];
        window.__fm1_isRecording = false;
        window.__fm1_sampleRate = ac.sampleRate;
        recorder.onaudioprocess = (e) => {
            if (!window.__fm1_isRecording) return;
            const L = new Float32Array(e.inputBuffer.getChannelData(0));
            const R = new Float32Array(e.inputBuffer.getChannelData(1));
            window.__fm1_chunks.push({ L, R });
        };
        recorder.connect(sink);
        window.__fm1_recorder = recorder;
        AudioNode.prototype.connect = function(dest, ...rest) {
            const result = origConnect.call(this, dest, ...rest);
            if (dest === ac.destination && this !== analyser && this !== recorder) {
                try { origConnect.call(this, analyser); } catch(e) {}
                try { origConnect.call(this, recorder); } catch(e) {}
            }
            return result;
        };
        return ac;
    };
    window.AudioContext.prototype = OrigAC.prototype;
    Object.setPrototypeOf(window.AudioContext, OrigAC);
}'''

JS_START_RECORDING = '() => { window.__fm1_chunks = []; window.__fm1_isRecording = true; }'

JS_STOP_AND_EXPORT_WAV = '''() => {
    window.__fm1_isRecording = false;
    const chunks = window.__fm1_chunks;
    if (!chunks || chunks.length === 0)
        return { error: "no audio captured", samples: 0, base64: "" };
    const sr = window.__fm1_sampleRate || 44100;
    const totalFrames = chunks.reduce((s, c) => s + c.L.length, 0);
    const numChannels = 2, bitsPerSample = 16;
    const bytesPerSample = bitsPerSample / 8;
    const blockAlign = numChannels * bytesPerSample;
    const dataSize = totalFrames * blockAlign;
    const buffer = new ArrayBuffer(44 + dataSize);
    const view = new DataView(buffer);
    const writeStr = (off, s) => { for (let i = 0; i < s.length; i++) view.setUint8(off + i, s.charCodeAt(i)); };
    writeStr(0, 'RIFF');
    view.setUint32(4, 36 + dataSize, true);
    writeStr(8, 'WAVE');
    writeStr(12, 'fmt ');
    view.setUint32(16, 16, true);
    view.setUint16(20, 1, true);
    view.setUint16(22, numChannels, true);
    view.setUint32(24, sr, true);
    view.setUint32(28, sr * blockAlign, true);
    view.setUint16(32, blockAlign, true);
    view.setUint16(34, bitsPerSample, true);
    writeStr(36, 'data');
    view.setUint32(40, dataSize, true);
    let off = 44;
    for (const chunk of chunks) {
        for (let i = 0; i < chunk.L.length; i++) {
            const clamp = (v) => Math.max(-1, Math.min(1, v));
            view.setInt16(off, clamp(chunk.L[i]) * 0x7FFF, true); off += 2;
            view.setInt16(off, clamp(chunk.R[i]) * 0x7FFF, true); off += 2;
        }
    }
    const bytes = new Uint8Array(buffer);
    let binary = '';
    for (let i = 0; i < bytes.length; i++) binary += String.fromCharCode(bytes[i]);
    return { samples: totalFrames, sampleRate: sr,
             durationMs: Math.round(totalFrames / sr * 1000), base64: btoa(binary) };
}'''

JS_READ_RMS = '''() => {
    const a = window.__fm1_analyser;
    if (!a) return { error: "no analyser", rms: 0, peak: 0 };
    const buf = new Float32Array(a.fftSize);
    a.getFloatTimeDomainData(buf);
    let sum = 0;
    for (let i = 0; i < buf.length; i++) sum += buf[i] * buf[i];
    const rms = Math.sqrt(sum / buf.length);
    const peak = buf.reduce((m, v) => Math.max(m, Math.abs(v)), 0);
    return { rms, peak, samples: buf.length };
}'''


def _save_wav(b64_data, path):
    raw = base64.b64decode(b64_data)
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(raw)
    return len(raw)


def _wav_rms(path):
    import struct
    data = Path(path).read_bytes()
    if len(data) < 44:
        return 0.0
    pcm = data[44:]
    n = len(pcm) // 2
    if n == 0:
        return 0.0
    samples = struct.unpack(f'<{n}h', pcm[:n * 2])
    sq_sum = sum(s * s for s in samples)
    return (sq_sum / n) ** 0.5 / 32768.0


def _canvas_pixel_hash(page):
    return page.evaluate('''() => {
        const c = document.getElementById('panel');
        if (!c) return '';
        const ctx = c.getContext('2d');
        const d = ctx.getImageData(0, 0, c.width, c.height).data;
        let h = 0;
        for (let i = 0; i < d.length; i += 16)
            h = ((h << 5) - h + d[i]) | 0;
        return String(h);
    }''')


class SchedAcceptanceTest(E2ETestCase):
    """Scheduler acceptance: boot, concurrent tasks, audio + UI stability."""

    _build_ok = False

    @classmethod
    def setUpClass(cls):
        TEST_RESULTS.mkdir(parents=True, exist_ok=True)
        if 'FM1_FWSC' in os.environ:
            cls._build_ok = True
        else:
            r, _ = e2e_build(DEFCONFIG, TEST_RESULTS)
            cls._build_ok = r.returncode == 0
        try:
            super().setUpClass()
        except unittest.SkipTest:
            pass

    def _skip_if_not_ready(self):
        if not self._build_ok:
            self.skipTest('Build failed; no .fwsc to boot')
        if not FWSC.is_file():
            self.skipTest(f'No firmware at {FWSC}')
        if not self._browser:
            self.skipTest('Emulator not available')

    def _boot(self, page, wait_s=5):
        page.locator('#open').set_input_files(str(FWSC))
        page.wait_for_function(
            'document.getElementById("status").textContent.includes("ready")')
        page.locator('#start').click()
        page.wait_for_function(
            'document.getElementById("status")?.textContent'
            '?.includes("instructions/s")',
            timeout=30_000)
        page.wait_for_timeout(wait_s * 1000)

    def _assert_not_halted(self, page, ctx=''):
        status = page.locator('#status').text_content()
        self.assertNotIn('halted', status,
                         f'Emulator halted{": " + ctx if ctx else ""}')

    def _assert_canvas_has_content(self, page, label='page'):
        lit = page.evaluate('''() => {
            const c = document.getElementById('panel');
            if (!c) return 0;
            const ctx = c.getContext('2d');
            const d = ctx.getImageData(0, 0, c.width, c.height).data;
            let lit = 0;
            for (let i = 0; i < d.length; i += 4)
                if (d[i] > 40 || d[i+1] > 40 || d[i+2] > 40) lit++;
            return lit;
        }''')
        self.assertGreater(lit, 200, f'{label} canvas appears blank')

    # -- Test 1: RF init + UI responsiveness --

    def test_01_boot_ui_responsive(self):
        """All scheduler tasks start; UI responds to controls after boot."""
        self._skip_if_not_ready()
        page = self.new_emu_page()
        try:
            self._boot(page, wait_s=5)
            self._assert_not_halted(page, 'after boot')
            self._assert_canvas_has_content(page, 'HOME after boot')
            self.screenshot(page, 'SCHED-home-afterBoot')

            home_hash = _canvas_pixel_hash(page)

            ctrl = self.controls(page)
            ctrl.press_button('fx', delay_ms=500)
            self._assert_not_halted(page, 'after FX press')
            fx_hash = _canvas_pixel_hash(page)
            self.assertNotEqual(home_hash, fx_hash,
                                'UI not responding: FX press did not change screen')
            self.screenshot(page, 'SCHED-fx-afterPress')

            ctrl.press_button('home', delay_ms=500)
            self._assert_not_halted(page, 'after HOME press')
            back_hash = _canvas_pixel_hash(page)
            self.assertNotEqual(fx_hash, back_hash,
                                'UI not responding: HOME press did not change screen')
            self.screenshot(page, 'SCHED-home-afterReturn')

        finally:
            self.write_log(page, 'sched-01-boot-ui.log')
            page.close()

    # -- Test 2: Audio + UI concurrent operation --

    def test_02_audio_during_page_nav(self):
        """Audio continues while navigating between pages."""
        self._skip_if_not_ready()
        page = self.new_emu_page()
        try:
            page.evaluate(JS_HOOK_AUDIO_CONTEXT)
            self._boot(page, wait_s=3)
            self._assert_not_halted(page, 'after boot')

            ctrl = self.controls(page)

            page.evaluate(JS_START_RECORDING)

            for note in ['c4', 'e4', 'g4']:
                ctrl.play_note(note, hold_ms=400, delay_ms=100)

            ctrl.press_button('fx', delay_ms=300)
            self._assert_not_halted(page, 'FX while playing')
            self._assert_canvas_has_content(page, 'FX page')
            self.screenshot(page, 'SCHED-fx-duringAudio')

            for note in ['c4', 'e4', 'g4']:
                ctrl.play_note(note, hold_ms=400, delay_ms=100)

            ctrl.press_button('home', delay_ms=300)
            self._assert_not_halted(page, 'HOME while playing')

            page.wait_for_timeout(500)
            wav_result = page.evaluate(JS_STOP_AND_EXPORT_WAV)

            TEST_RESULTS.mkdir(parents=True, exist_ok=True)

            if wav_result.get('error'):
                self.fail(f'Audio capture failed: {wav_result["error"]}')

            wav_path = TEST_RESULTS / 'sched-audio-nav.wav'
            _save_wav(wav_result['base64'], wav_path)
            rms = _wav_rms(wav_path)

            (TEST_RESULTS / 'sched-audio-nav.json').write_text(json.dumps({
                'samples': wav_result['samples'],
                'durationMs': wav_result['durationMs'],
                'rms': rms,
            }, indent=2) + '\n')

            self.assertGreater(rms, 0.001,
                               f'Audio dropped during page navigation (RMS {rms:.6f})')

        finally:
            self.write_log(page, 'sched-02-audio-nav.log')
            page.close()

    # -- Test 3: Page navigation stress --

    def test_03_rapid_page_switching(self):
        """Rapidly cycle through pages; no crash or halt."""
        self._skip_if_not_ready()
        page = self.new_emu_page()
        try:
            self._boot(page, wait_s=3)
            ctrl = self.controls(page)

            buttons = ['fx', 'home', 'seq', 'home', 'env', 'home',
                       'lfo', 'home', 'scl', 'home', 'edit', 'home']

            for btn in buttons:
                ctrl.press_button(btn, delay_ms=200)
                self._assert_not_halted(page, f'after {btn}')

            self._assert_canvas_has_content(page, 'after rapid nav')
            self.screenshot(page, 'SCHED-home-afterStress')

            for note in ['c4', 'd4', 'e4']:
                ctrl.play_note(note, hold_ms=200, delay_ms=50)

            for btn in ['fx', 'seq', 'env', 'lfo', 'scl', 'edit', 'home']:
                ctrl.press_button(btn, delay_ms=150)
                self._assert_not_halted(page, f'audio+nav {btn}')

            self._assert_canvas_has_content(page, 'after stress+audio')
            self.screenshot(page, 'SCHED-home-afterAudioStress')

        finally:
            self.write_log(page, 'sched-03-rapid-nav.log')
            page.close()

    # -- Test 4: Extended stability --

    def test_04_extended_run(self):
        """Run emulator for 30s with periodic interactions; no halt."""
        self._skip_if_not_ready()
        page = self.new_emu_page()
        try:
            self._boot(page, wait_s=3)
            ctrl = self.controls(page)

            for cycle in range(6):
                ctrl.play_note('c4', hold_ms=200, delay_ms=100)
                ctrl.rotate_encoder('select', steps=2, delay_ms=100)
                ctrl.rotate_encoder('select', direction=-1, steps=2, delay_ms=100)

                if cycle % 2 == 0:
                    ctrl.press_button('fx', delay_ms=200)
                    ctrl.press_button('home', delay_ms=200)
                else:
                    ctrl.rotate_encoder('knob1', steps=3, delay_ms=100)
                    ctrl.rotate_encoder('knob2', direction=-1, steps=2, delay_ms=100)

                page.wait_for_timeout(3000)
                self._assert_not_halted(page, f'cycle {cycle}')

            self._assert_canvas_has_content(page, 'after extended run')
            self.screenshot(page, 'SCHED-home-afterExtended')

        finally:
            self.write_log(page, 'sched-04-extended.log')
            page.close()

    # -- Tests requiring WiFi hardware (skip-stubs) --

    def test_05_wpa_connect_cancel(self):
        """WPA connect + cancel mid-PBKDF2. Requires real WiFi hardware."""
        self.skipTest('Requires WiFi hardware: WPA PBKDF2 yielding test')

    def test_06_nested_rx_tx_traffic(self):
        """Nested RX/TX traffic under load. Requires real WiFi hardware."""
        self.skipTest('Requires WiFi hardware: nested RX->TX traffic test')

    def test_07_dma_timeout_recovery(self):
        """DMA timeout recovery. Requires real hardware."""
        self.skipTest('Requires hardware: DMA timeout recovery test')

    def test_08_flash_writes_during_audio(self):
        """Flash writes during audio playback. Requires real hardware."""
        self.skipTest('Requires hardware: flash write + audio test')

    def test_09_max_load_wifi_audio(self):
        """Maximum audio load + WiFi traffic. Requires real WiFi hardware."""
        self.skipTest('Requires WiFi hardware: max load stress test')


if __name__ == '__main__':
    unittest.main()
