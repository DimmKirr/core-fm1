"""Packaging format checks without a target compiler or SDK download."""
import struct
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import build
import fm1pkg_make
import lz4blk


def test_driver_plan_splits_clang_subcommands():
    plan = ('clang version 4.0.1 () ()\nTarget: pi32v2\n'
            ' "/tc/common/bin/clang" "-cc1" "-triple" "pi32v2" "-E" "-o" "/tmp/a b.s" "x.S"\n'
            ' "/tc/common/bin/clang" "-cc1as" "-triple" "pi32v2" "-o" "x.o" "/tmp/a b.s"\n')
    assert build._driver_plan(plan) == [
        ['/tc/common/bin/clang', '-cc1', '-triple', 'pi32v2', '-E', '-o', '/tmp/a b.s', 'x.S'],
        ['/tc/common/bin/clang', '-cc1as', '-triple', 'pi32v2', '-o', 'x.o', '/tmp/a b.s'],
    ]


def test_qemu_runner_expands_the_clang_driver(monkeypatch):
    """The driver forks `clang -cc1`; that execve leaves qemu and the kernel may
    hand the x86-64 child to Rosetta, which rejects the nix-ld interpreter. Ask
    the driver for its plan (-###) and run every sub-command under qemu."""
    calls = []
    plan = ' "/tc/common/bin/clang" "-cc1" "-emit-obj" "-o" "x.o" "x.c"\n'

    class R:
        returncode = 0
        stdout = ''

    def fake_run(cmd, **kw):
        calls.append(cmd)
        r = R()
        r.stderr = plan if '-###' in cmd else ''
        r.args = cmd
        return r

    monkeypatch.setattr(build.subprocess, 'run', fake_run)
    monkeypatch.setattr(build.shutil, 'which', lambda n: '/usr/bin/qemu-x86_64')
    runner = build._QemuRunner(Path('/tc'), '/glibc', '/stdcpp')
    runner.run('pi32v2/bin/clang', ['-target', 'pi32v2', '-c', 'x.c', '-o', 'x.o'])
    assert calls[0][:3] == ['/usr/bin/qemu-x86_64', '/tc/pi32v2/bin/clang', '-###']
    assert calls[1] == ['/usr/bin/qemu-x86_64', '/tc/common/bin/clang', '-cc1', '-emit-obj', '-o', 'x.o', 'x.c']
    calls.clear()
    runner.run('pi32v2/bin/ld', ['-o', 'a.elf'])
    assert calls == [['/usr/bin/qemu-x86_64', '/tc/pi32v2/bin/ld', '-o', 'a.elf']], 'non-driver tools run as before'


def test_rosetta_runner_goes_through_the_x86_loader(monkeypatch):
    """When the kernel runs x86-64 ELFs itself, the only thing missing is an
    x86-64 ld.so and its libraries: every command, driver plan included, is
    started through that loader so the nix-ld shim at /lib64 is never consulted.
    Started that way the driver reads /proc/self/exe as the loader, so its plan
    names the loader as the -cc1 executable and puts the resource directory
    under the loader's directory; both are mapped back to the driver."""
    calls = []
    plan = (' "/x86/lib/ld-linux-x86-64.so.2" "-cc1" "-emit-obj"'
            ' "-resource-dir" "/x86/lib/../lib/clang/4.0.1" "-o" "x.o" "x.c"\n')

    class R:
        returncode = 0
        stdout = ''

    def fake_run(cmd, **kw):
        calls.append(cmd)
        r = R()
        r.stderr = plan if '-###' in cmd else ''
        r.args = cmd
        return r

    monkeypatch.setattr(build.subprocess, 'run', fake_run)
    runner = build._RosettaRunner(Path('/tc'), '/x86', '/x86')
    runner.run('pi32v2/bin/clang', ['-c', 'x.c', '-o', 'x.o'])
    loader = ['/x86/lib/ld-linux-x86-64.so.2', '--library-path', '/x86/lib:/x86/lib']
    assert calls[0] == [*loader, '/tc/pi32v2/bin/clang', '-###', '-c', 'x.c', '-o', 'x.o']
    assert calls[1] == [*loader, '/tc/pi32v2/bin/clang', '-cc1', '-emit-obj',
                        '-resource-dir', '/tc/pi32v2/bin/../lib/clang/4.0.1', '-o', 'x.o', 'x.c']
    assert runner.label() == ' (rosetta)'


