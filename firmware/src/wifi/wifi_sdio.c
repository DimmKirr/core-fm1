/* SPDX-License-Identifier: GPL-3.0-only */
/* Clean-room WiFi bus driver for AC791N (WL82) direct MMIO + DMA.
 *
 * From docs/hardware/wifi/SPECSHEET.md §4.2, §7.1, §7.2, §7.4 and the
 * RT2800 register layout (Linux GPL-2.0+, IP ancestor [IP:RT2x00]).
 *
 * Hardware facts used:
 *   - Clock/reset:  0x14040-0x1405C  [M:hw v15-v17]
 *   - Bus gate:     0x14000 (WL_CTL0) [M:hw v18]
 *   - MAC registers: 0x30000 + RT2800 offset [M:hw v19-v23c]
 *   - DMA registers: 0x40000 + RT2800 offset [M:hw v21-v23c]
 *
 * No vendor code, IR or disassembly. No SDIO bus (0x50000 = GPIO). */

/* ---- register definitions ---- */

/* DMA base: 0x40000 + RT2800 offset [M:hw v23c] */
#define WIFI_DMA_BASE          0x40000u

#define WIFI_INT_SOURCE_CSR    (*(volatile uint32_t *)(WIFI_DMA_BASE + 0x200u))
#define WIFI_INT_MASK_CSR      (*(volatile uint32_t *)(WIFI_DMA_BASE + 0x204u))
#define WIFI_WPDMA_GLO_CFG     (*(volatile uint32_t *)(WIFI_DMA_BASE + 0x208u))
#define WIFI_WPDMA_RST_IDX     (*(volatile uint32_t *)(WIFI_DMA_BASE + 0x20Cu))
#define WIFI_DELAY_INT_CFG     (*(volatile uint32_t *)(WIFI_DMA_BASE + 0x210u))

#define WIFI_TX_BASE_PTR0      (*(volatile uint32_t *)(WIFI_DMA_BASE + 0x230u))
#define WIFI_TX_MAX_CNT0       (*(volatile uint32_t *)(WIFI_DMA_BASE + 0x234u))
#define WIFI_TX_CTX_IDX0       (*(volatile uint32_t *)(WIFI_DMA_BASE + 0x238u))
#define WIFI_TX_DTX_IDX0       (*(volatile uint32_t *)(WIFI_DMA_BASE + 0x23Cu))

#define WIFI_RX_BASE_PTR       (*(volatile uint32_t *)(WIFI_DMA_BASE + 0x290u))
#define WIFI_RX_MAX_CNT        (*(volatile uint32_t *)(WIFI_DMA_BASE + 0x294u))
#define WIFI_RX_CRX_IDX        (*(volatile uint32_t *)(WIFI_DMA_BASE + 0x298u))
#define WIFI_RX_DRX_IDX        (*(volatile uint32_t *)(WIFI_DMA_BASE + 0x29Cu))

/* MAC base: 0x30000 + RT2800 offset [M:hw v18-v23c] */
#define WIFI_MAC_BASE          0x30000u

#define WIFI_MAC_SYS_CTRL      (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1004u))
#define WIFI_MAC_ADDR_DW0      (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x0300u))
#define WIFI_MAC_ADDR_DW1      (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x0304u))

/* WPDMA_GLO_CFG bit fields [IP:RT2x00] */
#define WPDMA_TX_DMA_EN        (1u << 0)
#define WPDMA_TX_DMA_BUSY      (1u << 1)
#define WPDMA_RX_DMA_EN        (1u << 2)
#define WPDMA_RX_DMA_BUSY      (1u << 3)
#define WPDMA_BIG_ENDIAN       (1u << 7)

/* INT_SOURCE_CSR / INT_MASK_CSR bit fields [IP:RT2x00] */
#define INT_TX_DONE0           (1u << 0)
#define INT_TX_DONE1           (1u << 1)
#define INT_RX_DONE            (1u << 2)

/* MAC_SYS_CTRL bit fields [IP:RT2x00] */
#define MAC_TX_EN              (1u << 2)
#define MAC_RX_EN              (1u << 3)

/* ---- DMA descriptor [IP:RT2x00] ---- */

/* RT2800 TXD/RXD format: 4 x 32-bit words.
 * Word 0: SD_PTR0 (buffer physical address)
 * Word 1: SDL0 [15:0] (buffer length), LAST_SEC0 [16], BURST [17], SDL1 [31:18]
 * Word 2: SD_PTR1 (second scatter buffer, 0 if unused)
 * Word 3: (reserved), DMA_DONE [17]
 */

struct wifi_txd {
    uint32_t sd_ptr0;
    uint32_t sd_len0;
    uint32_t sd_ptr1;
    uint32_t flags;
};

