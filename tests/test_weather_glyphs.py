#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Unit tests for tools/gen_weather_glyphs.py."""
import re
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'tools' / 'gen_weather_glyphs.py'

EXPECTED_GLYPHS = [
    'SUN', 'PARTIAL_CLOUD', 'CLOUD', 'DRIZZLE',
    'RAIN', 'SNOW', 'THUNDER', 'UNKNOWN',
]


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


def test_all_glyph_names_present(generated_output):
    for name in EXPECTED_GLYPHS:
        assert f'WTHR_G_{name}' in generated_output, f"Missing glyph: WTHR_G_{name}"


def test_each_glyph_array_has_32_elements(generated_output):
    for name in EXPECTED_GLYPHS:
        pattern = rf'wthr_glyph_{name.lower()}\[32\]'
        assert re.search(pattern, generated_output), \
            f"Glyph {name} missing [32] array declaration"


def test_lookup_function_present(generated_output):
    assert 'weather_glyph_for_code' in generated_output
    assert 'int weather_glyph_for_code(uint8_t wmo_code)' in generated_output


def test_draw_function_present(generated_output):
    assert 'weather_draw_glyph' in generated_output
    assert 'cv_rect' in generated_output


def test_enum_count(generated_output):
    assert f'WTHR_G_COUNT = {len(EXPECTED_GLYPHS)}' in generated_output


def test_has_include_guard(generated_output):
    assert '#ifndef WEATHER_GLYPHS_H' in generated_output
    assert '#define WEATHER_GLYPHS_H' in generated_output
    assert '#endif' in generated_output