def test_runner_order_rosetta_then_qemu_then_docker(monkeypatch):
    """On a non-x86 Linux host: direct x86-64 execution first, qemu-x86_64
    second, Docker only when neither is possible."""
    monkeypatch.setattr(build.platform, 'system', lambda: 'Linux')
    monkeypatch.setattr(build.platform, 'machine', lambda: 'aarch64')
    monkeypatch.delenv('JIELI_DOCKER', raising=False)
    monkeypatch.setattr(build, '_find_x86_libs', lambda tc: ('/x86', '/x86'))
    monkeypatch.setattr(build.shutil, 'which', lambda n: '/usr/bin/qemu-x86_64')

    monkeypatch.setattr(build, '_can_exec_x86', lambda glibc: True)
    assert isinstance(build._pick_runner(Path('/tc')), build._RosettaRunner)

    monkeypatch.setattr(build, '_can_exec_x86', lambda glibc: False)
    assert isinstance(build._pick_runner(Path('/tc')), build._QemuRunner)

    monkeypatch.setattr(build.shutil, 'which', lambda n: None)
    assert isinstance(build._pick_runner(Path('/tc')), build._DockerRunner)

    monkeypatch.setattr(build, '_find_x86_libs', lambda tc: ('', ''))
    monkeypatch.setattr(build, '_can_exec_x86', lambda glibc: True)
    assert isinstance(build._pick_runner(Path('/tc')), build._DockerRunner), 'no x86 libs: nothing to run the loader with'


def test_x86_libs_next_to_the_toolchain(tmp_path, monkeypatch):
    monkeypatch.delenv('JIELI_X86_GLIBC', raising=False)
    monkeypatch.delenv('JIELI_X86_STDCPP', raising=False)
    monkeypatch.setattr(build.glob, 'glob', lambda pattern: [])
    tc = tmp_path / 'toolchain'
    assert build._find_x86_libs(tc) == ('', '')
    lib = tmp_path / 'x86libs' / 'lib'
    lib.mkdir(parents=True)
    (lib / 'ld-linux-x86-64.so.2').write_bytes(b'')
    (lib / 'libstdc++.so.6').write_bytes(b'')
    local = str(tmp_path / 'x86libs')
    assert build._find_x86_libs(tc) == (local, local)


def test_ar_member_reads_gnu_archives_without_a_host_ar(tmp_path):
    def entry(name, body):
        hdr = f'{name:<16}{"0":<12}{"0":<6}{"0":<6}{"644":<8}{len(body):<10}`\n'.encode()
        return hdr + body + (b'\n' if len(body) & 1 else b'')
    archive = (b'!<arch>\n'
               + entry('/', b'\0' * 4)
               + entry('//', b'a_very_long_member_name.c.o/\n')
               + entry('usb.c.o/', b'BC\xc0\xde1')
               + entry('/0', b'long'))
    path = tmp_path / 'cpu.a'
    path.write_bytes(archive)
    assert build._ar_member(path, 'usb.c.o') == b'BC\xc0\xde1'
    assert build._ar_member(path, 'a_very_long_member_name.c.o') == b'long'
    with pytest.raises(SystemExit, match='no member nope.o'):
        build._ar_member(path, 'nope.o')


