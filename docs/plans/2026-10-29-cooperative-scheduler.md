# Cooperative Scheduler Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the monolithic `for(;;)` main loop with a round-robin cooperative scheduler, enabling non-blocking WiFi init and clean subsystem separation.

**Architecture:** A minimal cooperative scheduler (~200 lines C + context switch asm) using fm1-base's proven ctx.S pattern. Audio stays in ISR. Tasks yield voluntarily; scheduler round-robins through 6 slots. Phase 0 fixes WiFi bugs/buffers first, Phase 1 adds the scheduler core with WiFi as first task, Phases 2-4 measure, migrate, and validate.

**Tech Stack:** C (unity build, all static), pi32v2 assembly (context switch), Kconfiglib, Python (unit tests), Playwright (e2e tests)

**Design doc:** `.scratch/doc/2026-10-29-cooperative-scheduler-design.md`
**Master checklist:** `.scratch/tasks/todo/2026-10-29T02-cooperative-scheduler-design.md`

---

## Phase 0: Pre-Scheduler Fixes

These are independent of each other. No blockers. Can be done in any order.

---

### Task 1: WiFi Buffer Refactor (T03)

Move 6,124 bytes of stack-local buffers to `static` in the WiFi data/lwIP path.
Safe because cooperative scheduling ensures single-threaded WiFi access.

**Files:**
- Modify: `firmware/src/wifi/wifi_data.c:232-234` (wifi_data_tx frame buf)
- Modify: `firmware/src/wifi/wifi_data.c:260-261` (wifi_data_rx 802.11 buf)
- Modify: `firmware/src/wifi/wifi_data.c:304` (wifi_data_rx ethernet buf)
- Modify: `firmware/src/wifi/wifi_lwip.c:38` (wifi_lwip_linkoutput buf)
- Test: `tests/wifi/test_sdio.py` (existing), new assertions optional

**Step 1: Write host-side test verifying buffer sizes**

Create `tests/wifi/test_buffer_sizes.py`:

```python
#!/usr/bin/env python3
"""Verify WiFi stack buffers are static, not stack-local."""
import re
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
WIFI_DATA = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_data.c'
WIFI_LWIP = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_lwip.c'


def test_wifi_data_tx_buffer_is_static():
    src = WIFI_DATA.read_text()
    fn = src[src.index('wifi_data_tx('):]
    fn = fn[:fn.index('\nstatic ')]
    assert 'static uint8_t wifi_tx_frame' in src, \
        'wifi_data_tx frame buffer must be static'
    assert re.search(r'uint8_t\s+frame\[', fn) is None, \
        'wifi_data_tx must not have stack-local frame buffer'


def test_wifi_data_rx_buffers_are_static():
    src = WIFI_DATA.read_text()
    fn = src[src.index('wifi_data_rx('):]
    fn = fn[:fn.index('\nstatic ')]
    assert 'static uint8_t wifi_rx_frame' in src
    assert 'static uint8_t wifi_rx_eth' in src
    assert re.search(r'uint8_t\s+buf\[WIFI_DATA_MAX', fn) is None, \
        'wifi_data_rx must not have stack-local 802.11 buffer'
    assert re.search(r'uint8_t\s+eth\[WIFI_ETH_HDR', fn) is None, \
        'wifi_data_rx must not have stack-local ethernet buffer'


def test_wifi_lwip_linkoutput_buffer_is_static():
    src = WIFI_LWIP.read_text()
    fn_start = src.index('wifi_lwip_linkoutput(')
    fn = src[fn_start:]
    fn = fn[:fn.index('\nstatic ')]
    assert 'static uint8_t wifi_tx_eth' in src
    assert re.search(r'uint8_t\s+buf\[WIFI_LWIP_MTU', fn) is None, \
        'wifi_lwip_linkoutput must not have stack-local buffer'
```

**Step 2: Run test to verify it fails**

Run: `task test:unit -- tests/wifi/test_buffer_sizes.py -v`
Expected: FAIL (buffers are still stack-local)

**Step 3: Move buffers to static in wifi_data.c**

In `wifi_data.c`, add static buffers near the top (after the helpers, before TX path):

```c
/* static TX/RX frame buffers: moved off stack for cooperative scheduling.
 * Safe: WiFi task is single-threaded (cooperative yield). */
static uint8_t wifi_tx_frame[WIFI_DATA_MAX_FRAME + 16]; /* +16 CCMP */
static uint8_t wifi_rx_frame[WIFI_DATA_MAX_FRAME + 16];
static uint8_t wifi_rx_eth[WIFI_ETH_HDR_LEN + WIFI_DATA_MTU];
```

In `wifi_data_tx()` (line 232-234), replace:
```c
/* OLD: */ uint8_t frame[WIFI_DATA_MAX_FRAME + 16];
```
with a reference to the static buffer (just remove the local, use `wifi_tx_frame`):
```c
static int wifi_data_tx(const uint8_t *eth, uint32_t eth_len)
{
    uint8_t *frame = wifi_tx_frame;
    uint32_t frame_len;
    /* ... rest unchanged, frame is now a pointer to static storage ... */
```

In `wifi_data_rx()` (line 260-261, 304), replace:
```c
/* OLD: */ uint8_t buf[WIFI_DATA_MAX_FRAME + 16];
/* OLD: */ uint8_t eth[WIFI_ETH_HDR_LEN + WIFI_DATA_MTU];
```
with:
```c
static void wifi_data_rx(const uint8_t *frame, uint32_t len)
{
    uint8_t *buf = wifi_rx_frame;
    uint32_t dlen, eth_len;
    uint16_t fc, ethertype;
    /* ... */
    /* and further down, replace local eth[] with: */
    {
        uint8_t *eth = wifi_rx_eth;
        /* ... rest unchanged ... */
    }
```

**Step 4: Move buffer to static in wifi_lwip.c**

Add static buffer at file scope (inside `#ifdef CONFIG_WIFI_LWIP`):

```c
static uint8_t wifi_tx_eth[WIFI_LWIP_MTU + 14u];
```

In `wifi_lwip_linkoutput()` (line 38), replace:
```c
/* OLD: */ uint8_t buf[WIFI_LWIP_MTU + 14u];
```
with:
```c
static err_t wifi_lwip_linkoutput(struct netif *nif, struct pbuf *p)
{
    uint8_t *buf = wifi_tx_eth;
    /* ... rest unchanged ... */
```

**Step 5: Run test to verify it passes**

Run: `task test:unit -- tests/wifi/test_buffer_sizes.py -v`
Expected: PASS

**Step 6: Run full test suite**

Run: `task test:unit`
Expected: all PASS

**Step 7: Build check**

Run: `task build -- --defconfig core_fm1_dev`
Expected: build succeeds (or dry-run completes without config errors)

**Step 8: Commit**

```bash
git add firmware/src/wifi/wifi_data.c firmware/src/wifi/wifi_lwip.c tests/wifi/test_buffer_sizes.py
git commit -m "fix(wifi): move 6KB stack buffers to static for scheduler safety"
```

