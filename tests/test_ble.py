"""Host-compiled BLE C tests (no hardware, no embedded toolchain).

Each test compiles a C source from the vendored Optimist BLE stack on the
host with ``cc``, runs it as a native executable, and checks the exit code.
This integrates with ``task test:unit`` (pytest discovers it automatically).
"""
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
OPTIMIST = ROOT / 'firmware' / 'upstream' / 'optimist'
OPTIMIST_BLE = OPTIMIST / 'firmware' / 'src' / 'ble'
OPTIMIST_TESTS = OPTIMIST / 'tests'
CFM1_BLE = ROOT / 'firmware' / 'src' / 'ble'


def compile_and_run(source: Path, extra_includes: list[Path] | None = None,
                    defines: list[str] | None = None) -> str:
    """Compile a C test on the host and run it. Returns stdout."""
    includes = [OPTIMIST_BLE]
    if extra_includes:
        includes.extend(extra_includes)
    defs = defines or []
    with tempfile.NamedTemporaryFile(suffix='', delete=False) as tmp:
        exe = tmp.name
    cmd = ['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
           '-Wno-unused-function', '-O2', '-o', exe, str(source)]
    for inc in includes:
        cmd.extend(['-I', str(inc)])
    for d in defs:
        cmd.extend(['-D', d])
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    if result.returncode != 0:
        Path(exe).unlink(missing_ok=True)
        pytest.fail(f'compilation of {source.name} failed:\n{result.stderr}')
    result = subprocess.run([exe], capture_output=True, text=True, timeout=60)
    Path(exe).unlink(missing_ok=True)
    if result.returncode != 0:
        pytest.fail(f'{source.name} failed:\n{result.stdout}\n{result.stderr}')
    return result.stdout


def test_ble_midi_codec():
    """BLE-MIDI encoder/decoder round-trip (20,000 random events)."""
    out = compile_and_run(OPTIMIST_TESTS / 'ble_midi_test.c')
    assert 'FAIL' not in out
    assert 'all passed' in out


def test_ble_scan_parser():
    """AD parser, device table, ageing, signal bars."""
    out = compile_and_run(OPTIMIST_TESTS / 'ble_scan_test.c')
    assert 'FAIL' not in out
    assert 'all passed' in out


def test_ble_hid_report():
    """Xbox HID report to MIDI CC conversion."""
    out = compile_and_run(
        ROOT / 'tests' / 'ble_hid_test.c',
        extra_includes=[OPTIMIST_BLE, CFM1_BLE],
    )
    assert 'FAIL' not in out
    assert 'all passed' in out


def test_ble_gatt_integration():
    """Full BLE stack: connect, GATT discover, BLE-MIDI subscribe, MIDI exchange."""
    out = compile_and_run(ROOT / 'tests' / 'ble_gatt_test.c')
    assert 'FAIL' not in out
    assert 'all passed' in out


def test_ble_file_transfer():
    """BLE file sharing simulation: list 3 files over GATT via SysEx protocol."""
    out = compile_and_run(ROOT / 'tests' / 'ble_file_test.c')
    assert 'FAIL' not in out
    assert 'all passed' in out
    assert 'preset_01.fwp' in out
    assert 'wavetable.bin' in out
    assert 'firmware.fwsc' in out
