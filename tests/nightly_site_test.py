#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Dual-core publishing preserves releases/editor and retires single-core channels."""
import hashlib
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "web"))
from make_site import main
from publish_nightly import publish
from publish_rc import publish as publish_rc
from import_releases import verify_release
import verify_nightly as verifier


def package(path, product, variant):
    raw = bytearray(1024)
    for i in range(20):
        raw[i * 0x30 + 0x2F] = (ord(product[i]) + i + 1) & 255 if i < len(product) else 0x7D
    raw[960:976] = b"FELUCCA-LOADER-1" + bytes([variant])
    path.write_bytes(raw)
    return path


class NightlySite(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.site = self.root / "site"
        self.dual = package(self.root / "release.fwsc", "FM-1_91032", 1)
        self.normal = package(self.root / "next.fwsc", "FM-1_900", 3)
        main(self.dual, "1.3.2", self.site)

    def tearDown(self):
        self.temp.cleanup()

    def nightly(self):
        publish(self.site, self.normal, "next-abcdef0", "b" * 40, "2026-10-15T16:00:00Z")

    def test_history_preserves_dual_core_releases_without_requiring_pairs(self):
        old = package(self.site / "firmware/melodee-1.2.1.fwsc", "FM-1_91021", 2)
        original = old.read_bytes()
        main(self.dual, "1.3.2", self.site)
        releases = json.loads((self.site / "firmware/releases.json").read_text())
        self.assertEqual([r["version"] for r in releases], ["1.3.2", "1.2.1"])
        self.assertTrue(all(set(r["pkg"]) == {"dual"} for r in releases))
        self.nightly()
        self.assertEqual(json.loads((self.site / "firmware/releases.json").read_text()), releases)
        self.assertEqual(old.read_bytes(), original)

    def test_history_checksums_before_staging(self):
        dual = package(self.root / "melodee-1.2.1.fwsc", "FM-1_91021", 1)
        (self.root / "SHA256SUMS").write_text(f"{hashlib.sha256(dual.read_bytes()).hexdigest()}  {dual.name}\n")
        self.assertEqual(verify_release(self.root, "1.2.1"), [dual.name])
        dual.write_bytes(dual.read_bytes() + b"corrupted")
        with self.assertRaisesRegex(ValueError, "checksum"):
            verify_release(self.root, "1.2.1")

    def test_release_candidate(self):
        assets = self.root / "assets"
        assets.mkdir()
        version = "1.4.0-rc.1"
        candidate = package(assets / f"melodee-{version}.fwsc", "FM-1_91040", 5)
        builds = {candidate.name: {"MELODEE_DUAL_CORE": 1, "sha256": hashlib.sha256(candidate.read_bytes()).hexdigest()}}
        info = {"version": version, "product": "FM-1_91040", "commit": "a" * 40,
                "builtAt": "2026-10-15T16:40:00Z", "core_builds": builds}
        (assets / "BUILD_INFO.json").write_text(json.dumps(info))
        (assets / "SHA256SUMS").write_text(f"{builds[candidate.name]['sha256']}  {candidate.name}\n")
        editor = self.site / "webapp/editor/index.html"
        editor.write_text("existing published editor")
        self.nightly()
        publish_rc(self.site, assets)
        self.nightly()
        main(self.dual, "1.3.2", self.site, installer_only=True)
        self.assertEqual(editor.read_text(), "existing published editor")
        self.assertEqual((self.site / "firmware" / candidate.name).read_bytes(), candidate.read_bytes())
        rc = json.loads((self.site / "firmware/rc.json").read_text())
        self.assertEqual((rc["version"], rc["product"], rc["commit"]), (version, "FM-1_91040", "a" * 40))
        self.assertEqual(set(rc["pkg"]), {"dual"})
        installer = self.site / "webapp/installer/index.html"
        before = installer.read_bytes()
        builds[candidate.name]["MELODEE_DUAL_CORE"] = 0
        (assets / "BUILD_INFO.json").write_text(json.dumps(info))
        with self.assertRaises(SystemExit):
            publish_rc(self.site, assets)
        builds[candidate.name]["MELODEE_DUAL_CORE"] = 1
        (assets / "BUILD_INFO.json").write_text(json.dumps(info))
        candidate.write_bytes(candidate.read_bytes() + b"corrupted")
        with self.assertRaises(SystemExit):
            publish_rc(self.site, assets)
        self.assertEqual(installer.read_bytes(), before)

    def test_legacy_metadata_migrates_to_dual_only_and_preserves_editor(self):
        editor = self.site / "webapp/editor/index.html"
        editor.write_text("existing published editor")
        manifest = self.site / "firmware/release.json"
        legacy = json.loads(manifest.read_text())
        legacy["pkg"]["single"] = "../../firmware/melodee-1.3.2-1core.fwsc"
        manifest.write_text(json.dumps(legacy))
        (self.site / "firmware/melodee-1.3.2-1core.fwsc").write_bytes(b"retired firmware")
        self.nightly()
        nightly_manifest = self.site / "firmware/nightly.json"
        legacy = json.loads(nightly_manifest.read_text())
        legacy["pkg"]["single"] = "../../firmware/melodee-nightly-1core.fwsc"
        nightly_manifest.write_text(json.dumps(legacy))
        (self.site / "firmware/melodee-nightly-1core.fwsc").write_bytes(b"retired nightly")
        main(self.dual, "1.3.2", self.site, installer_only=True)
        self.assertEqual(editor.read_text(), "existing published editor")
        for name in ("release", "nightly"):
            self.assertEqual(set(json.loads((self.site / f"firmware/{name}.json").read_text())["pkg"]), {"dual"})
        self.assertFalse(list((self.site / "firmware").glob("*-1core.fwsc")))
        html = (self.site / "webapp/installer/index.html").read_text()
        self.assertNotIn("build-single", html)
        self.assertNotIn("nightly-single", html)
        self.assertEqual((self.site / "firmware/melodee-1.3.2.fwsc").read_bytes(), self.dual.read_bytes())
        self.assertEqual((self.site / "firmware/melodee-nightly.fwsc").read_bytes(), self.normal.read_bytes())

    def test_inline_only_legacy_release_metadata_still_loads(self):
        manifest = self.site / "firmware/release.json"
        legacy = json.loads(manifest.read_text())
        legacy["pkg"]["single"] = "../../firmware/melodee-1.3.2-1core.fwsc"
        installer = self.site / "webapp/installer/index.html"
        installer.write_text("const meta = " + json.dumps(legacy) + ";\n")
        manifest.unlink()
        self.nightly()
        self.assertEqual(set(json.loads(manifest.read_text())["pkg"]), {"dual"})

    def test_live_verifier_checks_one_dual_package_and_rejects_legacy_pairs(self):
        build = self.root / "build"
        build.mkdir()
        (build / "nightly-commit.txt").write_text("b" * 40)
        (build / "melodee-nightly.fwsc").write_bytes(self.normal.read_bytes())
        metadata = {"commit": "b" * 40, "pkg": {"dual": "melodee-nightly.fwsc"}}
        requested = []

        def fetch(url, timeout):
            name = url.split("?", 1)[0].rsplit("/", 1)[1]
            requested.append(name)
            return io.BytesIO(json.dumps(metadata).encode() if name == "nightly.json" else self.normal.read_bytes())

        with patch.object(verifier.urllib.request, "urlopen", side_effect=fetch), patch.object(verifier.time, "sleep"):
            verifier.verify("https://example.test/", build)
            self.assertEqual(requested, ["nightly.json", "melodee-nightly.fwsc"])
            metadata["pkg"]["single"] = "melodee-nightly-1core.fwsc"
            with self.assertRaisesRegex(SystemExit, "dual-core firmware only"):
                verifier.verify("https://example.test/", build)

    def test_publish_rejects_invalid_nightly_before_writing(self):
        self.nightly()
        installer = self.site / "webapp/installer/index.html"
        before = installer.read_bytes()
        for firmware, commit in ((self.dual, "b" * 40), (self.normal, "short")):
            with self.assertRaises(SystemExit):
                publish(self.site, firmware, "next-abcdef0", commit, "2026-10-15T16:00:00Z")
        self.assertEqual(installer.read_bytes(), before)

    def test_retired_synth_is_removed(self):
        self.nightly()
        for update in (self.nightly, lambda: main(self.dual, "1.3.2", self.site, installer_only=True)):
            (self.site / "firmware/synth.json").write_text("retired metadata")
            for filename in ("melodee-synth.fwsc", "melodee-synth-1core.fwsc"):
                (self.site / "firmware" / filename).write_bytes(b"retired firmware")
            retired = self.site / "webapp/synth-editor"
            retired.mkdir()
            (retired / "index.html").write_text("retired editor")
            update()
            self.assertFalse((self.site / "firmware/synth.json").exists())
            self.assertFalse(list((self.site / "firmware").glob("melodee-synth*.fwsc")))
            self.assertFalse(retired.exists())

    def test_target_build_rejects_single_core_before_generating_artifacts(self):
        result = subprocess.run([sys.executable, str(ROOT / "tools/build.py"), "--nightly"],
                                env={**os.environ, "MELODEE_DUAL_CORE": "0"}, capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("Single-core firmware is retired", result.stderr)


if __name__ == "__main__":
    unittest.main()
