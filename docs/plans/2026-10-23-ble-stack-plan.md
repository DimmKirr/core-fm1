# BLE Stack Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Vendor the Optimist BLE stack, wire it into core-fm1 via adapter + Kconfig, copy gamepad HID extension, test with C host tests (P0) and Bumble virtual peer (P1).

**Architecture:** Git subtree `firmware/upstream/optimist/` holds the unmodified BLE stack. Adapter `cfm1_ble.h` stubs when `CONFIG_BLE=off`. HID extension lives in `firmware/src/ble/` as core-fm1 code. C host tests compile BLE sources directly on x86-64 with `#define BLE_API static`.

**Tech Stack:** C99, Kconfiglib, pytest, cc (host compiler), Bumble (Python BLE stack for P1)

**Design doc:** `docs/plans/2026-10-23-ble-stack-design.md`

---

### Task 1: Vendor Optimist via Git Subtree

**Files:**
- Create: `firmware/upstream/optimist/` (subtree)

**Step 1: Add the subtree**

```bash
git subtree add --prefix firmware/upstream/optimist https://github.com/w0ts/optimist v0.1 --squash
```

**Step 2: Verify the BLE source landed**

```bash
ls firmware/upstream/optimist/firmware/src/ble/ble_stack.c
ls firmware/upstream/optimist/firmware/src/ble/ble.h
ls firmware/upstream/optimist/firmware/src/ble/ble_hw_stub.c
ls firmware/upstream/optimist/tests/ble_midi_test.c
ls firmware/upstream/optimist/tests/ble_scan_test.c
```

Expected: all files exist.

**Step 3: Commit**

```bash
git add firmware/upstream/optimist
git commit -m "vendor: add Optimist BLE stack via subtree (w0ts/optimist v0.1)"
```

---

### Task 2: Port BLE MIDI Codec Test (P0)

**Files:**
- Create: `tests/test_ble.py`
- Reference: `firmware/upstream/optimist/tests/ble_midi_test.c`
- Reference: `firmware/upstream/optimist/firmware/src/ble/ble_midi.c`

**Step 1: Write the Python test runner**

`tests/test_ble.py` compiles each C test with the host `cc`, runs it, checks
exit code. This integrates with `task test:unit` (pytest discovers it).

```python
"""Host-compiled BLE C tests (no hardware, no embedded toolchain)."""
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
OPTIMIST_BLE = ROOT / 'firmware' / 'upstream' / 'optimist' / 'firmware' / 'src' / 'ble'
OPTIMIST_TESTS = ROOT / 'firmware' / 'upstream' / 'optimist' / 'tests'
CFM1_BLE = ROOT / 'firmware' / 'src' / 'ble'


def compile_and_run(source: Path, extra_includes: list[Path] | None = None,
                    defines: list[str] | None = None) -> str:
    """Compile a C test on the host and run it. Returns stdout."""
    includes = [OPTIMIST_BLE]
    if extra_includes:
        includes.extend(extra_includes)
    defs = defines or []
    with tempfile.NamedTemporaryFile(suffix='', delete=False) as tmp:
        exe = tmp.name
    cmd = ['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
           '-O2', '-o', exe, str(source)]
    for inc in includes:
        cmd.extend(['-I', str(inc)])
    for d in defs:
        cmd.extend(['-D', d])
    subprocess.check_call(cmd)
    result = subprocess.run([exe], capture_output=True, text=True, timeout=30)
    Path(exe).unlink(missing_ok=True)
    if result.returncode != 0:
        pytest.fail(f'{source.name} failed:\n{result.stdout}\n{result.stderr}')
    return result.stdout


def test_ble_midi_codec():
    """BLE-MIDI encoder/decoder round-trip (20,000 random events)."""
    out = compile_and_run(OPTIMIST_TESTS / 'ble_midi_test.c')
    assert 'FAIL' not in out
    assert 'all passed' in out
```

