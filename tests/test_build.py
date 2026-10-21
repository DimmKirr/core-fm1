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


def test_unimplemented_subsystem_is_rejected(tmp_path, monkeypatch):
    monkeypatch.setattr(build, 'GEN', tmp_path)
    (tmp_path / 'config.h').write_text('#define CONFIG_AUDIO 1\n')
    with pytest.raises(SystemExit, match='Not integrated yet: AUDIO'):
        build.build(dry_run=True)
