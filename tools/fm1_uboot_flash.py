#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Flash FM-1 firmware via JieLi WL82 UBOOT mode (USB Mass Storage / Bulk-Only Transport).

Works on macOS and Linux using pyusb (libusb backend). No SCSI passthrough needed.

  fm1_uboot_flash.py build/core-fm1.fwsc [--yes]

The FM-1 must be in UBOOT mode (hold OCT+/- for 5 s, or send the SysEx softkey).
The device appears as VID 4C4A PID 8057, product "WL80UBOOT1.00".

Protocol: uploads wl82loader.bin to RAM (MengLi-ciphered), jumps to it, then
erases+writes only the changed 4 KiB sectors in the application area [0x4000, 0x93000).
Header [0, 0x4000) and device data [0x93000, end) are never touched.

Exit codes: 0 success, 1 error, 2 bad arguments, 3 device not found, 4 flash mismatch.
"""
import argparse
import ctypes.util
import glob
import os
import struct
import sys
import time
from pathlib import Path


def _find_libusb():
    """Find libusb, including nix store paths that ctypes.util.find_library misses."""
    found = ctypes.util.find_library("usb-1.0")
    if found:
        return found
    ext = ".dylib" if sys.platform == "darwin" else ".so"
    candidates = glob.glob(f"/nix/store/*-libusb-*/lib/libusb-1.0{ext}")
    if sys.platform == "darwin":
        candidates += glob.glob("/opt/homebrew/lib/libusb-1.0.dylib")
        candidates += glob.glob("/usr/local/lib/libusb-1.0.dylib")
    for c in candidates:
        if os.path.isfile(c):
            return c
    return None

_libusb_path = _find_libusb()

try:
    import usb.core
    import usb.util
    import usb.backend.libusb1
except ImportError:
    sys.exit("pyusb is required: uv pip install pyusb\n"
             "  (and libusb: brew install libusb / apt install libusb-1.0-0)")

_usb_backend = None
if _libusb_path:
    _usb_backend = usb.backend.libusb1.get_backend(find_library=lambda _: _libusb_path)

# JieLi WL82 (AC791N) UBOOT device
VID, PID = 0x4C4A, 0x8057
# loader blob: pre-ciphered, send raw in 512-byte chunks to 0x1C02000
LOADER_ADDR = 0x1C02000
LOADER_ARG = 0x0001  # target = SPI NOR
IO_SIZE = 512
SECTOR_SIZE = 0x1000
APP_START = 0x4000
APP_END = 0x93000
CHIP_KEY_EXPECT = 0x980F

# CBW/CSW signatures
CBW_SIG = b'USBC'
CSW_SIG = b'USBS'


# ---------------------------------------------------------------- CRC-16 ---

def crc16(data, init=0):
    crc = init
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 0xFFFF
    return crc


# --------------------------------------------------------- MengLi cipher ---

MENGLI_MAGIC = "孟黎我爱你，玉林".encode("gb2312")

def mengli_cipher(data, key=0xFFFFFFFF):
    buf = bytearray(data)
    crc = crc16(bytes([(key >> 16) & 0xFF, (key >> 24) & 0xFF]), key & 0xFFFF)
    for i in range(len(buf)):
        mi = i % len(MENGLI_MAGIC)
        crc = crc16(MENGLI_MAGIC[mi:mi + 1], crc)
        buf[i] ^= crc & 0xFF
    return bytes(buf)


# ---------------------------------------------------------- BOT framing ---

class UbootDevice:
    def __init__(self, dev, ep_in, ep_out):
        self.dev = dev
        self.ep_in = ep_in
        self.ep_out = ep_out
        self.tag = 0

    def _cbw(self, cdb, data_len, direction_in):
        self.tag += 1
        flags = 0x80 if direction_in else 0x00
        cbw = struct.pack('<4sIIBBB', CBW_SIG, self.tag, data_len, flags, 0, len(cdb))
        cbw += cdb + b'\x00' * (16 - len(cdb))
        self.ep_out.write(cbw)

    def _csw(self):
        data = self.ep_in.read(13, timeout=5000)
        sig, tag, residue, status = struct.unpack('<4sIIB', bytes(data))
        if sig != CSW_SIG:
            raise IOError(f"bad CSW signature: {sig!r}")
        if tag != self.tag:
            raise IOError(f"CSW tag mismatch: expected {self.tag}, got {tag}")
        if status != 0:
            raise IOError(f"CSW status {status}")
        return residue

    def cmd_exec(self, cmd, args):
        cdb = cmd.to_bytes(2, 'big') + args
        cdb += b'\xFF' * (16 - len(cdb))
        self._cbw(cdb, 16, True)
        resp = bytes(self.ep_in.read(16, timeout=5000))
        self._csw()
        return resp[2:]

    def cmd_datain(self, cmd, args, length):
        cdb = cmd.to_bytes(2, 'big') + args
        cdb += b'\xFF' * (16 - len(cdb))
        self._cbw(cdb, length, True)
        data = bytes(self.ep_in.read(length, timeout=10000))
        self._csw()
        return data

    def cmd_dataout(self, cmd, args, data):
        cdb = cmd.to_bytes(2, 'big') + args
        cdb += b'\xFF' * (16 - len(cdb))
        self._cbw(cdb, len(data), False)
        self.ep_out.write(data)
        self._csw()


# -------------------------------------------------- UBOOT1.00 commands ---

def rom_mem_write(dev, addr, data):
    dev.cmd_dataout(0xFB06,
        addr.to_bytes(4, 'big') + len(data).to_bytes(2, 'big')
        + b'\x00' + crc16(data).to_bytes(2, 'little'),
        data)

def rom_jump(dev, addr, arg):
    dev.cmd_exec(0xFB08, addr.to_bytes(4, 'big') + arg.to_bytes(2, 'big'))


# ------------------------------------------------------- Loader commands ---

def loader_chip_key(dev):
    resp = dev.cmd_exec(0xFC09, (0xAC6900).to_bytes(4, 'big'))
    dec = mengli_cipher(resp[4:6][::-1])
    return int.from_bytes(dec, 'little')

def loader_online_device(dev):
    resp = dev.cmd_exec(0xFC0A, b'')
    return {'type': resp[0], 'id': int.from_bytes(resp[2:6], 'little')}

def loader_flash_read(dev, addr, length):
    return dev.cmd_datain(0xFD05,
        addr.to_bytes(4, 'big') + length.to_bytes(2, 'big'), length)

def loader_erase_sector(dev, addr):
    dev.cmd_exec(0xFB01, addr.to_bytes(4, 'big'))

def loader_flash_write(dev, addr, data):
    dev.cmd_dataout(0xFB04,
        addr.to_bytes(4, 'big') + len(data).to_bytes(2, 'big')
        + b'\x00' + crc16(data).to_bytes(2, 'little'),
        data)

def loader_run_app(dev):
    dev.cmd_exec(0xFC0C, (1).to_bytes(4, 'big'))


# ------------------------------------------------------- .fwsc handling ---

def fwsc_extract_flash(path):
    raw = bytearray(path.read_bytes())
    for i in range(20):
        block = raw[i * 0x30:i * 0x30 + 0x2F]
        raw[i * 0x30:i * 0x30 + 0x2F] = block
        del raw[i * 0x30 + 0x2F]  # remove marker byte
    # now raw is the logical image
    # decrypt header
    hdr = bytearray(raw[:0x40])
    key = 0xFFFF
    for i in range(0x40):
        hdr[i] ^= key & 0xFF
        key = ((key << 1) ^ (0x1021 if key & 0x8000 else 0)) & 0xFFFF
    n_files = struct.unpack_from('<H', hdr, 8)[0]
    entries = []
    for i in range(n_files):
        off = 0x40 + i * 0x50
        e = bytearray(raw[off:off + 0x50])
        key = 0xFFFF
        for j in range(0x50):
            e[j] ^= key & 0xFF
            key = ((key << 1) ^ (0x1021 if key & 0x8000 else 0)) & 0xFFFF
        typ = struct.unpack_from('<H', e, 0)[0]
        data_off = struct.unpack_from('<I', e, 8)[0]
        data_len = struct.unpack_from('<I', e, 12)[0]
        name = e[0x40:0x50].split(b'\x00')[0].decode('ascii', errors='replace')
        entries.append((typ, name, data_off, data_len))
    for typ, name, data_off, data_len in entries:
        if typ == 0 and 'flash' in name.lower():
            return bytes(raw[data_off:data_off + data_len])
    raise ValueError("no flash.bin entry found in .fwsc")


# --------------------------------------------------------------- main ---

def find_device(timeout=15):
    print("Looking for FM-1 in UBOOT mode (VID:4C4A PID:8057)...")
    print("  If not in UBOOT mode: hold OCT+/- for 5 seconds.")
    print()
    deadline = time.time() + timeout
    while time.time() < deadline:
        dev = usb.core.find(idVendor=VID, idProduct=PID, backend=_usb_backend)
        if dev is not None:
            return dev
        time.sleep(0.5)
        print(".", end="", flush=True)
    print()
    return None


def setup_device(dev):
    try:
        dev.set_configuration()
    except usb.core.USBError:
        pass  # may already be configured
    cfg = dev.get_active_configuration()
    intf = cfg[(0, 0)]
    # detach kernel driver if needed
    try:
        if dev.is_kernel_driver_active(intf.bInterfaceNumber):
            dev.detach_kernel_driver(intf.bInterfaceNumber)
    except (usb.core.USBError, NotImplementedError):
        pass
    ep_in = usb.util.find_descriptor(intf, custom_match=lambda e:
        usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN
        and usb.util.endpoint_type(e.bmAttributes) == usb.util.ENDPOINT_TYPE_BULK)
    ep_out = usb.util.find_descriptor(intf, custom_match=lambda e:
        usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT
        and usb.util.endpoint_type(e.bmAttributes) == usb.util.ENDPOINT_TYPE_BULK)
    if not ep_in or not ep_out:
        raise IOError("bulk endpoints not found")
    return UbootDevice(dev, ep_in, ep_out)


def upload_loader(udev, loader_data):
    print(f"Uploading wl82loader ({len(loader_data)} bytes)...", end="", flush=True)
    for off in range(0, len(loader_data), IO_SIZE):
        chunk = loader_data[off:off + IO_SIZE]
        rom_mem_write(udev, LOADER_ADDR + off, chunk)
        print(".", end="", flush=True)
    print(" done")
    print(f"Jumping to loader at 0x{LOADER_ADDR:08X}...", end="", flush=True)
    rom_jump(udev, LOADER_ADDR, LOADER_ARG)
    print(" ok")
    time.sleep(0.5)


def flash_firmware(udev, flash_image):
    key = loader_chip_key(udev)
    print(f"  chip key: 0x{key:04X}", end="")
    if key != CHIP_KEY_EXPECT:
        print(f" (expected 0x{CHIP_KEY_EXPECT:04X}, WRONG)")
        sys.exit(4)
    print(" ok")

    info = loader_online_device(udev)
    print(f"  online device: type=0x{info['type']:02X} id=0x{info['id']:06X}")

    n_sectors = (APP_END - APP_START) // SECTOR_SIZE
    changed = []

    print(f"Reading flash [{APP_START:#x}..{APP_END:#x})...", end="", flush=True)
    for i in range(n_sectors):
        addr = APP_START + i * SECTOR_SIZE
        existing = bytearray()
        for off in range(0, SECTOR_SIZE, IO_SIZE):
            existing.extend(loader_flash_read(udev, addr + off, IO_SIZE))
        target = flash_image[addr:addr + SECTOR_SIZE]
        if bytes(existing) != target:
            changed.append(i)
        if i % 16 == 0:
            print(".", end="", flush=True)
    print(f" {len(changed)} sectors to update (of {n_sectors})")

    if not changed:
        print("Flash already matches. Nothing to write.")
        return True

    for idx, sec_i in enumerate(changed):
        addr = APP_START + sec_i * SECTOR_SIZE
        target = flash_image[addr:addr + SECTOR_SIZE]
        print(f"  [{idx+1}/{len(changed)}] sector 0x{addr:05X}: erase...", end="", flush=True)
        loader_erase_sector(udev, addr)
        print(" write...", end="", flush=True)
        for off in range(0, SECTOR_SIZE, IO_SIZE):
            loader_flash_write(udev, addr + off, target[off:off + IO_SIZE])
        print(" verify...", end="", flush=True)
        readback = bytearray()
        for off in range(0, SECTOR_SIZE, IO_SIZE):
            readback.extend(loader_flash_read(udev, addr + off, IO_SIZE))
        if bytes(readback) != target:
            print(" MISMATCH!")
            sys.exit(4)
        print(" ok")

    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("fwsc", type=Path, help="firmware package (.fwsc)")
    ap.add_argument("--yes", "-y", action="store_true", help="skip confirmation prompt")
    ap.add_argument("--timeout", type=int, default=15, help="device detection timeout (seconds)")
    ap.add_argument("--loader", type=Path, help="wl82loader.bin path (auto-detected)")
    a = ap.parse_args()

    if not a.fwsc.exists():
        sys.exit(f"not found: {a.fwsc}")

    # find loader blob
    loader_path = a.loader
    if not loader_path:
        candidates = [
            Path(__file__).parent.parent / ".scratch/examples/Transporter/pico-fork/recovery/jl-uboot-tool/data/loaderblobs/usb/wl82loader.bin",
        ]
        for c in candidates:
            if c.exists():
                loader_path = c
                break
    if not loader_path or not loader_path.exists():
        sys.exit("wl82loader.bin not found. Pass --loader path/to/wl82loader.bin")

    loader_data = loader_path.read_bytes()
    print(f"loader: {loader_path.name} ({len(loader_data)} bytes)")

    print(f"package: {a.fwsc.name}")
    flash_image = fwsc_extract_flash(a.fwsc)
    print(f"flash image: {len(flash_image)} bytes ({len(flash_image)//1024} KiB)")
    own_fwsc = Path(__file__).resolve().parent.parent / 'build' / 'core-fm1.fwsc'
    if len(flash_image) < APP_END:
        if a.fwsc.resolve() == own_fwsc.resolve():
            sys.exit(f"flash image too small ({len(flash_image)} < {APP_END})")
        pad = APP_END - len(flash_image)
        print(f"flash image padded with {pad} bytes (0xFF) to fill app partition")
        flash_image += b'\xff' * pad

    dev = find_device(a.timeout)
    if dev is None:
        print("\nFM-1 not found in UBOOT mode.")
        print("  1. Hold OCT+ and OCT- together for 5 seconds")
        print("  2. Screen goes black = UBOOT mode entered")
        print("  3. Run this command again")
        sys.exit(3)

    product = usb.util.get_string(dev, dev.iProduct) if dev.iProduct else "?"
    print(f"\nFound: {product} (VID:{dev.idVendor:04X} PID:{dev.idProduct:04X})")

    if not a.yes:
        ans = input("\nFlash firmware? [y/N] ")
        if ans.lower() != 'y':
            print("Cancelled.")
            sys.exit(0)

    print()
    udev = setup_device(dev)

    upload_loader(udev, loader_data)

    # re-find device after loader jump (it may re-enumerate)
    time.sleep(1.0)
    dev2 = usb.core.find(idVendor=VID, idProduct=PID, backend=_usb_backend)
    if dev2 is not None and dev2 != dev:
        udev = setup_device(dev2)

    print("Loader ready.")
    flash_firmware(udev, flash_image)

    print("\nRebooting into firmware...")
    try:
        loader_run_app(udev)
    except Exception:
        pass  # device resets, USB error expected

    print("Done! FM-1 should boot the new firmware.")


if __name__ == "__main__":
    main()