**Step 2: Run it to make sure it passes**

```bash
uv run python -m pytest tests/test_ble.py::test_ble_midi_codec -v
```

Expected: PASS. The test compiles `ble_midi_test.c` which `#include`s
`ble_midi.c` directly, runs codec round-trip, exits 0.

**Step 3: Commit**

```bash
git add tests/test_ble.py
git commit -m "test(ble): add host-compiled BLE MIDI codec test runner"
```

---

### Task 3: Port BLE Scanner Test (P0)

**Files:**
- Modify: `tests/test_ble.py`
- Reference: `firmware/upstream/optimist/tests/ble_scan_test.c`

**Step 1: Add scanner test to the runner**

Append to `tests/test_ble.py`:

```python
def test_ble_scan_parser():
    """AD parser, device table, ageing, signal bars."""
    out = compile_and_run(OPTIMIST_TESTS / 'ble_scan_test.c')
    assert 'FAIL' not in out
    assert 'all passed' in out
```

**Step 2: Run it**

```bash
uv run python -m pytest tests/test_ble.py::test_ble_scan_parser -v
```

Expected: PASS. `ble_scan_test.c` includes `ble_scan.c` with `#define BLE_API static`.

Note: the upstream `ble_scan_test.c` includes `../firmware/src/ble/ble_scan.c`.
The `-I` flag pointing at `OPTIMIST_BLE` ensures `ble_scan.h`, `ble_host.h`,
`ble_util.h` resolve. If the test's relative include path doesn't work,
we may need to adjust with a symlink or a `-include` flag. Check and fix in this step.

**Step 3: Run full test suite**

```bash
uv run python -m pytest tests/test_ble.py -v
```

Expected: both tests pass.

**Step 4: Commit**

```bash
git add tests/test_ble.py
git commit -m "test(ble): add host-compiled BLE scanner test"
```

---

### Task 4: Create Kconfig and Adapter

**Files:**
- Create: `kconf/ble.kconfig`
- Modify: `Kconfig:26` (add `source "kconf/ble.kconfig"` inside System menu)
- Create: `firmware/adapters/cfm1_ble.h`

**Step 1: Write `kconf/ble.kconfig`**

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

**Step 2: Source it from root Kconfig**

Add `source "kconf/ble.kconfig"` in `Kconfig` inside the System menu, after
`source "kconf/udisk.kconfig"` (line 24) and before `source "kconf/debug.kconfig"`:

```
source "kconf/ble.kconfig"
```

**Step 3: Write the adapter**

`firmware/adapters/cfm1_ble.h`:

```c
/* SPDX-License-Identifier: MIT */
/* BLE adapter: stubs when the BLE subsystem is off. */
#ifndef CFM1_BLE_H
#define CFM1_BLE_H
#include <stdint.h>

#ifndef CFM1_BLE_MIDI
static int ble_midi_ready(void) { return 0; }
static int ble_connected(void) { return 0; }
static void ble_midi_out(uint32_t msg) { (void)msg; }
static void ble_midi_poll(void) {}
static void ble_bond_poll(void) {}
#endif

#endif /* CFM1_BLE_H */
```

**Step 4: Verify Kconfig parses**

```bash
uv run python tools/genconfig.py --check 2>&1 | head -5
```

Expected: no errors about `ble.kconfig`.

**Step 5: Run existing unit tests (no regressions)**

```bash
uv run python -m pytest tests/test_genconfig.py -v
```

Expected: all pass.

**Step 6: Commit**

```bash
git add kconf/ble.kconfig Kconfig firmware/adapters/cfm1_ble.h
git commit -m "feat(ble): add Kconfig menu and stub adapter"
```

---

### Task 5: Copy HID Extension from felucca-gamepad

**Files:**
- Create: `firmware/src/ble/ble_hid.c` (copy from felucca-gamepad)
- Create: `firmware/src/ble/ble_hid.h` (copy from felucca-gamepad)

