#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate build/gen/config.h from .config using Kconfiglib.

  tools/genconfig.py                    # generate from .config
  tools/genconfig.py --defconfig NAME   # load configs/NAME_defconfig first
"""
import argparse
import os
from pathlib import Path
from kconfiglib import Kconfig

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--defconfig', help='load configs/<NAME>_defconfig')
    args = ap.parse_args()

    os.environ['srctree'] = str(ROOT)
    kconf = Kconfig(str(ROOT / 'Kconfig'), warn_to_stderr=True)
    config = ROOT / '.config'

    if args.defconfig:
        dc = ROOT / 'configs' / f'{args.defconfig}_defconfig'
        if not dc.exists():
            raise SystemExit(f'defconfig not found: {dc}')
        kconf.load_config(str(dc))
    elif config.exists():
        kconf.load_config(str(config))

    out = ROOT / 'build' / 'gen'
    out.mkdir(parents=True, exist_ok=True)
    kconf.write_autoconf(str(out / 'config.h'))
    kconf.write_config(str(config))
    print(f'genconfig: wrote {out / "config.h"}')


if __name__ == '__main__':
    main()
