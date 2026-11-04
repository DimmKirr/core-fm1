/* SPDX-License-Identifier: GPL-3.0-only */
/* Non-free WiFi integration: vendor binary libs + fm1-base shim.
 * Platform shim adapted from fm1-base's radio.c (MPL-2.0, Charles Vestal).
 * Provides shim_* functions for the separately compiled cooperative scheduler
 * (os.c, misc.c, systimer.c) and vendor .a libraries. */

/* shim.h is fm1-base's ble/shim/shim.h, added to include path by build.py */
#include "shim.h"

/* ---- shim_ms: monotonic milliseconds from TIMER4 ---- */

static volatile uint32_t nf_shim_depth;

uint32_t shim_ms(void)
{
    static uint32_t last, acc, ms;
    uint32_t now, r;
    fm1_irq_off();
    nf_shim_depth++;
    now = fm1_ticks();
    acc += now - last;
    last = now;
    if (acc >= 1000u * FM1_TICKS_PER_US) {
        ms += acc / (1000u * FM1_TICKS_PER_US);
        acc %= 1000u * FM1_TICKS_PER_US;
    }
    r = ms;
    if (--nf_shim_depth == 0)
        fm1_irq_on();
    return r;
}

/* ---- IRQ nesting for the cooperative scheduler ---- */

uint32_t shim_irq_save(void)
{
    fm1_irq_off();
    return nf_shim_depth++;
}

void shim_irq_restore(uint32_t f)
{
    (void)f;
    if (nf_shim_depth && --nf_shim_depth == 0)
        fm1_irq_on();
}

uint32_t shim_irq_depth(void) { return nf_shim_depth; }

uint32_t shim_irq_hold(void)
{
    fm1_irq_off();
    return nf_shim_depth;
}

void shim_irq_resume(uint32_t depth)
{
    nf_shim_depth = depth;
    if (!depth)
        fm1_irq_on();
}

int shim_in_irq(void) { return nf_shim_depth != 0; }
volatile uint32_t shim_isr_blocks;

/* ---- hardware platform ---- */

void shim_wdt(void) { fm1_wdt_feed(); }
void shim_reboot(void) { fm1_reboot(); }

void shim_delay_us(uint32_t us)
{
    uint32_t start = FM1_T4_CNT, span = us * FM1_TICKS_PER_US;
    while ((uint32_t)(FM1_T4_CNT - start) < span)
        ;
}

void shim_request_irq(int index, int prio, void (*handler)(void))
{
    uint32_t f = irq_save();
    fm1_guard_unlock_top();
    fm1_irq_attach((uint32_t)index, handler,
                   (uint32_t)(prio + 5 > 7 ? 7 : prio + 5));
    fm1_guard_lock_top();
    irq_restore(f);
}

/* ---- logging ---- */

void shim_log(const char *s)
{
#ifdef CONFIG_DEBUG_LOG
    cfm1_log(s);
#else
    (void)s;
#endif
}

/* ---- BLE event handler (not used in WiFi-only mode) ---- */

static volatile uint8_t nf_bt_ready;

void shim_bt_event(int from, const void *event, int len)
{
    (void)len;
    struct { uint8_t event; uint8_t args[7]; uint32_t value; } const *e = event;
    if (from == 3 && e && e->event == 3) {
        nf_bt_ready = 1;
        shim_log("nf: BT ready");
    }
}

/* ---- storage ---- */

int shim_store_load(void *dst, uint32_t max)
{
    (void)dst; (void)max;
    return -1;
}

int shim_store_save(const void *src, uint32_t len)
{
    (void)src; (void)len;
    return -1;
}

/* ---- idle hook ---- */

extern void shim_timers_post(void);

void shim_idle(void)
{
    static uint8_t in;
    fm1_wdt_feed();
    if (!in) {
        in = 1;
        shim_timers_post();
        in = 0;
    }
}

/* ---- platform callbacks for wifi.c ---- */

#define OBJ_BLE  1
#define OBJ_WIFI 2

int plat_store_load(uint32_t obj, void *dst, uint32_t max)
{
    (void)obj; (void)dst; (void)max;
    return -1;
}

int plat_store_save(uint32_t obj, const void *src, uint32_t len)
{
    (void)obj; (void)src; (void)len;
    return -1;
}

const uint8_t *plat_chip_uid(void)
{
    static uint8_t uid[16];
    return uid;
}

uint32_t plat_lock(void) { return irq_save(); }
void plat_unlock(uint32_t f) { irq_restore(f); }

int plat_net_task(void (*fn)(void *), void *arg, const char *name)
{
    return task_create(fn, arg, name);
}

/* ---- vendor BLE API (from btctrler.a, btstack.a) ---- */

extern void btstack_init(void);
extern void bt_ble_init(void);
extern void lmp_set_sniff_disable(void);
extern void bt_max_pwr_set(uint8_t pwr, uint8_t pg_pwr,
                           uint8_t iq_pwr, uint8_t ble_pwr);

/* ---- fm1-base wifi.c station logic ---- */

#include "nf_wifi_station.c"

/* ---- sys_timer task (needed by vendor WiFi libs) ---- */

extern int os_taskq_pend(const char *fmt, int *argv, int argc);

static void nf_sys_timer_task(void *p)
{
    int m[8];
    (void)p;
    for (;;)
        os_taskq_pend("taskq", m, 8);
}

/* ---- init and poll ---- */

static uint8_t nf_wifi_started;

static void nf_wifi_init(void)
{
    shim_os_init();
    /* TODO: set stack guard floor to shim_stacks_lo() when HAL supports it */
    task_create(nf_sys_timer_task, 0, "sys_timer");

    shim_log("nf: starting BT for radio");
    lmp_set_sniff_disable();
    bt_max_pwr_set(8, 6, 6, 11);
    btstack_init();
}

static void nf_wifi_poll(void)
{
    shim_timers();
    shim_run();

    if (nf_bt_ready && !nf_wifi_started) {
        nf_wifi_started = 1;
        bt_ble_init();
        shim_log("nf: starting WiFi");
        plat_wifi_on(1);
    }
}