**Step 1: Create the directory**

```bash
mkdir -p firmware/src/ble
```

**Step 2: Copy the HID files**

```bash
cp /Users/dmitry/dev/dimmkirr/felucca-gamepad/.tools/examples/optimist/firmware/src/ble/ble_hid.c firmware/src/ble/ble_hid.c
cp /Users/dmitry/dev/dimmkirr/felucca-gamepad/.tools/examples/optimist/firmware/src/ble/ble_hid.h firmware/src/ble/ble_hid.h
```

**Step 3: Copy the extended scanner**

The gamepad's `ble_scan.c` and `ble_scan.h` have HID-specific additions (16-bit
UUID parsing for 0x1812, `BLE_KIND_HID`, Appearance). Copy the gamepad versions
as they are:

```bash
cp /Users/dmitry/dev/dimmkirr/felucca-gamepad/.tools/examples/optimist/firmware/src/ble/ble_scan.c firmware/src/ble/ble_scan.c
cp /Users/dmitry/dev/dimmkirr/felucca-gamepad/.tools/examples/optimist/firmware/src/ble/ble_scan.h firmware/src/ble/ble_scan.h
```

Note: In the unity build, `ble_stack.c` includes the upstream `ble_scan.c`. When
`CONFIG_BLE_HID` is on, the core-fm1 `ble_scan.c` (with HID extensions) will be
included instead. This is wired in Task 8 (unity build integration).

**Step 4: Verify files are present**

```bash
ls -la firmware/src/ble/
```

Expected: `ble_hid.c`, `ble_hid.h`, `ble_scan.c`, `ble_scan.h`.

**Step 5: Commit**

```bash
git add firmware/src/ble/
git commit -m "feat(ble): copy HID extension and extended scanner from felucca-gamepad"
```

---

### Task 6: Write HID Report Decoding Test (TDD, P0)

**Files:**
- Create: `tests/ble_hid_test.c`
- Modify: `tests/test_ble.py` (add runner)

**Step 1: Write the failing C test**

`tests/ble_hid_test.c` tests the Xbox report decoding logic from `ble_hid.c`.
The gamepad's `hid_report_in()` function converts a 16-byte Xbox report into
MIDI CC values via `ble_app_midi_in()`. We capture those calls.

