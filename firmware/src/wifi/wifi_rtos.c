/* SPDX-License-Identifier: GPL-3.0-only */
/* WiFi RTOS shim: Linux kernel API emulation on bare-metal.
 * sk_buff pool, spinlock (IRQ disable), tasklet (deferred work), timer.
 * Clean-room from .scratch/examples/wifi/RTOS_SHIM_MAP.md. */

/* ---- sk_buff pool ---- */

#define WIFI_SKB_POOL_SIZE  32
#define WIFI_SKB_BUF_SIZE   1600

struct sk_buff {
    uint8_t  data_buf[WIFI_SKB_BUF_SIZE];
    uint8_t *data;
    uint8_t *head;
    uint8_t *tail;
    uint8_t *end;
    uint32_t len;
    uint8_t  in_use;
};

static struct sk_buff skb_pool[WIFI_SKB_POOL_SIZE];

static struct sk_buff *alloc_skb(uint32_t size, uint32_t flags)
{
    (void)flags;
    for (int i = 0; i < WIFI_SKB_POOL_SIZE; i++) {
        if (!skb_pool[i].in_use) {
            struct sk_buff *skb = &skb_pool[i];
            skb->in_use = 1;
            skb->head = skb->data_buf;
            skb->data = skb->data_buf + 128; /* headroom */
            skb->tail = skb->data;
            skb->end  = skb->data_buf + WIFI_SKB_BUF_SIZE;
            skb->len  = 0;
            return skb;
        }
    }
    return (struct sk_buff *)0;
}

static struct sk_buff *dev_alloc_skb(uint32_t size)
{
    return alloc_skb(size, 0);
}

static void kfree_skb(struct sk_buff *skb)
{
    if (skb) skb->in_use = 0;
}

static struct sk_buff *skb_clone(struct sk_buff *skb, uint32_t flags)
{
    (void)flags;
    struct sk_buff *n = alloc_skb(skb->len, 0);
    if (!n) return (struct sk_buff *)0;
    uint32_t off = (uint32_t)(skb->data - skb->head);
    n->data = n->head + off;
    n->len = skb->len;
    n->tail = n->data + skb->len;
    for (uint32_t i = 0; i < skb->len; i++)
        n->data[i] = skb->data[i];
    return n;
}

static uint8_t *skb_push(struct sk_buff *skb, uint32_t len)
{
    skb->data -= len;
    skb->len  += len;
    return skb->data;
}

static void skb_trim(struct sk_buff *skb, uint32_t len)
{
    if (len < skb->len) {
        skb->len  = len;
        skb->tail = skb->data + len;
    }
}

static uint8_t *skb_put(struct sk_buff *skb, uint32_t len)
{
    uint8_t *p = skb->tail;
    skb->tail += len;
    skb->len  += len;
    return p;
}

static void skb_reserve(struct sk_buff *skb, uint32_t len)
{
    skb->data += len;
    skb->tail += len;
}

/* ---- spinlock (IRQ disable/enable) ---- */

typedef uint32_t spinlock_t;

static void spin_lock_init(spinlock_t *lock)
{
    *lock = 0;
}

static uint32_t spin_lock_irqsave(spinlock_t *lock)
{
    (void)lock;
    /* bare-metal: disable interrupts, return previous state */
    /* TODO: wire to fm1_irq.h cli/sti when building on target */
    return 0;
}

static void spin_unlock_irqrestore(spinlock_t *lock, uint32_t flags)
{
    (void)lock; (void)flags;
    /* bare-metal: restore interrupt state */
}

static void spin_lock_free(spinlock_t *lock)
{
    (void)lock;
}

/* ---- tasklet (deferred work) ---- */

#define WIFI_TASKLET_MAX    16

typedef void (*tasklet_fn)(unsigned long);

struct tasklet_struct {
    tasklet_fn func;
    unsigned long data;
    uint8_t pending;
    uint8_t active;
};

static struct tasklet_struct wifi_tasklets[WIFI_TASKLET_MAX];
static int wifi_tasklet_count;

static void tasklet_init(struct tasklet_struct *t, tasklet_fn func, unsigned long data)
{
    t->func = func;
    t->data = data;
    t->pending = 0;
    t->active = 1;
    if (wifi_tasklet_count < WIFI_TASKLET_MAX)
        wifi_tasklets[wifi_tasklet_count++] = *t;
}

static void tasklet_hi_schedule(struct tasklet_struct *t)
{
    t->pending = 1;
}

static void tasklet_kill(struct tasklet_struct *t)
{
    t->active = 0;
    t->pending = 0;
}

static void wifi_tasklet_poll(void)
{
    for (int i = 0; i < wifi_tasklet_count; i++) {
        struct tasklet_struct *t = &wifi_tasklets[i];
        if (t->pending && t->active) {
            t->pending = 0;
            t->func(t->data);
        }
    }
}

/* ---- netif carrier state ---- */

static uint8_t wifi_carrier_on_flag;

static void netif_carrier_on(void *dev)
{
    (void)dev;
    wifi_carrier_on_flag = 1;
}

static void netif_carrier_off(void *dev)
{
    (void)dev;
    wifi_carrier_on_flag = 0;
}

static int netif_carrier_ok(void *dev)
{
    (void)dev;
    return wifi_carrier_on_flag;
}

static void netif_start_queue(void *dev) { (void)dev; }
static void netif_stop_queue(void *dev)  { (void)dev; }
static void netif_wake_queue(void *dev)  { (void)dev; }

static int netif_rx(struct sk_buff *skb)
{
    /* forward to lwIP netif_input: wired in wifi_lwip.c */
    (void)skb;
    return 0;
}

/* ---- timer ---- */

#define WIFI_TIMER_MAX 16

struct wifi_timer {
    void (*func)(unsigned long);
    unsigned long data;
    uint32_t expires_ms;
    uint8_t active;
};

static struct wifi_timer wifi_timers[WIFI_TIMER_MAX];

static void wifi_timer_add(struct wifi_timer *t, uint32_t ms, void (*func)(unsigned long), unsigned long data)
{
    t->func = func;
    t->data = data;
    t->expires_ms = ms; /* caller must add current time */
    t->active = 1;
}

static void wifi_timer_del(struct wifi_timer *t)
{
    t->active = 0;
}

/* ---- memory ---- */

static void *vmalloc(uint32_t size)
{
    /* bare-metal: fall back to sk_buff pool or static allocation */
    (void)size;
    return (void *)0; /* TODO: wire to pool allocator */
}

static void kfree(void *p)
{
    (void)p;
}

/* ---- wait ---- */

static int wait_event_interruptible(void *wq, int condition)
{
    (void)wq;
    /* bare-metal: poll until condition is true */
    return condition ? 0 : -1;
}

static void wake_up(void *wq)
{
    (void)wq;
}
