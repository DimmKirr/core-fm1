#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""E2E: configure -> build -> verify component manifest in binary -> boot.

Three checks per defconfig:
  1. manifest.json from genconfig matches expected component list
  2. compiled binary contains <<CFM1>>...<<CFM1>> with the same list
  3. firmware boots in the emulator without halting

Results saved to tests/results/<timestamp>-e2e_components_test/:
  manifest.json          — expected vs actual component list
  binary-manifest.json   — expected vs found from compiled binary
  build.log              — build stdout/stderr
  screenshots/           — LCD after boot
  <defconfig>-*.log      — emulator logs
"""
import json
import subprocess
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from e2e_fixtures import E2ETestCase, _run_timestamp, RESULTS_DIR

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
FWSC = BUILD / 'core-fm1.fwsc'
MANIFEST = BUILD / 'gen' / 'manifest.json'
MARKER = b'<<CFM1>>'

TEST_RESULTS = RESULTS_DIR / f'{_run_timestamp}-e2e_components_test'


def genconfig(defconfig):
    r = subprocess.run(
        [sys.executable, str(ROOT / 'tools' / 'genconfig.py'),
         '--defconfig', defconfig],
        capture_output=True, text=True, cwd=str(ROOT), timeout=60)
    if r.returncode != 0:
        raise RuntimeError(f'genconfig failed:\n{r.stderr}')
    return r


def build_firmware(defconfig):
    r = subprocess.run(
        [sys.executable, str(ROOT / 'tools' / 'build.py'),
         '--defconfig', defconfig],
        capture_output=True, text=True, cwd=str(ROOT), timeout=300)
    TEST_RESULTS.mkdir(parents=True, exist_ok=True)
    (TEST_RESULTS / 'build.log').write_text(
        f'--- stdout ---\n{r.stdout}\n--- stderr ---\n{r.stderr}\n'
        f'--- exit {r.returncode} ---\n')
    return r


def read_manifest():
    return sorted(json.loads(MANIFEST.read_text())['components'])


def find_manifest_in_binary():
    for name in ('text.bin', 'core-fm1.bin', 'body.bin'):
        path = BUILD / name
        if not path.exists():
            continue
        data = path.read_bytes()
        start = data.find(MARKER)
        if start < 0:
            continue
        start += len(MARKER)
        end = data.find(MARKER, start)
        if end < 0:
            continue
        return sorted(data[start:end].decode('ascii').split(','))
    return None


def save_result(filename, data):
    TEST_RESULTS.mkdir(parents=True, exist_ok=True)
    content = json.dumps(data, indent=2) + '\n' if isinstance(data, (list, dict)) else str(data) + '\n'
    (TEST_RESULTS / filename).write_text(content)


MINIMAL = sorted([
    'audio',
    'audio-core:melodee',
    'boot:melodee',
    'effects:melodee',
    'scales:melodee',
    'seq:melodee',
])


class TestMinimalComponents(E2ETestCase):
    """minimal defconfig: HAL + Audio + Melodee defaults."""

    DEFCONFIG = 'minimal'
    EXPECTED = MINIMAL
    _build_ok = False

    @classmethod
    def setUpClass(cls):
        TEST_RESULTS.mkdir(parents=True, exist_ok=True)
        genconfig(cls.DEFCONFIG)
        r = build_firmware(cls.DEFCONFIG)
        cls._build_ok = r.returncode == 0
        try:
            super().setUpClass()
        except unittest.SkipTest:
            pass

    def test_01_manifest_matches(self):
        """genconfig manifest.json lists the expected components."""
        actual = read_manifest()
        save_result('manifest.json',
                    {'defconfig': self.DEFCONFIG, 'expected': self.EXPECTED,
                     'actual': actual, 'pass': actual == self.EXPECTED})
        self.assertEqual(actual, self.EXPECTED)

    def test_02_binary_manifest(self):
        """Compiled binary contains the manifest marker with correct components."""
        if not self._build_ok:
            self.skipTest('Build failed; binary not available')
        found = find_manifest_in_binary()
        save_result('binary-manifest.json',
                    {'defconfig': self.DEFCONFIG, 'expected': self.EXPECTED,
                     'found': found})
        self.assertIsNotNone(found, 'Manifest marker not found in binary')
        self.assertEqual(found, self.EXPECTED)

    def test_03_boots(self):
        """Firmware boots in the emulator without halting."""
        if not self._build_ok:
            self.skipTest('Build failed; no .fwsc to boot')
        if not FWSC.is_file():
            self.skipTest('No .fwsc produced')
        if not self._browser:
            self.skipTest('Emulator not available')
        page = self.new_emu_page()
        try:
            page.locator('#open').set_input_files(str(FWSC))
            page.wait_for_function(
                'document.getElementById("status").textContent.includes("ready")')
            page.locator('#start').click()
            page.wait_for_function(
                'document.getElementById("status")?.textContent'
                '?.includes("instructions/s")',
                timeout=30_000)
            page.wait_for_timeout(5_000)
            status = page.locator('#status').text_content()
            self.assertNotIn('halted', status)
            self.screenshot(page, f'{self.DEFCONFIG}-boot')
        finally:
            self.write_log(page, f'{self.DEFCONFIG}-components.log')
            page.close()


if __name__ == '__main__':
    unittest.main()
