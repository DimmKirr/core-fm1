#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate build/gen/config.h from .config using Kconfiglib.

  tools/genconfig.py                    # generate from .config
  tools/genconfig.py --defconfig NAME   # load configs/NAME_defconfig first
"""
import argparse
import json
import os
from pathlib import Path
from kconfiglib import Kconfig

ROOT = Path(__file__).resolve().parents[1]

COMPONENT_SYMBOLS = {
    'AUDIO': 'audio',
    'DUAL_CORE': 'dual-core',
    'AUDIO_CORE_FELUCCA': 'audio-core:felucca',
    'AUDIO_CORE_SLOOP': 'audio-core:sloop',
    'AUDIO_CORE_MELODEE': 'audio-core:melodee',
    'ENGINE_FM6': 'engine:fm6',
    'ENGINE_DRUM': 'engine:drum',
    'ENGINE_PROPHET': 'engine:prophet',
    'ENGINE_CZ1': 'engine:cz1',
    'ENGINE_SID': 'engine:sid',
    'ENGINE_ANALOG': 'engine:analog',
    'ENGINE_DIGITAL': 'engine:digital',
    'ENGINE_PHASE': 'engine:phase',
    'ENGINE_SAMPLE': 'engine:sample',
    'ENGINE_GRANULAR': 'engine:granular',
    'ENGINE_LOFI': 'engine:lofi',
    'ENGINE_TRIO': 'engine:trio',
    'ENGINE_TONEWHEEL': 'engine:tonewheel',
    'ENGINE_FORMANT': 'engine:formant',
    'ENGINE_PHYSICAL': 'engine:physical',
    'ENGINE_NOISE': 'engine:noise',
    'EFFECTS_FELUCCA': 'effects:felucca',
    'EFFECTS_SLOOP': 'effects:sloop',
    'EFFECTS_MELODEE': 'effects:melodee',
    'MODULATION': 'modulation',
    'SEQ_FELUCCA': 'seq:felucca',
    'SEQ_SLOOP': 'seq:sloop',
    'SEQ_MELODEE': 'seq:melodee',
    'SCALES_FELUCCA': 'scales:felucca',
    'SCALES_MELODEE': 'scales:melodee',
    'MIDI_UART': 'midi-uart',
    'USB': 'usb',
    'MIDI_FELUCCA': 'midi:felucca',
    'MIDI_SLOOP': 'midi:sloop',
    'MIDI_MELODEE': 'midi:melodee',
    'USB_AUDIO': 'usb-audio',
    'INPUT': 'input',
    'UI_FELUCCA': 'ui:felucca',
    'UI_SLOOP': 'ui:sloop',
    'UI_MELODEE': 'ui:melodee',
    'FLASH': 'flash',
    'STORAGE_FELUCCA': 'storage:felucca',
    'STORAGE_SLOOP': 'storage:sloop',
    'STORAGE_MELODEE': 'storage:melodee',
    'BOOT_FELUCCA': 'boot:felucca',
    'BOOT_SLOOP': 'boot:sloop',
    'BOOT_MELODEE': 'boot:melodee',
}


def write_manifest(kconf, out_dir):
    enabled = []
    for sym_name, component_name in COMPONENT_SYMBOLS.items():
        sym = kconf.syms.get(sym_name)
        if sym and sym.str_value == 'y':
            enabled.append(component_name)
    manifest = {'components': sorted(enabled)}
    (out_dir / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')

    marker = '<<CFM1>>'
    manifest_str = marker + ','.join(sorted(enabled)) + marker
    lines = [
        '#ifndef CFM1_MANIFEST_H',
        '#define CFM1_MANIFEST_H',
        f'static const char cfm1_manifest[] = "{manifest_str}";',
        '#endif',
        '',
    ]
    (out_dir / 'manifest.h').write_text('\n'.join(lines))
    return enabled


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
    components = write_manifest(kconf, out)
    print(f'genconfig: wrote {out / "config.h"}')
    print(f'genconfig: manifest ({len(components)} components): {", ".join(components)}')


if __name__ == '__main__':
    main()
