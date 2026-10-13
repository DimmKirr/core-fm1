#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Publish a verified release candidate beside an existing site's stable and nightly builds."""
import argparse
import hashlib
import json
from pathlib import Path
from make_site import check_package, main, release_packages


def publish(out, release_dir):
    release_dir = Path(release_dir)
    info = json.loads((release_dir / "BUILD_INFO.json").read_text())
    version = info["version"]
    checksums = {name: digest for digest, name in
                 (line.split("  ", 1) for line in (release_dir / "SHA256SUMS").read_text().splitlines())}
    packages = {}
    for key, suffix, cores in (("dual", "", 1), ("single", "-1core", 0)):
        name = f"melodee-{version}{suffix}.fwsc"
        package = release_dir / name
        data, product = check_package(package)
        build = info["core_builds"][name]
        if (hashlib.sha256(data).hexdigest() != checksums.get(name) or
                checksums.get(name) != build["sha256"] or product != info["product"] or
                build["MELODEE_DUAL_CORE"] != cores):
            raise SystemExit(f"Release candidate package failed provenance/checksum checks: {name}")
        packages[key] = package
    stable, stable_packages = release_packages(out)
    main(stable_packages["dual"], stable_packages["single"], stable, out,
         rc={"pkg": packages, "version": version, "commit": info["commit"],
             "builtAt": info["builtAt"]}, installer_only=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", help="existing Pages checkout")
    parser.add_argument("release_dir", help="release assets including BUILD_INFO.json and SHA256SUMS")
    args = parser.parse_args()
    publish(args.out, args.release_dir)