---

### Task 2: SDIO TX Interrupt Masking Fix (T04)

The current `wifi_sdio_tx()` disables IRQs for up to 15ms while busy-waiting for DMA. This exceeds the 2.9ms audio deadline. Fix: short critical sections around register writes only, poll DMA with IRQs enabled.

**Files:**
- Modify: `firmware/src/wifi/wifi_sdio.c:230-278` (wifi_sdio_tx)
- Test: `tests/wifi/test_sdio.py` (existing, add new test)

**Step 1: Write test asserting IRQ-disable duration is bounded**

Add to `tests/wifi/test_sdio.py`:

```python
def test_sdio_tx_irq_disable_is_bounded():
    """wifi_sdio_tx must not hold IRQ-disable across DMA wait loops."""
    src = (ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_sdio.c').read_text()
    fn_start = src.index('wifi_sdio_tx(')
    fn_end = src.index('\nstatic ', fn_start + 1)
    fn = src[fn_start:fn_end]

    # The function should release IRQs before any DMA wait loop
    lines = fn.split('\n')
    in_claim = False
    for i, line in enumerate(lines):
        if 'wifi_sdio_claim()' in line:
            in_claim = True
        if 'wifi_sdio_release()' in line:
            in_claim = False
        # No delay_us loop while IRQs are disabled
        if in_claim and 'fm1_delay_us' in line:
            # Check we're inside a for/while wait loop
            context = '\n'.join(lines[max(0,i-3):i+1])
            if 'for' in context or 'while' in context:
                pytest.fail(
                    f'wifi_sdio_tx: fm1_delay_us inside claim at line {i}: '
                    f'IRQ-disable must not span DMA wait loops')
```

**Step 2: Run test to verify it fails**

Run: `task test:unit -- tests/wifi/test_sdio.py::test_sdio_tx_irq_disable_is_bounded -v`
Expected: FAIL (current code has delay_us loops inside claim/release)

**Step 3: Rewrite wifi_sdio_tx with short critical sections**

Replace `wifi_sdio_tx()` in `firmware/src/wifi/wifi_sdio.c`:

```c
static int wifi_sdio_tx(const uint8_t *frame, uint32_t len)
{
    struct wifi_txd *txd;
    uint32_t wait;

    if (!frame || len == 0 || len > WIFI_TX_MAX_FRAME)
        return -1;

    if (!wifi_sdio_state.initialized || !wifi_sdio_state.dma_active)
        return -1;

    txd = &wifi_tx_ring[0];

    /* Wait for previous TX to complete (IRQs enabled during poll) */
    for (wait = 0; wait < 5000u; wait++) {
        if (txd->sd_ptr0 == 0 || (txd->flags & TXD_DMA_DONE))
            break;
        fm1_delay_us(1);
    }
    if (txd->sd_ptr0 != 0 && !(txd->flags & TXD_DMA_DONE)) {
        if (wifi_sdio_state.error_count < 255u)
            wifi_sdio_state.error_count++;
        return -1;
    }

    /* Short critical section: program descriptor + kick DMA */
    wifi_sdio_claim();
    txd->sd_ptr0 = (uint32_t)frame;
    txd->sd_len0 = (len & TXD_SDL0_MASK) | TXD_LAST_SEC0;
    txd->sd_ptr1 = 0;
    txd->flags   = 0;
    WIFI_TX_CTX_IDX0 = 0;
    wifi_sdio_release();

    /* Wait for DMA completion (IRQs enabled) */
    for (wait = 0; wait < 10000u; wait++) {
        if (txd->flags & TXD_DMA_DONE)
            break;
        fm1_delay_us(1);
    }

    /* Short critical section: reclaim descriptor */
    wifi_sdio_claim();
    if (!(txd->flags & TXD_DMA_DONE)) {
        /* Timeout: abort. Reset DMA engine to ensure it stops
         * accessing the buffer before we release it. */
        WIFI_WPDMA_GLO_CFG &= ~WPDMA_TX_DMA_EN;
        fm1_delay_us(2);
        WIFI_WPDMA_GLO_CFG |= WPDMA_TX_DMA_EN;
    }
    txd->sd_ptr0 = 0;
    wifi_sdio_release();

    return (txd->flags & TXD_DMA_DONE) ? 0 : -1;
}
```

**Step 4: Run test to verify it passes**

Run: `task test:unit -- tests/wifi/test_sdio.py -v`
Expected: PASS

**Step 5: Commit**

```bash
git add firmware/src/wifi/wifi_sdio.c tests/wifi/test_sdio.py
git commit -m "fix(wifi): bound SDIO TX IRQ-disable to descriptor ops only

DMA wait loops now run with IRQs enabled. On timeout, the DMA
engine is stopped before releasing the buffer. Fixes audio underruns
during WiFi TX."
```

---

### Task 3: WiFi RTOS Deferred Work Bug Fix (T05)

Two bugs: (1) `wifi_defer_schedule()` modifies the original object instead of the registered copy. (2) `wifi_poll()` never calls `wifi_timer_poll()`.

**Files:**
- Modify: `firmware/src/wifi/wifi_rtos.c:174-189` (defer_init, defer_schedule)
- Modify: `firmware/src/wifi/wifi_api.c:137` (wifi_poll, add timer_poll)
- Test: `tests/wifi/test_rtos_shim.py` (existing, add new tests)

**Step 1: Write tests for deferred work correctness**

Add to `tests/wifi/test_rtos_shim.py`:

```python
def test_defer_schedule_modifies_registered_copy():
    """wifi_defer_schedule must set pending on the copy in wifi_defers[]."""
    src = (ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_rtos.c').read_text()
    # The function should find the registered copy by matching, or store
    # pointers instead of copies
    fn_start = src.index('wifi_defer_schedule(')
    fn_end = src.index('\n}', fn_start) + 2
    fn = src[fn_start:fn_end]
    # Must reference wifi_defers[] array, not just d->pending
    assert 'wifi_defers' in fn or 'wifi_defer_find' in fn, \
        'wifi_defer_schedule must modify the registered copy in wifi_defers[]'


def test_wifi_poll_calls_timer_poll():
    """wifi_poll must call wifi_timer_poll() for software timers."""
    src = (ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_api.c').read_text()
    fn_start = src.index('wifi_poll(')
    fn_end = src.index('\n}', fn_start) + 2
    fn = src[fn_start:fn_end]
    assert 'wifi_timer_poll()' in fn, \
        'wifi_poll must call wifi_timer_poll()'
```

**Step 2: Run test to verify it fails**

Run: `task test:unit -- tests/wifi/test_rtos_shim.py -v`
Expected: FAIL on both new tests

**Step 3: Fix wifi_defer_schedule to use registered copy**

Replace the defer management in `firmware/src/wifi/wifi_rtos.c`. Change from copy-based to pointer-based storage:

```c
static void wifi_defer_init(struct wifi_defer *d, wifi_defer_fn_t func, unsigned long data)
{
    d->func = func;
    d->data = data;
    d->pending = 0;
    d->active = 1;
    if (wifi_defer_count < WIFI_DEFER_MAX) {
        wifi_defers[wifi_defer_count] = *d;
        wifi_defer_count++;
    }
}

static struct wifi_defer *wifi_defer_find(struct wifi_defer *d)
{
    uint32_t i;
    for (i = 0; i < wifi_defer_count; i++) {
        if (wifi_defers[i].func == d->func && wifi_defers[i].data == d->data)
            return &wifi_defers[i];
    }
    return (struct wifi_defer *)0;
}

static void wifi_defer_schedule(struct wifi_defer *d)
{
    struct wifi_defer *reg = wifi_defer_find(d);
    if (reg)
        reg->pending = 1;
}

static void wifi_defer_cancel(struct wifi_defer *d)
{
    struct wifi_defer *reg = wifi_defer_find(d);
    if (reg) {
        reg->active = 0;
        reg->pending = 0;
    }
}
```

**Step 4: Add wifi_timer_poll() to wifi_poll()**

In `firmware/src/wifi/wifi_api.c`, inside `wifi_poll()`, add after `wifi_defer_poll()`:

```c
static void wifi_poll(void)
{
    /* deferred work from ISRs */
    wifi_defer_poll();

    /* software timers */
    wifi_timer_poll();

    /* SDIO: check for pending IRQs */
    wifi_sdio_poll_irq();
    /* ... rest unchanged ... */
```

**Step 5: Run tests**

Run: `task test:unit -- tests/wifi/test_rtos_shim.py -v`
Expected: PASS

**Step 6: Commit**

```bash
git add firmware/src/wifi/wifi_rtos.c firmware/src/wifi/wifi_api.c tests/wifi/test_rtos_shim.py
git commit -m "fix(wifi): deferred work modifies registered copy, add timer_poll

wifi_defer_schedule now finds the registered copy in wifi_defers[]
by matching func+data, instead of modifying the caller's original.
wifi_poll now calls wifi_timer_poll() so software timers actually fire."
```

---

### Task 4: Reorder WiFi Connect PMK Derivation (T06)

Current flow starts auth before deriving PMK. Fix: derive PMK first, then start auth. This also prepares for PBKDF2 yielding in Phase 1.

**Files:**
- Modify: `firmware/src/wifi/wifi_api.c:57-90` (wifi_connect)
- Modify: `firmware/src/wifi/wifi_wpa.c:688-708` (split wifi_wpa_init into init + derive)
- Test: `tests/wifi/test_api.py` (add new test)

**Step 1: Write test verifying PMK derivation happens before auth**

Add to `tests/wifi/test_api.py`:

```python
def test_wifi_connect_derives_pmk_before_auth():
    """wifi_connect must derive PMK before starting authentication."""
    src = (ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_api.c').read_text()
    fn_start = src.index('wifi_connect(')
    fn_end = src.index('\n}', fn_start) + 2
    fn = src[fn_start:fn_end]

    # PMK derivation (wpa_init or wpa_derive_pmk) must appear
    # BEFORE start_connect (which sends AUTH frame)
    pmk_pos = max(
        fn.find('wifi_wpa_init('),
        fn.find('wifi_wpa_derive_pmk('),
    )
    auth_pos = fn.find('wifi_sta_start_connect(')
    assert pmk_pos >= 0, 'wifi_connect must call wpa_init or wpa_derive_pmk'
    assert auth_pos >= 0, 'wifi_connect must call sta_start_connect'
    assert pmk_pos < auth_pos, \
        'PMK derivation must happen BEFORE start_connect (auth)'
```

**Step 2: Run test to verify it fails**

Run: `task test:unit -- tests/wifi/test_api.py::test_wifi_connect_derives_pmk_before_auth -v`
Expected: FAIL (current code calls start_connect before wpa_init)

**Step 3: Reorder wifi_connect**

In `firmware/src/wifi/wifi_api.c`, rewrite `wifi_connect()`:

```c
static int wifi_connect(const char *ssid, const char *password)
{
    int idx;

    if (!ssid)
        return -1;
    cfm1_log("wifi: connect");
    if (wifi_sta.state != WIFI_STATE_IDLE &&
        wifi_sta.state != WIFI_STATE_SCANNING)
        return -1;

    /* find AP in scan results first (need BSSID for PMK) */
    idx = wifi_sta_find_ap(ssid);
    if (idx < 0) {
        cfm1_log("wifi: AP not found");
        return -1;
    }

    wifi_cfg_ssid = ssid;
    wifi_cfg_pass = password;

#ifdef CONFIG_WIFI_WPA
    if (password) {
        uint32_t plen = 0, slen = 0;
        const char *p = password, *s = ssid;
        while (*p++) plen++;
        while (*s++) slen++;
        /* Derive PMK BEFORE starting auth so EAPOL can proceed
         * immediately after association completes */
        wifi_wpa_init((const uint8_t *)password, plen,
                      (const uint8_t *)ssid, slen,
                      wifi_sta.our_mac,
                      wifi_sta.results[idx].bssid);
    }
#endif

    /* Now start auth/assoc sequence */
    return wifi_sta_start_connect(ssid, password);
}
```

**Step 4: Run tests**

Run: `task test:unit -- tests/wifi/test_api.py -v`
Expected: PASS

**Step 5: Commit**

```bash
git add firmware/src/wifi/wifi_api.c tests/wifi/test_api.py
git commit -m "fix(wifi): derive PMK before starting auth sequence

Reordered wifi_connect to call wifi_wpa_init (PBKDF2) before
wifi_sta_start_connect (AUTH frame). Prevents the situation where
auth is underway while PMK is still being computed."
```

---

### Task 5: STA Poll Rate Fix (T07)

`wifi_sta_poll()` sends assoc requests whenever elapsed < 10ms, which depends on polling rate. Fix: make assoc a one-shot with explicit retry timer.

**Files:**
- Modify: `firmware/src/wifi/wifi_sta.c:588-596` (ASSOC case in wifi_sta_poll)
- Test: `tests/wifi/test_sta.py` (add new test)

**Step 1: Write test verifying one-shot assoc behavior**

Add to `tests/wifi/test_sta.py`:

```python
def test_sta_assoc_is_one_shot():
    """ASSOC state must send request once on entry, not every poll < 10ms."""
    src = (ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_sta.c').read_text()
    fn_start = src.index('wifi_sta_poll(')
    fn_end = src.index('\n}', src.index('case WIFI_STATE_CONNECTED:', fn_start))
    fn = src[fn_start:fn_end]

    assoc_section = fn[fn.index('WIFI_STATE_ASSOC'):]
    assoc_section = assoc_section[:assoc_section.index('case ')]

    # Must NOT have "elapsed < 10" or similar time-window send pattern
    assert '< 10' not in assoc_section and '<10' not in assoc_section, \
        'ASSOC must not send requests based on elapsed < 10ms window'
```

