#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""Exercise restoration failure paths without a device or persistent fixture."""
import sys
import unittest
import zlib
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import audio_cpu_device as cpu
import audio_performance_device as legacy


class Device:
    def __init__(self):
        self.guarded = False
        self.objects = {0: bytes(range(256))*3, 1: bytes(cpu.PREFERENCE_BYTES)}
        self.stored = [[1, 604, 123, 5]]
        self.staging = bytearray()
        self.fail_restore = False
        self.corrupt_read = False

    def silence(self):
        pass

    def inventory(self):
        return [(i, len(b), zlib.crc32(b)) for i, b in self.objects.items()]

    def request(self, cmd, a=()):
        if cmd == 98 and a[0] == 3:
            if len(a) == 2:
                self.guarded = bool(a[1])
            return [0, 3, int(self.guarded)]
        if cmd == 98 and a[0] == 4:
            return [0, 4, len(self.stored)] + [v for present, *rest in self.stored
                for v in [present, *(x for value in rest for x in cpu.u32(value))]]
        if cmd == 66:
            ident, off, count = a[0], cpu.r32(a[1:6]), a[6] + (a[7] << 7)
            raw = self.objects[ident][off:off+count]
            if self.corrupt_read:
                raw = bytes([raw[0] ^ 1])+raw[1:]
            return [ident, 0, *cpu.u32(off), count & 127, count >> 7, *cpu.pack(raw)]
        if cmd == 67:
            if self.fail_restore:
                raise RuntimeError('injected restoration failure')
            op, ident = a[:2]
            if op == 0:
                self.staging = bytearray()
            elif op == 1:
                self.staging.extend(cpu.unpack(a[7:]))
            elif op == 2:
                self.objects[ident] = bytes(self.staging)
            return [op, ident, 0]
        raise AssertionError((cmd, a))


class CpuBaselineTest(unittest.TestCase):
    def session(self, d):
        return patch.object(cpu, 'inventory', lambda device: device.inventory())

    def test_restoration_on_workload_exception(self):
        d = Device(); original = dict(d.objects)
        with self.session(d):
            session = cpu.RuntimeSession(d)
            with self.assertRaisesRegex(ValueError, 'workload'):
                with session:
                    d.objects[0] = b'temporary music'
                    d.objects[1] = b'changed preferences'
                    raise ValueError('workload failed')
        self.assertEqual(d.objects, original)
        self.assertFalse(d.guarded)
        self.assertTrue(session.result['verified'])

    def test_restore_failure_keeps_persistence_guarded(self):
        d = Device()
        with self.session(d):
            with self.assertRaisesRegex(RuntimeError, 'restoration failure'):
                with cpu.RuntimeSession(d):
                    d.fail_restore = True
        self.assertTrue(d.guarded)

    def test_committed_storage_change_retains_guard(self):
        d = Device()
        with self.session(d):
            with self.assertRaisesRegex(RuntimeError, 'Committed storage changed'):
                with cpu.RuntimeSession(d):
                    d.stored[0][2] ^= 1
        self.assertTrue(d.guarded)

    def test_capture_failure_releases_unmodified_session(self):
        d = Device(); d.corrupt_read = True
        with self.session(d):
            with self.assertRaisesRegex(RuntimeError, 'CRC failed'):
                with cpu.RuntimeSession(d):
                    self.fail('must not enter workload')
        self.assertFalse(d.guarded)

    def test_existing_guard_is_not_taken_over(self):
        d = Device(); d.guarded = True
        with self.assertRaisesRegex(RuntimeError, 'Existing guarded session'):
            cpu.RuntimeSession(d).__enter__()
        self.assertTrue(d.guarded)

    def test_oversize_snapshot_and_persistent_write_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'bounded snapshot'):
            cpu.read_object(Device(), (0, cpu.MAX_RUNTIME_BYTES+1, 0))
        with self.assertRaises(ValueError):
            cpu.write_ram(Device(), 2, b'project')

    def test_lost_guard_never_sends_preferences_to_persistence(self):
        with self.assertRaisesRegex(RuntimeError, 'Persistence guard lost'):
            cpu.write_ram(Device(), 1, bytes(cpu.PREFERENCE_BYTES))

    def test_histogram_upper_edges_and_overflow(self):
        bins = [0]*64; bins[1] = 98; bins[63] = 2
        class Histogram:
            def request(self, *args):
                return [0, 2, 64, *cpu.u32(1536), *cpu.u32(120000),
                        *(x for n in bins for x in cpu.u32(n))]
        h = cpu.histogram(Histogram(), 240)
        self.assertEqual(h['samples'], 100)
        self.assertEqual(h['complete_percentile_upper_us']['95'], (3072+336)/24)
        self.assertEqual(h['complete_percentile_upper_us']['99'], (120000+336)/24)
        self.assertEqual(h['overflow_samples'], 2)

    def test_old_and_extended_span_decoding(self):
        for count in (len(legacy.SPANS), len(legacy.SPANS+legacy.PANEL_SPANS)):
            class Profile:
                def request(self, *args):
                    return [0, 1, count]+[x for v in range(count*3+5) for x in cpu.u32(v)]
            result = legacy.profile(Profile())
            self.assertEqual(result['half'], dict(ticks=0, calls=1, max_ticks=2))
            self.assertEqual('input_shift' in result, count > len(legacy.SPANS))


if __name__ == '__main__':
    unittest.main()
