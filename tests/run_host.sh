#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Run firmware/src/fmlab.c on this computer (tests/host.c): build/host/fmlab.wav and the screens as PNG.
#
#   tests/run_host.sh                       the built-in script
#   tests/run_host.sh tests/algorithms.txt  all 32 algorithms, drawn, as build/host/alg-*.png
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
script="$1"
out=build/host/fmlab
case "$script" in */algorithms.txt) out=build/host/alg ;; esac
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER="\"${APP_VER:-dev}\"" -Ifirmware/src -Ifirmware/gen -o build/host/fmlab firmware/src/fmlab.c
rm -f "$out"-*.ppm "$out"-*.png
build/host/fmlab "$out" $script
OUT="$out" python3 - <<'PY'
import os
from pathlib import Path
from PIL import Image
for p in sorted(Path("build/host").glob(Path(os.environ["OUT"]).name + "-*.ppm")):
    Image.open(p).resize((480, 480), Image.NEAREST).save(p.with_suffix(".png"))
    p.unlink()
    print("screen ->", p.with_suffix(".png"))
PY
