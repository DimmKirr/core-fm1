#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build core-fm1: genconfig + JieLi clang compile + .fwsc package.

Usage:
  tools/build.py                    # build from .config
  tools/build.py --defconfig NAME   # load defconfig, then build
  tools/build.py --genconfig-only   # just write config.h, don't compile
  tools/build.py --dry-run          # show what would be compiled

The JieLi toolchain is Linux x86-64 only.
  - Native x86-64 Linux: runs directly.
  - ARM Linux with qemu-x86_64: runs via user-mode QEMU (auto-detected).
  - macOS (or JIELI_DOCKER=1): runs in a linux/amd64 Docker container.
"""
import argparse
import glob
import os
import platform
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import fm1pkg_make
import lz4blk

ROOT = Path(__file__).resolve().parents[1]
FW = ROOT / 'firmware'
OUT = ROOT / 'build'
GEN = OUT / 'gen'

DOCKER_IMAGE = os.environ.get('JIELI_DOCKER_IMAGE', 'debian:bookworm-slim')

CFLAGS = [
    '-mcpu=r3', '-mfprev1', '-ffp-contract=off',
    '-Werror=double-promotion', '-Os',
    '-ffunction-sections', '-fdata-sections',
    '-fno-builtin', '-Wall', '-Wno-unused-function',
]


def toolchain_path():
    tc = os.environ.get('JIELI_TOOLCHAIN')
    if not tc:
        raise SystemExit(
            'JIELI_TOOLCHAIN not set. Run "task fm1-platform:download" first.')
    p = Path(tc)
    if not (p / 'pi32v2' / 'bin' / 'clang').exists():
        raise SystemExit(
            f'JIELI_TOOLCHAIN={tc} does not contain pi32v2/bin/clang')
    return p


def _find_x86_libs():
    """Find x86-64 glibc and libstdc++ in the nix store for qemu."""
    glibc = os.environ.get('JIELI_X86_GLIBC', '')
    stdcpp = os.environ.get('JIELI_X86_STDCPP', '')
    if glibc and stdcpp:
        return glibc, stdcpp

    for p in sorted(glob.glob('/nix/store/*/lib/ld-linux-x86-64.so.2')):
        glibc = str(Path(p).parents[1])
        break
    for p in sorted(glob.glob('/nix/store/*/lib/libstdc++.so.6')):
        lib = Path(p)
        try:
            out = subprocess.run(
                ['readelf', '-h', str(lib)],
                capture_output=True, text=True, timeout=5)
            if 'X86-64' in out.stdout:
                stdcpp = str(lib.parents[1])
                break
        except (subprocess.TimeoutExpired, FileNotFoundError):
            continue
    return glibc, stdcpp


class _NativeRunner:
    def __init__(self, tc_path):
        self.tc = tc_path

    def run(self, tool_path, args, capture=False):
        cmd = [str(self.tc / tool_path), *args]
        return subprocess.run(cmd, check=True, cwd=str(ROOT),
                              stdout=subprocess.PIPE if capture else None, text=True).stdout

    def label(self):
        return ''


class _QemuRunner:
    def __init__(self, tc_path, glibc, stdcpp):
        self.tc = tc_path
        self.glibc = glibc
        self.stdcpp = stdcpp
        self.qemu = shutil.which('qemu-x86_64')

    def run(self, tool_path, args, capture=False):
        env = os.environ.copy()
        env['QEMU_LD_PREFIX'] = self.glibc
        env['LD_LIBRARY_PATH'] = f'{self.stdcpp}/lib:{self.glibc}/lib'
        cmd = [self.qemu, str(self.tc / tool_path), *args]
        return subprocess.run(cmd, check=True, cwd=str(ROOT), env=env,
                              stdout=subprocess.PIPE if capture else None, text=True).stdout

    def label(self):
        return ' (qemu)'


class _DockerRunner:
    def __init__(self, tc_path):
        self.tc = tc_path

    def run(self, tool_path, args, capture=False):
        mapped = []
        for a in args:
            s = str(a)
            mapped.append(s.replace(str(ROOT), '/work') if str(ROOT) in s else s)
        cmd = [
            'docker', 'run', '--rm', '--platform', 'linux/amd64',
            '-v', f'{ROOT}:/work',
            '-v', f'{self.tc}:/opt/jieli:ro',
            '-w', '/work',
            DOCKER_IMAGE,
            f'/opt/jieli/{tool_path}', *mapped,
        ]
        return subprocess.run(cmd, check=True, cwd=str(ROOT),
                              stdout=subprocess.PIPE if capture else None, text=True).stdout

    def label(self):
        return ' (docker)'


def _pick_runner(tc_path):
    forced = os.environ.get('JIELI_DOCKER')
    if forced == '1':
        return _DockerRunner(tc_path)

    is_x86 = platform.machine() in ('x86_64', 'AMD64')
    is_linux = platform.system() == 'Linux'

    if is_linux and is_x86:
        return _NativeRunner(tc_path)

    if is_linux and shutil.which('qemu-x86_64'):
        glibc, stdcpp = _find_x86_libs()
        if glibc and stdcpp:
            return _QemuRunner(tc_path, glibc, stdcpp)

    return _DockerRunner(tc_path)


_runner = None


def tc(tool, *args, capture=False):
    global _runner
    if _runner is None:
        _runner = _pick_runner(toolchain_path())

    if tool == 'cc':
        tool_path = 'pi32v2/bin/clang'
        extra = ['-target', 'pi32v2']
    else:
        tool_path = tool
        extra = []

    return _runner.run(tool_path, [*extra, *[str(a) for a in args]], capture=capture)


def genconfig(defconfig=None):
    cmd = [sys.executable, str(ROOT / 'tools' / 'genconfig.py')]
    if defconfig:
        cmd += ['--defconfig', defconfig]
    subprocess.run(cmd, check=True, cwd=str(ROOT))


def loader_wrap(image):
    """SPL loader container: JLFS headers and independently checked LZ4 blocks."""
    def header(crc, offset, size, attr):
        body = struct.pack('<HIIBBH16s', crc, offset, size, attr, 0, 0,
                           b'usb_hid_ota.bin')
        return struct.pack('<H', fm1pkg_make.crc16(body)) + body

    blocks = bytearray()
    for start in range(0, len(image), 4096):
        raw = image[start:start + 4096]
        compressed = lz4blk.compress(raw)
        if len(compressed) > 4096 or lz4blk.decompress(compressed) != raw:
            raise SystemExit(f'loader: invalid compressed block at {start}')
        blocks += struct.pack('<II', len(compressed), len(raw)) + compressed
    body = header(fm1pkg_make.crc16(image), len(image), 0x01C0A800, 0) + blocks
    return header(fm1pkg_make.crc16(body), 0x20, len(body), 0x41) + body


def build_support(name, entry):
    """Compile the standalone boot stub or RAM update loader."""
    src, out = FW / name, OUT / name
    out.mkdir(parents=True, exist_ok=True)
    tc('cc', f'-I{src}', '-c', src / entry, '-o', out / 'crt0.o')
    tc('cc', *CFLAGS, '-Ifirmware/hal', '-Ifirmware/src', f'-I{src}',
       '-c', src / f'{name}.c', '-o', out / f'{name}.o')
    elf = out / f'{name}.elf'
    tc('pi32v2/bin/ld', '--gc-sections', '-e', '_start', '-T', src / f'{name}.ld',
       out / 'crt0.o', out / f'{name}.o', '-o', elf)
    binary = out / f'{name}.bin'
    binary.unlink(missing_ok=True)
    tc('common/bin/objcopy', '-O', 'binary', '-j', '.text', elf, binary)
    return binary.read_bytes()


def package():
    stub = build_support('stub', 'crt0_stub.S')
    if not fm1pkg_make.has_stub(stub + bytes(fm1pkg_make.STUB_LEN)):
        raise SystemExit('boot stub: missing CRSTUB signature')
    if not stub or len(stub) > fm1pkg_make.STUB_LEN - 256:
        raise SystemExit('boot stub exceeds its reserved slot')
    image = build_support('loader', 'crt0_ldr.S')
    if not image or len(image) > 0x14000:
        raise SystemExit('loader: image empty or too big')
    ota = loader_wrap(image)
    if len(ota) > 0x4000:
        raise SystemExit('loader overlaps the update-record sector')
    (OUT / 'loader' / 'ota.bin').write_bytes(ota)
    body = (OUT / 'body.bin').read_bytes()
    if not body or len(body) > fm1pkg_make.BODY_MAX:
        raise SystemExit('application body empty or too big')
    app = stub.ljust(fm1pkg_make.STUB_LEN, b'\xff') + body
    (OUT / 'core-fm1.bin').write_bytes(app)
    fwsc = fm1pkg_make.ufw(fm1pkg_make.flash_image(app, fm1pkg_make.KEY), ota, 'FM-1_900')
    path = OUT / 'core-fm1.fwsc'
    path.write_bytes(fwsc)
    print(f'package: {path.relative_to(ROOT)} ({len(fwsc)} bytes)')


def build(dry_run=False):
    flags = [*CFLAGS, '-Ifirmware/hal', '-Ifirmware/src', '-Ibuild/gen']

    config_h = GEN / 'config.h'
    if not config_h.exists():
        raise SystemExit(
            f'{config_h} not found. Run "task config:generate" first.')

    enabled = set(re.findall(r'^#define CONFIG_(\w+) 1$', config_h.read_text(), re.M))
    pending = enabled & {'AUDIO', 'INPUT', 'FLASH', 'MIDI_UART', 'USB', 'DUAL_CORE'}
    unsupported = {s for s in enabled if s.startswith('HAL_') and
                   s.endswith(('_FELUCCA', '_SLOOP'))}
    if pending or unsupported:
        raise SystemExit('Not integrated yet: ' + ', '.join(sorted(pending | unsupported)) +
                         '. Load the minimal defconfig for the LCD boot milestone.')

    flags.append('-includebuild/gen/config.h')

    asm_srcs = []
    if (FW / 'crt0.S').exists():
        asm_srcs.append(('crt0.S', FW / 'crt0.S'))
    for f in sorted((FW / 'hal').glob('*.S')):
        asm_srcs.append((f.name, f))

    c_src = FW / 'src' / 'core_fm1.c'
    if not c_src.exists():
        raise SystemExit(f'{c_src} not found')

    if dry_run:
        print('Would compile:')
        for name, path in asm_srcs:
            print(f'  [asm] {path.relative_to(ROOT)}')
        print(f'  [cc]  {c_src.relative_to(ROOT)}')
        print(f'Flags: {" ".join(flags)}')
        print('Then build firmware/stub, firmware/loader and package build/core-fm1.fwsc')
        return

    runner = _pick_runner(toolchain_path())
    OUT.mkdir(parents=True, exist_ok=True)
    print(f'build: compiling{runner.label()}...')

    asm_flags = ['-Ifirmware', '-Ibuild/gen', '-include', 'build/gen/config.h']
    for name, path in asm_srcs:
        obj_name = Path(name).stem + '.o'
        tc('cc', *asm_flags, '-c', str(path), '-o', str(OUT / obj_name))

    tc('cc', *flags, '-c', str(c_src), '-o', str(OUT / 'core_fm1.o'))

    objs = [str(OUT / (Path(name).stem + '.o')) for name, _ in asm_srcs]
    objs.append(str(OUT / 'core_fm1.o'))
    elf = OUT / 'core_fm1.elf'
    tc('pi32v2/bin/ld', '--gc-sections', '-T', str(FW / 'app.ld'), *objs,
       '-o', str(elf))

    for sect in ('text.bin', 'data.bin'):
        (OUT / sect).unlink(missing_ok=True)
    tc('common/bin/objcopy', '-O', 'binary', '-j', '.text',
       str(elf), str(OUT / 'text.bin'))
    tc('common/bin/objcopy', '-O', 'binary', '-j', '.data',
       str(elf), str(OUT / 'data.bin'))

    text_size = (OUT / 'text.bin').stat().st_size
    print(f'build: {elf.relative_to(ROOT)} ({text_size} bytes text)')
    # JieLi objcopy accepts one section per invocation. Assemble relocated
    # code/data at linker load addresses, never their runtime RAM addresses.
    symbols = tc('common/bin/objdump', '-t', elf, capture=True)
    body = bytearray((OUT / 'text.bin').read_bytes())
    for section, symbol in (('.ram_text', '_rt_load'), ('.dsp_text', '_dsp_load'),
                            ('.dsp_tables', '_dt_load'), ('.data', '_data_load'),
                            ('.audio_text', '_audio_load')):
        binary = OUT / f'{section[1:]}.bin'
        binary.unlink(missing_ok=True)
        tc('common/bin/objcopy', '-O', 'binary', '-j', section, elf, binary)
        blob = binary.read_bytes() if binary.exists() else b''
        if not blob:
            continue
        match = re.search(r'^([0-9a-f]+) .*\s' + symbol + r'$', symbols, re.M)
        if not match:
            raise SystemExit(f'missing linker symbol: {symbol}')
        offset = int(match.group(1), 16) - 0x02001000
        if offset < len(body) or offset % 4 or offset + len(blob) > fm1pkg_make.BODY_MAX:
            raise SystemExit(f'invalid application load address: {symbol}')
        body += b'\xff' * (offset - len(body)) + blob
    body += b'\xff' * (-len(body) % 4)
    (OUT / 'body.bin').write_bytes(body)
    package()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--defconfig', help='load defconfig before building')
    ap.add_argument('--genconfig-only', action='store_true')
    ap.add_argument('--dry-run', action='store_true')
    args = ap.parse_args()

    genconfig(args.defconfig)
    if args.genconfig_only:
        return

    build(dry_run=args.dry_run)


if __name__ == '__main__':
    main()