def test_loader_container_roundtrip():
    image = (bytes(range(256)) * 39) + b'last block'
    wrapped = build.loader_wrap(image)
    hcrc, dcrc, offset, size, attr, _, _, name = struct.unpack('<HHIIBBH16s', wrapped[:32])
    assert hcrc == fm1pkg_make.crc16(wrapped[2:32])
    assert dcrc == fm1pkg_make.crc16(wrapped[32:])
    assert (offset, size, attr, name.rstrip(b'\0')) == (32, len(wrapped)-32, 0x41, b'usb_hid_ota.bin')
    hcrc, dcrc, size, address, attr, _, _, _ = struct.unpack('<HHIIBBH16s', wrapped[32:64])
    assert hcrc == fm1pkg_make.crc16(wrapped[34:64])
    assert (size, address, attr) == (len(image), 0x01c0a800, 0)
    decoded = bytearray()
    position = 64
    while position < len(wrapped):
        compressed, raw = struct.unpack_from('<II', wrapped, position)
        position += 8
        chunk = lz4blk.decompress(wrapped[position:position+compressed])
        assert len(chunk) == raw
        decoded += chunk
        position += compressed
    assert decoded == image
    assert dcrc == fm1pkg_make.crc16(decoded)


def test_stub_commit_contains_body_checksum():
    import zlib
    stub = bytearray(b'\xff' * fm1pkg_make.STUB_LEN)
    stub[8:14] = b'CRSTUB'
    body = b'firmware body!!!' * 4
    app = fm1pkg_make.commit(stub + body)
    magic, version, length, crc, marker_crc = struct.unpack_from('<IIIII', app, fm1pkg_make.MARK_OFF)
    assert (magic, version, length, crc) == (fm1pkg_make.MARK_MAGIC, 1, len(body), zlib.crc32(body))
    assert marker_crc == zlib.crc32(app[fm1pkg_make.MARK_OFF:fm1pkg_make.MARK_OFF+16])
    assert fm1pkg_make.crc16(app) == fm1pkg_make.APP_CRC_FIXED


def test_dry_run_needs_no_toolchain(tmp_path, monkeypatch, capsys):
    monkeypatch.setattr(build, 'GEN', tmp_path)
    monkeypatch.delenv('JIELI_TOOLCHAIN', raising=False)
    (tmp_path / 'config.h').write_text('#define CONFIG_HAL_SYS_MELODEE 1\n')
    build.build(dry_run=True)
    assert 'core-fm1.fwsc' in capsys.readouterr().out


def test_unimplemented_module_is_rejected(tmp_path, monkeypatch):
    monkeypatch.setattr(build, 'GEN', tmp_path)
    (tmp_path / 'config.h').write_text('#define CONFIG_MIDI_UART 1\n')
    with pytest.raises(SystemExit, match='Not integrated yet: MIDI_UART'):
        build.build(dry_run=True)


def test_engine_tables_generated(tmp_path):
    """gen_engine_tables.py produces valid fe_tables.h with expected symbols."""
    import subprocess
    dst = tmp_path / 'fe_tables.h'
    r = subprocess.run(
        [sys.executable, str(ROOT / 'tools' / 'gen_engine_tables.py'), str(dst)],
        capture_output=True, text=True, cwd=str(ROOT))
    assert r.returncode == 0, r.stderr
    content = dst.read_text()
    for sym in ('SINE', 'PITCH_OCT', 'pitch_inc', 'SVF_G', 'TANH_Q15', 'FS', 'CTL'):
        assert sym in content, f'{sym} not found in fe_tables.h'


def test_engines_without_audio_passes_dry_run(tmp_path, monkeypatch):
    """CONFIG_ENGINES without CONFIG_AUDIO should not be rejected."""
    monkeypatch.setattr(build, 'GEN', tmp_path)
    (tmp_path / 'config.h').write_text(
        '#define CONFIG_ENGINES 1\n'
        '#define CONFIG_ENGINE_NOISE 1\n'
        '#define CONFIG_HAL_SYS_MELODEE 1\n')
    build.build(dry_run=True)
