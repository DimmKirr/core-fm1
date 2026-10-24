# BLE Stack Design

Vendor the Optimist BLE stack into core-fm1 via git subtree, wire it through
adapters and Kconfig, copy the felucca-gamepad HID extension as core-fm1 code.
TDD with three test tiers: C host tests, Google Bumble virtual peer, optional
VHCI + bleak interop.

## Source Layout

```
firmware/upstream/optimist/              <- git subtree from w0ts/optimist v0.1
  firmware/src/ble/                      <- the full BLE stack (~8,200 lines)
    ble_cfg.h                            <- compile-time configuration
    ble.h                                <- public API
    ble_hw.h                             <- HW driver interface
    ble_hw_stub.c                        <- stub driver (testing/emulator)
    ble_hw_wl82.c                        <- AC791N radio driver
    ble_hw_wl82_central.c                <- central-role radio driver
    ble_ll.c / ble_ll.h                  <- link layer (peripheral)
    ble_ll_central.c                     <- link layer (central)
    ble_host.c / ble_host.h              <- L2CAP, GAP, SMP dispatch
    ble_att.c                            <- ATT/GATT server, BLE-MIDI service
    ble_gattc.c                          <- ATT/GATT client
    ble_central.c                        <- central role connection management
    ble_scan.c / ble_scan.h              <- AD parser, device table (base)
    ble_midi.c / ble_midi.h              <- BLE-MIDI codec
    ble_smp.c / ble_smp_init.c           <- SMP responder + initiator
    ble_store.c / ble_store.h            <- bond/device persistence
    ble_aes.c / ble_aes.h               <- AES-128 + LL AES-CCM
    ble_prim.c / ble_prim.h              <- CRC24, whitening, channel selection
    ble_vm.c / ble_vm.h                  <- RF calibration VM
    ble_diag.c / ble_diag.h              <- debug diagnostics
    ble_stack.c                          <- unity-build wrapper
    ble_util.h                           <- byte helpers
  tests/
    ble_midi_test.c                      <- codec round-trip (20k events)
    ble_scan_test.c                      <- AD parser + table tests

firmware/src/ble/                        <- core-fm1 BLE code (not vendored)
  ble_scan_ext.c                         <- gamepad scan extensions (HID UUID, Appearance)
  ble_hid.c                              <- Xbox HID GATT client (from felucca-gamepad)
  ble_hid.h                              <- HID client header

firmware/adapters/cfm1_ble.h             <- adapter: stubs when CONFIG_BLE=off
kconf/ble.kconfig                        <- BLE Kconfig menu
```

## Kconfig

```kconfig
menu "BLE"

config BLE
    bool "Bluetooth Low Energy"
    default n
    help
      The Optimist BLE stack: peripheral and optional central roles,
      BLE-MIDI transport, device scanning, bonding.

config BLE_MIDI
    bool "BLE MIDI transport"
    depends on BLE
    default y

config BLE_CENTRAL
    bool "Central role (scanner, connecting out)"
    depends on BLE
    default n
    select BLE_BOND

config BLE_HID
    bool "HID over GATT client (Xbox controller)"
    depends on BLE_CENTRAL
    default n

config BLE_BOND
    bool "Bonding (SMP + LL encryption)"
    depends on BLE
    default n

config BLE_DIAG
    bool "BLE diagnostics"
    depends on BLE
    default n

config BLE_STUB
    bool "Stub HW driver (testing/emulator)"
    depends on BLE
    default n
    help
      Use the no-op baseband driver instead of the AC791N radio.
      For emulator builds and host testing.

endmenu
```

## Adapter: cfm1_ble.h

```c
#ifndef CFM1_BLE_H
#define CFM1_BLE_H
#include <stdint.h>

static int ble_midi_ready(void) { return 0; }
static int ble_connected(void) { return 0; }
static void ble_midi_out(uint32_t msg) { (void)msg; }
static void ble_midi_poll(void) {}
static void ble_bond_poll(void) {}

#endif
```