**Step 2: Run test to verify it fails**

Run: `task test:unit -- tests/wifi/test_sta.py::test_sta_assoc_is_one_shot -v`
Expected: FAIL

**Step 3: Implement one-shot assoc with retry**

Add a flag and retry timer to `wifi_sta` struct (add after `last_rx_ms`):

```c
    uint8_t assoc_sent;         /* one-shot: assoc request sent this state */
    uint32_t assoc_retry_ms;    /* next retry deadline */
```

Replace the ASSOC case in `wifi_sta_poll()`:

```c
    case WIFI_STATE_ASSOC:
        if (!wifi_sta.assoc_sent) {
            wifi_sta_send_assoc_req();
            wifi_sta.assoc_sent = 1;
            wifi_sta.assoc_retry_ms = now + 500u;
        } else if ((int32_t)(now - wifi_sta.assoc_retry_ms) >= 0) {
            wifi_sta_send_assoc_req();
            wifi_sta.assoc_retry_ms = now + 500u;
        }
        if (wifi_sta_timed_out()) {
            cfm1_log("wifi: assoc timeout");
            wifi_sta_set_state(WIFI_STATE_IDLE);
        }
        break;
```

Reset `assoc_sent` when entering ASSOC state. In `wifi_sta_set_state()`:

```c
static void wifi_sta_set_state(wifi_state_t s)
{
    cfm1_log_int("wifi: sta state=", (int32_t)s);
    wifi_sta.state = s;
    wifi_sta.state_enter_ms = wifi_sta_now_ms();
    if (s == WIFI_STATE_ASSOC)
        wifi_sta.assoc_sent = 0;
}
```

**Step 4: Run tests**

Run: `task test:unit -- tests/wifi/test_sta.py -v`
Expected: PASS

**Step 5: Run full test suite**

Run: `task test:unit`
Expected: all PASS

**Step 6: Commit**

```bash
git add firmware/src/wifi/wifi_sta.c tests/wifi/test_sta.py
git commit -m "fix(wifi): make assoc request one-shot with 500ms retry

Replaces the elapsed < 10ms send window with a flag-based one-shot
on state entry and explicit 500ms retry deadline. Eliminates
poll-rate-dependent repeated transmissions."
```

---

### Task 6: Decouple Scheduler Clock from CONFIG_INPUT (T08)

`fm1_ms` only advances in Timer5, which is gated by `CONFIG_INPUT`. A WiFi-only config without input leaves `sleep_ms()` blocked forever.

**Files:**
- Modify: `firmware/src/core_fm1.c:1141-1149` (Timer5 init block)
- Modify: `firmware/src/core_fm1.c:931-940` (fm1_main, add early ms timer)
- Test: `tests/test_genconfig.py` (or new `tests/test_scheduler_clock.py`)

**Step 1: Write test verifying fm1_ms is always available**

Create `tests/test_scheduler_clock.py`:

```python
#!/usr/bin/env python3
"""Verify the millisecond clock is independent of CONFIG_INPUT."""
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / 'firmware' / 'src' / 'core_fm1.c'


def test_fm1_ms_timer_not_gated_by_config_input():
    """Timer5 (or equivalent ms timer) must init outside CONFIG_INPUT guard."""
    src = CORE.read_text()

    # Find the Timer5 init block
    # It should NOT be entirely inside #ifdef CONFIG_INPUT
    # Strategy: find fm1_ms increment and ensure Timer5 init is unconditional

    # The Timer5 setup (T5 CON, T5 CNT, T5 PRD) must appear outside
    # the CONFIG_INPUT ifdef block, or a separate timer must handle fm1_ms
    lines = src.split('\n')
    in_input_guard = 0
    timer5_in_guard = False
    ms_timer_outside = False

    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped == '#ifdef CONFIG_INPUT':
            in_input_guard += 1
        elif stripped == '#endif' and in_input_guard > 0:
            in_input_guard -= 1
        elif in_input_guard == 0:
            # Check for timer setup or fm1_ms init outside input guard
            if 'T5 PRD' in line or 'T5 CON' in line or \
               'fm1_ms_timer_init' in line or \
               ('IRQ_TIMER5' in line and 'fm1_irq_attach' in line):
                ms_timer_outside = True

    assert ms_timer_outside, \
        'Millisecond timer init must be outside #ifdef CONFIG_INPUT'
```

**Step 2: Run test to verify it fails**

Run: `task test:unit -- tests/test_scheduler_clock.py -v`
Expected: FAIL (Timer5 init is inside CONFIG_INPUT guard)

**Step 3: Move Timer5 ms-tick init outside CONFIG_INPUT**

The key insight: Timer5 handles TWO things: (1) fm1_ms increment, (2) input scanning. Split them: always install Timer5 for ms tick, only scan inputs when CONFIG_INPUT is set.

In `firmware/src/core_fm1.c`, move the Timer5 hardware init to just before `fm1_irq_enable_all()` (line ~1158), outside any `#ifdef CONFIG_INPUT`:

```c
    /* Millisecond clock: Timer5 at 10kHz, always active.
     * Input scanning runs inside the ISR when CONFIG_INPUT is set. */
    {
        *(volatile uint32_t *)0x10900u = 0x4000u;  /* T5 CON: clear */
        *(volatile uint32_t *)0x10904u = 0;         /* T5 CNT */
        *(volatile uint32_t *)0x10908u = 600u;      /* T5 PRD: 6MHz/600 = 10kHz */
        fm1_irq_attach(FM1_IRQ_TIMER5, isr_timer5, 4);
        *(volatile uint32_t *)0x10900u = 0x4019u;   /* T5 CON: enable */
    }
    cfm1_log("timer5: 10kHz ms clock");
```

Remove the duplicate Timer5 init from inside the `#ifdef CONFIG_INPUT` block.

The `isr_timer5` / `fm1_timer5_irq` C body already gates input scanning with `#ifdef CONFIG_INPUT` around the scan code and always increments `fm1_ms`. If it doesn't, adjust `fm1_timer5_irq` so `fm1_ms` increment is unconditional:

```c
static void fm1_timer5_irq(void)
{
    /* always: millisecond clock */
    static uint8_t ms_div;
    if (++ms_div >= 10u) {
        ms_div = 0;
        fm1_ms++;
    }
#ifdef CONFIG_INPUT
    /* input scanning at 10kHz */
    fm1_input_scan();
#endif
}
```

**Step 4: Run tests**

Run: `task test:unit -- tests/test_scheduler_clock.py -v`
Expected: PASS

**Step 5: Run full test suite**

Run: `task test:unit`
Expected: all PASS

**Step 6: Commit**

```bash
git add firmware/src/core_fm1.c tests/test_scheduler_clock.py
git commit -m "fix: decouple fm1_ms timer from CONFIG_INPUT

Timer5 now always initializes for the millisecond clock, regardless
of CONFIG_INPUT. Input scanning remains gated inside the ISR body.
Prevents sleep_ms from blocking forever in input-disabled configs."
```

