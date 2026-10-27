/* SPDX-License-Identifier: GPL-3.0-only */
/* Clean-room SDIO bus driver for AC791N (WL82) internal WiFi interconnect.
 * From docs/hardware/wifi/SPECSHEET.md SDIO internal bus section and the SDIO
 * Simplified Specification v3.00 for CMD52/CMD53 protocol.
 * No vendor code, IR or disassembly. */

/* ---- constants ---- */

/* SDIO controller registers (on-chip, from fact sheet §7.1) */
#define WIFI_SDIO_BASE     (*(volatile uint32_t *)0x50000u)
#define WIFI_SDIO_DATA     (*(volatile uint32_t *)0x50008u)
#define WIFI_SDIO_STATUS   (*(volatile uint32_t *)0x5000Cu)

#define WIFI_SDIO_POLLS    10000u   /* busy-wait ceiling */

/* SDIO status bit fields are unknown per fact sheet §7.1.
 * TODO: determine from emulator observation.  For now, poll
 * a non-zero status word as "busy" (conservative). */

/* SDIO commands per Simplified Spec */
#define SDIO_CMD52         52u    /* IO_RW_DIRECT: single byte */
#define SDIO_CMD53         53u    /* IO_RW_EXTENDED: multi-byte / block */

/* CMD52 argument bits (SDIO Simplified Spec 5.1) */
#define CMD52_WRITE        (1u << 31)
#define CMD52_FN_SHIFT     28
#define CMD52_RAW          (1u << 27)   /* read-after-write */
#define CMD52_ADDR_SHIFT   9

/* CMD53 argument bits (SDIO Simplified Spec 5.3) */
#define CMD53_WRITE        (1u << 31)
#define CMD53_FN_SHIFT     28
#define CMD53_BLOCK        (1u << 27)   /* block mode */
#define CMD53_INCR         (1u << 26)   /* incrementing address */
#define CMD53_ADDR_SHIFT   9

/* ---- internal state ---- */

static struct {
    uint8_t  initialized;
    uint8_t  error_count;
    uint16_t block_size;    /* negotiated block size for CMD53 */
} wifi_sdio_state;

/* ---- low-level bus operations ---- */

/* Wait for the internal SDIO controller to become idle.
 * Bit fields unknown: treat any non-zero status as busy (conservative). */
static int wifi_sdio_wait(void)
{
    uint32_t i;
    for (i = 0; i < WIFI_SDIO_POLLS; i++) {
        if (WIFI_SDIO_STATUS == 0u)
            return 0;
    }
    wifi_sdio_state.error_count++;
    return -1;
}

/* CMD52: IO_RW_DIRECT -- single-byte read/write (SDIO Simplified Spec 5.1).
 * This is an internal bus command: the CPU writes the command argument to the
 * controller base register and the hardware sequences the internal SDIO. */

static uint8_t wifi_sdio_cmd52_read(uint8_t fn, uint32_t addr)
{
    uint32_t arg;

    if (wifi_sdio_wait())
        return 0;

    /* CMD52 read: R/W=0, fn, register address */
    arg = ((uint32_t)(fn & 7u) << CMD52_FN_SHIFT) |
          ((addr & 0x1FFFFu) << CMD52_ADDR_SHIFT);

    WIFI_SDIO_BASE = (SDIO_CMD52 << 24) | arg;

    if (wifi_sdio_wait())
        return 0;

    return (uint8_t)(WIFI_SDIO_DATA & 0xFFu);
}

static int wifi_sdio_cmd52_write(uint8_t fn, uint32_t addr, uint8_t val)
{
    uint32_t arg;

    if (wifi_sdio_wait())
        return -1;

    /* CMD52 write: R/W=1, fn, register address, data */
    arg = CMD52_WRITE |
          ((uint32_t)(fn & 7u) << CMD52_FN_SHIFT) |
          ((addr & 0x1FFFFu) << CMD52_ADDR_SHIFT) |
          val;

    WIFI_SDIO_BASE = (SDIO_CMD52 << 24) | arg;

    return wifi_sdio_wait();
}

/* CMD53: IO_RW_EXTENDED -- multi-byte/block transfer (SDIO Simplified Spec 5.3).
 * For DMA transfers on the internal bus. */

static int wifi_sdio_cmd53_read(uint8_t fn, uint32_t addr, uint8_t *dst,
                                uint32_t len, int incr)
{
    uint32_t arg, i;
    uint32_t count;

    if (!dst || !len)
        return -1;

    if (wifi_sdio_wait())
        return -1;

    /* Determine byte/block mode */
    if (wifi_sdio_state.block_size && len >= wifi_sdio_state.block_size) {
        /* block mode */
        count = (len + wifi_sdio_state.block_size - 1u) / wifi_sdio_state.block_size;
        arg = ((uint32_t)(fn & 7u) << CMD53_FN_SHIFT) |
              CMD53_BLOCK |
              (incr ? CMD53_INCR : 0u) |
              ((addr & 0x1FFFFu) << CMD53_ADDR_SHIFT) |
              (count & 0x1FFu);
    } else {
        /* byte mode */
        arg = ((uint32_t)(fn & 7u) << CMD53_FN_SHIFT) |
              (incr ? CMD53_INCR : 0u) |
              ((addr & 0x1FFFFu) << CMD53_ADDR_SHIFT) |
              (len & 0x1FFu);
    }

    WIFI_SDIO_BASE = (SDIO_CMD53 << 24) | arg;

    if (wifi_sdio_wait())
        return -1;

    /* Read data from the data register.  The internal DMA may present
     * data word-aligned; we read 32 bits at a time and extract bytes. */
    for (i = 0; i < len; i += 4u) {
        uint32_t w = WIFI_SDIO_DATA;
        uint32_t remaining = len - i;
        dst[i] = (uint8_t)(w & 0xFFu);
        if (remaining > 1u) dst[i + 1] = (uint8_t)((w >> 8) & 0xFFu);
        if (remaining > 2u) dst[i + 2] = (uint8_t)((w >> 16) & 0xFFu);
        if (remaining > 3u) dst[i + 3] = (uint8_t)((w >> 24) & 0xFFu);
    }

    return 0;
}

