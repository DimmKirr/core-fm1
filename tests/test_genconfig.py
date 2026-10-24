#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Unit tests for Kconfig loading and genconfig output.

Results are written to tests/results/<timestamp>-test_genconfig/<subtest>/.
"""
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
KCONFIG = ROOT / 'Kconfig'
RESULTS_DIR = Path(__file__).resolve().parent / 'results'
_run_ts = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H%M%SZ')
RUN_DIR = RESULTS_DIR / f'{_run_ts}-test_genconfig'


def _outdir(name):
    d = RUN_DIR / name
    d.mkdir(parents=True, exist_ok=True)
    return d


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
    out = _outdir('header_minimal')
    r = subprocess.run(
        [sys.executable, str(genconfig), '--defconfig', 'minimal',
         '--outdir', str(out)],
        capture_output=True, text=True, cwd=str(ROOT))
    assert r.returncode == 0, r.stderr
    header = out / 'config.h'
    assert header.exists(), 'config.h not generated'
    content = header.read_text()
    assert '#define' in content
    assert '#define CONFIG_AUDIO 1' in content
    assert '#define CONFIG_USB 1' in content
    for symbol in ('INPUT', 'FLASH', 'MIDI_UART', 'DUAL_CORE'):
        assert f'#define CONFIG_{symbol} ' not in content
    assert (out / '.config').exists(), '.config not written to outdir'


def test_menuconfig_imports_library():
    r = subprocess.run([sys.executable, '-c',
        "import runpy; m = runpy.run_path('tools/menuconfig.py'); "
        "assert m['menuconfig'].__module__ == 'menuconfig'"],
        cwd=ROOT, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr


def test_genconfig_writes_manifest():
    genconfig = ROOT / 'tools' / 'genconfig.py'
    if not genconfig.exists():
        pytest.skip('tools/genconfig.py not yet created')
    out = _outdir('manifest_minimal')
    r = subprocess.run(
        [sys.executable, str(genconfig), '--defconfig', 'minimal',
         '--outdir', str(out)],
        capture_output=True, text=True, cwd=str(ROOT))
    assert r.returncode == 0, r.stderr
    import json
    manifest = out / 'manifest.json'
    assert manifest.exists(), 'manifest.json not generated'
    data = json.loads(manifest.read_text())
    assert 'components' in data
    assert 'audio' in data['components']
    assert 'audio-core:melodee' in data['components']
    manifest_h = out / 'manifest.h'
    assert manifest_h.exists(), 'manifest.h not generated'
    content = manifest_h.read_text()
    assert '<<CFM1>>' in content
    assert 'cfm1_manifest' in content


DEFCONFIGS = ['melodee', 'felucca', 'sloop', 'minimal', 'core_fm1_dev',
              'felucca_noise']


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


# -- component manifest assertions per defconfig --

EXPECTED_COMPONENTS = {
    'minimal': sorted([
        'audio',
        'audio-core:melodee',
        'boot:melodee',
        'controls:felucca',
        'effects:melodee',
        'midi:melodee',
        'scales:melodee',
        'seq:melodee',
        'usb',
    ]),
    'core_fm1_dev': sorted([
        'audio',
        'audio-core:melodee',
        'ble',
        'ble-bond',
        'ble-central',
        'ble-ft',
        'ble-midi',
        'boot:melodee',
        'controls:felucca',
        'debug-log',
        'effects:melodee',
        'engine:analog',
        'engine:fm6',
        'flash',
        'input',
        'midi:melodee',
        'ota',
        'scales:melodee',
        'seq:melodee',
        'storage:melodee',
        'udisk',
        'ui:felucca',
        'usb',
        'usb-host',
    ]),
    'felucca_noise': sorted([
        'audio',
        'audio-core:melodee',
        'ble',
        'ble-ft',
        'ble-midi',
        'controls:felucca',
        'debug-log',
        'effects:melodee',
        'engine:noise',
        'input',
        'midi:melodee',
        'scales:melodee',
        'seq:melodee',
        'udisk',
        'ui:felucca',
        'boot:melodee',
        'usb',
        'usb-host',
    ]),
}


@pytest.mark.parametrize('name,expected', [
    pytest.param(n, c, id=n) for n, c in EXPECTED_COMPONENTS.items()
])
def test_manifest_components(name, expected):
    """genconfig produces the expected component manifest for each defconfig."""
    dc = ROOT / 'configs' / f'{name}_defconfig'
    if not dc.exists():
        pytest.skip(f'configs/{name}_defconfig not yet created')
    out = _outdir(f'manifest_{name}')
    r = subprocess.run(
        [sys.executable, str(ROOT / 'tools' / 'genconfig.py'),
         '--defconfig', name, '--outdir', str(out)],
        capture_output=True, text=True, cwd=str(ROOT))
    assert r.returncode == 0, r.stderr
    import json
    manifest = out / 'manifest.json'
    assert manifest.exists(), 'manifest.json not generated'
    actual = sorted(json.loads(manifest.read_text())['components'])
    assert actual == expected, (
        f'{name} manifest mismatch:\n'
        f'  expected: {expected}\n'
        f'  actual:   {actual}'
    )


def test_usb_host_depends_on_usb():
    """CONFIG_USB_HOST must not be selectable without CONFIG_USB."""
    from kconfiglib import Kconfig
    k = Kconfig(str(KCONFIG), warn=False)
    k.load_config(str(ROOT / 'configs' / 'minimal_defconfig'))
    usb = k.syms['USB']
    usb_host = k.syms['USB_HOST']
    usb.set_value(0)
    usb_host.set_value(2)  # try to force y
    assert usb_host.str_value != 'y', \
        'USB_HOST must depend on USB: should not be selectable when USB=n'
