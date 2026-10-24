#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""E2E: load core-fm1 firmware into fm1-emu and verify boot."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from e2e_fixtures import E2ETestCase

FWSC_PATH = Path(__file__).resolve().parents[1] / 'build' / 'core-fm1.fwsc'


class BootTest(E2ETestCase):
    """Upload core-fm1.fwsc into the emulator and verify it boots."""

    @classmethod
    def setUpClass(cls):
        if not FWSC_PATH.is_file():
            raise unittest.SkipTest(
                f'Firmware not built ({FWSC_PATH}). Run "task build" first.')
        super().setUpClass()

    def _lcd_has_content(self, page, threshold=500):
        """True when the LCD area of the panel canvas has non-black pixels."""
        return page.evaluate('''(threshold) => {
            const c = document.getElementById('panel');
            const p = c.getContext('2d').getImageData(370, 42, 480, 480).data;
            let n = 0;
            for (let i = 0; i < p.length; i += 4)
                if (p[i] > 30 || p[i+1] > 30 || p[i+2] > 30) n++;
            return n > threshold;
        }''', threshold)

    def test_firmware_boots(self):
        page = self.new_emu_page(mhz=10)
        try:
            self.load_firmware(page, capture=True)
            status = page.locator('#status').text_content()
            self.assertIn('instructions/s', status)

            page.wait_for_function('''() => {
                const c = document.getElementById('panel');
                const p = c.getContext('2d').getImageData(370, 42, 480, 480).data;
                let n = 0;
                for (let i = 0; i < p.length; i += 4)
                    if (p[i] > 30 || p[i+1] > 30 || p[i+2] > 30) n++;
                return n > 500;
            }''', timeout=120_000)

            page.wait_for_timeout(30_000)
            self.screenshot(page, 'HOME-none-initial')

            self.assertNotIn('halted', page.locator('#status').text_content())
        finally:
            self.write_log(page)
            frames = self.stop_canvas_capture(page)
            if frames:
                splash_frame = self._find_splash_frame(frames)
                if splash_frame is not None:
                    self._save_frame(frames[splash_frame], 'boot-splash')
                gif = self.frames_to_gif(frames)
                print(f'\nBoot GIF ({len(frames)} frames, '
                      f'splash at frame {splash_frame}): {gif}')
            page.close()

    def _find_splash_frame(self, frames):
        """Find the first captured frame showing splash (LCD has content but not HOME page)."""
        import base64
        from io import BytesIO
        try:
            from PIL import Image
        except ImportError:
            return None
        lcd_x, lcd_y, lcd_w, lcd_h = 370, 42, 480, 480
        splash_idx = None
        for i, data_url in enumerate(frames):
            raw = base64.b64decode(data_url.split(',', 1)[1])
            img = Image.open(BytesIO(raw))
            lcd = img.crop((lcd_x, lcd_y, lcd_x + lcd_w, lcd_y + lcd_h))
            px = lcd.tobytes()
            step = 4 if lcd.mode == 'RGBA' else 3
            bright = sum(1 for j in range(0, len(px), step)
                         if px[j] > 30 or px[j+1] > 30 or px[j+2] > 30)
            if bright < 50:
                continue
            if splash_idx is None:
                splash_idx = i
            elif bright > 5000:
                return splash_idx
        return splash_idx

    def _save_frame(self, data_url, name):
        """Save a captured data-URL frame as a screenshot PNG."""
        import base64
        screenshot_dir = self._results_dir() / 'screenshots'
        screenshot_dir.mkdir(parents=True, exist_ok=True)
        raw = base64.b64decode(data_url.split(',', 1)[1])
        (screenshot_dir / f'{name}.png').write_bytes(raw)

    def load_firmware(self, page, capture=False):
        page.locator('#open').set_input_files(str(FWSC_PATH))
        page.wait_for_function(
            'document.getElementById("status").textContent.includes("ready")')
        if capture:
            self.start_canvas_capture(page, fps=30)
        page.locator('#start').click()
        page.wait_for_function(
            'document.getElementById("status")?.textContent?.includes("instructions/s")',
            timeout=30_000)

    def test_panel_controls_reach_emulator(self):
        """Observe real UI-to-worklet messages; minimal firmware ignores inputs."""
        page = self.new_emu_page()
        try:
            self.load_firmware(page, capture=False)
            page.evaluate('''() => {
                window.panelMessages = [];
                const original = MessagePort.prototype.postMessage;
                MessagePort.prototype.postMessage = function(message, ...args) {
                    if (['enc', 'key', 'pot'].includes(message?.type))
                        window.panelMessages.push(structuredClone(message));
                    return original.call(this, message, ...args);
                };
            }''')
            panel = self.controls(page)

            def take_messages():
                return page.evaluate('window.panelMessages.splice(0)')

            page.locator('#fw').focus()
            for name, encoder in [('select', 0), ('presets', 6), ('algorithm', 1),
                                  ('knob1', 2), ('knob2', 3), ('knob3', 4), ('knob4', 5),
                                  ('k1', 6), ('k2', 1)]:
                panel.rotate_encoder(name, steps=2, delay_ms=0)
                panel.rotate_encoder(name, direction=-1, delay_ms=0)
                self.assertEqual(take_messages(), [
                    {'type': 'enc', 'enc': encoder, 'clicks': clicks}
                    for clicks in (1, 1, -1)], name)
            self.send_enc(page, 3, -2, delay_ms=0)
            self.assertEqual(take_messages(), [
                {'type': 'enc', 'enc': 3, 'clicks': -1}] * 2)
            panel.rotate_encoder('master', direction=-1, delay_ms=0)
            self.assertEqual(take_messages(), [{'type': 'pot', 'value': 880}])

            for button, key_id in [('oct-', 0), ('oct+', 1), ('fx', 2), ('scl', 3),
                                   ('env', 4), ('lfo', 5), ('edit', 6), ('glo', 7),
                                   ('home', 8), ('save', 9), ('arp', 10), ('seq', 11),
                                   ('play', 12), ('rec', 13)]:
                panel.press_button(button, delay_ms=0)
                self.assertEqual(take_messages(), [
                    {'type': 'key', 'id': key_id, 'down': state}
                    for state in (True, False)], button)
            with panel.hold_button('edit', delay_ms=0):
                panel.rotate_encoder('knob1', delay_ms=0)
            self.assertEqual(take_messages(), [
                {'type': 'key', 'id': 6, 'down': True},
                {'type': 'enc', 'enc': 2, 'clicks': 1},
                {'type': 'key', 'id': 6, 'down': False}])
            for note, key_id in [('C4', 21), ('C#4', 22), ('F5', 38)]:
                panel.play_note(note, hold_ms=80, delay_ms=0)
                self.assertEqual(take_messages(), [
                    {'type': 'key', 'id': key_id, 'down': state}
                    for state in (True, False)], note)
            self.screenshot(page, 'HOME-note-noteOn')
        finally:
            self.write_log(page, 'panel-controls.log')
            page.close()


if __name__ == '__main__':
    unittest.main()
