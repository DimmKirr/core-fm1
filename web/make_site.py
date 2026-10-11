#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Make the site (GitHub Pages):

  index.html                        redirect to the installer (the old URL keeps working)
  firmware/melodee-VER.fwsc         the package, both cores (+ LICENSE, LICENSING.md, LICENSES/: the
                                    packages hold JieLi SDK files under Apache-2.0, see LICENSING.md)
  firmware/melodee-VER-1core.fwsc   the same release built with MELODEE_DUAL_CORE=0, for FM-1s that crash
                                    with the dual-core build; the installer offers both
  webapp/installer/index.html       index_pkg.html, self-contained (fm1pkg.js, fm1ota.js, metadata inlined)
  webapp/editor/index.html          editor.html (+ fukiai.ttf, FUKIAI-LICENSE.txt)
  src/                              not touched

  web/make_site.py build/melodee-X.Y.fwsc build/melodee-X.Y-1core.fwsc X.Y OUT_DIR

The packages must be ones made by tools/fm1pkg_make.py (Melodee's own loader, no vendor files), from one
release: their identity (FM-1_9xx, FM-1_9xxx from 0.10, FM-1_9xxxx for X.Y.Z) is read from them and must be
the same; the device must report it after the install.
"""
import json
import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F


def strip_module(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def check_package(pkg):
    """the package's bytes and identity; refuses anything that is not a Melodee package"""
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_9\d{2,4}", product):
        raise SystemExit(f"{pkg}: identity {product!r} is not a Melodee package (FM-1_9 and two to four digits)")
    if b"FELUCCA-LOADER-1" not in raw:              # Melodee loader marker: never publish a package with vendor files
        raise SystemExit(f"{pkg}: no Melodee loader in it; the site ships only fm1pkg_make.py packages "
                         "(a package patched from an official one carries vendor files)")
    return raw, product


def main(pkg, single, version, out, nightly=None, installer_only=False):
    pkg, single, out = Path(pkg), Path(single), Path(out)
    raw, product = check_package(pkg)
    raw1, product1 = check_package(single)
    if product1 != product:
        raise SystemExit(f"{single}: identity {product1}, {pkg} is {product}: build both from one release")
    if raw1 == raw:
        raise SystemExit(f"{single} is {pkg}: the single-core package is built with MELODEE_DUAL_CORE=0")
    if nightly is None and (out / "firmware/nightly.json").exists():
        nightly = json.loads((out / "firmware/nightly.json").read_text())
        nightly["pkg"] = {k: str(out / "firmware" / Path(v).name) for k, v in nightly["pkg"].items()}
    nightly_files = {}
    nightly_meta = None
    if nightly:
        if not re.fullmatch(r"[0-9a-f]{40}", nightly["commit"]):
            raise SystemExit("nightly commit must be a full Git SHA")
        for key, suffix in (("dual", ""), ("clock320", "-320")):
            path = Path(nightly["pkg"][key])
            data, identity = check_package(path)
            nightly_files[key] = (path, data, identity, f"melodee-nightly{suffix}.fwsc")
        if nightly_files["dual"][2] != nightly_files["clock320"][2]:
            raise SystemExit("nightly variants must have the same package identity")
        if nightly_files["dual"][1] == nightly_files["clock320"][1]:
            raise SystemExit("nightly variants must be distinct builds")
        nightly_meta = {"version": nightly["version"], "product": nightly_files["dual"][2],
                        "branch": "next", "commit": nightly["commit"], "builtAt": nightly["builtAt"],
                        "pkg": {k: "../../firmware/" + f[3] for k, f in nightly_files.items()}}
    html = (HERE / "index_pkg.html").read_text(encoding="utf-8")
    lib = strip_module((HERE / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1ota.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1backup.js").read_text(encoding="utf-8"))
    v = re.sub(r'[^A-Za-z0-9.-]', '-', version)
    name, name1 = f"melodee-{v}.fwsc", f"melodee-{v}-1core.fwsc"
    release_meta = {"version": version, "product": product,
                    "pkg": {"dual": "../../firmware/" + name, "single": "../../firmware/" + name1}}
    meta = json.dumps({**release_meta, "nightly": nightly_meta})
    for mark in ("/*LIB*/", "/*META*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"index_pkg.html must contain {mark} once; update make_site.py")
    html = html.replace("/*LIB*/", lib).replace("/*META*/", meta)
    inst, ed, fw = out / "webapp" / "installer", out / "webapp" / "editor", out / "firmware"
    for d in (inst, ed, fw):
        d.mkdir(parents=True, exist_ok=True)
    keep = {name, name1, "melodee-nightly.fwsc", "melodee-nightly-320.fwsc"}
    for old in fw.glob("melodee-*.fwsc"):
        if old.name not in keep:
            old.unlink()
    (inst / "index.html").write_text(html, encoding="utf-8")
    for src, dest in ((pkg, fw / name), (single, fw / name1)):
        if src.resolve() != dest.resolve():
            shutil.copy(src, dest)
    (fw / "release.json").write_text(json.dumps(release_meta, indent=2) + "\n")
    if nightly_meta:
        for path, data, identity, filename in nightly_files.values():
            (fw / filename).write_bytes(data)
        (fw / "nightly.json").write_text(json.dumps(nightly_meta, indent=2) + "\n")
    lic = HERE.parent / "LICENSES"                  # the packages hold JieLi SDK files (Apache-2.0): their
    (fw / "LICENSES").mkdir(exist_ok=True)          # licence travels next to it, with Melodee's own
    names = sorted(f.name for f in lic.glob("*.txt"))
    for n in names:
        shutil.copy(lic / n, fw / "LICENSES" / n)
    (fw / "LICENSES" / "index.html").write_text(    # the installer links this folder: Pages lists no folders
        '<!doctype html><meta charset="utf-8"><title>Melodee licences</title><h1>Licence texts</h1><ul>'
        + "".join(f'<li><a href="{n}">{n}</a></li>' for n in names)
        + '</ul><p><a href="../LICENSING.md">LICENSING.md</a> · <a href="../LICENSE">LICENSE (GPL-3.0)</a></p>\n',
        encoding="utf-8")
    for doc in ("LICENSE", "LICENSING.md"):
        shutil.copy(HERE.parent / doc, fw / doc)
    if not installer_only:
        shutil.copy(HERE / "editor.html", ed / "index.html")
    for f in ("fukiai.ttf", "FUKIAI-LICENSE.txt", "fm1backup.js"):
        if not installer_only and (HERE / f).exists():
            shutil.copy(HERE / f, ed / f)
    if not installer_only:
        (out / "index.html").write_text(
            '<!doctype html><meta charset="utf-8"><title>Melodee</title>'
            '<meta http-equiv="refresh" content="0; url=webapp/installer/">'
            '<a href="webapp/installer/">Melodee installer</a>\n', encoding="utf-8")
    print(f"site: {out}: webapp/installer ({len(html)} B), webapp/editor, firmware/{name} ({len(raw)} B) and "
          f"{name1} ({len(raw1)} B), {product}")


if __name__ == "__main__":
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    main(*sys.argv[1:5])