#define TXD_SDL0_MASK    0x0000FFFFu
#define TXD_LAST_SEC0    (1u << 16)
#define TXD_BURST        (1u << 17)
#define TXD_DMA_DONE     (1u << 17)  /* in word 3 */

struct wifi_rxd {
    uint32_t sd_ptr0;
    uint32_t sd_len0;
    uint32_t sd_ptr1;
    uint32_t flags;
};

#define RXD_SDL0_MASK    0x0000FFFFu
#define RXD_DMA_DONE     (1u << 17)  /* in word 3 */

/* ---- ring buffer configuration ---- */

#define WIFI_TX_RING_SIZE   1u
#define WIFI_RX_RING_SIZE   1u
#define WIFI_RX_BUF_SIZE    1600u
#define WIFI_TX_MAX_FRAME   1600u

/* ---- static allocations ---- */

static struct wifi_txd wifi_tx_ring[WIFI_TX_RING_SIZE] __attribute__((aligned(16)));
static struct wifi_rxd wifi_rx_ring[WIFI_RX_RING_SIZE] __attribute__((aligned(16)));

static uint8_t wifi_rx_buf[WIFI_RX_BUF_SIZE] __attribute__((aligned(4)));

static struct {
    uint8_t  initialized;
    uint8_t  dma_active;
    uint8_t  error_count;
    uint16_t block_size;       /* kept for API compat, unused in MMIO mode */
    uint32_t rx_cpu_idx;       /* next RX descriptor to check */
} wifi_sdio_state;

/* ---- bus claim/release (bare-metal: IRQ disable/enable) ---- */

static uint32_t wifi_sdio_saved_ie;

static void wifi_sdio_claim(void)
{
    wifi_sdio_saved_ie = fm1_icfg() & 0x200u;
    fm1_irq_off();
}

static void wifi_sdio_release(void)
{
    if (wifi_sdio_saved_ie)
        fm1_irq_on();
}

/* ---- DMA init ---- */

static void wifi_dma_init(void)
{
    uint32_t i;

    cfm1_log("wifi: dma step1 glo_cfg");
    /* Disable DMA before configuring rings */
    WIFI_WPDMA_GLO_CFG &= ~(WPDMA_TX_DMA_EN | WPDMA_RX_DMA_EN);
    fm1_delay_us(10);

    cfm1_log("wifi: dma step2 idle_wait");
    /* Wait for DMA to go idle */
    for (i = 0; i < 1000u; i++) {
        if (!(WIFI_WPDMA_GLO_CFG & (WPDMA_TX_DMA_BUSY | WPDMA_RX_DMA_BUSY)))
            break;
        fm1_delay_us(10);
    }

    cfm1_log("wifi: dma step3 rings");
    /* Initialize TX ring: no pending frames */
    for (i = 0; i < WIFI_TX_RING_SIZE; i++) {
        wifi_tx_ring[i].sd_ptr0 = 0;
        wifi_tx_ring[i].sd_len0 = 0;
        wifi_tx_ring[i].sd_ptr1 = 0;
        wifi_tx_ring[i].flags   = 0;
    }

    /* Initialize RX ring: single buffer */
    wifi_rx_ring[0].sd_ptr0 = (uint32_t)wifi_rx_buf;
    wifi_rx_ring[0].sd_len0 = WIFI_RX_BUF_SIZE & RXD_SDL0_MASK;
    wifi_rx_ring[0].sd_ptr1 = 0;
    wifi_rx_ring[0].flags   = 0;

    cfm1_log("wifi: dma step4 ptrs");
    /* Program ring base pointers and counts BEFORE resetting indices.
     * RST_IDX can trigger the DMA engine to look at ring base pointers;
     * if they are NULL, it DMA-reads from address 0 and corrupts RAM. */
    WIFI_TX_BASE_PTR0 = (uint32_t)wifi_tx_ring;
    WIFI_TX_MAX_CNT0  = WIFI_TX_RING_SIZE;

    WIFI_RX_BASE_PTR  = (uint32_t)wifi_rx_ring;
    WIFI_RX_MAX_CNT   = WIFI_RX_RING_SIZE;

    cfm1_log("wifi: dma step5 idx");
    /* Set ring indices directly. Do NOT write WPDMA_RST_IDX=0xFFFFFFFF:
     * on AC791N it triggers immediate DMA activity that corrupts RAM,
     * even with DMA disabled in GLO_CFG. Linux rt2800 sets indices
     * directly via CTX_IDX/CRX_IDX without touching RST_IDX. */
    WIFI_TX_CTX_IDX0  = 0;
    WIFI_RX_CRX_IDX   = 0;
    wifi_sdio_state.rx_cpu_idx = 0;

    cfm1_log("wifi: dma step6 int_mask=0");
    WIFI_INT_MASK_CSR = 0;
    /* Do NOT write INT_SOURCE_CSR=0xFFFFFFFF: on AC791N it crashes the
     * device, same as WPDMA_RST_IDX.  With mask=0 and DMA off there are
     * no pending interrupts to clear anyway. */

    /* DMA activation DISABLED for bringup -- enable once init is stable */
    /* WIFI_WPDMA_GLO_CFG |= (WPDMA_TX_DMA_EN | WPDMA_RX_DMA_EN); */
    fm1_delay_us(10);

    cfm1_log("wifi: dma init done (passive)");
}

