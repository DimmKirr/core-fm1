/* SPDX-License-Identifier: GPL-3.0-only */
/* WiFi internal SDIO bus driver: on-chip interconnect between CPU and WiFi MAC/BBP.
 * Reverse-engineered from .scratch/examples/wifi/ir/wl_rf_common/wl_sdio*.c.ll.
 * Protocol: .scratch/examples/wifi/SDIO_PROTOCOL.md. */

#include "../hal/fm1_wifi_sdio.h"
#include "../hal/fm1_wifi_rf.h"

/* ---- bus state ---- */

static uint8_t wifi_sdio_initialized;

/* ---- host controller state (from wl_sdio_host.c.ll) ---- */

struct sdio_host_state {
    const char *name;
    uint32_t flags;
    spinlock_t mutex[3];
};

static struct sdio_host_state sdio_host;

/* ---- delay helpers ---- */

static void wf_udelay(uint32_t us)
{
    volatile uint32_t n = us * 10;
    while (n--) ;
}

static void wf_mdelay(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++)
        wf_udelay(1000);
}

/* ---- clock/reset init (from wl_sdio.c.ll wf_clk_rst_init) ---- */

static void wf_clk_rst_init(void)
{
    /* clear clock gate bits 14-15 in system register 0x10010 */
    FM1_RF_REG(0x10010u) &= ~((1u << 14) | (1u << 15));

    /* zero all clock registers */
    FM1_WF_CLK0 = 0;
    FM1_WF_CLK1 = 0;
    FM1_WF_CLK2 = 0;
    FM1_WF_CLK3 = 0;
    FM1_WF_CLK4 = 0;

    /* progressive clock enable sequence with delays */
    FM1_WF_CLK0 = (1u << 8) | (1u << 6) | (1u << 0); /* 256 + 64 + 1 = 0x141 */
    wf_udelay(10);

    FM1_WF_CLK0 |= (1u << 2); /* bit 2: +4 */
    wf_udelay(10);

    FM1_WF_CLK0 |= (1u << 3); /* bit 3: +8 */
    wf_udelay(10);

    FM1_WF_CLK0 |= (1u << 1); /* bit 1: +2 */
    wf_udelay(50);

    /* configure reset register */
    FM1_WF_RST = (1u << 0) | (1u << 1);
}

/* ---- host controller init (from wl_sdio_host.c.ll sdio_host_init) ---- */

static void sdio_host_init(void)
{
    if (!sdio_host.name) {
        sdio_host.name = "wl";
        sdio_host.flags = 0;
        spin_lock_init(&sdio_host.mutex[0]);
        spin_lock_init(&sdio_host.mutex[1]);
        spin_lock_init(&sdio_host.mutex[2]);
    }
    sdio_host.flags |= (1u << 3); /* IRQ flag */
}

/* ---- host mutex (from wl_sdio_host.c.ll) ---- */

static void mmc_claim_host(void)
{
    spin_lock_irqsave(&sdio_host.mutex[2]);
}

static void mmc_release_host(void)
{
    spin_unlock_irqrestore(&sdio_host.mutex[2], 0);
}

/* ---- IRQ flag (from wl_sdio_host.c.ll sdio_cap_irq) ---- */

static int sdio_cap_irq(void)
{
    return (sdio_host.flags & (1u << 3)) != 0;
}

/* ---- bus enumeration command helper ---- */

static int sdio_host_send_command(uint32_t arg, uint32_t cmd)
{
    /* internal SDIO: write arg/cmd to MAC control registers */
    FM1_WF_MAC_CFG0 = arg;
    FM1_WF_MAC_CFG1 = cmd;

    /* poll for command completion (bit 24) */
    volatile uint32_t timeout = 100000;
    while (!(FM1_WF_MAC_CFG1 & (1u << 24)) && --timeout)
        ;
    return timeout ? 0 : -1;
}

/* ---- SDIO enumeration (from wl_sdio.c.ll wl30_sdio_enum) ---- */

static int wl30_sdio_enum(void)
{
    int ret;

    /* CMD0-like reset, CMD5 voltage negotiation, CMD3 address assignment */
    ret = sdio_host_send_command(520093696u,  17669u);
    if (ret < 0) return ret;
    ret = sdio_host_send_command(520093696u,  17669u);
    if (ret < 0) return ret;
    ret = sdio_host_send_command(0u,          17157u);
    if (ret < 0) return ret;
    ret = sdio_host_send_command(65536u,      18177u);
    if (ret < 0) return ret;

    /* CMD52-style register configuration via send_command path */
    ret = sdio_host_send_command(0x88000BFFu, 29701u); /* -2013264641 = 0x88000BFF */
    if (ret < 0) return ret;
    ret = sdio_host_send_command(0x88000F02u, 29701u); /* -2013262334 = 0x88000F02 */
    if (ret < 0) return ret;
    ret = sdio_host_send_command(0x88000C03u, 29701u); /* -2013263869 = 0x88000C03 */
    if (ret < 0) return ret;

    /* I/O block size configuration */
    ret = sdio_host_send_command(0x98002200u, 29701u); /* -1744822272 = 0x98002200 */
    if (ret < 0) return ret;
    ret = sdio_host_send_command(0x98002400u, 29701u); /* -1744821760 = 0x98002400 */
    if (ret < 0) return ret;
    ret = sdio_host_send_command(0x98002600u, 29701u); /* -1744821248 = 0x98002600 */
    if (ret < 0) return ret;
    ret = sdio_host_send_command(0x98002800u, 29701u); /* -1744820736 = 0x98002800 */
    if (ret < 0) return ret;

    return 0;
}

/* ---- DMA init (from wl_sdio.c.ll wdma_init) ---- */

