#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Unit tests for Kconfig loading and genconfig output."""
import subprocess
import tempfile
import shutil
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
KCONFIG = ROOT / 'Kconfig'


@pytest.fixture(autouse=True)
def skip_if_no_kconfig():
    if not KCONFIG.exists():
        pytest.skip('Kconfig not yet created')


def test_kconfig_parses():
    r = subprocess.run(
        ['python3', '-c',
         'from kconfiglib import Kconfig; '
         f'k = Kconfig("{KCONFIG}"); '
         'print(len(k.syms), "symbols")'],
        capture_output=True, text=True, cwd=str(ROOT))
    assert r.returncode == 0, r.stderr


def test_genconfig_writes_header():
    genconfig = ROOT / 'tools' / 'genconfig.py'
    if not genconfig.exists():
        pytest.skip('tools/genconfig.py not yet created')
    with tempfile.TemporaryDirectory() as tmp:
        project = Path(tmp)
        shutil.copy(ROOT / 'Kconfig', project)
        for directory in ('kconf', 'configs', 'tools'):
            shutil.copytree(ROOT / directory, project / directory,
                            ignore=shutil.ignore_patterns('__pycache__'))
        r = subprocess.run(
            [sys.executable, str(project / 'tools/genconfig.py'), '--defconfig', 'minimal'],
            capture_output=True, text=True, cwd=tmp)
        assert r.returncode == 0, r.stderr
        header = project / 'build' / 'gen' / 'config.h'
        assert header.exists(), 'config.h not generated'
        content = header.read_text()
        assert '#define' in content
        for symbol in ('AUDIO', 'INPUT', 'FLASH', 'MIDI_UART', 'USB', 'DUAL_CORE'):
            assert f'#define CONFIG_{symbol} ' not in content


def test_menuconfig_imports_library():
    r = subprocess.run([sys.executable, '-c',
        "import runpy; m = runpy.run_path('tools/menuconfig.py'); "
        "assert m['menuconfig'].__module__ == 'menuconfig'"],
        cwd=ROOT, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr


DEFCONFIGS = ['melodee', 'felucca', 'sloop', 'minimal']


@pytest.mark.parametrize('name', DEFCONFIGS)
def test_defconfig_loads(name):
    dc = ROOT / 'configs' / f'{name}_defconfig'
    if not dc.exists():
        pytest.skip(f'configs/{name}_defconfig not yet created')
    r = subprocess.run(
        ['python3', '-c',
         'from kconfiglib import Kconfig; '
         f'k = Kconfig("{KCONFIG}"); '
         f'k.load_config("{dc}"); '
         'enabled = sum(1 for s in k.syms.values() if s.str_value == "y"); '
         f'print("{name}:", enabled, "enabled")'],
        capture_output=True, text=True, cwd=str(ROOT))
    assert r.returncode == 0, f'{name}: {r.stderr}'