/* ---- SDIO-compatible init (called from wifi_api.c) ---- */

static void wifi_sdio_init(void)
{
    uint32_t i;
    cfm1_log("wifi: sdio init enter");

    for (i = 0; i < sizeof(wifi_sdio_state); i++)
        ((uint8_t *)&wifi_sdio_state)[i] = 0;

    cfm1_log("wifi: sdio clk");
    /* Step 1: WiFi clock and reset [M:hw v15-v17] */
    FM1_WL_CLK0 |= 0x01u;
    FM1_WL_CLK4 |= 0x01u;
    fm1_delay_us(50);
    cfm1_log("wifi: sdio rst");
    FM1_WL_RST = 0x00u;
    fm1_delay_us(10);
    FM1_WL_RST = 0x01u;
    fm1_delay_us(100);

    cfm1_log("wifi: sdio gate");
    /* Step 2: Bus gate [M:hw v18] */
    *(volatile uint32_t *)0x14000u = 0x0000FFFFu;
    *(volatile uint32_t *)0x14008u = 0x0000FFFFu;
    fm1_delay_us(100);

    cfm1_log("wifi: sdio dma_init");
    wifi_dma_init();

    wifi_sdio_state.initialized = 1;
    cfm1_log("wifi: sdio init done");
}

/* ---- TX ---- */

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

/* ---- CMD52/CMD53 stubs for MAC addr read ---- */

/* Read from MAC register space directly instead of SDIO CMD53.
 * The MAC address is at MAC_ADDR_DW0/DW1 (0x30300/0x30304) [M:hw v23c]. */
static uint8_t wifi_sdio_cmd52_read(uint8_t fn, uint32_t addr)
{
    (void)fn;
    (void)addr;
    return 0;
}

static int wifi_sdio_cmd53_read(uint8_t fn, uint32_t addr, uint8_t *dst,
                                uint32_t len, int incr)
{
    (void)fn;
    (void)incr;

    /* MAC address OTP read: return from MAC_ADDR_DW0/DW1 registers */
    if (addr == 0x1000u && dst && len >= 6) {
        uint32_t dw0 = WIFI_MAC_ADDR_DW0;
        uint32_t dw1 = WIFI_MAC_ADDR_DW1;
        dst[0] = (uint8_t)(dw0);
        dst[1] = (uint8_t)(dw0 >> 8);
        dst[2] = (uint8_t)(dw0 >> 16);
        dst[3] = (uint8_t)(dw0 >> 24);
        dst[4] = (uint8_t)(dw1);
        dst[5] = (uint8_t)(dw1 >> 8);
        return 0;
    }

    return -1;
}

/* ---- poll (called from wifi_poll in main loop) ---- */

static void wifi_sdio_poll_irq(void)
{
    uint32_t status;
    struct wifi_rxd *rxd;
    uint32_t idx, frame_len;

    if (!wifi_sdio_state.initialized || !wifi_sdio_state.dma_active)
        return;

    /* ACK any pending interrupts */
    status = WIFI_INT_SOURCE_CSR;
    if (status)
        WIFI_INT_SOURCE_CSR = status;

    /* Process all available RX frames directly from DMA buffers */
    for (;;) {
        idx = wifi_sdio_state.rx_cpu_idx;
        rxd = &wifi_rx_ring[idx];

        if (!(rxd->flags & RXD_DMA_DONE))
            break;

        frame_len = rxd->sd_len0 & RXD_SDL0_MASK;
        if (frame_len >= 24 && frame_len <= WIFI_RX_BUF_SIZE)
            wifi_mac_rx((uint8_t *)rxd->sd_ptr0, frame_len);

        /* Re-post buffer for DMA */
        rxd->sd_ptr0 = (uint32_t)wifi_rx_buf;
        rxd->sd_len0 = WIFI_RX_BUF_SIZE & RXD_SDL0_MASK;
        rxd->sd_ptr1 = 0;
        rxd->flags   = 0;

        wifi_sdio_state.rx_cpu_idx = (idx + 1u) % WIFI_RX_RING_SIZE;
        WIFI_RX_CRX_IDX = wifi_sdio_state.rx_cpu_idx;
    }
}
