#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Run firmware/src/fmlab.c on this computer (tests/host.c): build/host/fmlab.wav and the screens as PNG.
#
#   tests/run_host.sh                       the built-in script
#   tests/run_host.sh tests/algorithms.txt  all 32 algorithms, drawn, as build/host/algorithms-*.png
#   tests/run_host.sh tests/operators.txt   the operator switches and the A/B compare
#
# A script's output is named after it, so the runs do not overwrite each other.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
script="$1"
out=build/host/fmlab
if [ -n "$script" ]; then
    name=${script##*/}
    out=build/host/${name%.*}
fi
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
case "$script" in
*/operators.txt) python3 tests/wavcheck.py "$out".wav ;;    # the switches have to agree with the sound
esac
