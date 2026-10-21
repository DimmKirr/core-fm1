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

    def test_firmware_boots(self):
        page = self.new_emu_page()
        try:
            self.load_firmware(page)
            status = page.locator('#status').text_content()
            self.assertIn('instructions/s', status)
            # fm1-emu v0.2.0 panel.rs places the 2x LCD at (370, 42).
            # Read actual rendered pixels, not the emulator's execution status.
            page.wait_for_function('''() => {
                const c = document.getElementById('panel');
                const p = c.getContext('2d').getImageData(370, 42, 480, 480).data;
                let white = 0;
                for (let i = 0; i < p.length; i += 4)
                    if (p[i] === 255 && p[i+1] === 255 && p[i+2] === 255) white++;
                return white > 1000;
            }''', timeout=60_000)
            self.assert_boot_text(page)
            page.wait_for_timeout(10_000)
            self.assertNotIn('halted', page.locator('#status').text_content())
            self.assert_boot_text(page)
            self.screenshot(page, 'boot', 'after-load')
        finally:
            self.write_log(page)
            page.close()

    @staticmethod
    def load_firmware(page):
        page.locator('#open').set_input_files(str(FWSC_PATH))
        page.wait_for_function(
            'document.getElementById("status").textContent.includes("ready")')
        page.locator('#start').click()
        page.wait_for_function(
            'document.getElementById("status")?.textContent?.includes("instructions/s")',
            timeout=30_000)

    def test_panel_controls_reach_emulator(self):
        """Observe real UI-to-worklet messages; minimal firmware ignores inputs."""
        page = self.new_emu_page()
        try:
            self.load_firmware(page)
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

            # A focused dropdown must not swallow keyboard encoder events.
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
            self.screenshot(page, 'panel', 'after-controls')
        finally:
            self.write_log(page, 'panel-controls.log')
            page.close()

    def assert_boot_text(self, page):
        pixels = page.evaluate('''() => {
            const c = document.getElementById('panel');
            const p = c.getContext('2d').getImageData(370, 42, 480, 480).data;
            const result = [];
            for (let y = 100; y < 114; y++)
                for (let x = 48; x < 144; x++) {
                    const i = ((2*y)*480 + 2*x)*4;
                    result.push((p[i]<<16) | (p[i+1]<<8) | p[i+2]);
                }
            return result;
        }''')
        # Golden 5x7 glyphs for the literal boot identity, drawn at 2x scale.
        glyphs = [
            [0x38, 0x44, 0x44, 0x44, 0x20],  # c
            [0x38, 0x44, 0x44, 0x44, 0x38],  # o
            [0x7c, 0x08, 0x04, 0x04, 0x08],  # r
            [0x38, 0x54, 0x54, 0x54, 0x18],  # e
            [0x08, 0x08, 0x08, 0x08, 0x08],  # -
            [0x08, 0x7e, 0x09, 0x01, 0x02],  # f
            [0x7c, 0x04, 0x18, 0x04, 0x78],  # m
            [0x00, 0x42, 0x7f, 0x40, 0x00],  # 1
        ]
        expected = []
        for y in range(14):
            for x in range(96):
                glyph, col = x // 12, (x % 12) // 2
                on = col < 5 and glyphs[glyph][col] & (1 << (y // 2))
                expected.append(0xffffff if on else 0)
        self.assertEqual(pixels, expected, 'LCD must render core-fm1 at 2x scale')


if __name__ == '__main__':
    unittest.main()