```c
/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the Xbox HID report decoder (firmware/src/ble/ble_hid.c):
 *   - a centered gamepad report produces the expected CC values
 *   - stick extremes map to 0 and 127
 *   - trigger extremes map to 0 and 127
 *   - hat switch values map to d-pad CCs
 *   - buttons map to on/off CCs
 *   - shadow filtering: unchanged values are not re-sent */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* --- fakes for the BLE stack dependencies ble_hid.c needs --- */
#define BLE_API static
#define BLE_CENTRAL 1
#define BLE_LL_ENC 1
#define BLE_SMP_LEGACY 1
#define BLE_DG(x)

static uint16_t ble_rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static void ble_wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static int ble_eq(const uint8_t *a, const uint8_t *b, uint32_t n) {
    uint32_t i; for (i = 0; i < n; i++) if (a[i] != b[i]) return 0; return 1;
}
static void ble_zero(uint8_t *p, uint32_t n) { memset(p, 0, n); }

/* stub: the link layer send (ble_hid.c calls it) */
static int ble_ll_send(uint16_t cid, const uint8_t *p, uint16_t n) {
    (void)cid; (void)p; (void)n; return 1;
}
static uint32_t ble_hw_time_us(void) { return 0; }
#define L2CAP_CID_ATT 4

/* capture MIDI events injected by hid_report_in */
#define MAX_MIDI 256
static uint32_t midi_pkts[MAX_MIDI];
static unsigned midi_n;
static void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last_ts) {
    (void)ts; (void)last_ts;
    if (midi_n < MAX_MIDI) midi_pkts[midi_n++] = pkt;
}

/* stubs for central callbacks ble_hid.c references */
static void ble_central_hid_ready(void) {}
static void ble_central_hid_fail(uint8_t why, uint8_t code) { (void)why; (void)code; }
static int ble_central_hid_auth(uint8_t code) { (void)code; return 0; }

/* include the source under test */
#include "../firmware/src/ble/ble_hid.c"

static int fails;
static void check(const char *what, int ok) {
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

/* find a CC packet: CIN 0x0B, status 0xB0, cc_num, cc_val */
static int find_cc(uint8_t cc_num, uint8_t *val_out) {
    unsigned i;
    for (i = 0; i < midi_n; i++) {
        uint32_t p = midi_pkts[i];
        if ((p & 0xFF) == 0x0B && ((p >> 8) & 0xF0) == 0xB0 &&
            ((p >> 16) & 0x7F) == cc_num) {
            *val_out = (uint8_t)(p >> 24);
            return 1;
        }
    }
    return 0;
}

static void make_report(uint8_t *rpt, uint16_t lx, uint16_t ly,
                        uint16_t rx, uint16_t rz, uint16_t lt, uint16_t rt,
                        uint8_t hat, uint16_t buttons) {
    memset(rpt, 0, 16);
    rpt[0] = (uint8_t)lx; rpt[1] = (uint8_t)(lx >> 8);
    rpt[2] = (uint8_t)ly; rpt[3] = (uint8_t)(ly >> 8);
    rpt[4] = (uint8_t)rx; rpt[5] = (uint8_t)(rx >> 8);
    rpt[6] = (uint8_t)rz; rpt[7] = (uint8_t)(rz >> 8);
    rpt[8] = (uint8_t)lt; rpt[9] = (uint8_t)(lt >> 8);
    rpt[10] = (uint8_t)rt; rpt[11] = (uint8_t)(rt >> 8);
    rpt[12] = hat;
    rpt[13] = (uint8_t)buttons; rpt[14] = (uint8_t)(buttons >> 8);
}

/* set up bhid so hid_report_in accepts report ID 1 on a known handle */
static void setup_hid(void) {
    memset(&bhid, 0, sizeof bhid);
    bhid.st = H_READY;
    bhid.rpt_n = 1;
    bhid.rpt[0].val_h = 0x0020;
    bhid.rpt[0].report_id = 1;
    bhid.rpt[0].report_type = 1; /* Input */
    bhid.rpt[0].subscribed = 1;
    bhid.shadow_valid = 0;
}

static void test_centered(void) {
    uint8_t rpt[16], val;
    setup_hid();
    midi_n = 0;
    make_report(rpt, 0x8000, 0x8000, 0x8000, 0x8000, 0, 0, 8, 0);
    hid_report_in(0x0020, rpt, 16);
    check("centered: LX CC85 = 64", find_cc(85, &val) && val == 64);
    check("centered: LY CC86 = 63", find_cc(86, &val) && (val == 63 || val == 64));
    check("centered: LT CC89 = 0", find_cc(89, &val) && val == 0);
    check("centered: RT CC90 = 0", find_cc(90, &val) && val == 0);
    check("centered: hat 8 (neutral) -> DU CC113 = 0", find_cc(113, &val) && val == 0);
    check("centered: no buttons -> A CC102 = 0", find_cc(102, &val) && val == 0);
}

static void test_extremes(void) {
    uint8_t rpt[16], val;
    setup_hid();
    midi_n = 0;
    make_report(rpt, 0xFFFF, 0x0000, 0xFFFF, 0x0000, 0x03FF, 0x03FF, 0, 0xFFFF);
    hid_report_in(0x0020, rpt, 16);
    check("max LX -> CC85 = 127", find_cc(85, &val) && val == 127);
    check("min LY -> CC86 = 127", find_cc(86, &val) && val == 127);
    check("max LT -> CC89 = 127", find_cc(89, &val) && val == 127);
    check("max RT -> CC90 = 127", find_cc(90, &val) && val == 127);
    check("hat 0 (N) -> DU CC113 = 127", find_cc(113, &val) && val == 127);
    check("all buttons -> A CC102 = 127", find_cc(102, &val) && val == 127);
}

static void test_shadow(void) {
    uint8_t rpt[16], val;
    unsigned n1;
    setup_hid();
    midi_n = 0;
    make_report(rpt, 0x8000, 0x8000, 0x8000, 0x8000, 0, 0, 8, 0);
    hid_report_in(0x0020, rpt, 16);
    n1 = midi_n;
    midi_n = 0;
    /* same report again: shadow filtering means no new events */
    hid_report_in(0x0020, rpt, 16);
    check("shadow: identical report produces no new MIDI events", midi_n == 0);
    /* change one axis */
    midi_n = 0;
    make_report(rpt, 0x9000, 0x8000, 0x8000, 0x8000, 0, 0, 8, 0);
    hid_report_in(0x0020, rpt, 16);
    check("shadow: changed LX produces at least 1 event", midi_n >= 1);
    (void)n1;
}

static void test_wrong_report_id(void) {
    uint8_t rpt[16];
    setup_hid();
    bhid.rpt[0].report_id = 2; /* not ID 1 */
    midi_n = 0;
    make_report(rpt, 0x8000, 0x8000, 0x8000, 0x8000, 0, 0, 8, 0);
    hid_report_in(0x0020, rpt, 16);
    check("report ID 2 (not Xbox gamepad): ignored", midi_n == 0);
}

int main(void) {
    test_centered();
    test_extremes();
    test_shadow();
    test_wrong_report_id();
    printf("%s\n", fails ? "ble_hid: FAILED" : "ble_hid: all passed");
    return fails != 0;
}
```

