#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""E2E: test engine audio output from core_fm1_dev firmware.

All ported engines (ANALOG, NOISE, FM6) are compiled into one firmware.
Engine tests switch via the SELECT encoder. WAV files are saved in
tests/results/<timestamp>-e2e_audio_engines_test/.
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
FWSC = BUILD / 'core-fm1.fwsc'

TEST_RESULTS = RESULTS_DIR / f'{_run_timestamp}-e2e_audio_engines_test'

# --- JS snippets (shared with e2e_audio_test.py) ---

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

JS_START_RECORDING = '''() => {
    window.__fm1_chunks = [];
    window.__fm1_isRecording = true;
}'''

JS_STOP_AND_EXPORT_WAV = '''() => {
    window.__fm1_isRecording = false;
    const chunks = window.__fm1_chunks;
    if (!chunks || chunks.length === 0)
        return { error: "no audio captured", samples: 0, base64: "" };

    const sr = window.__fm1_sampleRate || 44100;
    const totalFrames = chunks.reduce((s, c) => s + c.L.length, 0);
    const numChannels = 2;
    const bitsPerSample = 16;
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
    const b64 = btoa(binary);

    return { samples: totalFrames, sampleRate: sr, durationMs: Math.round(totalFrames / sr * 1000), base64: b64 };
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


def save_wav(b64_data, path):
    raw = base64.b64decode(b64_data)
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(raw)
    return len(raw)


def wav_rms(path):
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


class TestNoiseEngine(E2ETestCase):
    """Boot core_fm1_dev, switch to Noise engine via SELECT encoder, play notes."""

    DEFCONFIG = 'core_fm1_dev'
    _build_ok = False

    @classmethod
    def setUpClass(cls):
        TEST_RESULTS.mkdir(parents=True, exist_ok=True)
        if not FWSC.is_file():
            r, _ = e2e_build(cls.DEFCONFIG, TEST_RESULTS)
            cls._build_ok = r.returncode == 0
        else:
            cls._build_ok = True
        try:
            super().setUpClass()
        except unittest.SkipTest:
            pass

    def test_noise_engine_produces_audio(self):
        """Play C4-G4 on the Noise engine and verify the recorded WAV has audio."""
        if not self._build_ok:
            self.skipTest('Build failed (needs Docker + JieLi toolchain)')
        if not FWSC.is_file():
            self.skipTest(f'No firmware at {FWSC}')
        if not self._browser:
            self.skipTest('Emulator not available')

        page = self.new_emu_page()
        try:
            page.evaluate(JS_HOOK_AUDIO_CONTEXT)

            page.locator('#open').set_input_files(str(FWSC))
            page.wait_for_function(
                'document.getElementById("status").textContent.includes("ready")')
            page.locator('#start').click()
            page.wait_for_function(
                'document.getElementById("status")?.textContent'
                '?.includes("instructions/s")',
                timeout=30_000)
            page.wait_for_timeout(3_000)

            self.screenshot(page, 'noise-before')

            ctrl = self.controls(page)

            # Switch to engine 1 (NOISE) via SELECT encoder
            ctrl.rotate_encoder('select', direction=1, steps=1)
            page.wait_for_timeout(1000)

            # Silence baseline
            page.evaluate(JS_START_RECORDING)
            page.wait_for_timeout(1000)
            silence_wav = page.evaluate(JS_STOP_AND_EXPORT_WAV)

            silence_path = TEST_RESULTS / 'noise-silence.wav'
            if not silence_wav.get('error'):
                save_wav(silence_wav['base64'], silence_path)

            # Play notes and record
            page.evaluate(JS_START_RECORDING)
            for note in ['c4', 'd4', 'e4', 'f4', 'g4']:
                ctrl.play_note(note, hold_ms=300, delay_ms=100)
            page.wait_for_timeout(500)
            playing_wav = page.evaluate(JS_STOP_AND_EXPORT_WAV)

            self.screenshot(page, 'noise-playing')

            playing_path = TEST_RESULTS / 'noise-playing.wav'
            if playing_wav.get('error'):
                (TEST_RESULTS / 'noise.json').write_text(
                    json.dumps({'error': playing_wav['error']}, indent=2) + '\n')
                self.fail(f'Recording failed: {playing_wav["error"]}')

            save_wav(playing_wav['base64'], playing_path)

            silence_rms = wav_rms(silence_path) if silence_path.exists() else 0.0
            playing_rms = wav_rms(playing_path)
            live_rms = page.evaluate(JS_READ_RMS)

            (TEST_RESULTS / 'noise.json').write_text(json.dumps({
                'firmware': str(FWSC),
                'defconfig': self.DEFCONFIG,
                'engine': 'NOISE',
                'silence': {
                    'samples': silence_wav.get('samples', 0),
                    'durationMs': silence_wav.get('durationMs', 0),
                    'rms': silence_rms,
                },
                'playing': {
                    'samples': playing_wav['samples'],
                    'durationMs': playing_wav['durationMs'],
                    'rms': playing_rms,
                    'fileBytes': playing_path.stat().st_size,
                },
                'liveRms': live_rms,
            }, indent=2) + '\n')

            self.assertTrue(playing_path.exists(), 'WAV file not written')
            self.assertGreater(playing_path.stat().st_size, 1000,
                               'WAV file suspiciously small')
            self.assertGreater(playing_rms, 0.001,
                               f'No audio from Noise engine (RMS {playing_rms:.6f})')
        finally:
            self.write_log(page, 'noise.log')
            page.close()


class TestAnalogEngine(E2ETestCase):
    """Boot core_fm1_dev (Analog is the default engine at index 0), play notes, record WAV."""

    DEFCONFIG = 'core_fm1_dev'
    _build_ok = False

    @classmethod
    def setUpClass(cls):
        TEST_RESULTS.mkdir(parents=True, exist_ok=True)
        if not FWSC.is_file():
            r, _ = e2e_build(cls.DEFCONFIG, TEST_RESULTS)
            cls._build_ok = r.returncode == 0
        else:
            cls._build_ok = True
        try:
            super().setUpClass()
        except unittest.SkipTest:
            pass

    def test_analog_engine_produces_audio(self):
        """Play C4-G4 on the Analog engine (default SAW waveform) and verify audio output."""
        if not self._build_ok:
            self.skipTest('Build failed (needs Docker + JieLi toolchain)')
        if not FWSC.is_file():
            self.skipTest(f'No firmware at {FWSC}')
        if not self._browser:
            self.skipTest('Emulator not available')

        page = self.new_emu_page()
        try:
            page.evaluate(JS_HOOK_AUDIO_CONTEXT)

            page.locator('#open').set_input_files(str(FWSC))
            page.wait_for_function(
                'document.getElementById("status").textContent.includes("ready")')
            page.locator('#start').click()
            page.wait_for_function(
                'document.getElementById("status")?.textContent'
                '?.includes("instructions/s")',
                timeout=30_000)
            page.wait_for_timeout(3_000)

            self.screenshot(page, 'analog-before')

            # Silence baseline
            page.evaluate(JS_START_RECORDING)
            page.wait_for_timeout(1000)
            silence_wav = page.evaluate(JS_STOP_AND_EXPORT_WAV)

            silence_path = TEST_RESULTS / 'analog-silence.wav'
            if not silence_wav.get('error'):
                save_wav(silence_wav['base64'], silence_path)

            # Play notes and record
            page.evaluate(JS_START_RECORDING)
            ctrl = self.controls(page)
            for note in ['c4', 'd4', 'e4', 'f4', 'g4']:
                ctrl.play_note(note, hold_ms=300, delay_ms=100)
            page.wait_for_timeout(500)
            playing_wav = page.evaluate(JS_STOP_AND_EXPORT_WAV)

            self.screenshot(page, 'analog-playing')

            playing_path = TEST_RESULTS / 'analog-playing.wav'
            if playing_wav.get('error'):
                (TEST_RESULTS / 'analog.json').write_text(
                    json.dumps({'error': playing_wav['error']}, indent=2) + '\n')
                self.fail(f'Recording failed: {playing_wav["error"]}')

            save_wav(playing_wav['base64'], playing_path)

            silence_rms = wav_rms(silence_path) if silence_path.exists() else 0.0
            playing_rms = wav_rms(playing_path)
            live_rms = page.evaluate(JS_READ_RMS)

            (TEST_RESULTS / 'analog.json').write_text(json.dumps({
                'firmware': str(FWSC),
                'defconfig': self.DEFCONFIG,
                'engine': 'ANALOG',
                'silence': {
                    'samples': silence_wav.get('samples', 0),
                    'durationMs': silence_wav.get('durationMs', 0),
                    'rms': silence_rms,
                },
                'playing': {
                    'samples': playing_wav['samples'],
                    'durationMs': playing_wav['durationMs'],
                    'rms': playing_rms,
                    'fileBytes': playing_path.stat().st_size,
                },
                'liveRms': live_rms,
            }, indent=2) + '\n')

            self.assertTrue(playing_path.exists(), 'WAV file not written')
            self.assertGreater(playing_path.stat().st_size, 1000,
                               'WAV file suspiciously small')
            self.assertGreater(playing_rms, 0.001,
                               f'No audio from Analog engine (RMS {playing_rms:.6f})')
        finally:
            self.write_log(page, 'analog.log')
            page.close()


class TestFM6Engine(E2ETestCase):
    """Boot core_fm1_dev, switch to FM6 engine (index 2), play notes, record WAV."""

    DEFCONFIG = 'core_fm1_dev'
    _build_ok = False

    @classmethod
    def setUpClass(cls):
        TEST_RESULTS.mkdir(parents=True, exist_ok=True)
        if not FWSC.is_file():
            r, _ = e2e_build(cls.DEFCONFIG, TEST_RESULTS)
            cls._build_ok = r.returncode == 0
        else:
            cls._build_ok = True
        try:
            super().setUpClass()
        except unittest.SkipTest:
            pass

    def test_fm6_engine_produces_audio(self):
        """Play C4-G4 on the FM6 engine (DX7 EPIANO 1 default) and verify audio output."""
        if not self._build_ok:
            self.skipTest('Build failed (needs Docker + JieLi toolchain)')
        if not FWSC.is_file():
            self.skipTest(f'No firmware at {FWSC}')
        if not self._browser:
            self.skipTest('Emulator not available')

        page = self.new_emu_page()
        try:
            page.evaluate(JS_HOOK_AUDIO_CONTEXT)

            page.locator('#open').set_input_files(str(FWSC))
            page.wait_for_function(
                'document.getElementById("status").textContent.includes("ready")')
            page.locator('#start').click()
            page.wait_for_function(
                'document.getElementById("status")?.textContent'
                '?.includes("instructions/s")',
                timeout=30_000)
            page.wait_for_timeout(3_000)

            ctrl = self.controls(page)

            # Switch to engine 2 (FM6) via SELECT encoder: 2 clicks from ANALOG
            ctrl.rotate_encoder('select', direction=1, steps=2)
            page.wait_for_timeout(1000)

            self.screenshot(page, 'fm6-before')

            # Silence baseline
            page.evaluate(JS_START_RECORDING)
            page.wait_for_timeout(1000)
            silence_wav = page.evaluate(JS_STOP_AND_EXPORT_WAV)

            silence_path = TEST_RESULTS / 'fm6-silence.wav'
            if not silence_wav.get('error'):
                save_wav(silence_wav['base64'], silence_path)

            # Play notes and record
            page.evaluate(JS_START_RECORDING)
            for note in ['c4', 'd4', 'e4', 'f4', 'g4']:
                ctrl.play_note(note, hold_ms=300, delay_ms=100)
            page.wait_for_timeout(500)
            playing_wav = page.evaluate(JS_STOP_AND_EXPORT_WAV)

            self.screenshot(page, 'fm6-playing')

            playing_path = TEST_RESULTS / 'fm6-playing.wav'
            if playing_wav.get('error'):
                (TEST_RESULTS / 'fm6.json').write_text(
                    json.dumps({'error': playing_wav['error']}, indent=2) + '\n')
                self.fail(f'Recording failed: {playing_wav["error"]}')

            save_wav(playing_wav['base64'], playing_path)

            silence_rms = wav_rms(silence_path) if silence_path.exists() else 0.0
            playing_rms = wav_rms(playing_path)
            live_rms = page.evaluate(JS_READ_RMS)

            (TEST_RESULTS / 'fm6.json').write_text(json.dumps({
                'firmware': str(FWSC),
                'defconfig': self.DEFCONFIG,
                'engine': 'FM6',
                'lineage': 'fmlab',
                'silence': {
                    'samples': silence_wav.get('samples', 0),
                    'durationMs': silence_wav.get('durationMs', 0),
                    'rms': silence_rms,
                },
                'playing': {
                    'samples': playing_wav['samples'],
                    'durationMs': playing_wav['durationMs'],
                    'rms': playing_rms,
                    'fileBytes': playing_path.stat().st_size,
                },
                'liveRms': live_rms,
            }, indent=2) + '\n')

            self.assertTrue(playing_path.exists(), 'WAV file not written')
            self.assertGreater(playing_path.stat().st_size, 1000,
                               'WAV file suspiciously small')
            self.assertGreater(playing_rms, 0.001,
                               f'No audio from FM6 engine (RMS {playing_rms:.6f})')
        finally:
            self.write_log(page, 'fm6.log')
            page.close()


class TestMasterVolume(E2ETestCase):
    """Verify master pot controls audio volume at 0%, ~1%, 50%, 100%."""

    DEFCONFIG = 'core_fm1_dev'
    _build_ok = False

    @classmethod
    def setUpClass(cls):
        TEST_RESULTS.mkdir(parents=True, exist_ok=True)
        r, _ = e2e_build(cls.DEFCONFIG, TEST_RESULTS)
        cls._build_ok = (r.returncode == 0)
        try:
            super().setUpClass()
        except unittest.SkipTest:
            pass

    def _record_at_volume(self, page, ctrl, pot_value, label):
        ctrl.set_master_volume(pot_value)
        page.wait_for_timeout(200)
        page.evaluate(JS_START_RECORDING)
        for note in ['c4', 'e4', 'g4']:
            ctrl.play_note(note, hold_ms=200, delay_ms=50)
        page.wait_for_timeout(300)
        wav_data = page.evaluate(JS_STOP_AND_EXPORT_WAV)
        if wav_data.get('error'):
            return 0.0
        path = TEST_RESULTS / f'volume-{label}.wav'
        save_wav(wav_data['base64'], path)
        return wav_rms(path)

    def test_master_volume_controls_output(self):
        """Record audio at pot 0, 10, 512, 1023 and verify volume scales."""
        if not self._build_ok:
            self.skipTest('Build failed')
        if not FWSC.is_file():
            self.skipTest(f'No firmware at {FWSC}')
        if not self._browser:
            self.skipTest('Emulator not available')

        page = self.new_emu_page()
        try:
            page.evaluate(JS_HOOK_AUDIO_CONTEXT)
            page.locator('#open').set_input_files(str(FWSC))
            page.wait_for_function(
                'document.getElementById("status").textContent.includes("ready")')
            page.locator('#start').click()
            page.wait_for_function(
                'document.getElementById("status")?.textContent'
                '?.includes("instructions/s")',
                timeout=30_000)
            page.wait_for_timeout(3_000)

            ctrl = self.controls(page)

            rms_zero = self._record_at_volume(page, ctrl, 0, '0pct')
            rms_low = self._record_at_volume(page, ctrl, 10, '1pct')
            rms_half = self._record_at_volume(page, ctrl, 512, '50pct')
            rms_full = self._record_at_volume(page, ctrl, 1023, '100pct')

            (TEST_RESULTS / 'volume.json').write_text(json.dumps({
                'rms_zero': rms_zero,
                'rms_low': rms_low,
                'rms_half': rms_half,
                'rms_full': rms_full,
            }, indent=2) + '\n')

            self.assertLess(rms_zero, 0.001,
                            f'Volume at 0 should be silent (RMS {rms_zero:.6f})')
            self.assertGreater(rms_full, 0.001,
                               f'Volume at max should produce sound (RMS {rms_full:.6f})')
            self.assertGreater(rms_full, rms_half,
                               f'Full > half: {rms_full:.6f} vs {rms_half:.6f}')
            self.assertGreater(rms_half, rms_low,
                               f'Half > low: {rms_half:.6f} vs {rms_low:.6f}')
        finally:
            self.write_log(page, 'volume.log')
            page.close()


class TestAudioLevelNormalization(E2ETestCase):
    """Engines at identical per-channel settings must produce similar output levels.

    Boots a fresh emulator page per engine to avoid state leakage.
    Three checks:
      1. Every engine produces audible signal (not silent).
      2. RMS levels across engines are within 6 dB (2x ratio).
      3. Master volume knob actually scales the output (half < full).
    """

    DEFCONFIG = 'core_fm1_dev'
    _build_ok = False

    ENGINES = [
        {'idx': 0, 'name': 'analog'},
        {'idx': 1, 'name': 'noise'},
        {'idx': 2, 'name': 'fm6'},
    ]

    @classmethod
    def setUpClass(cls):
        TEST_RESULTS.mkdir(parents=True, exist_ok=True)
        if not FWSC.is_file():
            r, _ = e2e_build(cls.DEFCONFIG, TEST_RESULTS)
            cls._build_ok = r.returncode == 0
        else:
            cls._build_ok = True
        try:
            super().setUpClass()
        except unittest.SkipTest:
            pass

    def _boot_page(self):
        """Boot a fresh emulator page with audio hooks, wait for HOME screen."""
        page = self.new_emu_page()
        page.evaluate(JS_HOOK_AUDIO_CONTEXT)
        page.locator('#open').set_input_files(str(FWSC))
        page.wait_for_function(
            'document.getElementById("status").textContent.includes("ready")')
        page.locator('#start').click()
        page.wait_for_function(
            'document.getElementById("status")?.textContent'
            '?.includes("instructions/s")',
            timeout=30_000)
        page.wait_for_timeout(5_000)
        ctrl = self.controls(page)
        ctrl.press_button('home')
        page.wait_for_timeout(1_000)
        return page

    def _record_engine(self, page, ctrl, engine_idx, name, volume=512):
        """Select engine, set master volume, play a chord, return (rms, path)."""
        ctrl.rotate_encoder('select', direction=-1, steps=10)
        page.wait_for_timeout(300)
        if engine_idx > 0:
            ctrl.rotate_encoder('select', direction=1, steps=engine_idx)
            page.wait_for_timeout(500)

        ctrl.set_master_volume(volume)
        page.wait_for_timeout(300)

        self.screenshot(page, f'norm-{name}-vol{volume}-before')

        page.evaluate(JS_START_RECORDING)
        for note in ['c4', 'e4', 'g4']:
            ctrl.play_note(note, hold_ms=300, delay_ms=100)
        page.wait_for_timeout(600)
        wav_data = page.evaluate(JS_STOP_AND_EXPORT_WAV)

        self.screenshot(page, f'norm-{name}-vol{volume}-after')

        if wav_data.get('error'):
            return 0.0, None
        path = TEST_RESULTS / f'norm-{name}-vol{volume}.wav'
        save_wav(wav_data['base64'], path)
        return wav_rms(path), path

    def _skip_unless_ready(self):
        if not self._build_ok:
            self.skipTest('Build failed')
        if not FWSC.is_file():
            self.skipTest(f'No firmware at {FWSC}')
        if not self._browser:
            self.skipTest('Emulator not available')

    def test_engines_not_silent(self):
        """Every engine must produce audible output at 50% master volume."""
        self._skip_unless_ready()

        results = {}
        for eng in self.ENGINES:
            page = self._boot_page()
            try:
                ctrl = self.controls(page)
                rms, _ = self._record_engine(
                    page, ctrl, eng['idx'], eng['name'], volume=512)
                results[eng['name']] = rms
            finally:
                self.write_log(page, f'norm-{eng["name"]}.log')
                page.close()

        (TEST_RESULTS / 'norm-silence-check.json').write_text(
            json.dumps(results, indent=2) + '\n')

        for name, rms in results.items():
            self.assertGreater(
                rms, 0.001,
                f'{name} engine produced no audio at 50% volume (RMS {rms:.6f})')

    def test_engine_levels_within_6db(self):
        """RMS levels across all engines must be within 6 dB (2x ratio)."""
        self._skip_unless_ready()

        results = {}
        for eng in self.ENGINES:
            page = self._boot_page()
            try:
                ctrl = self.controls(page)
                rms, _ = self._record_engine(
                    page, ctrl, eng['idx'], eng['name'], volume=512)
                results[eng['name']] = rms
            finally:
                self.write_log(page, f'norm-level-{eng["name"]}.log')
                page.close()

        ref_name = 'analog'
        ref_rms = results[ref_name]
        ratios = {}
        for name, rms in results.items():
            if name == ref_name:
                continue
            ratios[f'{name}_vs_{ref_name}'] = rms / max(ref_rms, 1e-9)

        (TEST_RESULTS / 'norm-levels.json').write_text(json.dumps({
            **{f'rms_{k}': v for k, v in results.items()},
            **ratios,
        }, indent=2) + '\n')

        self.assertGreater(ref_rms, 0.001,
                           f'Reference engine {ref_name} is silent')

        for name, rms in results.items():
            if name == ref_name:
                continue
            ratio = rms / ref_rms
            self.assertLess(
                ratio, 2.0,
                f'{name} too loud vs {ref_name}: {ratio:.2f}x '
                f'(RMS {rms:.6f} vs {ref_rms:.6f})')
            self.assertGreater(
                ratio, 0.5,
                f'{name} too quiet vs {ref_name}: {ratio:.2f}x '
                f'(RMS {rms:.6f} vs {ref_rms:.6f})')

    def test_master_volume_scales_output(self):
        """Master volume at 50% must produce less signal than at ~100%."""
        self._skip_unless_ready()

        page = self._boot_page()
        try:
            ctrl = self.controls(page)
            rms_half, _ = self._record_engine(
                page, ctrl, 0, 'analog', volume=512)
            rms_full, _ = self._record_engine(
                page, ctrl, 0, 'analog', volume=1000)

            (TEST_RESULTS / 'norm-volume-scaling.json').write_text(
                json.dumps({
                    'rms_half': rms_half,
                    'rms_full': rms_full,
                    'ratio': rms_full / max(rms_half, 1e-9),
                }, indent=2) + '\n')

            self.assertGreater(rms_half, 0.001,
                               f'No audio at 50% volume (RMS {rms_half:.6f})')
            self.assertGreater(rms_full, 0.001,
                               f'No audio at ~100% volume (RMS {rms_full:.6f})')
            self.assertGreater(
                rms_full, rms_half,
                f'Full volume should be louder than half: '
                f'{rms_full:.6f} vs {rms_half:.6f}')
        finally:
            self.write_log(page, 'norm-volume-scaling.log')
            page.close()


class TestEngineSelectDisplay(E2ETestCase):
    """Verify LCD updates immediately when switching engines via SELECT encoder."""

    DEFCONFIG = 'core_fm1_dev'
    _build_ok = False

    @classmethod
    def setUpClass(cls):
        TEST_RESULTS.mkdir(parents=True, exist_ok=True)
        r, _ = e2e_build(cls.DEFCONFIG, TEST_RESULTS)
        cls._build_ok = (r.returncode == 0)
        try:
            super().setUpClass()
        except unittest.SkipTest:
            pass

    def test_engine_name_updates_on_select(self):
        """Rotate SELECT encoder 1 click, screenshot must differ (engine name changed)."""
        if not self._build_ok:
            self.skipTest('Build failed')
        if not FWSC.is_file():
            self.skipTest(f'No firmware at {FWSC}')
        if not self._browser:
            self.skipTest('Emulator not available')

        page = self.new_emu_page()
        try:
            page.locator('#open').set_input_files(str(FWSC))
            page.wait_for_function(
                'document.getElementById("status").textContent.includes("ready")')
            page.locator('#start').click()
            page.wait_for_function(
                'document.getElementById("status")?.textContent'
                '?.includes("instructions/s")',
                timeout=30_000)
            page.wait_for_timeout(3_000)

            ctrl = self.controls(page)

            # Screenshot before: should show ANALOG (default engine)
            before_bytes = page.locator('canvas#panel').screenshot()
            self.screenshot(page, 'select-before')

            # One click on SELECT encoder to switch engine
            ctrl.rotate_encoder('select', direction=1, steps=1)
            page.wait_for_timeout(500)

            # Screenshot after: should show NOISE (next engine)
            after_bytes = page.locator('canvas#panel').screenshot()
            self.screenshot(page, 'select-after')

            self.assertNotEqual(before_bytes, after_bytes,
                                'Screen did not change after engine select rotation')
        finally:
            self.write_log(page, 'engine-select.log')
            page.close()


if __name__ == '__main__':
    unittest.main()
