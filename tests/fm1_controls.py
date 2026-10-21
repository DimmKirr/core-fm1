# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
# Copyright (c) 2026 DimmKirr
"""FM-1 panel controls adapted from setup-fm1-emulator's UI example.

Source: DimmKirr/setup-fm1-emulator, commit
9d0fdd259343edbe29f1e4a3bfea85c7739f5d3c, examples/e2e_firmware_ui_test.py.
Mappings/positions reconciled with fm1-emu v0.2.0 main.js and panel.rs.
License: LICENSES/MIT.txt. This helper drives the FM-1, not the SMK-37 panel.
"""
from contextlib import contextmanager


_ENC_KEYS = {
    'select': ('ArrowRight', 'ArrowLeft'),
    'presets': ('ArrowUp', 'ArrowDown'),
    'algorithm': ('.', ','),
}
# Upstream keyboard aliases refer to panel slots, not KNOB1/KNOB2 labels.
_ENC_ALIASES = {'k1': 'presets', 'k2': 'algorithm'}
# Native canvas coordinates (1280x820), scaled to the displayed canvas below.
_KNOB_POSITIONS = {
    'master': (110, 135),
    'knob1': (935, 120),
    'knob2': (1027, 120),
    'knob3': (1119, 120),
    'knob4': (1211, 120),
}
_PHYSICAL_ENCODERS = {
    0: 'select', 1: 'algorithm', 2: 'knob1', 3: 'knob2',
    4: 'knob3', 5: 'knob4', 6: 'presets',
}
_BUTTON_KEYS = {
    'fx': '1', 'scl': '2', 'env': '3', 'lfo': '4', 'edit': '5',
    'glo': '6', 'home': '7', 'save': '8', 'arp': '9', 'seq': '0',
    'oct-': 'z', 'oct+': 'x', 'play': ' ', 'rec': 'r',
}
_NOTE_KEYS = {
    'c4': 'a', 'c#4': 'w', 'd4': 's', 'd#4': 'e', 'e4': 'd',
    'f4': 'f', 'f#4': 't', 'g4': 'g', 'g#4': 'y', 'a4': 'h',
    'a#4': 'u', 'b4': 'j', 'c5': 'k', 'c#5': 'o', 'd5': 'l',
    'd#5': 'p', 'e5': ';', 'f5': "'",
}


class FM1Controls:
    """Named controls bound to one initialized Playwright emulator page."""

    def __init__(self, page):
        self.page = page

    def _focus_panel(self):
        # The emulator ignores keydown events originating from a select menu.
        self.page.locator('canvas#panel').evaluate('''canvas => {
            canvas.tabIndex = 0;
            canvas.focus({preventScroll: true});
        }''')

    def press_key(self, key, hold_ms=80, delay_ms=300):
        self._focus_panel()
        self.page.keyboard.down(key)
        try:
            self.page.wait_for_timeout(hold_ms)
        finally:
            self.page.keyboard.up(key)
        self.page.wait_for_timeout(delay_ms)

    def rotate_encoder(self, name, direction=1, steps=1, delay_ms=300):
        """Turn clockwise (+1) or counterclockwise (-1), one click per step.

        Names: select, presets, algorithm, knob1..knob4, master.
        Upstream aliases k1/k2 mean presets/algorithm. Master is an analog pot.
        """
        name = name.lower()
        name = _ENC_ALIASES.get(name, name)
        if name not in _ENC_KEYS and name not in _KNOB_POSITIONS:
            raise ValueError(f'Unknown encoder: {name}')
        if direction not in (-1, 1):
            raise ValueError('direction must be +1 or -1')
        if not isinstance(steps, int) or steps < 0:
            raise ValueError('steps must be a nonnegative integer')
        self._focus_panel()
        for _ in range(steps):
            if name in _ENC_KEYS:
                self.page.keyboard.press(_ENC_KEYS[name][0 if direction > 0 else 1])
            else:
                x, y = _KNOB_POSITIONS[name]
                self.page.locator('canvas#panel').evaluate('''(canvas, [x, y, direction]) => {
                    const r = canvas.getBoundingClientRect();
                    canvas.dispatchEvent(new WheelEvent('wheel', {
                        clientX: r.left + r.width * x / canvas.width,
                        clientY: r.top + r.height * y / canvas.height,
                        deltaY: -40 * direction, bubbles: true, cancelable: true
                    }));
                }''', [x, y, direction])
            self.page.wait_for_timeout(50)
        self.page.wait_for_timeout(delay_ms)

    def send_encoder(self, physical_enc, clicks, delay_ms=300):
        """Rotate by firmware encoder ID (0..6), with signed click count."""
        if physical_enc not in _PHYSICAL_ENCODERS:
            raise ValueError(f'Unknown physical encoder: {physical_enc}')
        if not isinstance(clicks, int):
            raise ValueError('clicks must be an integer')
        self.rotate_encoder(_PHYSICAL_ENCODERS[physical_enc],
                            direction=1 if clicks >= 0 else -1,
                            steps=abs(clicks), delay_ms=delay_ms)

    def press_button(self, name, delay_ms=300, hold_ms=80):
        """Press a panel button; hold_ms also supports long presses."""
        key = _BUTTON_KEYS.get(name.lower())
        if key is None:
            raise ValueError(f'Unknown button: {name}')
        self.press_key(key, hold_ms=hold_ms, delay_ms=delay_ms)

    @contextmanager
    def hold_button(self, name, delay_ms=300):
        """Keep a button down during other actions, releasing even on failure."""
        key = _BUTTON_KEYS.get(name.lower())
        if key is None:
            raise ValueError(f'Unknown button: {name}')
        self._focus_panel()
        self.page.keyboard.down(key)
        try:
            self.page.wait_for_timeout(80)
            yield
        finally:
            self.page.keyboard.up(key)
            self.page.wait_for_timeout(delay_ms)

    def play_note(self, note, hold_ms=300, delay_ms=100):
        """Play a named note (C4..F5) on the emulator's piano keyboard."""
        key = _NOTE_KEYS.get(note.lower())
        if key is None:
            raise ValueError(f'Unknown note: {note}')
        self.press_key(key, hold_ms=hold_ms, delay_ms=delay_ms)
