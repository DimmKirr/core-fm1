#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Copy the screens tests/docshots.txt took into docs/, under the names the README uses.

    tests/run_host.sh tests/docshots.txt     (or tests/run_host.ps1 on Windows)
    python3 tools/docshots.py

The shots are taken in the order NAMES lists them; each is scaled 2x with no smoothing, so a pixel
of the FM-1's screen is a square of four.
"""
import re
import sys
from pathlib import Path

from PIL import Image

NAMES = ["spectrum", "screen", "scope", "envelope", "solo", "save"]

root = Path(__file__).resolve().parent.parent
shots = sorted((root / "build" / "host").glob("docshots-*.ppm"),
               key=lambda p: int(re.search(r"-(\d+)\.ppm$", p.name).group(1)))
if len(shots) != len(NAMES):
    sys.exit(f"expected {len(NAMES)} shots from tests/docshots.txt, found {len(shots)}: run it first")
for name, p in zip(NAMES, shots):
    out = root / "docs" / f"{name}.png"
    img = Image.open(p)
    img.resize((img.width * 2, img.height * 2), Image.NEAREST).save(out, optimize=True)
    print(p.name, "->", out.relative_to(root))