**Step 2: Add the Python runner entry**

Append to `tests/test_ble.py`:

```python
def test_ble_hid_report():
    """Xbox HID report to MIDI CC conversion."""
    out = compile_and_run(
        ROOT / 'tests' / 'ble_hid_test.c',
        extra_includes=[OPTIMIST_BLE, CFM1_BLE],
    )
    assert 'FAIL' not in out
    assert 'all passed' in out
```

**Step 3: Run it**

```bash
uv run python -m pytest tests/test_ble.py::test_ble_hid_report -v
```

Expected: PASS if the stubs are correct. If it fails to compile, fix the
fakes iteratively (the `ble_hid.c` includes reference `ble.h`, `ble_host.h`,
`ble_ll.h`, `ble_util.h`, `ble_diag.h` from the upstream BLE dir).

**Step 4: Commit**

```bash
git add tests/ble_hid_test.c tests/test_ble.py
git commit -m "test(ble): add Xbox HID report decoder test (TDD P0)"
```

---

### Task 7: Write GATT Handle Dispatch Test (TDD, P0)

**Files:**
- Create: `tests/ble_att_test.c`
- Modify: `tests/test_ble.py` (add runner)

**Step 1: Write the test**

`tests/ble_att_test.c` tests the ATT server's static GATT database. It
verifies that the handle table is well-formed (no gaps, correct service
grouping) and that specific handle reads return expected values.

