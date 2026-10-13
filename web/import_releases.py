#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Stage checksum-verified stable GitHub release pairs in a Pages checkout.

Run before make_site/publish_nightly/publish_rc. Requires authenticated gh.
Already staged versions are left untouched; no device or published site is changed.
"""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

from make_site import check_package


def gh(*args):
    return subprocess.check_output(["gh", *args], text=True)


def verify_pair(folder, version):
    names = [f"melodee-{version}.fwsc", f"melodee-{version}-1core.fwsc"]
    sums = {}
    for line in (folder / "SHA256SUMS").read_text().splitlines():
        fields = line.split(maxsplit=1)
        if len(fields) == 2:
            sums[fields[1].lstrip("*")] = fields[0]
    builds = []
    for name in names:
        path = folder / name
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if sums.get(name) != digest:
            raise ValueError(f"{name}: published checksum does not match")
        builds.append(check_package(path))
    if builds[0][1] != builds[1][1] or builds[0][0] == builds[1][0]:
        raise ValueError(f"{version}: mismatched or identical core builds")
    return names


def import_releases(out, repo):
    fw = Path(out) / "firmware"
    fw.mkdir(parents=True, exist_ok=True)
    releases = json.loads(gh("release", "list", "--repo", repo, "--limit", "100",
                            "--json", "tagName,isPrerelease,isDraft"))
    for release in releases:
        tag = release["tagName"]
        if release["isDraft"] or release["isPrerelease"] or not re.fullmatch(r"v[1-9]\d*\.\d+\.\d+", tag):
            continue
        version = tag[1:]
        names = [f"melodee-{version}.fwsc", f"melodee-{version}-1core.fwsc"]
        if all((fw / name).is_file() for name in names):
            continue
        assets = json.loads(gh("release", "view", tag, "--repo", repo, "--json", "assets"))["assets"]
        if not set(names + ["SHA256SUMS"]).issubset({a["name"] for a in assets}):
            continue                         # early releases have no single-core pair
        with tempfile.TemporaryDirectory(prefix="melodee-releases-") as tmp:
            args = ["release", "download", tag, "--repo", repo, "--dir", tmp]
            for name in names + ["SHA256SUMS"]:
                args.extend(["--pattern", name])
            gh(*args)
            for name in verify_pair(Path(tmp), version):
                shutil.copyfile(Path(tmp) / name, fw / name)
        print(f"history: verified and staged {version}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out")
    parser.add_argument("--repo", default="keremimo/melodee")
    args = parser.parse_args()
    import_releases(args.out, args.repo)