static void wdma_init(void)
{
    uint32_t cfg1 = FM1_WF_MAC_CFG1;
    cfg1 |= (1u << 8) | (1u << 9);   /* enable DMA TX/RX channels */
    FM1_WF_MAC_CFG1 = cfg1;

    uint32_t cfg0 = FM1_WF_MAC_CFG0;
    cfg0 |= (1u << 0) | (1u << 4);   /* DMA engine enable + burst mode */
    FM1_WF_MAC_CFG0 = cfg0;
}

/* ---- CMD52: single-byte read/write via direct MAC register access ---- */

static uint8_t wl30_mmc_io_rw_direct_read(uint8_t fn, uint32_t addr)
{
    (void)fn;
    return (uint8_t)FM1_WF_MAC_REG(addr);
}

static void wl30_mmc_io_rw_direct_write(uint8_t fn, uint32_t addr, uint8_t val)
{
    (void)fn;
    FM1_WF_MAC_REG(addr) = val;
}

/* ---- CMD53: block DMA transfer (from wl_sdio.c.ll wl30_mmc_io_rw_extended) ---- */

static int wl30_mmc_io_rw_extended(uint8_t fn, uint32_t addr, uint8_t *buf, uint32_t len, int write)
{
    (void)fn; (void)addr;
    if (!buf || len == 0) return -1;

    mmc_claim_host();

    if (write) {
        /* TX DMA: set bit 10 of MAC_CFG1 to start TX */
        FM1_WF_MAC_CFG1 |= (1u << 10);

        /* write source address and length */
        FM1_WF_MAC_10 = (uint32_t)(uintptr_t)buf;
        FM1_WF_MAC_14 = (0u << 31) | len;  /* incr=0, length */

        /* poll bit 20 for TX completion */
        volatile uint32_t timeout = 1000000;
        while (!(FM1_WF_MAC_CFG1 & (1u << 20)) && --timeout)
            ;
        FM1_WF_MAC_CFG1 &= ~(1u << 10); /* clear TX start */

        mmc_release_host();
        return timeout ? 0 : -1;
    }

    /* RX DMA: set bit 11 of MAC_CFG1 to start RX */
    FM1_WF_MAC_CFG1 |= (1u << 11);

    /* write destination address and length */
    FM1_WF_MAC_18 = (uint32_t)(uintptr_t)buf;
    FM1_WF_MAC_1C = len;

    /* poll bit 21 for RX header completion */
    volatile uint32_t timeout = 1000000;
    while (!(FM1_WF_MAC_CFG1 & (1u << 21)) && --timeout)
        ;

    if (!timeout) {
        FM1_WF_MAC_CFG1 &= ~(1u << 11);
        mmc_release_host();
        return -1;
    }

    /* read byte-swapped length from MAC_1C for actual payload */
    uint32_t rx_len = FM1_WF_MAC_1C;
    uint32_t actual = ((rx_len >> 8) & 0xFF) | ((rx_len & 0xFF) << 8);
    if (actual > len) actual = len;

    /* second DMA burst for actual data (reuse same buffer) */
    FM1_WF_MAC_18 = (uint32_t)(uintptr_t)buf;
    FM1_WF_MAC_1C = actual;

    timeout = 1000000;
    while (!(FM1_WF_MAC_CFG1 & (1u << 21)) && --timeout)
        ;
    FM1_WF_MAC_CFG1 &= ~(1u << 11); /* clear RX start */

    mmc_release_host();
    return timeout ? (int)actual : -1;
}

/* ---- PA enable (from wl_sdio.c.ll wifi_hw_pa_enable) ---- */

static void wifi_hw_pa_enable(void)
{
    /* GPIO port A: clear bits 1-2 of data/pull-up, then set for output */
    FM1_WF_SDIO_DATA  &= ~((1u << 1) | (1u << 2)); /* 0x50008: clear PA data */
    FM1_WF_SDIO_STATUS |= (1u << 1) | (1u << 2);   /* 0x5000C: enable pull-up */
    FM1_WF_SDIO_BASE  &= ~((1u << 1) | (1u << 2)); /* 0x50000: set as output */

    /* control register: enable PA pins */
    FM1_WF_CTL_08 |= (1u << 0) | (1u << 1) | (1u << 8);
}

/* ---- TX/RX data bridge ---- */

static int wifi_sdio_tx(uint8_t *data, uint32_t len)
{
    return wl30_mmc_io_rw_extended(1, 0, data, len, 1);
}

static int wifi_sdio_rx(uint8_t *buf, uint32_t len)
{
    return wl30_mmc_io_rw_extended(1, 0, buf, len, 0);
}

/* ---- IRQ handler (from wl_sdio.c.ll sdio_irq_thread concept) ---- */

static volatile uint8_t wifi_sdio_irq_pending;

static void wifi_sdio_irq_handler(void)
{
    wifi_sdio_irq_pending = 1;
}

static void wifi_sdio_poll_irq(void)
{
    if (!wifi_sdio_irq_pending) return;
    wifi_sdio_irq_pending = 0;

    if (!sdio_cap_irq()) return;

    /* RX: read frame into buffer */
    static uint8_t rx_buf[1600];
    int n = wifi_sdio_rx(rx_buf, sizeof(rx_buf));
    if (n > 0) {
        wifi_mac_rx(rx_buf, (uint32_t)n);
    }
}

/* ---- init ---- */

static int wifi_sdio_init(void)
{
    wf_clk_rst_init();
    sdio_host_init();

    int ret = wl30_sdio_enum();
    if (ret < 0) return ret;

    wdma_init();
    wifi_hw_pa_enable();
    wifi_sdio_initialized = 1;
    return 0;
}