static int wifi_sdio_cmd53_write(uint8_t fn, uint32_t addr, const uint8_t *src,
                                 uint32_t len, int incr)
{
    uint32_t arg, i;
    uint32_t count;

    if (!src || !len)
        return -1;

    if (wifi_sdio_wait())
        return -1;

    /* Write data to the data register first (the controller latches it) */
    for (i = 0; i < len; i += 4u) {
        uint32_t w = (uint32_t)src[i];
        if (i + 1u < len) w |= (uint32_t)src[i + 1] << 8;
        if (i + 2u < len) w |= (uint32_t)src[i + 2] << 16;
        if (i + 3u < len) w |= (uint32_t)src[i + 3] << 24;
        WIFI_SDIO_DATA = w;
    }

    if (wifi_sdio_state.block_size && len >= wifi_sdio_state.block_size) {
        count = (len + wifi_sdio_state.block_size - 1u) / wifi_sdio_state.block_size;
        arg = CMD53_WRITE |
              ((uint32_t)(fn & 7u) << CMD53_FN_SHIFT) |
              CMD53_BLOCK |
              (incr ? CMD53_INCR : 0u) |
              ((addr & 0x1FFFFu) << CMD53_ADDR_SHIFT) |
              (count & 0x1FFu);
    } else {
        arg = CMD53_WRITE |
              ((uint32_t)(fn & 7u) << CMD53_FN_SHIFT) |
              (incr ? CMD53_INCR : 0u) |
              ((addr & 0x1FFFFu) << CMD53_ADDR_SHIFT) |
              (len & 0x1FFu);
    }

    WIFI_SDIO_BASE = (SDIO_CMD53 << 24) | arg;

    return wifi_sdio_wait();
}

/* ---- bus claim/release (bare-metal: just IRQ disable/enable) ---- */

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

/* ---- SDIO initialization ---- */

/* Init order from fact sheet: clock/reset via 0x14040-0x1405C,
 * SDIO enumeration, DMA init, PA enable, then RF init. */
static void wifi_sdio_init(void)
{
    uint32_t i;

    for (i = 0; i < sizeof(wifi_sdio_state); i++)
        ((uint8_t *)&wifi_sdio_state)[i] = 0;

    /* Step 1: WiFi clock and reset (also done in wifi_rf_group3, but
     * SDIO needs them before any bus transaction) */
    FM1_WL_CLK0 |= 0x01u;     /* enable WiFi subsystem clock */
    FM1_WL_CLK4 |= 0x01u;     /* SDIO clock enable */
    fm1_delay_us(50);

    FM1_WL_RST = 0x00u;       /* assert reset */
    fm1_delay_us(10);
    FM1_WL_RST = 0x01u;       /* de-assert */
    fm1_delay_us(100);

    /* Step 2: SDIO enumeration (Simplified Spec 3.1.2).
     * For the internal bus, CMD52 to CCCR (fn=0) configures the
     * IO function and sets the block size. */

    /* Enable function 1 (WiFi) via CCCR I/O Enable (0x02) */
    wifi_sdio_cmd52_write(0, 0x02u, 0x02u);   /* enable fn1 */
    fm1_delay_us(10);

    /* Wait for function ready via CCCR I/O Ready (0x03) */
    for (i = 0; i < 100u; i++) {
        if (wifi_sdio_cmd52_read(0, 0x03u) & 0x02u)
            break;
        fm1_delay_us(100);
    }

    /* Set block size for function 1 via FBR (0x110-0x111) */
    wifi_sdio_state.block_size = 256;
    wifi_sdio_cmd52_write(0, 0x110u, (uint8_t)(wifi_sdio_state.block_size & 0xFFu));
    wifi_sdio_cmd52_write(0, 0x111u, (uint8_t)(wifi_sdio_state.block_size >> 8));

    /* Enable bus-level interrupts via CCCR Int Enable (0x04) */
    wifi_sdio_cmd52_write(0, 0x04u, 0x03u);   /* master + fn1 */

    /* Step 3: DMA configuration.  The internal SDIO controller has
     * its own DMA for CMD53 block transfers. */
    /* TODO: DMA ring buffer setup for TX/RX.  The DMA registers are
     * in the 0x50000 range but their exact layout beyond base/data/status
     * must be determined from emulator observation. */

    wifi_sdio_state.initialized = 1;
}

/* ---- IRQ handling (stub for bare-metal polling) ---- */

/* Check SDIO status.  In the polled architecture (no RTOS), this is
 * called from wifi_poll() in the main loop.
 * Status bit fields are unknown: returns raw status word.
 * TODO: map bits once observed in emulator. */
static uint32_t wifi_sdio_poll_irq(void)
{
    return WIFI_SDIO_STATUS;
}
