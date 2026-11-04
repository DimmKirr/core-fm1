#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read debug logs from a running FM-1 over USB-MIDI SysEx.

Requires python-rtmidi:
    uv run --with python-rtmidi tools/fm1log.py
    uv run --with python-rtmidi tools/fm1log.py --follow
    uv run --with python-rtmidi tools/fm1log.py --hello
    uv run --with python-rtmidi tools/fm1log.py --wifi
    uv run --with python-rtmidi tools/fm1log.py --bytes 1024   # only the last 1 KB

A plain dump returns everything the device ring still holds (CFM1_DBG_LOG_SIZE, 4 KB):
the device clamps any older read position to the start of the ring.

Uses the fm1-dock SysEx protocol (D_HELLO=0x01, D_LOG=0x20) via hbdeploy.py."""
import argparse
import datetime
import struct
import sys
import time

sys.path.insert(0, __import__("os").path.dirname(__file__))
import hbdeploy

D_WIFI = 0x25
D_WIFI_BSS = 0x26
D_PROBES = 0x27
D_WL0 = 0x29      # CONFIG_WIFI_DEBUG_WLCTL0_PROBE: run the WL_CTL0 release probe once
D_SWEEP = 0x28    # read consecutive words from a base (SoC, WL control, MAC, DMA windows)
SWEEP_MAX = 60    # words per reply: 8-byte header + 60 words = 248 bytes

WIFI_STATES = [
    "OFF", "IDLE", "SCANNING", "AUTH", "ASSOC", "WPA", "CONNECTED"
]

WEATHER_STATES = [
    "IDLE", "GEO_FETCH", "GEO_WAIT", "WX_FETCH", "WX_WAIT", "DONE"
]


def print_hello(p):
    h = hbdeploy.hello(p)
    print(f"proto={h['proto']}  crash_pc={h['crash_pc']:#010x}  crash_mod={h['crash_module']}")
    print(f"worst={h['worst']}  overruns={h['overruns']}  log_w={h['log_w']}")
    print(f"free={h['free']}  largest={h['largest']}  max_file={h['max_file']}")
    return h


def _ip_str(addr):
    if addr == 0:
        return "-"
    return f"{addr & 0xFF}.{(addr >> 8) & 0xFF}.{(addr >> 16) & 0xFF}.{(addr >> 24) & 0xFF}"


def _mac_str(b):
    return ":".join(f"{x:02X}" for x in b)


def print_wifi(p):
    d = p.ask(D_WIFI)
    if not d or d[0] != 0:
        print("wifi: query failed (firmware may not have CONFIG_WIFI + CONFIG_DEBUG_DAP)")
        return

    sta = d[1]
    sdio_init = d[2]
    sdio_err = d[3]
    mac = d[4:10]
    ip = struct.unpack_from("<I", d, 10)[0]
    gw = struct.unpack_from("<I", d, 14)[0]
    dns = struct.unpack_from("<I", d, 18)[0]
    scan_n = d[22]
    bss_n = d[23]
    wx_state = d[24]
    wx_valid = d[25]
    wx_temp = struct.unpack_from("<i", d, 26)[0]
    wx_hum = d[30]
    wx_wmo = d[31]
    wx_wind = struct.unpack_from("<I", d, 32)[0]
    wx_ms = struct.unpack_from("<I", d, 36)[0]

    sta_name = WIFI_STATES[sta] if sta < len(WIFI_STATES) else f"?{sta}"
    wx_name = WEATHER_STATES[wx_state] if wx_state < len(WEATHER_STATES) else f"?{wx_state}"

    print(f"sta:     {sta_name}")
    print(f"mac:     {_mac_str(mac)}")
    print(f"sdio:    init={sdio_init}  errors={sdio_err}")
    print(f"ip:      {_ip_str(ip)}")
    print(f"gw:      {_ip_str(gw)}")
    print(f"dns:     {_ip_str(dns)}")
    print(f"scan:    {scan_n} results, {bss_n} BSS entries")
    print(f"weather: {wx_name}" + (f"  temp={wx_temp/10:.1f}C  hum={wx_hum}%  wmo={wx_wmo}  wind={wx_wind/10:.1f}km/h" if wx_valid else ""))
    if wx_valid:
        print(f"         updated {wx_ms}ms ago" if wx_ms else "         just updated")

    d2 = p.ask(D_WIFI_BSS)
    if d2 and d2[0] == 0 and len(d2) > 1:
        off = 1
        idx = 0
        print("--- BSS table ---")
        while off + 10 <= len(d2):
            bssid = d2[off:off+6]
            off += 6
            ch = d2[off]; off += 1
            rssi = d2[off] if d2[off] < 128 else d2[off] - 256; off += 1
            has_rsn = d2[off]; off += 1
            slen = d2[off]; off += 1
            if off + slen > len(d2):
                break
            ssid = bytes(d2[off:off+slen]).decode("latin-1", errors="replace")
            off += slen
            lock = "WPA" if has_rsn else "   "
            print(f"  [{idx}] {_mac_str(bssid)}  ch={ch:2d}  rssi={rssi:4d}  {lock}  {ssid}")
            idx += 1
        if idx == 0:
            print("  (empty)")


PHASE_NAMES = ["pre-clock", "post-clock", "post-reset"]

RT2800_NAMES = {
    # Legacy addresses (open bus on real HW, kept for reference)
    0x00200: "INT_SOURCE_CSR(legacy)",
    0x00204: "INT_MASK_CSR(legacy)",
    0x00208: "WPDMA_GLO_CFG(legacy)",
    0x0020C: "WPDMA_RST_IDX(legacy)",
    0x00210: "DELAY_INT_CFG(legacy)",
    0x00230: "TX_BASE_PTR0(legacy)",
    0x00234: "TX_MAX_CNT0(legacy)",
    0x00290: "RX_BASE_PTR(legacy)",
    0x00294: "RX_MAX_CNT(legacy)",
    0x00298: "RX_CRX_IDX(legacy)",
    0x0029C: "RX_DRX_IDX(legacy)",
    # DMA candidate at 0x40000 base (RT2800 offsets)
    0x40200: "INT_SOURCE_CSR?",
    0x40204: "INT_MASK_CSR?",
    0x40208: "WPDMA_GLO_CFG?",
    0x4020C: "WPDMA_RST_IDX?",
    0x40210: "DELAY_INT_CFG?",
    0x40230: "TX_BASE_PTR0?",
    0x40234: "TX_MAX_CNT0?",
    0x40238: "TX_CTX_IDX0?",
    0x4023C: "TX_DTX_IDX0?",
    0x40240: "TX_BASE_PTR1?",
    0x40244: "TX_MAX_CNT1?",
    0x40248: "TX_CTX_IDX1?",
    0x4024C: "TX_DTX_IDX1?",
    0x40260: "TX_BASE_PTR2?",
    0x40264: "TX_MAX_CNT2?",
    0x40268: "TX_CTX_IDX2?",
    0x4026C: "TX_DTX_IDX2?",
    0x40280: "TX_BASE_PTR4?",
    0x40284: "TX_MAX_CNT4?",
    0x40288: "TX_CTX_IDX4?",
    0x4028C: "TX_DTX_IDX4?",
    0x40290: "RX_BASE_PTR?",
    0x40294: "RX_MAX_CNT?",
    0x40298: "RX_CRX_IDX?",
    0x4029C: "RX_DRX_IDX?",
    # MAC registers (0x30000 base)
    0x31000: "CHIP_VERSION",
    0x31004: "MAC_SYS_CTRL",
    0x3101C: "BBP_CSR_CFG",
    0x30300: "MAC_ADDR_DW0",
    0x30304: "MAC_ADDR_DW1",
    0x31100: "XIFS_TIME_CFG",
    0x31104: "BKOFF_SLOT_CFG",
    0x31330: "TX_SW_CFG0",
    # GPIO (not WiFi)
    0x50000: "GPIO_PA_OUT",
    0x5000C: "GPIO_PA_DIE",
}


def print_probes(p):
    d = p.ask(D_PROBES)
    if not d or d[0] != 0:
        print("probes: query failed (firmware may not have CONFIG_WIFI_PROBES)")
        return

    count = d[1]
    print(f"=== WiFi Hardware Probe Results ({count} readings) ===")
    print(f"{'Phase':<12} {'Address':>8}  {'Value':>10}  {'RT2800 Name':<20}  Tag")
    print("-" * 72)

    off = 2
    for i in range(count):
        if off + 10 > len(d):
            break
        addr = (d[off] << 24) | (d[off+1] << 16) | (d[off+2] << 8) | d[off+3]
        off += 4
        val = (d[off] << 24) | (d[off+1] << 16) | (d[off+2] << 8) | d[off+3]
        off += 4
        phase = d[off]; off += 1
        rw = chr(d[off]); off += 1

        phase_name = PHASE_NAMES[phase] if phase < len(PHASE_NAMES) else f"?{phase}"
        rt_name = RT2800_NAMES.get(addr, "")
        tag = "[M:hw]"
        if addr >= 0x50000:
            tag = "[M:hw] ERRATA"

        print(f"  {phase_name:<10} 0x{addr:05X}  0x{val:08X}  {rt_name:<20}  {tag}")

    if count == 0:
        print("  (no results - probes may not have run yet)")

    print()
    print("Legend: pre-clock = before WiFi clock enable")
    print("        post-clock = clocks on, MAC still in reset")
    print("        post-reset = MAC reset released, registers accessible")
    print()
    print("Copy these results into SPECSHEET.md with [M:hw] tags.")


def wl0_request(p, mode=0):
    """D_WL0 modes: 0 release WL_CTL0 and scan once; 2 retry the SoC writes that refused on a
    held block, then scan (12k). Mode 1 (warm-reboot variant) was removed in 12p."""
    d = p.ask(D_WL0, struct.pack("<I", mode))
    return bool(d) and d[0] == 0


def sweep_read(p, base, count):
    """Read `count` words from `base` in reply-sized chunks. Returns {addr: value}."""
    regs = {}
    addr = base & ~3
    end = addr + 4 * count
    while addr < end:
        n = min(SWEEP_MAX, (end - addr) // 4)
        d = p.ask(D_SWEEP, struct.pack("<IH", addr, n))
        if not d or d[0] != 0:
            raise RuntimeError(f"sweep refused at {addr:#x}: outside the allowed windows, "
                               "or MAC/DMA space before the bus gate")
        got = struct.unpack_from("<I", d, 4)[0]
        for i, v in enumerate(struct.unpack_from(f"<{n}I", d, 8)):
            regs[got + 4 * i] = v
        addr += 4 * n
    return regs


def sweep_text(regs):
    """`0xADDR = 0xVALUE` lines, the fm1-base /status and /sweep format, for wifi_sweep_diff.py."""
    return "".join(f"0x{a:08x} = 0x{v:08x}\n" for a, v in sorted(regs.items()))


def _hostts():
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


def _read_from(p, pos):
    """Read log from byte position `pos` forward. Returns (text, new_pos)."""
    out = bytearray()
    frm = pos
    while True:
        d = p.ask(hbdeploy.LOG, struct.pack("<I", frm))
        log_w, at = struct.unpack_from("<II", d, 4)
        chunk = d[12:]
        if not chunk:
            return out.decode("latin-1"), max(frm, log_w)
        out += chunk
        frm = at + len(chunk)
        if frm >= log_w:
            return out.decode("latin-1"), frm


def _emit(text, host_ts):
    for line in text.splitlines(keepends=True):
        if host_ts:
            sys.stdout.write(f"[{_hostts()}] {line}")
        else:
            sys.stdout.write(line)
    sys.stdout.flush()


def dump_log(p, host_ts=True, tail=0):
    """Print what the ring holds (all of it, or the last `tail` bytes); return the read position."""
    h = hbdeploy.hello(p)
    pos = max(0, h["log_w"] - tail) if tail > 0 else 0
    text, pos = _read_from(p, pos)
    if text:
        _emit(text, host_ts)
    return pos


def follow_log(p, pos, interval=0.2, host_ts=True, stop=None, timeout=None):
    """Poll for new log text from `pos`. Returns 'stop' once `stop` appears in new text,
    'timeout' after `timeout` seconds, 'lost' when the device stops answering (a halted
    unit); with neither limit it follows until interrupted."""
    t0 = time.monotonic()
    carry = ""
    while True:
        time.sleep(interval)
        try:
            text, pos = _read_from(p, pos)
        except SystemExit:
            _emit("[fm1log] device stopped answering\n", host_ts)
            return "lost"
        if text:
            _emit(text, host_ts)
            if stop and stop in carry + text:
                return "stop"
            carry = text[-len(stop):] if stop else ""
        if timeout is not None and time.monotonic() - t0 >= timeout:
            _emit("[fm1log] timeout\n", host_ts)
            return "timeout"


def read_log(p, follow=False, interval=0.2, host_ts=True, tail=0):
    """Print the device log. `tail` 0 reads everything the ring still holds;
    N > 0 reads only the last N bytes. With `follow`, keep polling."""
    pos = dump_log(p, host_ts=host_ts, tail=tail)
    if follow:
        follow_log(p, pos, interval=interval, host_ts=host_ts)


def build_parser():
    ap = argparse.ArgumentParser(description="Read FM-1 debug logs over USB-MIDI")
    ap.add_argument("--port", help="MIDI port name substring (default: auto-detect)")
    ap.add_argument("--hello", action="store_true", help="Print device status and exit")
    ap.add_argument("--wifi", action="store_true", help="Print WiFi subsystem state and exit")
    ap.add_argument("--probes", action="store_true", help="Print WiFi hardware probe results and exit")
    ap.add_argument("--follow", "-f", action="store_true", help="Poll for new log entries continuously")
    ap.add_argument("--interval", type=float, default=0.2, help="Poll interval in seconds (default: 0.2)")
    ap.add_argument("--no-ts", action="store_true", help="Disable host-side timestamps")
    ap.add_argument("--wl0", action="store_true",
                    help="Run the WL_CTL0 release probe (CONFIG_WIFI_DEBUG_WLCTL0_PROBE) and follow until its scan ends")
    ap.add_argument("--retry", action="store_true",
                    help="With --wl0: retry the writes refused on a held block (12k) instead of the release probe")
    ap.add_argument("--sweep", nargs=2, type=lambda s: int(s, 0), metavar=("BASE", "COUNT"),
                    help="Dump COUNT words from BASE as `addr = value` lines (task debug:sweep)")
    ap.add_argument("--timeout", type=float, default=30,
                    help="--wl0: seconds to follow before giving up (default: 30)")
    ap.add_argument("--bytes", type=int, default=0,
                    help="Read only the last N bytes of the device ring (default: all it holds, 4 KB)")
    return ap


def main():
    args = build_parser().parse_args()

    def open_port():
        return hbdeploy.Port(name=args.port)

    p = open_port()

    if args.hello:
        print_hello(p)
        return

    if args.wifi:
        print_wifi(p)
        return

    if args.probes:
        print_probes(p)
        return

    if args.sweep:
        sys.stdout.write(sweep_text(sweep_read(p, args.sweep[0], args.sweep[1])))
        return

    if args.wl0:
        print_hello(p)
        print("---")
        pos = dump_log(p, host_ts=not args.no_ts, tail=args.bytes)
        if not wl0_request(p, mode=2 if args.retry else 0):
            print("wl0: request refused (firmware built without CONFIG_WIFI_DEBUG_WLCTL0_PROBE?)")
            return
        print("wl0: " + ("retry of the refused writes" if args.retry else "probe")
              + " requested; following until its scan finishes")
        why = follow_log(p, pos, interval=args.interval, host_ts=not args.no_ts,
                         stop="wifi: scan done n=", timeout=args.timeout)
        print(f"wl0: {why}")
        return

    if args.follow:
        h = print_hello(p)
        print("---")

    read_log(p, follow=args.follow, interval=args.interval, host_ts=not args.no_ts,
             tail=args.bytes)


if __name__ == "__main__":
    main()
