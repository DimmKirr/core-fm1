# WiFi Driver Design: GPL-3.0 STA Driver for AC791N

## Goal

Build a GPL-3.0 compatible WiFi STA driver for the AC791N (WL82 radio) from
clean-room reverse engineering of JieLi's LLVM bitcode blobs and the Ralink
RT30xx GPL driver family. The driver enables 802.11b/g/n station mode with
WPA2-PSK, connecting the FM-1 synthesizer to WiFi networks for MIDI transport,
firmware OTA, and web configuration.

## Constraints

- **License**: GPL-3.0-only. No binary blob linking. All code must be clean-room
  or from compatible open-source (GPL, BSD, MIT).
- **Architecture**: monolithic unity build (Approach A), matching Optimist BLE pattern.
- **Coexistence**: WiFi-only first. `CONFIG_WIFI` mutually exclusive with `CONFIG_BLE`.
  Coexistence via RFPRIO is a follow-up.
- **Scope**: STA mode only. No AP, P2P, WPS, WPA3, or monitor mode.
- **Target**: 16K-26K lines of C. 6-8 months part-time.
- **Testing**: emulator-based cross-tests first, hardware later.

## Spike Summary

The feasibility spike (`.scratch/examples/wifi/`) produced:

| Artifact | Key Finding |
|----------|-------------|
| 729 decompiled IR files (112 MB) | LLVM bitcode with full debug info |
| 21,380 function index | 9,235 defined, 12,145 external |
| Register map | 41 known + 291 new MMIO addresses |
| External symbols | 3,472 unique, categorized |
| Replaceability | 63% replaceable, 2% irreplaceable, 35% out of scope |
| Driver lineage | Ralink RT30xx standalone GPL (pre-mainline, ~2010) |

The 181 irreplaceable functions are all in `wl_rf_common.a` (RF HAL). Everything
else has GPL or BSD source available.

## Directory Layout

```
firmware/
  hal/
    fm1_wifi_rf.h           # WiFi RF HAL (header-only, register macros)
    fm1_wifi_sdio.h         # SDIO internal bus (header-only)
    fm1_wifi_bbp.h          # BBP table access and programming
    wifi_rf_tables.h        # RF init constant tables (generated/captured)
  src/
    wifi/
      wifi_rf.c             # RF init, calibration, channel, power
      wifi_sdio.c           # SDIO CMD52/CMD53 bus driver
      wifi_mac.c            # 802.11 MAC frame handling
      wifi_sta.c            # STA state machine
      wifi_data.c           # TX/RX data path
      wifi_wpa.c            # WPA2-PSK + CCMP
      wifi_rtos.c           # sk_buff, spinlock, tasklet shim
      wifi_lwip.c           # lwIP netif driver
      wifi_api.c            # Public API
  adapters/
    cfm1_wifi.h             # Stubs when CONFIG_WIFI is off

kconf/
  wifi.kconfig              # Kconfig menu

tests/
  wifi/
    test_rf_init.py         # RF init register sequence cross-test
    test_sdio.py            # SDIO protocol tests
    test_mac_sta.py         # STA state machine tests
    e2e_wifi_test.py        # E2E emulator WiFi test

tools/
  wifi_ir_to_c.py           # Semi-automated LLVM IR to C converter
```

## Kconfig

```kconfig
menu "WiFi"

config WIFI
    bool "WiFi (802.11 b/g/n STA)"
    default n
    depends on !BLE
    help
      GPL-3.0 WiFi STA driver for the AC791N WL82 radio.
      Mutually exclusive with BLE in this version.

config WIFI_LWIP
    bool "lwIP TCP/IP stack"
    depends on WIFI
    default y

config WIFI_WPA
    bool "WPA2-PSK authentication"
    depends on WIFI
    default y

config WIFI_MIDI
    bool "MIDI over WiFi (rtpMIDI/OSC)"
    depends on WIFI && WIFI_LWIP
    default n

endmenu
```

