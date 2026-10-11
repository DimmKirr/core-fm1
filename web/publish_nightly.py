#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Update an existing Pages checkout with two next nightlies; preserve its release/editor."""
import argparse
import json
import re
from pathlib import Path
from make_site import main


def publish(out, dual, clock320, version, commit, built_at):
    out = Path(out)
    manifest = out / "firmware/release.json"
    if manifest.exists():
        release = json.loads(manifest.read_text())
    else:  # Existing sites predate the release manifest.
        html = (out / "webapp/installer/index.html").read_text()
        match = re.search(r"^const meta = (\{.*\});$", html, re.M)
        if not match:
            raise SystemExit("Pages installer has no release metadata")
        release = json.loads(match[1])
    packages = {k: out / "firmware" / Path(v).name for k, v in release["pkg"].items()}
    main(packages["dual"], packages["single"], release["version"], out,
         nightly={"pkg": {"dual": dual, "clock320": clock320}, "version": version,
                  "commit": commit, "builtAt": built_at}, installer_only=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out")
    parser.add_argument("--dual", required=True)
    parser.add_argument("--clock320", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--built-at", required=True)
    args = parser.parse_args()
    publish(args.out, args.dual, args.clock320, args.version, args.commit, args.built_at)
