#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Nightly publishing preserves releases/editor, removes retired channels and rejects mismatched builds."""
import hashlib
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "web"))
from make_site import main, manual_figures
import manual_shots
from publish_nightly import publish
from publish_rc import publish as publish_rc
from import_releases import verify_pair


def package(path, product, variant):
    raw = bytearray(1024)
    for i in range(20):
        raw[i * 0x30 + 0x2F] = (ord(product[i]) + i + 1) & 255 if i < len(product) else 0x7D
    raw[960:976] = b"FELUCCA-LOADER-1" + bytes([variant])
    path.write_bytes(raw)
    return path


class NightlySite(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # A temp copy: a killed run must not leave screenshot stubs in web/manual/.
        cls._scratch = tempfile.TemporaryDirectory()
        cls.manual = Path(cls._scratch.name) / "manual"
        cls.manual.mkdir()
        shutil.copy(ROOT / "web" / "manual" / "index.html", cls.manual / "index.html")
        for name in manual_figures(cls.manual):
            (cls.manual / name).write_bytes(b"png")

    @classmethod
    def tearDownClass(cls):
        cls._scratch.cleanup()

    def build_site(self, pkg, single, version, out, **kwargs):
        kwargs.setdefault("manual", self.manual)
        main(pkg, single, version, out, **kwargs)

    def test_history_preserves_versions_and_core_variants(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            site = root / "site"
            old = package(root / "old.fwsc", "FM-1_91021", 1)
            old_single = package(root / "old-single.fwsc", "FM-1_91021", 2)
            current = package(root / "current.fwsc", "FM-1_91032", 3)
            current_single = package(root / "current-single.fwsc", "FM-1_91032", 4)
            self.build_site(old, old_single, "1.2.1", site)
            self.build_site(current, current_single, "1.3.2", site)
            releases = json.loads((site / "firmware/releases.json").read_text())
            self.assertEqual([r["version"] for r in releases], ["1.3.2", "1.2.1"])
            self.assertEqual([r["product"] for r in releases], ["FM-1_91032", "FM-1_91021"])
            for source, name in ((old, "melodee-1.2.1.fwsc"), (old_single, "melodee-1.2.1-1core.fwsc")):
                self.assertEqual((site / "firmware" / name).read_bytes(), source.read_bytes())
            publish(site, old, old_single, "next-abcdef0", "a" * 40, "2026-10-13T16:00:00Z")
            self.assertEqual(json.loads((site / "firmware/releases.json").read_text()), releases)
            self.assertIn('"product": "FM-1_91021"', (site / "webapp/installer/index.html").read_text())
            # An incomplete pair is never offered; a mismatched pair is refused.
            stray = site / "firmware/melodee-1.1.0.fwsc"
            package(stray, "FM-1_91010", 1)
            self.build_site(current, current_single, "1.3.2", site)
            self.assertFalse(stray.exists())
            package(stray, "FM-1_91010", 1)
            package(site / "firmware/melodee-1.1.0-1core.fwsc", "FM-1_91011", 2)
            with self.assertRaises(SystemExit):
                self.build_site(current, current_single, "1.3.2", site)

    def test_history_checksums_before_staging(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dual = package(root / "melodee-1.2.1.fwsc", "FM-1_91021", 1)
            single = package(root / "melodee-1.2.1-1core.fwsc", "FM-1_91021", 2)
            sums = root / "SHA256SUMS"
            sums.write_text("".join(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n" for p in (dual, single)))
            self.assertEqual(verify_pair(root, "1.2.1"), [dual.name, single.name])
            single.write_bytes(single.read_bytes() + b"corrupted")
            with self.assertRaisesRegex(ValueError, "checksum"):
                verify_pair(root, "1.2.1")

    def test_release_candidate(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dual = package(root / "release.fwsc", "FM-1_91032", 1)
            single = package(root / "single.fwsc", "FM-1_91032", 2)
            normal = package(root / "next.fwsc", "FM-1_900", 3)
            slow = package(root / "slow.fwsc", "FM-1_900", 4)
            site, assets = root / "site", root / "assets"
            assets.mkdir()
            version = "1.4.0-rc.1"
            rc_dual = package(assets / f"melodee-{version}.fwsc", "FM-1_91040", 5)
            rc_single = package(assets / f"melodee-{version}-1core.fwsc", "FM-1_91040", 6)
            builds = {p.name: {"MELODEE_DUAL_CORE": cores, "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
                      for p, cores in ((rc_dual, 1), (rc_single, 0))}
            (assets / "BUILD_INFO.json").write_text(json.dumps({"version": version, "product": "FM-1_91040",
                "commit": "a" * 40, "builtAt": "2026-10-13T16:40:00Z", "core_builds": builds}))
            (assets / "SHA256SUMS").write_text("".join(f"{info['sha256']}  {name}\n" for name, info in builds.items()))
            self.build_site(dual, single, "1.3.2", site)
            editor = site / "webapp/editor/index.html"
            editor.write_text("existing published editor")
            publish(site, normal, slow, "next-abcdef0", "b" * 40, "2026-10-13T16:00:00Z")
            publish_rc(site, assets)
            # Nightly and stable regeneration must retain the published RC bytes.
            publish(site, normal, slow, "next-abcdef0", "b" * 40, "2026-10-13T16:00:00Z")
            self.build_site(dual, single, "1.3.2", site, installer_only=True)
            self.assertEqual(editor.read_text(), "existing published editor")
            for p in (rc_dual, rc_single):
                self.assertEqual((site / "firmware" / p.name).read_bytes(), p.read_bytes())
            for name, source in (("melodee-1.3.2.fwsc", dual), ("melodee-1.3.2-1core.fwsc", single),
                                 ("melodee-nightly.fwsc", normal), ("melodee-nightly-1core.fwsc", slow)):
                self.assertEqual((site / "firmware" / name).read_bytes(), source.read_bytes())
            self.assertEqual(json.loads((site / "firmware/release.json").read_text())["version"], "1.3.2")
            rc = json.loads((site / "firmware/rc.json").read_text())
            self.assertEqual((rc["version"], rc["product"], rc["commit"]), (version, "FM-1_91040", "a" * 40))
            installer = site / "webapp/installer/index.html"
            self.assertIn('"rc": {"version": "1.4.0-rc.1"', installer.read_text())
            before = installer.read_bytes()
            rc_single.write_bytes(rc_single.read_bytes() + b"corrupted")
            with self.assertRaises(SystemExit):
                publish_rc(site, assets)
            self.assertEqual(installer.read_bytes(), before)

    def test_publish(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dual = package(root / "release.fwsc", "FM-1_91010", 1)
            single = package(root / "single.fwsc", "FM-1_91010", 2)
            normal = package(root / "next.fwsc", "FM-1_900", 3)
            slow = package(root / "slow.fwsc", "FM-1_900", 4)   # (the single-core nightly)
            site = root / "site"
            self.build_site(dual, single, "1.1.0", site)
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
            self.build_site(dual, single, "1.1.0", site, installer_only=True)
            self.assertIn('"single": "../../firmware/melodee-nightly-1core.fwsc"', (site / "webapp/installer/index.html").read_text())
            self.assertEqual((site / "firmware/melodee-nightly-1core.fwsc").read_bytes(), slow.read_bytes())
            before = hashlib.sha256((site / "webapp/installer/index.html").read_bytes()).digest()
            wrong = package(root / "wrong.fwsc", "FM-1_9010", 5)
            for other in (wrong, normal):
                with self.assertRaises(SystemExit):
                    publish(site, str(normal), str(other), "next-abcdef0", "abcdef0" * 5 + "abcde", "2026-10-11T02:00:00Z")
                self.assertEqual(hashlib.sha256((site / "webapp/installer/index.html").read_bytes()).digest(), before)

    def test_retired_synth_is_removed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dual = package(root / "release.fwsc", "FM-1_91010", 1)
            single = package(root / "single.fwsc", "FM-1_91010", 2)
            normal = package(root / "next.fwsc", "FM-1_900", 3)
            slow = package(root / "slow.fwsc", "FM-1_900", 4)
            site = root / "site"
            self.build_site(dual, single, "1.1.0", site)
            editor = site / "webapp/editor/index.html"
            editor.write_text("existing published editor")
            for update in ("nightly", "release"):
                # Stale or malformed metadata must not revive the retired channel.
                (site / "firmware/synth.json").write_text("retired metadata")
                for filename in ("melodee-synth.fwsc", "melodee-synth-1core.fwsc"):
                    (site / "firmware" / filename).write_bytes(b"retired firmware")
                retired = site / "webapp/synth-editor"
                retired.mkdir()
                (retired / "index.html").write_text("retired editor")
                if update == "nightly":
                    publish(site, str(normal), str(slow), "next-abcdef0", "abcdef0" * 5 + "abcde", "2026-10-11T02:00:00Z")
                else:
                    self.build_site(dual, single, "1.1.0", site, installer_only=True)
                self.assertFalse((site / "firmware/synth.json").exists())
                self.assertFalse(list((site / "firmware").glob("melodee-synth*.fwsc")))
                self.assertFalse(retired.exists())
                self.assertNotIn("synth", (site / "webapp/installer/index.html").read_text())
                self.assertEqual(editor.read_text(), "existing published editor")
                self.assertEqual((site / "firmware/melodee-1.1.0.fwsc").read_bytes(), dual.read_bytes())
                self.assertEqual((site / "firmware/melodee-1.1.0-1core.fwsc").read_bytes(), single.read_bytes())
                self.assertEqual((site / "firmware/melodee-nightly.fwsc").read_bytes(), normal.read_bytes())
                self.assertEqual((site / "firmware/melodee-nightly-1core.fwsc").read_bytes(), slow.read_bytes())


    def test_manual_publish_drops_older_versions(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dual = package(root / "release.fwsc", "FM-1_91010", 1)
            single = package(root / "single.fwsc", "FM-1_91010", 2)
            site = root / "site"
            self.build_site(dual, single, "1.1.0", site)
            manual = site / "webapp/manual"
            text = (manual / "index.html").read_text()
            self.assertIn("<h1>Manual</h1>", text)
            self.assertNotIn('id="manual-ver"', text)
            self.assertFalse((manual / "versions.json").exists())
            self.assertFalse(any(p.is_dir() and p.name.startswith("v") for p in manual.iterdir()))
            retired = manual / "v1.0.0"
            retired.mkdir()
            (retired / "index.html").write_text("old")
            (manual / "versions.json").write_text("[]\n")
            (manual / "gone.png").write_bytes(b"stale")
            (self.manual / "orphan.png").write_bytes(b"orphan")
            try:
                self.build_site(dual, single, "1.1.0", site)
            finally:
                (self.manual / "orphan.png").unlink(missing_ok=True)
            self.assertFalse(retired.exists())
            self.assertFalse((manual / "versions.json").exists())
            self.assertFalse((manual / "gone.png").exists())
            self.assertFalse((manual / "orphan.png").exists())
            self.assertTrue((manual / "home.png").is_file())
            self.assertIn("<h1>Manual</h1>", (manual / "index.html").read_text())

    def test_manual_figure_list_comes_from_the_html(self):
        self.assertEqual(manual_shots.figure_names(self.manual), manual_figures(self.manual))
        self.assertIn("home.png", manual_shots.figure_names(self.manual))

    def test_installer_only_leaves_the_manual(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dual = package(root / "release.fwsc", "FM-1_91010", 1)
            single = package(root / "single.fwsc", "FM-1_91010", 2)
            site = root / "site"
            self.build_site(dual, single, "1.1.0", site)
            manual = site / "webapp/manual"
            (manual / "index.html").write_text("stable manual")
            (manual / "retired.png").write_bytes(b"stay")
            editor = site / "webapp/editor/index.html"
            editor.write_text("existing published editor")
            empty = root / "empty-manual"
            empty.mkdir()
            (empty / "index.html").write_text('<img src="missing.png">')
            self.build_site(dual, single, "1.1.0", site, installer_only=True, manual=empty)
            self.assertEqual((manual / "index.html").read_text(), "stable manual")
            self.assertEqual((manual / "retired.png").read_bytes(), b"stay")
            self.assertEqual(editor.read_text(), "existing published editor")
            fresh = root / "fresh"
            self.build_site(dual, single, "1.1.0", fresh, installer_only=True, manual=empty)
            self.assertFalse((fresh / "webapp/manual").exists())

    def test_missing_screenshot_does_not_write_the_site(self):
        shot = self.manual / "home.png"
        saved = shot.read_bytes()
        shot.unlink()
        try:
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                dual = package(root / "release.fwsc", "FM-1_91010", 1)
                single = package(root / "single.fwsc", "FM-1_91010", 2)
                site = root / "site"
                site.mkdir()
                (site / "sentinel").write_text("keep")
                with self.assertRaises(SystemExit) as caught:
                    self.build_site(dual, single, "1.1.0", site)
                self.assertIn("home.png", str(caught.exception))
                self.assertEqual([p.name for p in site.iterdir()], ["sentinel"])
                self.assertEqual((site / "sentinel").read_text(), "keep")
        finally:
            shot.write_bytes(saved)


if __name__ == "__main__":
    unittest.main()