## Layer Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  wifi_api.c                                                 │
│    wifi_init(), wifi_scan(), wifi_connect(ssid, pwd),       │
│    wifi_disconnect(), wifi_get_state(), wifi_get_ip()       │
├─────────────────────────────────────────────────────────────┤
│  wifi_lwip.c: lwIP netif driver                             │
│  wifi_wpa.c: WPA2-PSK 4-way handshake, CCMP encrypt/decrypt│
├─────────────────────────────────────────────────────────────┤
│  wifi_sta.c: STA state machine                              │
│    IDLE -> SCANNING -> AUTH -> ASSOC -> WPA -> CONNECTED    │
├─────────────────────────────────────────────────────────────┤
│  wifi_mac.c: 802.11 frame TX/RX, beacon parse, rate control │
│  wifi_data.c: data frame handling, A-MSDU, reorder          │
├─────────────────────────────────────────────────────────────┤
│  wifi_rtos.c: sk_buff pool, spinlock (IRQ disable),         │
│    tasklet (deferred work), timer, memory allocator          │
├─────────────────────────────────────────────────────────────┤
│  wifi_rf.c: RF init, VCO scan, calibration, channel set     │
│  wifi_sdio.c: SDIO CMD52/53, DMA, IRQ handler               │
│  HAL: fm1_wifi_rf.h, fm1_wifi_sdio.h, fm1_wifi_bbp.h        │
└─────────────────────────────────────────────────────────────┘
```

## RF HAL (the gating layer)

### Source

Reverse-engineered from `.scratch/examples/wifi/ir/wl_rf_common/` (181 functions,
~3,847 estimated C lines). IR has function names, source paths, variable names,
and string literals.

### Approach: hybrid IR-to-C

1. **Tool-assisted** (`wifi_ir_to_c.py`): converts simple register read/write
   functions and lookup-table-driven code automatically. Covers ~60% of functions.
2. **Manual translation**: calibration algorithms (VCO scan, IQ correction,
   DC offset), state machines, and multi-step init sequences. ~40% of functions.
3. **Register naming**: all `inttoptr(i32 N)` in IR mapped to named macros
   (e.g., 72040 -> `FM1_WL_CAL_START` = 0x11968).

### Key modules

| IR Source | C Target | Functions | Complexity |
|-----------|----------|-----------|------------|
| `wf_rf_init.c.ll` | `wifi_rf.c` | 21 | HIGH: PLL, oscillator, trim application |
| `wf_rf_cal.c.ll` | `wifi_rf.c` | ~40 | HIGH: TX/RX IQ cal, DC offset, BW tuning |
| `wf_phy_mac_init.c.ll` | `wifi_rf.c` | 3 | MEDIUM: PHY init, SIFS |
| `wf_phy_mac_lib.c.ll` | HAL header | 14 | LOW: BBP read/write helpers |
| `wl_sdio.c.ll` | `wifi_sdio.c` | 12 | MEDIUM: bus enumeration, IRQ, DMA |
| `wl_sdio_host.c.ll` | `wifi_sdio.c` | 18 | LOW: host controller, mutex |
| `wl_usb_to_sdio.c.ll` | `wifi_sdio.c` | 25 | MEDIUM: TX/RX data path |
| `wf_deep_sleep.c.ll` | `wifi_rf.c` | ~22 | MEDIUM: power management |
| Others (5 files) | various | ~46 | LOW-MEDIUM |

### Relationship with fm1_ble_rf.h

The existing `fm1_ble_rf.h` handles the shared RF analog init (groups 1-14 of
the boot sequence). WiFi RF init extends from where BLE leaves off, adding:
- WiFi-specific BBP configuration
- WiFi channel selection (vs BLE's fixed 2402-2480 hopping)
- WiFi-specific power amplifier settings
- WiFi MAC/PHY register programming

For WiFi-only mode, `fm1_wifi_rf.h` includes the full init (not depending on
BLE's partial init). The shared analog registers are programmed by whichever
subsystem boots first.

## MAC / STA Layer

### Source

Derived from the Ralink RT30xx standalone GPL driver structure, informed by
the decompiled IR function index. Not a copy: the Linux mainline rt2x00
and the Ralink standalone driver provide the 802.11 state machine and frame
handling reference. The code is written for core-fm1's bare-metal environment.

### STA state machine

```
IDLE ──scan()──> SCANNING ──found()──> AUTH_REQ
                                          │
                           auth_rsp() <───┘
                                │
                          ASSOCIATING ──assoc_rsp()──> WPA_HANDSHAKE
                                                           │
                                              msg4_sent() <┘
                                                    │
                                               CONNECTED ──disconnect()──> IDLE
