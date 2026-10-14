#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Relyt29 (@relyt29), Signals Intelligence Sound (https://sigintsound.com/)
"""Copy a fixed allowlist of NIGHT UI renders into the current web manual.

Renders come from tests/ui_render.c -> tests/ui_render.py, under build/ui_new/NIGHT.
web/manual/index.html embeds them, and web/make_site.py publishes that folder as
webapp/manual/. The manual is the current build only.

The PNGs are generated, not committed. CI writes them before publishing the site.
web/quick-host.sh writes them when the manual is missing figures.

  tools/manual_shots.py            # build/ui_new/NIGHT → web/manual/
  tools/manual_shots.py --require  # exit 0 when every figure is already present
  tools/manual_shots.py --sources  # exit 0 when every source render exists
  tools/manual_shots.py --check    # list missing / stale, write nothing

Only NIGHT is used (the manual's palette). Add a name here and reference it from
web/manual/index.html as <img src="name.png">.
"""
import argparse
import filecmp
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "build" / "ui_new" / "NIGHT"
MANUAL = ROOT / "web" / "manual"

# one clear figure per page family; the device's own NIGHT renders (240 x 240).
SHOTS = [
    # Stage
    "home.png", "home_note.png", "stage_p5.png", "stage_cz.png", "stage_drum.png", "stage_filter.png",
    # Sounds / names / sheets
    "presets.png", "presets_cat.png", "user.png", "name_typing.png", "ref_sheet_sound.png",
    # Projects
    "project.png", "project_new.png", "new_key.png", "new_roles.png",
    # Envelopes and modulation
    "env.png", "env_dest.png", "lfo.png", "mod.png",
    # Effects: track inserts and the shared return
    "fx_phaser.png", "fx_echo.png", "fx_reverb.png", "fx_send.png", "fx_bus1.png",
    # Hold layers
    "layer_glo_active.png", "layer_scl_active.png", "layer_edit_active.png",
    # Scales and chords
    "scl.png", "scale_picker_favorites.png", "chord.png",
    # Arpeggiator
    "arp.png",
    # Sequencer
    "notes_mixed.png", "step.png", "pattern.png", "tempo.png", "patterns.png", "patterns_sheet.png",
    "song.png", "ref_sheet_song.png", "ref_motion.png", "drum.png", "ref_sheet_note.png",
    # Mixer / global
    "mixer.png", "global.png", "system.png",
    # Menu
    "menu.png", "menu_hold.png", "menu_autosave_on.png", "menu_usb_stereo.png",
    # Engines
    "ref_map_p5.png", "ref_edit_osc.png", "fm6_alg_01.png", "fm6_eg.png", "fm6_freq.png",
    "fm6_store.png", "ref_map_fm6.png", "cz1_env.png", "voice.png", "drum_sound_808.png", "drum_mix_909.png",
    # Sound designer
    "designer_start.png",
]


def sources_present():
    """exit 0 when every allowlisted NIGHT render is already on disk"""
    if not SRC.is_dir():
        print(f"manual_shots: {SRC.relative_to(ROOT)} is missing", file=sys.stderr)
        sys.exit(1)
    missing = [name for name in SHOTS if not (SRC / name).is_file()]
    if missing:
        print(f"manual_shots: {len(missing)} source render(s) missing from {SRC.relative_to(ROOT)}:",
              file=sys.stderr)
        for name in missing:
            print(f"  {name}", file=sys.stderr)
        sys.exit(1)
    print(f"manual_shots: {len(SHOTS)} source renders present")


def require_present():
    """exit 0 when web/manual/ already contains every allowlisted figure"""
    missing = [name for name in SHOTS if not (MANUAL / name).is_file()]
    if missing:
        print(f"manual_shots: {len(missing)} screenshot(s) missing:", file=sys.stderr)
        for name in missing:
            print(f"  web/manual/{name}", file=sys.stderr)
        sys.exit(1)
    print(f"manual_shots: {len(SHOTS)} figures present")


def sync_dir(check):
    """copy allowlisted NIGHT renders into web/manual/; with check, report stale files and write nothing"""
    MANUAL.mkdir(parents=True, exist_ok=True)
    missing, copied, same = [], [], 0
    for name in SHOTS:
        src = SRC / name
        if not src.is_file():
            missing.append(name)
            continue
        out = MANUAL / name
        if out.is_file() and filecmp.cmp(src, out, shallow=False):
            same += 1
            continue
        if not check:
            shutil.copy(src, out)
        copied.append(name)
    known = set(SHOTS)
    extra = sorted(p.name for p in MANUAL.glob("*.png") if p.name not in known)
    if missing:
        print(f"missing from {SRC}: {', '.join(missing)}", file=sys.stderr)
    if extra:
        print(f"not in the allowlist (left in place): {', '.join(extra)}", file=sys.stderr)
    verb = "stale" if check else "copied"
    print(f"manual_shots: {MANUAL.relative_to(ROOT)}: {len(SHOTS)} figures, {same} up to date, "
          f"{len(copied)} {verb}, {len(missing)} missing")
    return bool(missing or (check and copied))


def main():
    """copy NIGHT UI renders into the manual, or only check that they are present"""
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="report only; write nothing")
    ap.add_argument("--require", action="store_true",
                    help="exit 0 when every figure is already in web/manual/; write nothing")
    ap.add_argument("--sources", action="store_true",
                    help="exit 0 when every allowlisted render exists under build/ui_new/NIGHT")
    a = ap.parse_args()
    if sum((a.require, a.check, a.sources)) > 1:
        sys.exit("pass only one of --require, --sources, or --check")
    if a.sources:
        sources_present()
        return
    if a.require:
        require_present()
        return
    if not SRC.is_dir():
        sys.exit(f"{SRC} is missing: run the UI renders first "
                 "(tests/run_tests.sh, or UI=renders tests/run_tests.sh)")
    if sync_dir(a.check):
        sys.exit(1)


if __name__ == "__main__":
    main()