```c
/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the ATT server's GATT database (firmware/upstream/optimist/.../ble_att.c):
 *   - the handle table covers 1..H_LAST with no gaps
 *   - GAP service at handle 1, GATT at 8, BLE-MIDI at 12
 *   - Device Name handle (3) is readable
 *   - MIDI I/O CCCD (15) is writable
 *   - Appearance reads BLE_APPEARANCE */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BLE_API static
#define BLE_CENTRAL 0
#define BLE_LL_ENC 0
#define BLE_SMP_LEGACY 0
#define BLE_SC_ON_SUBSCRIBE 0
#define BLE_MIDI_NEED_ENC 0
#define BLE_DG(x)

/* minimal fakes for ble_att.c dependencies */
static uint16_t ble_rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static void ble_wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static int ble_eq(const uint8_t *a, const uint8_t *b, uint32_t n) {
    uint32_t i; for (i = 0; i < n; i++) if (a[i] != b[i]) return 0; return 1;
}
static void ble_zero(uint8_t *p, uint32_t n) { memset(p, 0, n); }
static void ble_copy(uint8_t *d, const uint8_t *s, uint32_t n) { memcpy(d, s, n); }
static int ble_ll_send(uint16_t cid, const uint8_t *p, uint16_t n) {
    (void)cid; (void)p; (void)n; return 1;
}
static uint32_t ble_hw_time_us(void) { return 0; }
static int ble_ll_encrypted(void) { return 0; }
#define L2CAP_CID_ATT 4
static const uint8_t BLE_UUID_MIDI_SVC[16] = {
    0x5A, 0x0E, 0xB8, 0x03, 0xED, 0xE8, 0x4B, 0x33,
    0xA7, 0x51, 0x6C, 0xE3, 0x4E, 0xC4, 0xC7, 0x00
};
/* stub: ble_midi functions called from ble_att.c */
static int ble_midi_enc_len_val;
static uint16_t att_midi_mtu;
struct ble_midi_enc { int dummy; };
static uint16_t ble_midi_enc_len(struct ble_midi_enc *e) { (void)e; return 0; }
static int ble_app_midi_peek(uint32_t *p, uint32_t *t) { (void)p; (void)t; return 0; }
static void ble_app_state(void) {}
static char ble_gap_name[25] = "Optimist TEST";
static uint16_t att_mtu = 23;

#include "../firmware/upstream/optimist/firmware/src/ble/ble_att.c"

static int fails;
static void check(const char *what, int ok) {
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

int main(void) {
    /* check the database is well-formed */
    check("handle 0 unused (flags=0)", ATT_DB[0].flags == 0);
    check("H_GAP (1) is a service",    ATT_DB[H_GAP].flags & A_SVC);
    check("H_GATT (8) is a service",   ATT_DB[H_GATT].flags & A_SVC);
    check("H_MIDI (12) is a service",  ATT_DB[H_MIDI].flags & A_SVC);
    check("H_MIDI is 128-bit",         ATT_DB[H_MIDI].flags & A_128);
    check("H_NAME (3) is readable",    ATT_DB[H_NAME].flags & A_R);
    check("H_APP (5) is readable",     ATT_DB[H_APP].flags & A_R);
    check("H_MIDI_CCC (15) writable",  ATT_DB[H_MIDI_CCC].flags & A_W);
    check("H_LAST is 15",              H_LAST == 15);
    /* verify Appearance value matches BLE_APPEARANCE */
    check("Appearance bytes match",    ATT_DB[H_APP].val[0] == (BLE_APPEARANCE & 0xFF) &&
                                       ATT_DB[H_APP].val[1] == (BLE_APPEARANCE >> 8));
    printf("%s\n", fails ? "ble_att: FAILED" : "ble_att: all passed");
    return fails != 0;
}
```

**Step 2: Add runner entry**

```python
def test_ble_att_gatt_db():
    """GATT handle table structure and service layout."""
    out = compile_and_run(ROOT / 'tests' / 'ble_att_test.c')
    assert 'FAIL' not in out
    assert 'all passed' in out
```

**Step 3: Run it**

```bash
uv run python -m pytest tests/test_ble.py::test_ble_att_gatt_db -v
```

Expected: PASS (or compile errors from missing stubs to fix iteratively).

**Step 4: Commit**

```bash
git add tests/ble_att_test.c tests/test_ble.py
git commit -m "test(ble): add GATT database structure test (TDD P0)"
```

---

### Task 8: Wire Unity Build Integration