---

## Phase 1: Scheduler Core (requires Phase 0 complete + T09 resolved)

### Blockers

Before starting Phase 1, the following must be resolved:

1. **T09: pi32v2 ABI investigation** (see Task 7 below)
2. **RF trim XIP corruption**: confirm whether yielding during RF calibration is safe

---

### Task 7: pi32v2 ABI Investigation (T09)

Research task. Examine crt0.S, fm1_isr.S, and fm1-base ctx.S to answer:
- Which stack pointer (sp vs ssp) does the main loop use?
- What are the callee-saved registers?
- Do ISRs run on the interrupted task's stack?
- How should EMU stack guards be configured for task stacks?

**Files to examine:**
- `firmware/crt0.S` (sp=_ustack_top, ssp=_sstack_top)
- `firmware/hal/fm1_isr.S` (ISR pushes to sp, saves r0-r3 + psr/rets/reti)
- `firmware/app.ld` (stack regions: _ustack 0x74100-0x7A000, _sstack 0x7A100-0x7C000)
- fm1-base ctx.S (reference: saves rets + r4-r15, 13-word frame)

**Step 1: Document findings**

Create `docs/plans/2026-10-29-pi32v2-abi-findings.md` with answers to each question.

From static analysis of existing code:
- `crt0.S` sets `sp = _ustack_top` (user stack) and `ssp = _sstack_top` (supervisor stack)
- `fm1_cstart` and `fm1_main` run after `call fm1_cstart`, so main loop uses user sp
- ISR entry (`fm1_isr.S`): `[--sp] = {psr, rets, reti}` then `[--sp] = {r3-r0}` pushes onto *current* sp at interrupt time
- fm1-base saves `rets` + `r4-r15` (12 regs + rets = 13 words = 52 bytes), exchanges sp
- Callee-saved: r4-r15 (ISR only saves r0-r3, so r4-r15 must be preserved by called functions)

**Step 2: Verify with disassembly**

Run: `task build -- --defconfig minimal` (if toolchain available)

Then disassemble a compiled function to verify register convention. If toolchain is not available, document assumptions from code and flag for hardware verification.

**Step 3: Document EMU guard strategy decision**

Based on findings, choose one of:
- (A) Allocate task stacks within guard window
- (B) Widen guard in linker script
- (C) Reconfigure EMU on context switch (4 instructions per switch)

**Step 4: Commit findings**

```bash
git add docs/plans/2026-10-29-pi32v2-abi-findings.md
git commit -m "docs: pi32v2 ABI investigation findings for scheduler"
```

---

### Task 8: Scheduler Core Implementation

The scheduler itself: data structures, context switch, task creation, round-robin dispatch.

**Files:**
- Create: `firmware/src/sched/sched.c` (scheduler core)
- Create: `firmware/src/sched/ctx.S` (context switch assembly)
- Modify: `firmware/src/core_fm1.c` (include sched.c, wire up)
- Create: `kconf/sched.kconfig` (CONFIG_SCHED)
- Modify: `Kconfig` (source sched.kconfig)
- Test: `tests/test_scheduler.py`

**Step 1: Add Kconfig symbol**

Create `kconf/sched.kconfig`:

```kconfig
menu "Scheduler"

config SCHED
    bool "Cooperative scheduler"
    default y
    help
      Round-robin cooperative scheduler. Tasks yield voluntarily.
      Without this, firmware runs the traditional monolithic main loop.

config SCHED_MAX_TASKS
    int "Maximum task slots"
    default 6
    depends on SCHED

config SCHED_STACK_CANARY
    bool "Stack canary checks"
    default y
    depends on SCHED
    help
      Paint task stacks with 0xDEADBEEF and check bottom 8 bytes
      on every context switch. Detects stack overflow.

endmenu
```

Add `source "kconf/sched.kconfig"` to root `Kconfig`.

**Step 2: Write tests for Kconfig**

Create `tests/test_scheduler.py`:

```python
#!/usr/bin/env python3
"""Unit tests for scheduler Kconfig and source structure."""
from pathlib import Path

import pytest
from kconfiglib import Kconfig, BOOL

ROOT = Path(__file__).resolve().parents[1]
KCONFIG = ROOT / 'Kconfig'


@pytest.fixture(autouse=True)
def skip_if_no_kconfig():
    if not KCONFIG.exists():
        pytest.skip('Kconfig not found')


@pytest.fixture
def kconf():
    return Kconfig(str(KCONFIG), warn=False)


def test_sched_kconfig_parses(kconf):
    s = kconf.syms['SCHED']
    assert s.type == BOOL


def test_sched_max_tasks_default(kconf):
    s = kconf.syms['SCHED_MAX_TASKS']
    assert s.str_value == '6'


def test_sched_source_exists():
    assert (ROOT / 'firmware' / 'src' / 'sched' / 'sched.c').exists()
```

**Step 3: Run tests to verify they fail**

Run: `task test:unit -- tests/test_scheduler.py -v`
Expected: FAIL

**Step 4: Implement context switch assembly**

Create `firmware/src/sched/ctx.S` based on fm1-base's proven implementation:

```asm
/* SPDX-License-Identifier: GPL-3.0-only */
/* Cooperative context switch for pi32v2.
 * Based on fm1-base ctx.S (proven on AC791N hardware).
 * Saves rets + r4-r15, exchanges sp, restores through pc. */

    .section .text,"ax",@progbits

/* void sched_switch(uint32_t **old_sp, uint32_t *new_sp)
 * r0 = &old_task->saved_sp
 * r1 = new_task->saved_sp
 *
 * Saves callee-saved registers + return address onto current stack.
 * Stores current sp into *old_sp.
 * Loads new sp from new_sp.
 * Restores registers and returns to new task's saved pc. */
    .globl sched_switch
sched_switch:
    [--sp] = {rets}
    [--sp] = {r15-r4}
    [r0] = sp          /* save old sp */
    sp = r1             /* load new sp */
    {r15-r4} = [sp++]
    {rets} = [sp++]
    rts                 /* return to new task's saved rets */
```

**Step 5: Implement scheduler core**

Create `firmware/src/sched/sched.c`:

```c
/* SPDX-License-Identifier: GPL-3.0-only */
/* Round-robin cooperative scheduler for core-fm1.
 * See docs/plans/2026-10-29-cooperative-scheduler.md for design. */

#ifdef CONFIG_SCHED

#define SCHED_CANARY 0xDEADBEEFu

typedef enum {
    SCHED_DEAD = 0,
    SCHED_READY,
    SCHED_SLEEPING,
} sched_state_t;

typedef struct {
    uint32_t *saved_sp;
    uint32_t *stack_base;
    uint32_t  stack_words;
    sched_state_t state;
    uint32_t  wake_ms;
} sched_task_t;

static struct {
    sched_task_t tasks[CONFIG_SCHED_MAX_TASKS];
    uint32_t     count;
    uint32_t     current;
} sched;

extern void sched_switch(uint32_t **old_sp, uint32_t *new_sp);

static void sched_trampoline(void);

static void sched_init(void)
{
    uint32_t i;
    for (i = 0; i < CONFIG_SCHED_MAX_TASKS; i++)
        sched.tasks[i].state = SCHED_DEAD;
    sched.count = 0;
    sched.current = 0;
}

static int sched_create(void (*fn)(void), uint32_t *stack, uint32_t size_bytes)
{
    sched_task_t *t;
    uint32_t *sp;
    uint32_t words = size_bytes / 4u;

    if (sched.count >= CONFIG_SCHED_MAX_TASKS)
        return -1;

    t = &sched.tasks[sched.count];
    t->stack_base = stack;
    t->stack_words = words;
    t->state = SCHED_READY;
    t->wake_ms = 0;

#ifdef CONFIG_SCHED_STACK_CANARY
    {
        uint32_t i;
        for (i = 0; i < words; i++)
            stack[i] = SCHED_CANARY;
    }
#endif

    /* Build initial frame at top of stack.
     * Frame layout (growing down): r4..r15 (12 words), rets (1 word)
     * Total: 13 words. Stack top must be 8-byte aligned. */
    sp = stack + words;
    sp = (uint32_t *)((uint32_t)sp & ~7u);

    /* rets: sched_trampoline wraps fn to catch accidental return */
    *(--sp) = (uint32_t)sched_trampoline;
    /* r4 = fn pointer (trampoline reads it) */
    *(--sp) = 0; /* r15 */
    *(--sp) = 0; /* r14 */
    *(--sp) = 0; /* r13 */
    *(--sp) = 0; /* r12 */
    *(--sp) = 0; /* r11 */
    *(--sp) = 0; /* r10 */
    *(--sp) = 0; /* r9 */
    *(--sp) = 0; /* r8 */
    *(--sp) = 0; /* r7 */
    *(--sp) = 0; /* r6 */
    *(--sp) = 0; /* r5 */
    *(--sp) = (uint32_t)fn; /* r4 = task entry point */

    t->saved_sp = sp;
    sched.count++;
    return 0;
}

static void sched_trampoline(void)
{
    void (*fn)(void);
    /* r4 was loaded with fn pointer by the initial frame */
    __asm__ volatile("mov %0, r4" : "=r"(fn));
    fn();
    /* Task returned: log fault and idle forever */
    cfm1_log("sched: task returned!");
    for (;;)
        yield();
}

static void yield(void)
{
    uint32_t cur = sched.current;
    uint32_t next = cur;
    uint32_t checked = 0;
    sched_task_t *ct = &sched.tasks[cur];
    sched_task_t *nt;

    /* Find next ready task */
    do {
        next = (next + 1u) % sched.count;
        nt = &sched.tasks[next];
        if (nt->state == SCHED_SLEEPING) {
            if ((int32_t)(fm1_ms - nt->wake_ms) >= 0)
                nt->state = SCHED_READY;
        }
        if (nt->state == SCHED_READY)
            break;
        checked++;
    } while (checked < sched.count);

    if (next == cur && nt->state != SCHED_READY)
        return;

#ifdef CONFIG_SCHED_STACK_CANARY
    if (nt->stack_base[0] != SCHED_CANARY ||
        nt->stack_base[1] != SCHED_CANARY) {
        cfm1_log("sched: STACK OVERFLOW!");
        for (;;) ;
    }
#endif

    sched.current = next;
    sched_switch(&ct->saved_sp, nt->saved_sp);
}

static void sleep_ms(uint32_t ms)
{
    sched_task_t *t = &sched.tasks[sched.current];
    t->wake_ms = fm1_ms + ms;
    t->state = SCHED_SLEEPING;
    yield();
}

static void sched_run(void)
{
    sched_task_t *first;
    if (sched.count == 0)
        return;

    /* Start first task by switching from the boot context */
    sched.current = 0;
    first = &sched.tasks[0];
    first->state = SCHED_READY;

    /* Switch into first task. sched_run's stack becomes the
     * "boot" context: never returned to. */
    {
        uint32_t *dummy_sp;
        sched_switch(&dummy_sp, first->saved_sp);
    }
}

#endif /* CONFIG_SCHED */
```

**Step 6: Wire into unity build**

In `firmware/src/core_fm1.c`, add the include (near other subsystem includes):

```c
#ifdef CONFIG_SCHED
#include "sched/sched.c"
#endif
```

**Step 7: Run tests**

Run: `task test:unit -- tests/test_scheduler.py -v`
Expected: PASS

**Step 8: Commit**

```bash
git add firmware/src/sched/sched.c firmware/src/sched/ctx.S \
        kconf/sched.kconfig Kconfig tests/test_scheduler.py \
        firmware/src/core_fm1.c
git commit -m "feat(sched): add cooperative round-robin scheduler core

6-slot round-robin with stack canaries, sleep_ms, and yield.
Context switch saves rets + r4-r15 (fm1-base pattern).
Gated by CONFIG_SCHED."
```

---

### Task 9: WiFi Task + Cross-Task Command Queue

Extract WiFi init and polling into a scheduler task. Add a command queue for UI-to-WiFi communication.

**Files:**
- Create: `firmware/src/wifi/wifi_cmd.c` (command queue)
- Modify: `firmware/src/wifi/wifi_api.c` (add wifi_task, wifi_cmd_poll)
- Modify: `firmware/src/core_fm1.c` (create wifi task in sched_create)
- Test: `tests/wifi/test_api.py` (add task structure tests)

**Step 1: Write test for command queue structure**

Add to `tests/wifi/test_api.py`:

```python
def test_wifi_cmd_queue_exists():
    """WiFi module must define a command queue for cross-task communication."""
    cmd_path = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_cmd.c'
    assert cmd_path.exists(), 'wifi_cmd.c must exist'
    src = cmd_path.read_text()
    assert 'wifi_cmd_t' in src
    assert 'WIFI_CMD_CONNECT' in src
    assert 'wifi_cmd_enqueue' in src
    assert 'wifi_cmd_poll' in src


def test_wifi_task_entry_exists():
    """wifi_api.c must define wifi_task() as the scheduler entry point."""
    src = (ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_api.c').read_text()
    assert 'wifi_task(void)' in src
```

**Step 2: Implement wifi_cmd.c**

Create `firmware/src/wifi/wifi_cmd.c`:

```c
/* SPDX-License-Identifier: GPL-3.0-only */
/* Cross-task command queue: UI -> WiFi.
 * Commands are copied (not pointer-based) to avoid cross-yield aliasing. */

#ifdef CONFIG_SCHED

#define WIFI_CMD_QUEUE_SIZE 4

typedef enum {
    WIFI_CMD_NONE = 0,
    WIFI_CMD_CONNECT,
    WIFI_CMD_DISCONNECT,
    WIFI_CMD_SCAN,
} wifi_cmd_type_t;

typedef struct {
    wifi_cmd_type_t type;
    char ssid[WIFI_STA_SSID_MAX + 1];
    char password[64];
    void (*scan_cb)(const char *ssid, int rssi, int channel);
} wifi_cmd_t;

static struct {
    wifi_cmd_t cmds[WIFI_CMD_QUEUE_SIZE];
    uint32_t head;
    uint32_t tail;
} wifi_cmd_q;

static int wifi_cmd_enqueue(const wifi_cmd_t *cmd)
{
    uint32_t next = (wifi_cmd_q.head + 1u) % WIFI_CMD_QUEUE_SIZE;
    if (next == wifi_cmd_q.tail)
        return -1;
    wifi_cmd_q.cmds[wifi_cmd_q.head] = *cmd;
    wifi_cmd_q.head = next;
    return 0;
}

static int wifi_cmd_dequeue(wifi_cmd_t *out)
{
    if (wifi_cmd_q.tail == wifi_cmd_q.head)
        return -1;
    *out = wifi_cmd_q.cmds[wifi_cmd_q.tail];
    wifi_cmd_q.tail = (wifi_cmd_q.tail + 1u) % WIFI_CMD_QUEUE_SIZE;
    return 0;
}

static void wifi_cmd_poll(void)
{
    wifi_cmd_t cmd;
    while (wifi_cmd_dequeue(&cmd) == 0) {
        switch (cmd.type) {
        case WIFI_CMD_CONNECT:
            wifi_connect(cmd.ssid, cmd.password);
            break;
        case WIFI_CMD_DISCONNECT:
            wifi_disconnect();
            break;
        case WIFI_CMD_SCAN:
            wifi_scan(cmd.scan_cb);
            break;
        default:
            break;
        }
    }
}

#endif /* CONFIG_SCHED */
```

**Step 3: Add wifi_task to wifi_api.c**

Add at the end of `firmware/src/wifi/wifi_api.c`:

```c
#ifdef CONFIG_SCHED
static void wifi_task(void)
{
    sleep_ms(3000);
    cfm1_log("wifi: task init GO");
    wifi_rtos_init(); yield();
    wifi_rf_init(); yield();
    wifi_sdio_init(); yield();
    wifi_mac_init(); yield();
    wifi_sta_init(wifi_mac_addr);
    wifi_lwip_set_mac(wifi_sta.our_mac);
    wifi_lwip_init();
    weather_init();
    cfm1_log("wifi: task init done");
    for (;;) {
        wifi_cmd_poll();
        wifi_poll();
        yield();
    }
}
#endif
```

**Step 4: Wire wifi task creation in core_fm1.c**

In `fm1_main()`, replace the `#ifdef CONFIG_WIFI` main-loop WiFi block with task creation (inside `#ifdef CONFIG_SCHED`):

```c
#if defined(CONFIG_WIFI) && defined(CONFIG_SCHED)
    {
        static uint32_t wifi_stack[1024] __attribute__((section(".pool")));
        sched_create(wifi_task, wifi_stack, sizeof wifi_stack);
        cfm1_log("sched: wifi task created");
    }
#endif
```

**Step 5: Run tests**

Run: `task test:unit -- tests/wifi/test_api.py -v`
Expected: PASS

**Step 6: Commit**

```bash
git add firmware/src/wifi/wifi_cmd.c firmware/src/wifi/wifi_api.c \
        firmware/src/core_fm1.c tests/wifi/test_api.py
git commit -m "feat(wifi): add wifi_task and cross-task command queue

WiFi init and polling now run in a scheduler task with 4KB stack.
UI communicates via wifi_cmd_t queue (CONNECT, DISCONNECT, SCAN).
Command payloads are copied to avoid cross-yield pointer aliasing."
```

---

### Task 10: PBKDF2 Yielding

Insert yield points into the PBKDF2 inner loop so WiFi task stays responsive during PMK derivation (1-5 seconds blocking).

**Files:**
- Modify: `firmware/src/wifi/wifi_wpa.c:316-352` (wifi_pbkdf2_sha1)
- Test: `tests/wifi/test_wpa.py` (add yield-point test)

**Step 1: Write test verifying yield is called inside PBKDF2**

Add to `tests/wifi/test_wpa.py`:

```python
def test_pbkdf2_yields_during_inner_loop():
    """PBKDF2 inner loop must yield periodically for scheduler cooperation."""
    src = (ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_wpa.c').read_text()
    fn_start = src.index('wifi_pbkdf2_sha1(')
    fn_end = src.index('\n}', fn_start) + 2
    fn = src[fn_start:fn_end]

    # The inner 4096-iteration loop must contain a yield call
    assert 'yield()' in fn, \
        'wifi_pbkdf2_sha1 must yield inside the iteration loop'
```

**Step 2: Run test to verify it fails**

Run: `task test:unit -- tests/wifi/test_wpa.py::test_pbkdf2_yields_during_inner_loop -v`
Expected: FAIL

**Step 3: Add yield to PBKDF2 inner loop**

In `firmware/src/wifi/wifi_wpa.c`, modify `wifi_pbkdf2_sha1`:

```c
static void wifi_pbkdf2_sha1(const uint8_t *pass, uint32_t pass_len,
                             const uint8_t *salt, uint32_t salt_len,
                             uint32_t iterations, uint32_t dk_len,
                             uint8_t *out)
{
    uint32_t block = 1, pos = 0;

    while (pos < dk_len) {
        uint8_t u[WIFI_SHA1_DIGEST], t[WIFI_SHA1_DIGEST];
        uint8_t be[4];
        uint32_t j, k, copy;

        be[0] = (uint8_t)(block >> 24);
        be[1] = (uint8_t)(block >> 16);
        be[2] = (uint8_t)(block >> 8);
        be[3] = (uint8_t)(block);

        wifi_hmac_sha1_2(pass, pass_len, salt, salt_len, be, 4, u);
        for (k = 0; k < WIFI_SHA1_DIGEST; k++)
            t[k] = u[k];

        for (j = 1; j < iterations; j++) {
            wifi_hmac_sha1(pass, pass_len, u, WIFI_SHA1_DIGEST, u);
            for (k = 0; k < WIFI_SHA1_DIGEST; k++)
                t[k] ^= u[k];
#ifdef CONFIG_SCHED
            if ((j & 63u) == 0)
                yield();
#endif
        }

        copy = dk_len - pos;
        if (copy > WIFI_SHA1_DIGEST)
            copy = WIFI_SHA1_DIGEST;
        for (k = 0; k < copy; k++)
            out[pos + k] = t[k];
        pos += copy;
        block++;
    }
}
```

**Step 4: Run tests**

Run: `task test:unit -- tests/wifi/test_wpa.py -v`
Expected: PASS

**Step 5: Commit**

```bash
git add firmware/src/wifi/wifi_wpa.c tests/wifi/test_wpa.py
git commit -m "feat(wifi): yield every 64 HMAC iterations during PBKDF2

Inserts yield() into the PBKDF2 inner loop (4096 iterations x 2 blocks).
WiFi task remains responsive during the 1-5 second PMK derivation.
Guarded by CONFIG_SCHED so non-scheduler builds are unchanged."
```

