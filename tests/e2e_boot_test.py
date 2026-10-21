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
            page.wait_for_function('''() => {
                const c = document.getElementById('panel');
                const p = c.getContext('2d').getImageData(370, 42, 480, 480).data;
                let nonblack = 0;
                for (let i = 0; i < p.length; i += 4)
                    if (p[i] > 30 || p[i+1] > 30 || p[i+2] > 30) nonblack++;
                return nonblack > 500;
            }''', timeout=60_000)
            page.wait_for_timeout(10_000)
            self.assertNotIn('halted', page.locator('#status').text_content())
            self.screenshot(page, 'boot')
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
            self.screenshot(page, 'panel')
        finally:
            self.write_log(page, 'panel-controls.log')
            page.close()


if __name__ == '__main__':
    unittest.main()