**Files:**
- Modify: `firmware/src/core_fm1.c:303-318` (adapters section)
- Modify: `firmware/src/core_fm1.c:399` (after USB MIDI, before OTG)
- Modify: `configs/core_defconfig` (add BLE + STUB)

**Step 1: Add CFM1_BLE_MIDI define before adapters**

In `firmware/src/core_fm1.c`, after the `CONFIG_USB` / `CFM1_USB_MIDI` block
(line 305-307) and before the adapter includes (line 309), add:

```c
#ifdef CONFIG_BLE
#define CFM1_BLE_MIDI 1
#endif
```

**Step 2: Add BLE adapter include**

In the adapters section (after `cfm1_controls.h`, line 317), add:

```c
#include "cfm1_ble.h"
```

**Step 3: Add BLE stack include**

After the OTG section (after line 405), add a new BLE section:

```c
/* --- BLE stack (Optimist lineage) --- */

#ifdef CONFIG_BLE
#define FELUCCA_BLE_STUB 1
#ifdef CONFIG_BLE_CENTRAL
#define BLE_CENTRAL 1
#else
#define BLE_CENTRAL 0
#endif
#ifdef CONFIG_BLE_BOND
#define BLE_LL_ENC 1
#define BLE_SMP_LEGACY 1
#else
#define BLE_LL_ENC 0
#define BLE_SMP_LEGACY 0
#endif
#include "../upstream/optimist/firmware/src/ble/ble_stack.c"
#ifdef CONFIG_BLE_HID
#include "ble/ble_hid.c"
#endif
/* app callbacks the BLE stack expects (stubs until a lineage wires them) */
static int ble_app_midi_peek(uint32_t *pkt, uint32_t *t_ms) { (void)pkt; (void)t_ms; return 0; }
static void ble_app_midi_pop(void) {}
static void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last_ts) { (void)pkt; (void)ts; (void)last_ts; }
static void ble_app_state(void) {}
#if BLE_SMP_LEGACY
static void ble_app_bond(const uint8_t rand[8], uint16_t ediv, const uint8_t ltk[16]) { (void)rand; (void)ediv; (void)ltk; }
static void ble_app_peer_id(const uint8_t irk[16], const uint8_t addr[6], uint8_t addr_rand) { (void)irk; (void)addr; (void)addr_rand; }
#endif
#if BLE_CENTRAL
static void ble_app_central_keys(const struct ble_keys *k) { (void)k; }
#endif
#endif
```

**Step 4: Add BLE to core_defconfig**

Append to `configs/core_defconfig`:

```
CONFIG_BLE=y
CONFIG_BLE_STUB=y
```

**Step 5: Generate config and try a dry-run build**

```bash
uv run python tools/genconfig.py
uv run python tools/build.py --dry-run
```

Expected: no errors. The `CONFIG_BLE` and `CONFIG_BLE_STUB` symbols resolve,
the include paths for the upstream BLE source are found.

**Step 6: Try a real build (if toolchain available)**

```bash
task build -- --defconfig core
```

Expected: compiles. If include paths fail, add `-I firmware/upstream/optimist/firmware/src/ble`
to the build flags in `tools/build.py`.

**Step 7: Commit**

```bash
git add firmware/src/core_fm1.c configs/core_defconfig
git commit -m "feat(ble): wire Optimist BLE stack into unity build"
```

---

### Task 9: Add Bumble to Dependencies (P1 prep)

**Files:**
- Modify: `pyproject.toml` (add bumble to dev deps)
- Modify: `uv.lock` (re-lock)

**Step 1: Add bumble dependency**

In `pyproject.toml`, add to the `[dependency-groups]` dev section:

```toml
dev = [
    "pytest>=8.0",
    "playwright>=1.50",
    "bumble>=0.0.222",
]
```

**Step 2: Re-lock**

```bash
uv lock
uv sync
```

**Step 3: Verify bumble imports**

```bash
uv run python -c "import bumble; print(bumble.__version__)"
```

