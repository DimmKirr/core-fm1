/* SPDX-License-Identifier: GPL-3.0-only */
/* Clean-room RTOS shim from docs/hardware/wifi/SPECSHEET.md §8.
 * No vendor code, IR or disassembly.
 *
 * Bare-metal shim for the OS abstractions the MAC layer expects:
 * frame buffer pool, IRQ lock, deferred work queue, link state, heap.
 * Behavioral contracts from the hardware fact sheet; all names are
 * project-original. */

/* ---- configuration ---- */

#define WIFI_BUF_POOL_COUNT 16
#define WIFI_BUF_SIZE       1600
#define WIFI_BUF_HEADROOM   128
#define WIFI_DEFER_MAX      8
#define WIFI_HEAP_SIZE      4096

/* ---- wifi_buf: fixed-pool frame buffer allocator ---- */

struct wifi_buf {
    uint8_t  raw[WIFI_BUF_SIZE];
    uint8_t *head;
    uint8_t *data;
    uint8_t *tail;
    uint8_t *end;
    uint32_t len;
    uint16_t protocol;
    uint8_t  allocated;
    uint8_t  cloned;
};

static struct wifi_buf wifi_buf_pool[WIFI_BUF_POOL_COUNT];

static struct wifi_buf *wifi_buf_alloc(uint32_t size, uint32_t flags)
{
    uint32_t i;
    (void)size; (void)flags;
    for (i = 0; i < WIFI_BUF_POOL_COUNT; i++) {
        if (!wifi_buf_pool[i].allocated) {
            struct wifi_buf *b = &wifi_buf_pool[i];
            b->allocated = 1;
            b->cloned = 0;
            b->head = b->raw;
            b->end = b->raw + WIFI_BUF_SIZE;
            b->data = b->raw + WIFI_BUF_HEADROOM;
            b->tail = b->data;
            b->len = 0;
            b->protocol = 0;
            return b;
        }
    }
    return (struct wifi_buf *)0;
}

static struct wifi_buf *wifi_buf_alloc_rx(uint32_t size)
{
    return wifi_buf_alloc(size, 0);
}

static void wifi_buf_free(struct wifi_buf *b)
{
    if (b)
        b->allocated = 0;
}

static void wifi_buf_memcpy(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        dst[i] = src[i];
}

static struct wifi_buf *wifi_buf_clone(struct wifi_buf *b, uint32_t flags)
{
    struct wifi_buf *n;
    uint32_t off;
    (void)flags;
    if (!b)
        return (struct wifi_buf *)0;
    n = wifi_buf_alloc(b->len, 0);
    if (!n)
        return (struct wifi_buf *)0;
    off = (uint32_t)(b->data - b->head);
    n->data = n->head + off;
    n->tail = n->data + b->len;
    n->len = b->len;
    n->protocol = b->protocol;
    n->cloned = 1;
    wifi_buf_memcpy(n->data, b->data, b->len);
    return n;
}

static struct wifi_buf *wifi_buf_copy(struct wifi_buf *b, uint32_t flags)
{
    return wifi_buf_clone(b, flags);
}

static uint8_t *wifi_buf_push(struct wifi_buf *b, uint32_t len)
{
    b->data -= len;
    b->len += len;
    return b->data;
}

static uint8_t *wifi_buf_pull(struct wifi_buf *b, uint32_t len)
{
    if (len > b->len)
        return (uint8_t *)0;
    b->data += len;
    b->len -= len;
    return b->data;
}

static uint8_t *wifi_buf_put(struct wifi_buf *b, uint32_t len)
{
    uint8_t *p = b->tail;
    b->tail += len;
    b->len += len;
    return p;
}

static void wifi_buf_trim(struct wifi_buf *b, uint32_t len)
{
    if (len < b->len) {
        b->len = len;
        b->tail = b->data + len;
    }
}

static void wifi_buf_reserve(struct wifi_buf *b, uint32_t len)
{
    b->data += len;
    b->tail += len;
}

/* ---- wifi_lock: IRQ disable/enable on bare metal ---- */

typedef uint32_t wifi_lock_t;

static void wifi_lock_init(wifi_lock_t *lock)
{
    *lock = 0;
}

