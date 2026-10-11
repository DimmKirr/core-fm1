#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Nightly site generation preserves releases/editor and rejects mismatched builds."""
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "web"))
from make_site import main
from publish_nightly import publish


def package(path, product, variant):
    raw = bytearray(1024)
    for i in range(20):
        raw[i * 0x30 + 0x2F] = (ord(product[i]) + i + 1) & 255 if i < len(product) else 0x7D
    raw[960:976] = b"FELUCCA-LOADER-1" + bytes([variant])
    path.write_bytes(raw)
    return path


class NightlySite(unittest.TestCase):
    def test_publish(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dual = package(root / "release.fwsc", "FM-1_91010", 1)
            single = package(root / "single.fwsc", "FM-1_91010", 2)
            normal = package(root / "next.fwsc", "FM-1_900", 3)
            slow = package(root / "slow.fwsc", "FM-1_900", 4)
            site = root / "site"
            main(dual, single, "1.1.0", site)
            editor = site / "webapp/editor/index.html"
            editor.write_text("existing published editor")
            # Exercise migration from the existing inline-only release metadata.
            (site / "firmware/release.json").unlink()
            publish(site, str(normal), str(slow), "next-abcdef0", "abcdef0" * 5 + "abcde", "2026-10-11T02:00:00Z")
            self.assertEqual(editor.read_text(), "existing published editor")
            self.assertEqual((site / "firmware/melodee-1.1.0.fwsc").read_bytes(), dual.read_bytes())
            self.assertEqual((site / "firmware/melodee-1.1.0-1core.fwsc").read_bytes(), single.read_bytes())
            manifest = json.loads((site / "firmware/nightly.json").read_text())
            self.assertEqual((manifest["product"], manifest["branch"]), ("FM-1_900", "next"))
            # Regenerating a release keeps both published nightly variants.
            main(dual, single, "1.1.0", site, installer_only=True)
            self.assertIn('"clock320"', (site / "webapp/installer/index.html").read_text())
            before = hashlib.sha256((site / "webapp/installer/index.html").read_bytes()).digest()
            wrong = package(root / "wrong.fwsc", "FM-1_9010", 5)
            for other in (wrong, normal):
                with self.assertRaises(SystemExit):
                    publish(site, str(normal), str(other), "next-abcdef0", "abcdef0" * 5 + "abcde", "2026-10-11T02:00:00Z")
                self.assertEqual(hashlib.sha256((site / "webapp/installer/index.html").read_bytes()).digest(), before)


if __name__ == "__main__":
    unittest.main()