```

### Frame types handled

- **Management**: Beacon, Probe Req/Rsp, Auth, Assoc Req/Rsp, Deauth, Disassoc
- **Data**: Data, QoS Data (for WMM)
- **Control**: ACK, RTS/CTS (hardware-assisted)

## RTOS Shim

Thin wrappers mapping the 46 RTOS API functions and 38 Linux compat functions
to core-fm1 bare-metal primitives:

| Abstraction | Implementation |
|-------------|---------------|
| `spinlock` | IRQ disable/enable (`cli`/`sti`) |
| `sk_buff` | Static pool of pre-allocated buffers with header prepend |
| `tasklet` | Deferred function call from ISR context via a pending queue |
| `wait_event` | Polling loop with `os_time_dly` |
| `timer` | Software timer list checked from main loop |
| `kmalloc/kfree` | Pool allocator from `_pool_start`/`_pool_end` |

## WPA2-PSK

Port of wpa_supplicant's STA-only path. The original is BSD-licensed (hostap.w1.fi),
GPL-compatible. Needs:
- PBKDF2-SHA1 for PSK derivation
- 4-way handshake state machine
- AES-128 for CCMP encryption/decryption
- EAPOL frame handling

AES implementation from mbedTLS (Apache 2.0) or the existing `lib_ccm_aes`
equivalent, rewritten.

## lwIP Integration

lwIP 2.x (BSD licensed) port. Standard `netif` driver pattern:
- `low_level_init()`: allocate TX/RX buffers
- `low_level_output(netif, pbuf)`: wifi_mac TX path
- `low_level_input()`: called from RX IRQ, passes pbuf to `netif_input()`
- DHCP client for IP address acquisition

## Cross-Test Harness

### Design

Python test framework extending the existing E2E infrastructure (`tests/e2e_fixtures.py`).
Two firmware builds loaded into the emulator:

1. **GPL build**: our clean-room driver compiled with `task build`
2. **SDK build**: JieLi SDK firmware (reference, not shipped)

### Test strategy

| Level | What | How |
|-------|------|-----|
| Unit | RF init register sequences | Capture emulator register writes, compare sequences |
| Unit | BBP table values | Compare BBP window programming between builds |
| Unit | VCO scan convergence | Both must find a valid band in the same range |
| Integration | SDIO data transfer | TX/RX round-trip through emulator SDIO model |
| Integration | STA association | State machine reaches CONNECTED with a mock AP |
| E2E | WiFi connection | Full connect + DHCP + data transfer in emulator |

### Cross-validation

Tests assert **functional equivalence**, not bit-identity:
- Same register set must be written (order may differ)
- Same BBP entries must be programmed (values may have minor trim differences)
- Same state machine endpoints reached
- Data transfer works end-to-end

## Implementation Order

1. **IR-to-C tooling** (`wifi_ir_to_c.py`): build the hybrid converter
2. **RF HAL** (`fm1_wifi_rf.h`, `wifi_rf.c`): reverse-engineer from IR
3. **SDIO bus** (`fm1_wifi_sdio.h`, `wifi_sdio.c`): from IR
4. **RTOS shim** (`wifi_rtos.c`): clean-room, 500-800 lines
5. **MAC frame handling** (`wifi_mac.c`): from Ralink GPL reference
6. **STA state machine** (`wifi_sta.c`): from Ralink GPL + clean-room
7. **Data path** (`wifi_data.c`): TX/RX with encryption
8. **WPA2** (`wifi_wpa.c`): port from wpa_supplicant BSD source
9. **lwIP** (`wifi_lwip.c`): standard port
10. **API** (`wifi_api.c`): thin wrapper
11. **Cross-test harness**: built incrementally alongside each layer
12. **Kconfig + integration**: wire into unity build

## Risk Mitigation

| Risk | Mitigation |
|------|-----------|
| RF calibration doesn't converge | IR is readable; calibration is algorithmic; emulator validates first |
| BBP magic numbers are chip-revision-specific | Extract as const tables from IR; test on hardware |
| Flash too small | STA-only code ~50K; FM-1 has 1 MB flash with headroom |
| Power management breaks audio DMA | Start with WiFi-only mode; add PM incrementally |
| Emulator doesn't model WiFi MAC | Emulator WiFi support is being added |