static uint32_t wifi_lock_irq(wifi_lock_t *lock)
{
    uint32_t flags = fm1_icfg() & 0x200u;
    fm1_irq_off();
    (void)lock;
    return flags;
}

static void wifi_unlock_irq(wifi_lock_t *lock, uint32_t flags)
{
    (void)lock;
    if (flags)
        fm1_irq_on();
}

/* ---- wifi_defer: deferred work queue, drained in wifi_poll ---- */

typedef void (*wifi_defer_fn_t)(unsigned long);

struct wifi_defer {
    wifi_defer_fn_t func;
    unsigned long data;
    uint8_t pending;
    uint8_t active;
};

static struct wifi_defer wifi_defers[WIFI_DEFER_MAX];
static uint32_t wifi_defer_count;

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

static void wifi_defer_schedule(struct wifi_defer *d)
{
    d->pending = 1;
}

static void wifi_defer_cancel(struct wifi_defer *d)
{
    d->active = 0;
    d->pending = 0;
}

static void wifi_defer_poll(void)
{
    uint32_t i;
    for (i = 0; i < wifi_defer_count; i++) {
        struct wifi_defer *d = &wifi_defers[i];
        if (d->pending && d->active) {
            d->pending = 0;
            d->func(d->data);
        }
    }
}

/* ---- link state and TX flow control ---- */

static uint8_t wifi_carrier_state;
static uint8_t wifi_queue_active;

static void wifi_link_up(void *dev)   { (void)dev; wifi_carrier_state = 1; }
static void wifi_link_down(void *dev) { (void)dev; wifi_carrier_state = 0; }
static int  wifi_link_ok(void *dev)   { (void)dev; return wifi_carrier_state; }

static void wifi_tx_start(void *dev) { (void)dev; wifi_queue_active = 1; }
static void wifi_tx_stop(void *dev)  { (void)dev; wifi_queue_active = 0; }
static void wifi_tx_wake(void *dev)  { (void)dev; wifi_queue_active = 1; }

static int wifi_rx_deliver(struct wifi_buf *b)
{
    (void)b;
    return 0;
}

/* ---- heap: bump allocator for small MAC-layer allocations ---- */

static uint8_t wifi_heap[WIFI_HEAP_SIZE] __attribute__((aligned(4)));
static uint32_t wifi_heap_ptr;

static void *wifi_heap_alloc(uint32_t size)
{
    uint32_t aligned = (size + 3u) & ~3u;
    uint32_t p;
    if (wifi_heap_ptr + aligned > WIFI_HEAP_SIZE)
        return (void *)0;
    p = wifi_heap_ptr;
    wifi_heap_ptr += aligned;
    return &wifi_heap[p];
}

static void wifi_heap_free(void *p)
{
    (void)p; /* bump allocator: no individual free */
}

/* ---- timer stubs ---- */

#define WIFI_TIMER_MAX 8

struct wifi_timer {
    void (*func)(unsigned long);
    unsigned long data;
    uint32_t expire_us;
    uint8_t active;
};

static struct wifi_timer wifi_timers[WIFI_TIMER_MAX];

static void wifi_timer_set(struct wifi_timer *t, uint32_t ms,
                           void (*func)(unsigned long), unsigned long data)
{
    t->func = func;
    t->data = data;
    t->expire_us = fm1_micros() + ms * 1000u;
    t->active = 1;
}

static void wifi_timer_cancel(struct wifi_timer *t)
{
    t->active = 0;
}

static void wifi_timer_poll(void)
{
    uint32_t i, now = fm1_micros();
    for (i = 0; i < WIFI_TIMER_MAX; i++) {
        struct wifi_timer *t = &wifi_timers[i];
        if (t->active && (int32_t)(now - t->expire_us) >= 0) {
            t->active = 0;
            t->func(t->data);
        }
    }
}

static void wifi_rtos_init(void)
{
    uint32_t i;
    for (i = 0; i < WIFI_BUF_POOL_COUNT; i++)
        wifi_buf_pool[i].allocated = 0;
    wifi_defer_count = 0;
    wifi_carrier_state = 0;
    wifi_queue_active = 0;
    wifi_heap_ptr = 0;
    for (i = 0; i < WIFI_TIMER_MAX; i++)
        wifi_timers[i].active = 0;
}
