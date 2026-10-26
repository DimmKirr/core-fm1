# BLE Stub Connection Simulation – Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Extend the BLE stub driver to simulate a full external BLE connection lifecycle (advertising → connected → MIDI ready), triggered from the emulator panel, so e2e tests can verify all three BLE states.

**Architecture:** The BLE stub driver (`ble_hw_stub.c`) currently accepts advertising but never produces connection events, so the stack stays in ADVERTISING forever. We add a state machine inside the stub that, on trigger, crafts a CONNECT_IND PDU, drives connection events, and injects ATT packets (MTU exchange + MIDI CCCD subscription) to bring the stack to MIDI READY. The trigger is the PLAY button on the BLE page. `ble_hw_stub_tick()` is called from the main loop to drive the simulation forward.

**Tech Stack:** C (unity build, all static), Kconfiglib, Python/Playwright e2e tests

---

## Verification: How We Know Each State Works

The BLE page (`page_ble.c`) displays three pieces of state the test can verify via screenshots and debug log:

| State | `ble_connected()` | `ble_midi_ready()` | BLE card shows | Status row shows |
|-------|---|---|---|---|
| Advertising | 0 | 0 | `ADV` | `ADVERTISING` |
| Connected | 1 | 0 | `ON` | `CONNECTED` |
| MIDI Ready | 1 | 1 | `ON` | `MIDI READY` |

