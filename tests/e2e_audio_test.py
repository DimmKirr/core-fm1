#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""E2E: verify audio capture and engine output.

Two litmus tests:
  1. Oscillator: play a JS 440Hz tone, record to WAV, verify detection works.
  2. Felucca Analog ACID: boot Felucca firmware, play notes on the default
     Analog engine (ACID preset), record to WAV, verify audio output.

Audio is captured in-browser via a ScriptProcessorNode tap, encoded as 16-bit
PCM WAV in JS, and transferred to Python as base64. WAV files are saved in
tests/results/<timestamp>-e2e_audio_test/.

Set FM1_FWSC to point at an external .fwsc (e.g. Felucca build) to skip the
core-fm1 build step.
"""
import base64
import json
import os
import subprocess
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from e2e_fixtures import E2ETestCase, _run_timestamp, RESULTS_DIR, e2e_build

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
# FM1_FWSC env var: point at any .fwsc to skip the build step
FWSC = Path(os.environ['FM1_FWSC']) if 'FM1_FWSC' in os.environ else BUILD / 'core-fm1.fwsc'

TEST_RESULTS = RESULTS_DIR / f'{_run_timestamp}-e2e_audio_test'

# --- JS snippets for in-browser audio capture ---

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

        // ScriptProcessorNode for WAV capture (deprecated but universal)
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

        // When any node connects to ac.destination, also connect to
        // our analyser and recorder so we tap the audio output.
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

    // WAV header
    const writeStr = (off, s) => { for (let i = 0; i < s.length; i++) view.setUint8(off + i, s.charCodeAt(i)); };
    writeStr(0, 'RIFF');
    view.setUint32(4, 36 + dataSize, true);
    writeStr(8, 'WAVE');
    writeStr(12, 'fmt ');
    view.setUint32(16, 16, true);           // fmt chunk size
    view.setUint16(20, 1, true);            // PCM
    view.setUint16(22, numChannels, true);
    view.setUint32(24, sr, true);
    view.setUint32(28, sr * blockAlign, true);
    view.setUint16(32, blockAlign, true);
    view.setUint16(34, bitsPerSample, true);
    writeStr(36, 'data');
    view.setUint32(40, dataSize, true);

    // Interleave L/R as 16-bit PCM
    let off = 44;
    for (const chunk of chunks) {
        for (let i = 0; i < chunk.L.length; i++) {
            const clamp = (v) => Math.max(-1, Math.min(1, v));
            view.setInt16(off, clamp(chunk.L[i]) * 0x7FFF, true); off += 2;
            view.setInt16(off, clamp(chunk.R[i]) * 0x7FFF, true); off += 2;
        }
    }

    // base64-encode
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


def build_firmware(defconfig):
    r, _ = e2e_build(defconfig, TEST_RESULTS)
    return r


def save_wav(b64_data, path):
    """Decode a base64 WAV and write to disk."""
    raw = base64.b64decode(b64_data)
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(raw)
    return len(raw)


def wav_rms(path):
    """Read a 16-bit stereo WAV and return the RMS amplitude (0..1)."""
    import struct
    data = Path(path).read_bytes()
    if len(data) < 44:
        return 0.0
    # skip 44-byte header, read interleaved 16-bit samples
    pcm = data[44:]
    n = len(pcm) // 2
    if n == 0:
        return 0.0
    samples = struct.unpack(f'<{n}h', pcm[:n * 2])
    sq_sum = sum(s * s for s in samples)
    rms = (sq_sum / n) ** 0.5 / 32768.0
    return rms


class TestAudioLitmus(E2ETestCase):
    """Verify in-browser audio recording works using a JS oscillator."""

    def test_oscillator_recorded(self):
        """A 440Hz JS oscillator should produce a WAV with nonzero RMS."""
        if not self._browser:
            self.skipTest('Browser not available')

        page = self._browser.new_page()
        try:
            page.set_content('<html><body></body></html>')

            # Set up oscillator + capture chain
            result = page.evaluate('''() => {
                const ac = new AudioContext({ sampleRate: 44100 });
                window.__fm1_ac = ac;
                window.__fm1_sampleRate = ac.sampleRate;

                const osc = ac.createOscillator();
                osc.frequency.value = 440;

                // Analyser for RMS spot-check
                const analyser = ac.createAnalyser();
                analyser.fftSize = 2048;
                analyser.smoothingTimeConstant = 0;
                window.__fm1_analyser = analyser;

                // ScriptProcessor for WAV recording
                const recorder = ac.createScriptProcessor(4096, 2, 2);
                const sink = ac.createMediaStreamDestination();
                window.__fm1_chunks = [];
                window.__fm1_isRecording = false;
                recorder.onaudioprocess = (e) => {
                    if (!window.__fm1_isRecording) return;
                    const L = new Float32Array(e.inputBuffer.getChannelData(0));
                    const R = new Float32Array(e.inputBuffer.getChannelData(1));
                    window.__fm1_chunks.push({ L, R });
                };
                recorder.connect(sink);

                // osc -> analyser -> destination
                //     -> recorder -> sink (silent)
                osc.connect(analyser);
                osc.connect(recorder);
                analyser.connect(ac.destination);
                osc.start();

                window.__fm1_osc = osc;
                return { sampleRate: ac.sampleRate, state: ac.state };
            }''')

            # Record 1 second of oscillator
            page.evaluate(JS_START_RECORDING)
            page.wait_for_timeout(1000)
            wav_result = page.evaluate(JS_STOP_AND_EXPORT_WAV)

            page.evaluate('''() => {
                window.__fm1_osc.stop();
                window.__fm1_ac.close();
            }''')

            TEST_RESULTS.mkdir(parents=True, exist_ok=True)
            wav_path = TEST_RESULTS / 'litmus-440hz.wav'

            if wav_result.get('error'):
                (TEST_RESULTS / 'litmus.json').write_text(
                    json.dumps(wav_result, indent=2) + '\n')
                self.fail(f'Recording failed: {wav_result["error"]}')

            nbytes = save_wav(wav_result['base64'], wav_path)
            rms = wav_rms(wav_path)

            (TEST_RESULTS / 'litmus.json').write_text(json.dumps({
                'context': result,
                'recording': {
                    'samples': wav_result['samples'],
                    'durationMs': wav_result['durationMs'],
                    'sampleRate': wav_result['sampleRate'],
                    'fileBytes': nbytes,
                    'rms': rms,
                },
            }, indent=2) + '\n')

            self.assertGreater(rms, 0.01,
                               f'Oscillator WAV RMS too low ({rms:.6f}): {wav_path}')
            self.assertTrue(wav_path.exists(), 'WAV file not written')
            self.assertGreater(wav_path.stat().st_size, 1000,
                               'WAV file suspiciously small')
        finally:
            page.close()


class TestFeluccaAnalogAcid(E2ETestCase):
    """Litmus: boot Felucca firmware, play Analog ACID preset, record WAV.

    Confirms the audio capture pipeline works end-to-end with real firmware.
    Uses FM1_FWSC env var or falls back to building core_fm1_dev.
    """

    DEFCONFIG = 'core_fm1_dev'
    _build_ok = False

    @classmethod
    def setUpClass(cls):
        TEST_RESULTS.mkdir(parents=True, exist_ok=True)
        if 'FM1_FWSC' in os.environ:
            cls._build_ok = True
        else:
            r = build_firmware(cls.DEFCONFIG)
            cls._build_ok = r.returncode == 0
        try:
            super().setUpClass()
        except unittest.SkipTest:
            pass

    def test_analog_acid_produces_audio(self):
        """Boot Felucca, play C4-G4 on Analog ACID, verify WAV has audio."""
        if not self._build_ok:
            self.skipTest('Build failed; no .fwsc to boot')
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

            self.screenshot(page, 'analog-acid-before')

            # Record silence (1s baseline)
            page.evaluate(JS_START_RECORDING)
            page.wait_for_timeout(1000)
            silence_wav = page.evaluate(JS_STOP_AND_EXPORT_WAV)

            silence_path = TEST_RESULTS / 'analog-acid-silence.wav'
            if not silence_wav.get('error'):
                save_wav(silence_wav['base64'], silence_path)

            # Record while playing notes
            page.evaluate(JS_START_RECORDING)
            ctrl = self.controls(page)
            for note in ['c4', 'd4', 'e4', 'f4', 'g4']:
                ctrl.play_note(note, hold_ms=300, delay_ms=100)
            page.wait_for_timeout(500)
            playing_wav = page.evaluate(JS_STOP_AND_EXPORT_WAV)

            self.screenshot(page, 'analog-acid-playing')

            playing_path = TEST_RESULTS / 'analog-acid-playing.wav'
            if playing_wav.get('error'):
                (TEST_RESULTS / 'analog-acid.json').write_text(
                    json.dumps({'error': playing_wav['error']}, indent=2) + '\n')
                self.fail(f'Recording failed: {playing_wav["error"]}')

            save_wav(playing_wav['base64'], playing_path)

            silence_rms = wav_rms(silence_path) if silence_path.exists() else 0.0
            playing_rms = wav_rms(playing_path)

            live_rms = page.evaluate(JS_READ_RMS)

            (TEST_RESULTS / 'analog-acid.json').write_text(json.dumps({
                'firmware': str(FWSC),
                'engine': 'Analog',
                'preset': 'ACID',
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
                               f'No audio in WAV while playing notes (RMS {playing_rms:.6f})')
        finally:
            self.write_log(page, 'analog-acid.log')
            page.close()


if __name__ == '__main__':
    unittest.main()