When `CONFIG_BLE` is on, the real stack overrides these via `#define CFM1_BLE_MIDI 1`
guard (same pattern as `cfm1_midi.h`).

## Include Order in core_fm1.c

```c
/* before adapters */
#ifdef CONFIG_BLE
#define CFM1_BLE_MIDI 1
#endif

/* adapters section */
#include "cfm1_ble.h"

/* BLE stack section (after USB + audio core, before UI) */
#ifdef CONFIG_BLE
#include "../upstream/optimist/firmware/src/ble/ble_stack.c"
#ifdef CONFIG_BLE_HID
#include "ble/ble_scan_ext.c"
#include "ble/ble_hid.c"
#endif
#endif
```

## Gamepad HID Extension

Files copied from felucca-gamepad (not vendored) into `firmware/src/ble/`:

- `ble_hid.c` (~660 lines): GATT client for Xbox HID service discovery,
  Report Map read, Input Report subscription, gamepad-to-MIDI-CC conversion.
- `ble_hid.h`: public interface (`ble_hid_reset`, `ble_hid_start`, `ble_hid_rx`, etc.)
- `ble_scan_ext.c`: scan extensions. Patches the scanner to recognize HID
  service UUID 0x1812 in 16-bit lists, adds `BLE_KIND_HID`, parses Appearance.
  Included after the upstream `ble_scan.c` (via `ble_stack.c`), overriding the
  base `ble_ad_parse` and `ble_scan_add` with extended versions.

## Testing Strategy

### Tier 0: Enhanced Stub Driver + C Host Tests (P0)

**Zero external dependencies. Compile with `cc`, run as native Linux executable.**

The Optimist already has two C host tests that `#include` the source directly with
`#define BLE_API static`. This pattern compiles the BLE logic on x86-64 without
any OS, radio, or embedded toolchain.

Existing tests to port:
- `ble_midi_test.c`: codec round-trip (20,000 random events through random packet sizes)
- `ble_scan_test.c`: AD parser, device table, ageing, bars, UTF-8 names

New tests to write:
- `ble_hid_test.c`: Xbox report decoding (16-byte report to MIDI CC), shadow
  change-only filtering, Report Map parsing, multi-report discovery
- `ble_att_test.c`: GATT handle dispatch (write to each handle, verify callback),
  CCCD enable/disable, MTU exchange, error responses
- `ble_smp_test.c`: Just Works pairing state machine (canned PDU sequences),
  bond key extraction, encryption start
- `ble_ll_test.c`: link layer control procedures (connection update, channel map,
  data length extension, feature exchange) via canned events

Enhanced stub driver: make `ble_hw_stub.c` call back into link layer with
pre-built PDUs (CONNECT_IND, data, control). This creates a deterministic
test harness for the full stack.

Python test runner: `tests/test_ble.py` compiles and runs each C test, captures
exit code and output, integrates with `task test:unit`.

### Tier 1: Google Bumble Virtual BLE Peer (P1)

**Pure Python, no hardware, no kernel modules, runs in CI.**

[Bumble](https://google.github.io/bumble/) is a full BLE stack (link layer through
GATT) in Python. It can simulate a virtual BLE peer using `LocalLink` (shared-memory
virtual RF between two virtual controllers).

Architecture:
```
[Our C stack]  <--pipe/socket-->  [Virtual HCI transport]
                                         |
                                    [LocalLink]
                                         |
                                  [Bumble BLE stack]
                                  (GATT client, scanner, etc.)
```

Our enhanced stub driver exposes a transport (Unix socket or pipe) speaking
HCI-like frames. Bumble connects as the other side. Two virtual controllers
on a `LocalLink` simulate the RF layer.

Test scenarios:
- Bumble scans, sees our advertising with BLE-MIDI service UUID
- Bumble connects as central, discovers GATT services
- Bumble subscribes to BLE-MIDI notifications, sends/receives MIDI
- Bumble initiates pairing (Just Works), verifies encryption
- Bumble simulates Xbox controller (HID peripheral), our central connects

