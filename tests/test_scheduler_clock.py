#!/usr/bin/env python3
"""Verify the millisecond clock is independent of CONFIG_INPUT."""
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / 'firmware' / 'src' / 'core_fm1.c'


def test_fm1_ms_timer_not_gated_by_config_input():
    """Timer5 (or equivalent ms timer) must init outside CONFIG_INPUT guard."""
    src = CORE.read_text()

    lines = src.split('\n')
    in_input_guard = 0
    ms_timer_outside = False

    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped == '#ifdef CONFIG_INPUT':
            in_input_guard += 1
        elif stripped == '#endif' and in_input_guard > 0:
            in_input_guard -= 1
        elif in_input_guard == 0:
            if 'T5 PRD' in line or 'T5 CON' in line or \
               'fm1_ms_timer_init' in line or \
               ('IRQ_TIMER5' in line and 'fm1_irq_attach' in line):
                ms_timer_outside = True

    assert ms_timer_outside, \
        'Millisecond timer init must be outside #ifdef CONFIG_INPUT'
