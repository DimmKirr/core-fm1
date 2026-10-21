#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Interactive menuconfig for core-fm1."""
import os
import sys
from pathlib import Path

# This wrapper has the same filename as Kconfiglib's UI module.
# Exclude tools/ from import lookup to avoid importing this file again.
sys.path = [p for p in sys.path if Path(p or '.').resolve() != Path(__file__).resolve().parent]
from kconfiglib import Kconfig
from menuconfig import menuconfig

ROOT = Path(__file__).resolve().parents[1]


def main():
    os.environ['srctree'] = str(ROOT)
    os.environ['KCONFIG_CONFIG'] = str(ROOT / '.config')
    kconf = Kconfig(str(ROOT / 'Kconfig'), warn_to_stderr=True)
    config = ROOT / '.config'
    if config.exists():
        kconf.load_config(str(config))
    menuconfig(kconf)
    kconf.write_config(str(config))


if __name__ == '__main__':
    main()
