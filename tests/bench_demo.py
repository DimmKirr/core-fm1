#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bench: DEMO view walkthrough via native fm1-live --record.

Uses fm1-live's headless --shot/--record mode with FM1_BENCH_KEYS scripted
input. Produces PPM frame dumps + raw audio, then encodes to MP4 video.

Output: tests/results/<timestamp>-bench_demo/
  - frames/    PPM frames (every 16ms of emulated time)
  - audio.raw  44.1kHz stereo s16le
  - demo.mp4   encoded video with audio (if ffmpeg available)
  - bench.log  emulator stats

Usage:
    uv run python tests/bench_demo.py
    LCD_ONLY=1 uv run python tests/bench_demo.py   # LCD-only (no panel chrome)
"""
import os
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FWSC = ROOT / 'build' / 'core-fm1.fwsc'
FM1_LIVE = Path.home() / '.fm1-emulator' / 'bin' / 'fm1-live'
RESULTS = Path(__file__).resolve().parent / 'results'


def results_dir():
    ts = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H%M%SZ')
    d = RESULTS / f'{ts}-bench_demo'
    d.mkdir(parents=True, exist_ok=True)
    return d


def build_keys_script():
    """Build FM1_BENCH_KEYS for a DEMO view walkthrough.

    Encoder mapping (Felucca knob pattern):
      enc0 = SELECT (switches screens)
      enc2 = K1 (adjusts card values / list scroll)

    Timeline (emulated ms, firmware boots ~700ms):
      1000: long-press FX to open DEMO (hold 600ms)
      2000: K1 +1 x5 (explore cards counter)
      3000: play C4, E4, G4, C5 notes
      4500: SELECT +1 (-> LIST screen)
      5000: K1 +1 x8 (scroll list down)
      6500: K1 -1 x4 (scroll back)
      7500: SELECT +1 (-> GRAPH screen)
      8000: K1 +1 x5 (trigger redraws)
     10000: SELECT +1 (-> TEXT screen)
     11000: SELECT +1 (-> EDIT screen)
     12000: SELECT +1 (-> wrap to CARDS)
     13000: HOME tap (return)
     13500: end
    """
    items = []

    # long-press FX (button id '1' in emulator, held 600ms)
    items.append('1000-1600:FX')

    # K1 encoder +1 x5 (every 150ms)
    for i in range(5):
        items.append(f'{2000 + i * 150}:enc2:+1')

    # play notes: C4, E4, G4, C5
    notes = [('n7', 3000), ('n11', 3300), ('n14', 3600), ('n19', 3900)]
    for note, t in notes:
        items.append(f'{t}-{t + 200}:{note}')

    # SELECT +1 -> LIST screen
    items.append('4500:enc0:+1')

    # K1 +1 x8 (scroll list down, every 100ms)
    for i in range(8):
        items.append(f'{5000 + i * 100}:enc2:+1')

    # K1 -1 x4 (scroll back up)
    for i in range(4):
        items.append(f'{6500 + i * 100}:enc2:-1')

    # SELECT +1 -> GRAPH screen
    items.append('7500:enc0:+1')

    # K1 +1 x5 (trigger graph redraws)
    for i in range(5):
        items.append(f'{8000 + i * 200}:enc2:+1')

    # SELECT +1 -> TEXT screen
    items.append('10000:enc0:+1')

    # SELECT +1 -> EDIT screen
    items.append('11000:enc0:+1')

    # SELECT +1 -> wrap back to CARDS
    items.append('12000:enc0:+1')

    # HOME tap (return to HOME)
    items.append('13000-13100:HOME')

    return ','.join(items)


def encode_video(frame_dir, audio_raw, out_mp4, lcd_only=False):
    """Encode PPM frames + raw audio to MP4 via ffmpeg."""
    ffmpeg = shutil.which('ffmpeg')
    if not ffmpeg:
        print('  ffmpeg not found, skipping video encode')
        return None

    # fm1-live --record dumps at ~62.5 fps (every 16ms)
    fps = 62.5
    frame_files = sorted(frame_dir.glob('*.ppm'))
    if not frame_files:
        print('  No PPM frames found')
        return None

    # build concat list for ffmpeg
    concat = frame_dir / 'concat.txt'
    with open(concat, 'w') as f:
        for p in frame_files:
            f.write(f"file '{p.name}'\n")
            f.write(f"duration {1/fps:.6f}\n")

    cmd = [
        ffmpeg, '-y',
        '-f', 'concat', '-safe', '0', '-i', str(concat),
    ]

    if audio_raw.is_file() and audio_raw.stat().st_size > 0:
        cmd.extend([
            '-f', 's16le', '-ar', '44100', '-ac', '2', '-i', str(audio_raw),
        ])

    scale = '240:240' if lcd_only else '-1:-1'
    cmd.extend([
        '-vf', f'scale={scale}:flags=neighbor',
        '-c:v', 'libx264', '-preset', 'fast', '-crf', '18',
        '-pix_fmt', 'yuv420p',
    ])

    if audio_raw.is_file() and audio_raw.stat().st_size > 0:
        cmd.extend(['-c:a', 'aac', '-b:a', '128k', '-shortest'])

    cmd.append(str(out_mp4))

    r = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    if r.returncode != 0:
        print(f'  ffmpeg failed: {r.stderr[-500:]}')
        return None
    return out_mp4


def main():
    if not FWSC.is_file():
        print(f'Firmware not found at {FWSC}. Run "task build" first.')
        sys.exit(1)

    if not FM1_LIVE.is_file():
        print(f'fm1-live not found at {FM1_LIVE}. Run "task fm1-emulator:download:fm1-emu" first.')
        sys.exit(1)

    out = results_dir()
    frame_dir = out / 'frames'
    frame_dir.mkdir(exist_ok=True)

    keys = build_keys_script()
    duration_ms = 14000
    lcd_only = bool(os.environ.get('LCD_ONLY'))

    print(f'Running {duration_ms}ms emulated walkthrough...')
    print(f'Keys: {keys[:80]}...')

    env = {**os.environ}
    env['FM1_BENCH_KEYS'] = keys
    env['FM1_WARMUP_MS'] = '800'

    cmd = [
        str(FM1_LIVE),
        '--fwsc', str(FWSC),
        '--shot', str(duration_ms),
        '--record', str(frame_dir),
    ]
    if lcd_only:
        cmd.append('--lcd-only')

    r = subprocess.run(cmd, capture_output=True, text=True,
                       timeout=600, env=env, cwd=str(ROOT))

    log = f'--- stdout ---\n{r.stdout}\n--- stderr ---\n{r.stderr}\n--- exit {r.returncode} ---\n'
    (out / 'bench.log').write_text(log)
    print(r.stdout.strip() if r.stdout.strip() else r.stderr.strip())

    # count frames
    frames = sorted(frame_dir.glob('*.ppm'))
    print(f'  {len(frames)} PPM frames captured')

    audio_raw = frame_dir / 'audio.raw'
    if audio_raw.is_file():
        sz = audio_raw.stat().st_size
        duration_s = sz / (44100 * 2 * 2)
        print(f'  Audio: {sz} bytes ({duration_s:.1f}s)')
        # move audio to results root
        shutil.move(str(audio_raw), str(out / 'audio.raw'))
        audio_raw = out / 'audio.raw'

    # encode video
    if frames:
        mp4 = out / 'demo.mp4'
        result = encode_video(frame_dir, audio_raw, mp4, lcd_only=lcd_only)
        if result:
            print(f'  Video: {result} ({result.stat().st_size / 1024:.0f} KB)')
        else:
            print(f'  Raw frames at: {frame_dir}')

    print(f'\nResults: {out}')


if __name__ == '__main__':
    main()