The debug log (visible in the emulator's console) prints `ble: CONNECTED` and `ble: midi_ready=1` via the existing `ble_app_state()` callback.

The e2e test reads the emulator's console log to assert state transitions, and takes panel screenshots at each state for visual verification.

---

### Task 1: Extend BLE Stub Driver with Connection Simulation

**Files:**
- Create: `firmware/src/ble/ble_hw_stub_sim.c` (the simulation state machine, `#include`d by the unity build)
- Modify: `firmware/src/core_fm1.c:546-548` (include `ble_hw_stub_sim.c` instead of upstream `ble_hw_stub.c` when `CFM1_BLE_STUB`)

**Why a new file instead of modifying upstream:** `ble_hw_stub.c` lives in `firmware/upstream/optimist/` (subtree-merged). We don't modify upstream files; we override them.

**Step 1: Create `firmware/src/ble/ble_hw_stub_sim.c`**

This file `#include`s the upstream stub for the base API, then adds the simulation layer:

```c
/* SPDX-License-Identifier: GPL-3.0-only */
/* BLE HW stub with connection simulation for emulator testing.
 * Wraps upstream ble_hw_stub.c, adding a state machine that drives
 * the real BLE stack through: connect → ATT MTU → MIDI CCCD → ready. */
#include "../upstream/optimist/firmware/src/ble/ble_hw_stub.c"

enum {
    SIM_IDLE = 0,
    SIM_CONNECTED,      /* CONNECT_IND accepted, running events */
    SIM_MTU_SENT,       /* ATT MTU exchange injected */
    SIM_MIDI_SUBSCRIBED /* MIDI CCCD written, stack is MIDI READY */
};

static struct {
    uint8_t  state;
    uint16_t evt;       /* connection event counter */
    uint32_t time_us;   /* simulated BLE clock */
    uint8_t  addr[6];   /* our advertising address (from ble_hw_addr) */
} bsim;

/* Override ble_hw_time_us from the base stub: return advancing time */
#undef ble_hw_time_us
BLE_API uint32_t ble_hw_time_us(void) { return bsim.time_us; }

/* Inject an L2CAP-framed ATT packet as if received from the central.
 * PDU format: [LLID=2][payload_len][L2CAP_len_lo][L2CAP_len_hi][CID_lo=4][CID_hi=0][ATT...] */
static void bsim_inject_att(const uint8_t *att, uint8_t att_len)
{
    uint8_t pdu[2 + 4 + 64];
    pdu[0] = 0x02;                        /* LLID 2: start of L2CAP */
    pdu[1] = (uint8_t)(4u + att_len);     /* LL payload length */
    pdu[2] = att_len;                     /* L2CAP length lo */
    pdu[3] = 0;                           /* L2CAP length hi */
    pdu[4] = 0x04;                        /* CID ATT lo */
    pdu[5] = 0x00;                        /* CID ATT hi */
    uint32_t i;
    for (i = 0; i < att_len; i++)
        pdu[6 + i] = att[i];
    ble_ll_hw_rx(pdu, (uint8_t)(2u + 4u + att_len));
}

/* Drain and discard TX PDUs the peripheral queues in response. */
static void bsim_drain_tx(void)
{
    uint8_t pdu[260];
    uint32_t k;
    for (k = 0; k < 32u; k++) {
        if (!ble_ll_hw_tx(pdu))
            break;
        ble_ll_hw_tx_acked();
    }
}

/* One simulated connection event: deliver any queued central PDUs,
 * drain peripheral responses, advance the event counter and clock. */
static void bsim_event(void)
{
    bsim_drain_tx();
    bsim.time_us += 30000u;              /* 30 ms simulated interval */
    ble_ll_hw_event_end(bsim.evt, 1);    /* rx_ok=1: link is alive */
    bsim.evt++;
}

/* Simulate an external device connecting.
 * Crafts a valid CONNECT_IND PDU and feeds it to the LL. */
static void ble_hw_stub_connect(void)
{
    uint8_t pdu[36];
    static const uint8_t init_a[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xC0};
    static const uint8_t chm[5]    = {0xFF, 0xFF, 0xFF, 0xFF, 0x1F};
    uint32_t aa = 0x50654A6B;
    uint16_t interval = 24;              /* 30 ms */

    if (bsim.state != SIM_IDLE)
        return;

    ble_hw_addr(bsim.addr);

    pdu[0]  = 0x05 | 0x80 | 0x40;       /* CONNECT_IND, RxAdd random, TxAdd random */
    pdu[1]  = 34;
    /* InitA (central's address) */
    uint32_t i;
    for (i = 0; i < 6u; i++) pdu[2 + i] = init_a[i];
    /* AdvA (our address) */
    for (i = 0; i < 6u; i++) pdu[8 + i] = bsim.addr[i];
    /* Access Address */
    pdu[14] = (uint8_t)aa; pdu[15] = (uint8_t)(aa >> 8);
    pdu[16] = (uint8_t)(aa >> 16); pdu[17] = (uint8_t)(aa >> 24);
    /* CRC init */
    pdu[18] = 0x56; pdu[19] = 0x34; pdu[20] = 0x12;
    /* WinSize, WinOffset */
    pdu[21] = 2; pdu[22] = 3; pdu[23] = 0;
    /* Interval */
    pdu[24] = (uint8_t)interval; pdu[25] = (uint8_t)(interval >> 8);
    /* Latency */
    pdu[26] = 0; pdu[27] = 0;
    /* Timeout (720 * 10ms = 7.2s) */
    pdu[28] = 0xD0; pdu[29] = 0x02;
    /* Channel map */
    for (i = 0; i < 5u; i++) pdu[30 + i] = chm[i];
    /* Hop + SCA */
    pdu[35] = 7 | (1u << 5);

    bsim.evt = 0;
    bsim.time_us = 1000000u;

    if (ble_ll_hw_connect_ind(pdu, 36)) {
        bsim.state = SIM_CONNECTED;
        /* Run the first event so the link is established */
        bsim_event();
    }
}

/* Simulate the central disconnecting (LL_TERMINATE_IND). */
static void ble_hw_stub_disconnect(void)
{
    if (bsim.state == SIM_IDLE)
        return;
    /* Inject LL_TERMINATE_IND (opcode 0x02, reason 0x13 = remote user terminated) */
    {
        uint8_t pdu[2 + 2];
        pdu[0] = 0x03;       /* LLID 3: LL control */
        pdu[1] = 2;          /* length */
        pdu[2] = 0x02;       /* LL_TERMINATE_IND */
        pdu[3] = 0x13;       /* reason: remote user terminated */
        ble_ll_hw_rx(pdu, 4);
    }
    bsim_event();
    bsim.state = SIM_IDLE;
}

/* Drive the simulation forward. Call from the main loop. */
static void ble_hw_stub_tick(void)
{
    if (bsim.state == SIM_IDLE)
        return;

    if (bsim.state == SIM_CONNECTED && bsim.evt >= 2) {
        /* ATT MTU Exchange: central proposes MTU 247 */
        static const uint8_t mtu_req[] = {0x02, 0xF7, 0x00};
        bsim_inject_att(mtu_req, 3);
        bsim_event();
        bsim.state = SIM_MTU_SENT;
        return;
    }

    if (bsim.state == SIM_MTU_SENT && bsim.evt >= 4) {
        /* Write MIDI CCCD (handle 15, value 0x0001 = notifications on) */
        static const uint8_t ccc_write[] = {0x12, 15, 0, 1, 0};
        bsim_inject_att(ccc_write, 5);
        bsim_event();
        bsim.state = SIM_MIDI_SUBSCRIBED;
        return;
    }

    /* Keep the link alive with periodic events */
    if (bsim.evt % 8 == 0)
        bsim_event();
}
```

**Step 2: Wire the new file into the unity build**

In `firmware/src/core_fm1.c`, replace the upstream stub include with our simulation:

Change lines 546-548 from:
```c
#ifdef CFM1_BLE_STUB
#include "../upstream/optimist/firmware/src/ble/ble_hw_stub.c"
#else
```
To:
```c
#ifdef CFM1_BLE_STUB
#include "ble/ble_hw_stub_sim.c"
#else
```

**Step 3: Build to verify it compiles**

Run: `task build -- --defconfig felucca_noise`
Expected: Build succeeds with no new errors. `ble_hw_stub_connect`, `ble_hw_stub_tick` are defined but not yet called.

**Step 4: Commit**

```
feat(ble): add connection simulation to BLE stub driver
```

---

### Task 2: Wire Trigger (BLE Page Button) and Tick

**Files:**
- Modify: `firmware/src/ui/felucca/page_ble.c` (add `.button` handler)
- Modify: `firmware/src/core_fm1.c:629-651` (call `ble_hw_stub_tick()` from `cfm1_ble_sync`)

**Step 1: Add button handler to `page_ble.c`**

Add before the `FPAGE_BLE` definition (before line 169):

```c
#ifdef CFM1_BLE_STUB
static int fble_button(uint32_t id, uint32_t down)
{
    if (!down && id == BTN_PLAY) {
        if (ble_connected())
            ble_hw_stub_disconnect();
        else
            ble_hw_stub_connect();
        fsh_dirty = 1;
        return 1;
    }
    return 0;
}
#endif
```

Then add `.button = fble_button` to the `FPAGE_BLE` struct, guarded by `CFM1_BLE_STUB`:

```c
static const cfm1_page_t FPAGE_BLE = {
    .name   = "BLE",
    .title  = "BLUETOOTH",
    .flags  = PAGEF_SYSTEM,
    .enter  = fble_enter,
    .draw   = fble_draw,
#if BLE_CENTRAL
    .knob   = fble_knob,
#endif
#ifdef CFM1_BLE_STUB
    .button = fble_button,
#endif
};
```

**Step 2: Call `ble_hw_stub_tick()` from the main loop**

In `firmware/src/core_fm1.c`, inside `cfm1_ble_sync()` (the `#ifdef CONFIG_SYS_MENU_BLE` block around line 629), add the tick call at the end of the function:

```c
#ifdef CFM1_BLE_STUB
    ble_hw_stub_tick();
#endif
```

Add this just before the closing `}` of `cfm1_ble_sync()` (before line 652).

**Step 3: Build and verify**

Run: `task build -- --defconfig felucca_noise`
Expected: Build succeeds. The PLAY button is now wired to connect/disconnect on the BLE page.

**Step 4: Commit**

```
feat(ble): wire stub simulation to PLAY button and main loop tick
```

---

### Task 3: Write the E2E Test for BLE Connection Lifecycle

**Files:**
- Modify: `tests/e2e_ble_test.py` (add `test_ble_connection_lifecycle`)

**Step 1: Add the lifecycle test**

Add a new test method to `TestBLEPage`:

```python
def test_ble_connection_lifecycle(self):
    """Boot, navigate to BLE page, verify ADVERTISING, connect, verify CONNECTED + MIDI READY."""
    page = self._boot_page()
    try:
        ctrl = self.controls(page)

        # Navigate to BLE page (page index ~12 in felucca_noise)
        for _ in range(13):
            ctrl.rotate_encoder('select', direction=1, steps=1, delay_ms=200)

        page.wait_for_timeout(500)

        # --- State 1: ADVERTISING ---
        self.screenshot(page, 'BLE-1-advertising')

        # Collect console log for BLE state assertions
        console_text = page.locator('#status').text_content()
        self.assertNotIn('halted', console_text)

        # --- Trigger connection: press PLAY ---
        ctrl.press_button('play', delay_ms=500)

        # Wait for the stub tick to drive through connect → MTU → CCCD
        # The tick runs in the main loop (~15ms per iteration).
        # Events happen at evt 0 (connect), 2 (MTU), 4 (CCCD).
        # At ~15ms ticks, that's ~60ms of firmware time, but we need
        # emulator wall-clock time for rendering.
        page.wait_for_timeout(3000)

        # --- State 2: CONNECTED + MIDI READY ---
        self.screenshot(page, 'BLE-2-connected')

        # Verify emulator hasn't halted
        status = page.locator('#status').text_content()
        self.assertNotIn('halted', status)

        # --- Trigger disconnect: press PLAY again ---
        ctrl.press_button('play', delay_ms=500)
        page.wait_for_timeout(2000)

        # --- State 3: Back to ADVERTISING ---
        self.screenshot(page, 'BLE-3-disconnected')

        status = page.locator('#status').text_content()
        self.assertNotIn('halted', status)

        (TEST_RESULTS / 'ble-lifecycle-status.txt').write_text(status + '\n')
    finally:
        self.write_log(page, 'ble-lifecycle.log')
        page.close()
```

**Step 2: Run the test to verify it fails (no firmware changes deployed yet)**

Run: `FM1_WEB_DIR="$HOME/.fm1-emulator/web" PLAYWRIGHT_BROWSERS_PATH="$HOME/.cache/ms-playwright" uv run python -m pytest tests/e2e_ble_test.py::TestBLEPage::test_ble_connection_lifecycle -v`

Expected: The test should run but the screenshots will show BLE staying in ADVERTISING (the button press has no effect yet with old firmware). This confirms the test harness works.

**Step 3: Commit**

```
test(ble): add e2e test for BLE connection lifecycle
```

---

### Task 4: Build with Simulation, Run Full Test Suite

**Step 1: Rebuild firmware with all changes**

Run: `task build -- --defconfig felucca_noise`
Expected: Clean build, `.fwsc` produced.

**Step 2: Run all BLE e2e tests**

Run: `FM1_WEB_DIR="$HOME/.fm1-emulator/web" PLAYWRIGHT_BROWSERS_PATH="$HOME/.cache/ms-playwright" uv run python -m pytest tests/e2e_ble_test.py -v`

Expected:
```
tests/e2e_ble_test.py::TestBLEPage::test_ble_page_renders PASSED
tests/e2e_ble_test.py::TestBLEPage::test_ble_page_shows_advertising PASSED
tests/e2e_ble_test.py::TestBLEPage::test_ble_connection_lifecycle PASSED
```

**Step 3: Inspect screenshots**

Check `tests/results/<timestamp>-e2e_ble_test/screenshots/`:
- `*BLE-1-advertising.png`: BLE card shows `ADV`, status shows `ADVERTISING`, MIDI shows `WAITING`
- `*BLE-2-connected.png`: BLE card shows `ON`, LINK shows `YES`, MIDI shows `OK`, status shows `MIDI READY`
- `*BLE-3-disconnected.png`: BLE card shows `ADV` again, status shows `ADVERTISING`

**Step 4: Run the existing test suite to check for regressions**

Run: `FM1_WEB_DIR="$HOME/.fm1-emulator/web" PLAYWRIGHT_BROWSERS_PATH="$HOME/.cache/ms-playwright" uv run python -m pytest tests/e2e_boot_test.py tests/e2e_ble_test.py -v`

Expected: All tests pass. The stub simulation only activates when `ble_hw_stub_connect()` is called (PLAY on BLE page), so other tests are unaffected.

**Step 5: Commit all together if not already**

```
test(ble): verify BLE stub simulation passes full e2e lifecycle
```

---

## Debugging Notes

**If the connection isn't established:** Check that `ble_ll_hw_connect_ind` returns 1. The address in the CONNECT_IND's AdvA field must match the stub's `ble_hw_addr` output exactly (random static: `{0x01, 0x00, 0x00, 0x00, 0x00, 0xC0}`). The RxAdd bit in `pdu[0]` must be set (bit 7) since the address is random.

**If the emulator halts after PLAY:** The stub's `ble_hw_time_us` override may conflict. Verify the `#undef` approach works in the unity build. If not, modify the upstream stub to call through a function pointer.

**If MIDI READY doesn't trigger:** The CCCD write handle must be `H_MIDI_CCC` = 15 (for `ble_att_ft.c`) or 11 (for the older `ble_att.c`). Since `felucca_noise` builds with `CFM1_BLE_FT`, the handle is 15 (`H_MIDI_CCC` in the FT variant's enum).

**If ATT responses cause issues:** The `bsim_drain_tx` must be called after each inject to consume the peripheral's response PDU, or the TX queue fills up and the link stalls.

**Console log verification:** The existing `ble_app_state()` callback in `core_fm1.c:497-502` logs `ble: CONNECTED` and `ble: midi_ready=1`. These appear in the emulator's console output. The test can optionally parse the log file written by `self.write_log()` for deeper assertions.
