#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Unit tests for tools/gen_weather_colors.py."""
import re
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'tools' / 'gen_weather_colors.py'


@pytest.fixture(scope='module')
def generated_output():
    """Run the generator and capture stdout."""
    r = subprocess.run(
        [sys.executable, str(SCRIPT)],
        capture_output=True, text=True, cwd=str(ROOT))
    assert r.returncode == 0, f"Generator failed: {r.stderr}"
    return r.stdout


def test_script_runs_without_error(generated_output):
    assert len(generated_output) > 0


def test_contains_weather_color_function(generated_output):
    assert 'weather_color' in generated_output
    assert 'uint16_t weather_color(uint8_t wmo_code)' in generated_output


def test_contains_condition_name_function(generated_output):
    assert 'weather_condition_name' in generated_output
    assert 'const char *weather_condition_name(uint8_t wmo_code)' in generated_output


def test_all_hex_values_are_valid_rgb565(generated_output):
    """Every 0xNNNN hex literal must be in the RGB565 range 0x0000-0xFFFF."""
    hex_vals = re.findall(r'0x([0-9A-Fa-f]{4})\b', generated_output)
    assert len(hex_vals) > 0, "No hex values found in output"
    for h in hex_vals:
        val = int(h, 16)
        assert 0x0000 <= val <= 0xFFFF, f"0x{h} out of RGB565 range"


def test_foreground_defines(generated_output):
    assert 'WTHR_FG' in generated_output
    assert 'WTHR_FG_DIM' in generated_output


def test_has_include_guard(generated_output):
    assert '#ifndef WEATHER_COLORS_H' in generated_output
    assert '#define WEATHER_COLORS_H' in generated_output
    assert '#endif' in generated_output
