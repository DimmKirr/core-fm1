# Development

## Build

```sh
task build
```

Produces `build/core-fm1.fwsc`. Requires the JieLi toolchain (downloaded
automatically on first run).

## Flashing firmware

Two methods are available. Both require a built `.fwsc` package.

### USB-MIDI SysEx (`task flash`)

The normal update path. The running firmware receives the package over
USB-MIDI SysEx, boots the update loader, and the loader writes flash.

```sh
task flash
```

Prerequisites:
- The FM-1 must be connected via USB and enumerated as a MIDI device.
- `task flash:debug` lists visible MIDI ports and watches for the device.

How it works:
1. `fm1_install.py` finds the FM-1 MIDI port (matches "FM-1", "Felucca", or
   "Update" in the port name).
2. Sends a SysEx upgrade command. The firmware reads part of the package,
   validates it, and reboots into the update loader.
3. The update loader ("Felucca Update" MIDI port) requests data blocks via
   SysEx. The script replies with the corresponding chunks.
4. After the last block, the loader writes flash and reboots. The script
   waits for the device to reappear and verifies the installed identity.

Troubleshooting:
- **No MIDI port visible**: run `task flash:debug`. If no device appears,
  check the USB cable. If only "FM-1" appears but flash fails, the device
  may not be entering update mode: try `task flash:uboot` instead.
- **"MIDI inputs: (none)"**: USB polling may not be running. Ensure
  `usb_poll()` is called from Timer5 ISR (not the main loop).
- **Install hangs at "waiting for loader"**: the firmware did not enter
  update mode. Power-cycle and retry, or use UBOOT mode.

### UBOOT mode (`task flash:uboot`)

Direct flash write via the ROM-stage USB bootloader. Bypasses the running
firmware entirely: works even when the firmware is broken or MIDI does not
enumerate.

```sh
task flash:uboot
```

Entering UBOOT mode:
1. Power off the FM-1 (unplug USB).
2. Hold **OCT+** and **OCT-** simultaneously.
3. Plug USB in while holding both buttons.
4. Keep holding for ~5 seconds until the host detects a new USB device
   (VID `4C4A`, PID `8057`, product "WL80UBOOT1.00").

The script:
1. Prompts for confirmation (skip with `--yes`).
2. Uploads `wl82loader.bin` to the SoC RAM (MengLi-ciphered via SCSI
   vendor commands over USB Bulk-Only Transport).
3. Jumps to the loader.
4. Reads the existing flash content in the application area
   (`0x4000`..`0x93000`).
5. Compares sectors (4 KiB each), erases and writes only changed ones.
6. Verifies each written sector by reading back.
7. Reboots the device.

Requirements:
- `libusb` must be installed (`brew install libusb` on macOS,
  `apt install libusb-1.0-0` on Linux, or via nix).
- macOS needs `sudo` because the kernel claims USB mass storage devices.

Troubleshooting:
- **"No backend available"**: libusb is not found. Install it or check that
  the nix store path is reachable.
- **"Access denied"**: run with `sudo task flash:uboot` (macOS).
- **Device not found**: ensure the FM-1 is in UBOOT mode (hold OCT+/-
  during plug-in). Check `lsusb` or `system_profiler SPUSBDataType` for
  VID `4C4A`.

### Choosing a method

| | SysEx (`task flash`) | UBOOT (`task flash:uboot`) |
|---|---|---|
| Requires working firmware | Yes | No |
| Requires MIDI enumeration | Yes | No |
| Needs sudo (macOS) | No | Yes |
| Speed | ~30 s | ~15 s (diff-based) |
| Risk | Low (loader validates) | Low (header area untouched) |
| Use when | Normal development | Firmware is broken, MIDI not working |

### Flash endurance

The FM-1 uses an ESMT SPI NOR flash (JEDEC `0x856014`, 1 MB). Each 4 KiB
sector supports ~100,000 program/erase cycles. The UBOOT flasher only
writes changed sectors, so incremental builds during development use
very few cycles per update.

## Debugging

### USB error codes on LCD

Navigate to the FILES page (USB host mode). If a USB disk connection fails,
the LCD shows `USB:ERR -N` where N is the error code:

| Code | Meaning |
|------|---------|
| -1 to -7 | USB enumeration error (step number) |
| -11 to -16 | MSC (Mass Storage Class) init error, offset by -10 |

### Flash debug

```sh
task flash:debug
```

Lists all visible MIDI ports and watches for FM-1 or update loader
devices for 10 seconds.