Expected: prints the version.

**Step 4: Commit**

```bash
git add pyproject.toml uv.lock
git commit -m "deps: add Bumble BLE stack for virtual peer testing"
```

---

### Task 10: Write Bumble Scan + Connect Test (P1)

**Files:**
- Create: `tests/test_ble_bumble.py`

This is the most exploratory task. The core challenge: our BLE stack is
sub-HCI (it implements its own link layer), so we can't plug it into Bumble's
HCI transport directly. The practical approach for P1:

**Option A (simpler):** Test at the ATT/L2CAP level. Build canned BLE packets
in Python (using Bumble's packet builders), feed them through our enhanced
stub driver via a subprocess pipe, verify responses.

**Option B (full):** Write a thin C wrapper that bridges `ble_hw.h` to a
Unix socket speaking raw BLE PDUs. Bumble's `LocalLink` connects the other
side. Full connection establishment, GATT discovery, MIDI subscription.

Start with Option A. Write a test that:
1. Spawns a C process with the BLE stack + stub driver + a socket transport
2. Bumble sends advertising PDUs, connection requests
3. Verifies GATT service discovery responses

**Step 1: Write a minimal test skeleton**

```python
"""Bumble virtual BLE peer integration tests (P1).

These tests require the 'bumble' package and verify the BLE stack's
protocol behavior against a real BLE stack implementation.
"""
import pytest

bumble = pytest.importorskip('bumble')


@pytest.mark.skip(reason='P1: stub transport not yet implemented')
def test_bumble_scan_sees_midi_service():
    """A Bumble scanner discovers our BLE-MIDI service UUID in advertising."""
    pass


@pytest.mark.skip(reason='P1: stub transport not yet implemented')
def test_bumble_gatt_discover():
    """A Bumble central connects and discovers GAP, GATT, BLE-MIDI services."""
    pass


@pytest.mark.skip(reason='P1: stub transport not yet implemented')
def test_bumble_midi_subscribe():
    """A Bumble central subscribes to BLE-MIDI notifications."""
    pass
```

**Step 2: Run it (all skip)**

```bash
uv run python -m pytest tests/test_ble_bumble.py -v
```

Expected: 3 skipped.

**Step 3: Commit**

```bash
git add tests/test_ble_bumble.py
git commit -m "test(ble): add Bumble integration test skeleton (P1)"
```

---

### Task 11: Run Full Test Suite + Verify

**Files:** none (verification only)

**Step 1: Run all unit tests**

```bash
uv run python -m pytest tests/ -v --ignore-glob="tests/e2e_*"
```

Expected: all BLE tests pass (midi codec, scanner, HID, ATT), bumble tests
skip, existing genconfig/build/controls tests still pass.

**Step 2: Run defconfig validation**

```bash
uv run python -m pytest tests/test_genconfig.py -v
```

Expected: all pass (including any that validate `core_defconfig`).

**Step 3: Commit any fixups**

If any tests needed adjustments, commit them now.

```bash
git add -u
git commit -m "fix(ble): test suite fixups after integration"
```

---

## Summary

| Task | What | Test type |
|------|------|-----------|
| 1 | Vendor Optimist subtree | - |
| 2 | BLE MIDI codec test | P0 (port) |
| 3 | BLE scanner test | P0 (port) |
| 4 | Kconfig + adapter | infra |
| 5 | Copy HID extension | - |
| 6 | HID report decoding test | P0 (TDD) |
| 7 | GATT database test | P0 (TDD) |
| 8 | Unity build integration | build |
| 9 | Add Bumble dependency | P1 prep |
| 10 | Bumble test skeleton | P1 (skeleton) |
| 11 | Full suite verification | - |

Tasks 1-8 deliver a working BLE stack with P0 test coverage.
Tasks 9-10 scaffold P1 (Bumble) for future implementation.
Task 11 verifies nothing is broken.
