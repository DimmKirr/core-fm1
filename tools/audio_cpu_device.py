#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Shared-budget CPU baseline. No backup directory, flash writes or installation.

Requires the Stage-0 profiling build. Temporarily replaces stopped runtime;
holds runtime/preferences in bounded host memory, blocks device persistence,
restores and verifies on exit. A failed restoration leaves persistence guarded.
Run separately with each installed package in interleaved baseline/candidate
order. This screening tool does not certify physical controls or full capacity.
"""
import argparse
import contextlib
import hashlib
import json
import math
import sys
import time
import zlib
from pathlib import Path

from prophet_device import inventory, pack, param_ids, r32, u32, unpack
from audio_performance_device import (ReliableDevice, metadata, run, usb_streams,
                                      DEADLINE_US)

MAX_RUNTIME_BYTES = 256 * 1024
PREFERENCE_BYTES = 604


def guard(d, enabled=None):
    args = [3] if enabled is None else [3, int(enabled)]
    reply = d.request(98, args)
    if len(reply) != 3 or reply[:2] != [0, 3]:
        raise RuntimeError('Requires Stage-0 persistence guard')
    return bool(reply[2])


def committed(d):
    b = d.request(98, [4])
    if len(b) < 3 or b[:2] != [0, 4] or len(b) != 3 + b[2] * 16:
        raise RuntimeError('Requires committed-object CRC inventory')
    return [list(b[i:i+1]) + [r32(b[j:j+5]) for j in range(i+1, i+16, 5)]
            for i in range(3, len(b), 16)]


def read_object(d, entry):
    ident, length, crc = entry
    if length > MAX_RUNTIME_BYTES:
        raise RuntimeError(f'Object {ident} exceeds bounded snapshot size')
    raw = bytearray()
    for off in range(0, length, 256):
        count = min(256, length-off)
        b = d.request(66, [ident, *u32(off), count & 127, count >> 7])
        if len(b) < 9 or b[:2] != [ident, 0] or r32(b[2:7]) != off or b[7] + (b[8] << 7) != count:
            raise RuntimeError(f'Runtime read failed: {ident}/{off}')
        raw.extend(unpack(b[9:]))
    if len(raw) != length or zlib.crc32(raw) != crc:
        raise RuntimeError(f'Runtime CRC failed: {ident}')
    return bytes(raw)


def write_ram(d, ident, raw):
    if ident not in (0, 1) or (ident == 1 and len(raw) != PREFERENCE_BYTES):
        raise ValueError('Only runtime and guarded RAM preferences may be sent')
    if not guard(d):
        raise RuntimeError('Persistence guard lost; refusing any restore transaction')
    # Restart a whole idempotent RAM transaction on a lost acknowledgement.
    for attempt in range(3):
        try:
            payloads = [[0, ident, *u32(len(raw)), *u32(zlib.crc32(raw))]]
            payloads += [[1, ident, *u32(off), *pack(raw[off:off+128])]
                         for off in range(0, len(raw), 128)]
            payloads += [[2, ident]]
            for payload in payloads:
                if d.request(67, payload) != [payload[0], ident, 0]:
                    raise RuntimeError(f'RAM restoration rejected: {ident}/{payload[0]}')
            return
        except TimeoutError:
            if attempt == 2:
                raise


class RuntimeSession:
    """No snapshot bytes are written to disk, including on an exception."""
    def __init__(self, d):
        self.d = d
        self.captured = False
        self.result = dict(verified=False)

    def __enter__(self):
        d = self.d
        if guard(d):
            raise RuntimeError('Existing guarded session needs restoration; refusing to replace it')
        guard(d, True)
        try:
            self.committed = committed(d)
            entries = inventory(d)
            self.entries = {ident: (ident, length, crc) for ident, length, crc in entries}
            self.runtime = read_object(d, self.entries[0])
            self.preferences = read_object(d, self.entries[1])
            if len(self.preferences) < PREFERENCE_BYTES:
                raise RuntimeError('Unsupported live preference layout')
            self.captured = True
        except BaseException:
            # Setup has not edited runtime. It is safe to release this guard.
            guard(d, False)
            raise
        return self

    def __exit__(self, *exc):
        d = self.d
        # Never release the guard after a failed restore or verification.
        d.request(99, [0]) if getattr(d, 'performance_benchmark_support', False) else None
        d.silence()
        write_ram(d, 1, self.preferences[:PREFERENCE_BYTES])
        write_ram(d, 0, self.runtime)
        entries = {ident: (ident, length, crc) for ident, length, crc in inventory(d)}
        if entries != self.entries:
            raise RuntimeError('Live runtime/preferences or archived object CRCs differ after restore; guard retained')
        # Verify actual committed settings as well as the synthetic live export.
        if committed(d) != self.committed:
            raise RuntimeError('Committed storage changed; guard retained')
        guard(d, False)
        self.result = dict(verified=True, runtime_crc=self.entries[0][2],
                           committed_objects=len(self.committed), snapshot_on_disk=False)


def recover(session, port):
    """Retain the only original snapshot while the operator repairs transport.

    A terminal is required before any fixture starts. Exiting after a failed
    restore would discard the original music held only in this process.
    """
    while not session.result['verified']:
        print('Restoration failed. Original runtime remains in this process; persistence stays guarded.', flush=True)
        answer = input('Reconnect the same device, then press Enter to retry. Type ABANDON to discard the in-memory original: ')
        if answer == 'ABANDON':
            raise RuntimeError('Operator explicitly abandoned in-memory restoration; device guard retained')
        try:
            session.d.close()
            session.d = ReliableDevice(port)
            session.__exit__(None, None, None)
        except Exception as error:
            print(f'Restoration still needs recovery: {error}', flush=True)


def histogram(d, timer5_ticks):
    b = d.request(98, [2])
    if len(b) < 13 or b[:2] != [0, 2] or b[2] != 64 or len(b) != 13 + b[2]*5:
        raise RuntimeError('Requires bounded assembly-envelope histogram')
    width, peak = r32(b[3:8]), r32(b[8:13])
    bins = [r32(b[i:i+5]) for i in range(13, len(b), 5)]
    n = sum(bins)
    # Upper bin edges + conservative wrapper/nested-service allowance. The
    # overflow bin has no finite edge: report its observed maximum instead.
    allowance = timer5_ticks + 96
    percentiles = {}
    for q in (50, 95, 99, 99.9):
        wanted = max(1, math.ceil(n*q/100))
        acc = 0
        for i, count in enumerate(bins):
            acc += count
            if acc >= wanted:
                ticks = peak if i == len(bins)-1 else (i+1)*width
                percentiles[str(q)] = (ticks+allowance)/24
                break
    return dict(samples=n, raw_bin_width_ticks=width, raw_bins=bins,
                complete_percentile_upper_us=percentiles,
                complete_max_upper_us=(peak+allowance)/24,
                allowance_ticks=allowance, overflow_samples=bins[-1])


def configure_modulation(d, categories, variant, notes):
    """After the common fixture, select destinations relevant to route caching."""
    if d.cpu_modulation_routing == 'engine':
        for track in range(4):
            d.params(track, {f'P_M{slot+1}DST': 12+slot for slot in range(4)})
    return notes


def control_updates(d, categories, notes, tick):
    if d.cpu_event_bursts:
        from audio_performance_adversarial import events
        events(d, categories, notes, tick)
    if d.cpu_source_churn:
        # All ten legal sources, including voice/track dependency transitions.
        # This is host-paced controller traffic, not one change per audio block.
        for track in range(4):
            d.params(track, {f'P_M{slot+1}SRC': 1+(tick+slot)%10 for slot in range(4)})


def assess_row(row, usb):
    """Use the more conservative complete-envelope bound for every decision."""
    row['stats_peak_complete_upper_us'] = row['peak_complete_upper_us']
    row['peak_complete_upper_us'] = max(row['peak_complete_upper_us'], row['histogram']['complete_max_upper_us'])
    row['headroom_pct'] = 100*(1-row['peak_complete_upper_us']/DEADLINE_US)
    row['deadline_passed'] = row['peak_complete_upper_us'] < DEADLINE_US
    row['target_passed'] = row['peak_complete_upper_us'] <= .8*DEADLINE_US
    usb_ok = row['usb_play_alt'] > 0 and row['usb_capture_alt'] > 0 if usb == 'stereo' else row['usb_play_alt'] == row['usb_capture_alt'] == 0
    row['passed'] = bool(row['passed'] and row['target_passed'] and usb_ok and row['histogram']['samples'] and
        row['normal_budget_fades'] == 0 and row['delta']['capacity_steals'] == 0 and
        not any(row['delta'][key] for key in ('missed_frames', 'play_underruns', 'play_overruns',
            'cap_underruns', 'cap_overruns', 'bad_packets')))


def ranked_profile(row):
    """Inclusive/concurrent spans are individually ranked and never summed."""
    groups = {'audio_inclusive': [], 'panel_all_ticks': []}
    halves = row['profile']['half']['calls']
    for name, span in row['profile'].items():
        if not isinstance(span, dict) or not span['calls']:
            continue
        groups['panel_all_ticks' if name.startswith('input_') else 'audio_inclusive'].append(dict(
            span=name, calls=span['calls'], mean_us=span['ticks']/span['calls']/24,
            max_us=span['max_ticks']/24, total_us=span['ticks']/24,
            us_per_half=span['ticks']/halves/24 if halves else None))
    for values in groups.values():
        values.sort(key=lambda span: span['total_us'], reverse=True)
    return dict(note='Inclusive spans overlap; engine spans can execute concurrently. Do not sum rows.', **groups)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port', default='Melodee')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--build-manifest', type=Path, required=True,
                   help='Manifest identifying the installed package, source and build flags')
    p.add_argument('--assignment', default='0124')
    p.add_argument('--voices', default='1,1,1,2', help='Four counts, including zero for idle tracks')
    p.add_argument('--seconds', type=float, default=60)
    p.add_argument('--repeat', type=int, default=3)
    p.add_argument('--budget', type=int, default=16)
    p.add_argument('--variant', type=int, default=0)
    p.add_argument('--fx', action='store_true')
    p.add_argument('--modulation', action='store_true')
    p.add_argument('--modulation-routing', choices=('voice', 'engine'), default='voice')
    p.add_argument('--source-churn', action='store_true', help='Host-paced changes through all ten modulation sources')
    p.add_argument('--event-bursts', action='store_true')
    p.add_argument('--usb', choices=('inactive', 'stereo'), default='inactive')
    a = p.parse_args()
    if len(a.assignment) != 4 or any(c not in '012345' for c in a.assignment):
        p.error('assignment: four category digits 0..5')
    try:
        voices = list(map(int, a.voices.split(',')))
    except ValueError:
        p.error('voices: four counts 0..16')
    if len(voices) != 4 or any(v < 0 or v > 16 for v in voices) or not sum(voices):
        p.error('voices: four counts 0..16, at least one sounding voice')
    if a.seconds <= 0 or a.repeat < 1 or not 8 <= a.budget <= 32:
        p.error('seconds/repeat positive; budget 8..32')
    if a.seconds > 120:
        p.error('Profiling windows are limited to 120 seconds to avoid 32-bit span-total wrap')
    if (a.modulation_routing != 'voice' or a.source_churn) and not a.modulation:
        p.error('modulation-routing/source-churn require --modulation')
    manifest = json.loads(a.build_manifest.read_text())
    package = Path(manifest['package'])
    if hashlib.sha256(package.read_bytes()).hexdigest() != manifest['package_sha256']:
        p.error('Package no longer matches build manifest')
    if a.output.exists():
        p.error('Output exists; preserve earlier evidence')
    if not sys.stdin.isatty():
        p.error('Run in an interactive terminal so a failed restoration can retain its in-memory snapshot for recovery')
    a.output.mkdir(parents=True)
    d = ReliableDevice(a.port)
    session = RuntimeSession(d)
    try:
        d.performance_metadata = True
        m = metadata(d)
        identity = bytes(d.request(1)).split(b'\0', 1)[0].decode('ascii')
        if identity != manifest['editor_identity']:
            raise RuntimeError(f'Installed identity differs from manifest: {identity}')
        if not m['profiling'] or m['half_frames'] != 128 or m['clock_mhz'] != 360 or 'budget_points' not in m:
            raise RuntimeError(f'Requires current shared-budget 360-MHz profiling build: {m}')
        expected = manifest['flags']
        for field, flag in (('profiling', 'MELODEE_AUDIO_PROFILE'), ('benchmark_support', 'MELODEE_AUDIO_BENCH'),
                            ('admission', 'MELODEE_ADMISSION')):
            if m[field] != expected[flag]:
                raise RuntimeError(f'Installed {field} differs from manifest')
        d.performance_benchmark_support = m['benchmark_support']
        d.performance_fx = a.fx
        d.performance_modulation = a.modulation
        d.cpu_modulation_routing = a.modulation_routing
        d.cpu_event_bursts = a.event_bursts
        d.cpu_source_churn = a.source_churn
        if a.modulation:
            d.performance_patch_hook = configure_modulation
        if a.event_bursts or a.source_churn:
            d.performance_step_hook = control_updates
        (a.output/'run.json').write_text(json.dumps(dict(build=manifest, identity=identity,
            metadata=m, workload=vars(a), evidence='device screening; physical validation pending'),
            indent=2, default=str)+'\n')
        with session:
            d.silence()
            _, globals_ = param_ids()
            values = dict(G_BUDGET=a.budget, G_CLOCK=0, G_ROUTE=0, G_MFXTYPE=0, G_MFX2TYPE=0, G_MFX3TYPE=0)
            if a.fx:
                values.update(G_FXMODE=0, G_MFXTYPE=9, G_MFXMIX=127, G_MFXRATE=127, G_MFXCHAR=127, G_FXRETURN=127)
            for key, value in values.items():
                v = value+8192
                d.request(3, [1, globals_[key], v & 127, v >> 7])
            streams = usb_streams(a.output/'capture.wav', 'int24') if a.usb == 'stereo' else contextlib.nullcontext()
            with streams, (a.output/'cases.jsonl').open('w') as f:
                for repeat in range(a.repeat):
                    row = run(d, list(map(int, a.assignment)), voices, a.variant, a.seconds, False)
                    row['histogram'] = histogram(d, row['metadata']['timer5_max_ticks'])
                    assess_row(row, a.usb)
                    row['ranked_profile'] = ranked_profile(row)
                    row['repeat'] = repeat
                    f.write(json.dumps(row)+'\n');f.flush()
                    print(json.dumps({key:row[key] for key in ('repeat', 'passed', 'voices_min', 'peak_complete_upper_us')}), flush=True)
    finally:
        try:
            if session.captured and not session.result['verified']:
                recover(session, a.port)
        finally:
            (a.output/'restoration.json').write_text(json.dumps(session.result, indent=2)+'\n')
            session.d.close()


if __name__ == '__main__':
    main()