---

### Task 11: Main Loop as System Task + sched_run()

Replace the `for(;;)` main loop with `sched_run()`. The existing main loop body becomes the "system/UI" task.

**Files:**
- Modify: `firmware/src/core_fm1.c:1162-1289` (main loop becomes ui_task)

**Step 1: Extract ui_task from main loop**

Add a `ui_task()` function containing the existing main loop body, with `yield()` calls:

```c
#ifdef CONFIG_SCHED
static void ui_task(void)
{
    for (;;) {
        uint32_t m = fm1_ms;
        fm1_wdt_feed();
#ifdef CONFIG_USB
        usb_retry(fm1_ms);
        /* ... existing USB/OTG/OTA/uboot code ... */
#endif
#ifdef CONFIG_INPUT
        /* uboot combo check ... */
#endif
#ifdef CONFIG_AUDIO
        /* master volume ADC ... */
#endif
#ifdef CONFIG_EDITOR
        editor_poll();
#endif
#if defined(CONFIG_DEBUG_DAP) && defined(CONFIG_USB)
        cfm1_debug_poll();
#endif
        /* ... settings, console, BLE sync ... */
#ifdef CONFIG_INPUT
        ui_input();
#endif
#ifdef CONFIG_SYS_MENU
        sys_settings_flush();
#endif
#ifdef CONFIG_BLE
        cfm1_ble_sync();
#endif
#if defined(CONFIG_UI_FELUCCA)
        fsh_draw();
#elif defined(CONFIG_UI_CORE)
        shell_draw();
#endif
        while (fm1_ms - m < 15u) {
#ifdef CONFIG_INPUT
            ui_input();
#endif
            yield();
        }
    }
}
#endif
```

Note the key change: the 15ms frame wait loop now calls `yield()` instead of busy-waiting.

**Step 2: Replace main loop with sched_run()**

At the end of `fm1_main()`, replace the `for(;;)` block:

```c
#ifdef CONFIG_SCHED
    {
        static uint32_t ui_stack[640] __attribute__((section(".pool")));
        sched_create(ui_task, ui_stack, sizeof ui_stack);
        cfm1_log("sched: ui task created");
    }

    cfm1_log("sched: starting");
    sched_run();
    /* never returns */
#else
    /* Original main loop for non-scheduler builds */
    for (;;) {
        /* ... existing code preserved ... */
    }
#endif
```

**Step 3: Run full test suite**

Run: `task test:unit`
Expected: all PASS

**Step 4: Run e2e tests**

Run: `task test:e2e`
Expected: boot and basic interactions work (with scheduler enabled in defconfig)

**Step 5: Commit**

```bash
git add firmware/src/core_fm1.c
git commit -m "feat(sched): replace main loop with sched_run + ui_task

The existing for(;;) main loop body is now the ui_task running in
the scheduler. WiFi runs in its own task. Non-scheduler builds
retain the original main loop via #else. The 15ms frame wait now
yields to other tasks instead of busy-waiting."
```

---

## Phase 2: Measure on Hardware

### Task 12: Instrumentation

Add diagnostic counters and stack high-water marks for hardware validation.

**Files:**
- Modify: `firmware/src/sched/sched.c` (add stack HWM measurement)
- Modify: `firmware/src/core_fm1.c` (add ISR timing diagnostics)

**Step 1: Add stack high-water mark to yield()**

In `sched.c`, add to `yield()` after the canary check:

```c
#ifdef CONFIG_SCHED_STACK_CANARY
    /* High-water mark: scan from bottom for first non-canary word */
    {
        uint32_t i;
        for (i = 2; i < nt->stack_words; i++) {
            if (nt->stack_base[i] != SCHED_CANARY)
                break;
        }
        /* i * 4 = bytes of stack remaining */
        uint32_t used = (nt->stack_words - i) * 4u;
        if (used > nt->hwm)
            nt->hwm = used;
    }
#endif
```

Add `uint32_t hwm;` to `sched_task_t`.

**Step 2: Add debug log command for stack report**

Add a function callable from the debug console:

```c
static void sched_log_stats(void)
{
    uint32_t i;
    for (i = 0; i < sched.count; i++) {
        sched_task_t *t = &sched.tasks[i];
        cfm1_log_int("sched: task ", (int32_t)i);
        cfm1_log_int("  stack hwm=", (int32_t)t->hwm);
        cfm1_log_int("  stack size=", (int32_t)(t->stack_words * 4u));
    }
}
```

**Step 3: Commit**

```bash
git add firmware/src/sched/sched.c
git commit -m "feat(sched): add stack high-water mark instrumentation"
```

---

## Phase 3: Migrate Remaining Subsystems (after Phase 2 validation)

### Task 13: USB/OTG Task

Extract USB/OTG/OTA polling from ui_task into a dedicated 2KB task.

### Task 14: Settings/Storage Task

Extract sys_settings_flush and flash writes into a dedicated 2KB task with flash-write policy (defer to audio-stopped moments).

### Task 15: Services Task

Extract editor_poll, console_poll, cfm1_debug_poll into a dedicated 2KB task.

> Phase 3 tasks are outlined but not fully spec'd: they follow the same pattern as Task 9 (extract from main loop, create task, wire in sched_create). Details depend on Phase 2 measurement results.

---

## Phase 4: Hardware Acceptance Tests (after Phase 3)

### Task 16: E2E Acceptance Test Suite

Create e2e tests for:
- RF init + UI responsiveness simultaneously
- WPA connect + cancel mid-PBKDF2
- Nested RX -> TX traffic under load
- DMA timeout recovery
- Flash writes during audio playback
- Maximum audio load + WiFi traffic simultaneously

> These require hardware or emulator; spec out after Phase 3.

---

## Summary: Execution Order

```
Phase 0 (parallel, no blockers):
  Task 1: WiFi buffer refactor (T03)
  Task 2: SDIO TX IRQ fix (T04)
  Task 3: Deferred work bug (T05)
  Task 4: PMK reorder (T06)
  Task 5: STA poll rate fix (T07)
  Task 6: Scheduler clock (T08)

Phase 1 (sequential, requires Phase 0 + T09):
  Task 7:  ABI investigation (T09) -- BLOCKER
  Task 8:  Scheduler core
  Task 9:  WiFi task + command queue
  Task 10: PBKDF2 yielding
  Task 11: Main loop -> sched_run + ui_task

Phase 2:
  Task 12: Instrumentation + hardware measurement

Phase 3:
  Task 13-15: Migrate USB, Storage, Services tasks

Phase 4:
  Task 16: Hardware acceptance tests
```

Total Phase 0: 6 tasks, can run in parallel
Total Phase 1: 5 tasks, sequential
Estimated: ~2-3 sessions for Phase 0+1, hardware required for Phase 2+