Install: `uv add bumble` in pyproject.toml. Tests in `tests/test_ble_bumble.py`.

### Tier 2: BlueZ VHCI + bleak (P2, optional)

**Tests that real Linux BLE clients see correct services. Needs root/NET_ADMIN.**

The Linux `hci_vhci` kernel module creates a virtual HCI controller. Bumble
can drive the virtual controller. [bleak](https://github.com/hbldh/bleak)
(Python BLE client) connects through BlueZ to the virtual device.

```
[Our stack via Bumble VHCI transport]
         |
    [hci_vhci kernel module]
         |
    [BlueZ bluetoothd]
         |
    [bleak Python client]
```

This validates that a real BlueZ client sees correct advertising data, GATT
table, and can subscribe to characteristics. bleak already supports
`--bleak-bluez-vhci` for CI integration tests.

Docker: `--device=/dev/vhci --cap-add=NET_ADMIN`.

### What's Not Worth It

- **BabbleSim**: Zephyr tooling, our stack isn't HCI-based, heavy integration
- **QEMU BLE**: no native BLE controller emulation
- **nRF Sniffer**: needs physical hardware
- **btvirt**: BlueZ-specific, not useful for custom stacks

## Implementation Phases

### Phase 1: Vendor + Scaffold + P0 Tests

1. `git subtree add --prefix firmware/upstream/optimist https://github.com/w0ts/optimist v0.1`
2. Create `firmware/adapters/cfm1_ble.h` (stubs)
3. Create `kconf/ble.kconfig`, source from root Kconfig
4. Port `ble_midi_test.c` and `ble_scan_test.c` into `tests/`
5. Write Python runner `tests/test_ble.py` that compiles + runs C tests
6. Verify: `task test:unit` passes with BLE tests

### Phase 2: HID Extension + New C Tests

1. Copy `ble_hid.c`, `ble_hid.h` from felucca-gamepad
2. Create `ble_scan_ext.c` with gamepad scan diffs
3. Write `tests/ble_hid_test.c` (TDD: test Xbox report decoding first)
4. Write `tests/ble_att_test.c` (GATT handle dispatch)
5. Enhance `ble_hw_stub.c` to feed canned PDUs for LL tests
6. Write `tests/ble_ll_test.c` (link layer control procedures)

### Phase 3: Unity Build Integration

1. Wire `core_fm1.c` includes (BLE stack + HID extension)
2. Add CONFIG_BLE to defconfigs (core_defconfig with STUB, felucca with real)
3. Verify: `task build -- --defconfig core` compiles with BLE
4. E2E: emulator build with stub driver boots correctly

### Phase 4: Bumble Integration Tests (P1)

1. Add bumble to pyproject.toml
2. Enhance stub driver with socket/pipe transport
3. Write `tests/test_ble_bumble.py`: scan, connect, GATT discover, MIDI, pairing
4. Wire into `task test:unit` or a new `task test:ble` target

### Phase 5: VHCI Interop (P2, optional)

1. Docker setup with NET_ADMIN
2. Bumble VHCI transport + bleak test client
3. Validate GATT table visible to real BlueZ stack

## Risks

1. **Subtree include paths**: `ble_stack.c` includes siblings with relative paths.
   The unity build's `-I` flags must reach `firmware/upstream/optimist/firmware/src/ble/`.
2. **ble_scan_ext.c override**: the gamepad extends `ble_ad_parse` and `ble_scan_add`.
   In unity build, the last definition wins (both are `static`). Include order matters.
3. **Bumble transport**: our stack is not HCI-based (it's below HCI). The Bumble
   integration needs a custom transport that maps our ble_hw interface to HCI frames,
   or we test at the GATT/ATT level with canned L2CAP frames.
4. **RF calibration**: `ble_vm.c` reads AC791N flash for RF tables. Stub/emulator
   needs a fallback (default tables or skip).
