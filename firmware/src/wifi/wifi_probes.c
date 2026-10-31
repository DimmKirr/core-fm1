/* SPDX-License-Identifier: GPL-3.0-only */
/* WiFi hardware register probes: cleanroom analyst measurement instrument.
 *
 * This code runs on real FM-1 hardware to probe WiFi MAC/DMA register
 * addresses identified from IP ancestry analysis (RT2x00) and the SPECSHEET.
 * It does NOT use any vendor code. It reads/writes known MMIO addresses and
 * logs what the hardware returns.
 *
 * Written from: docs/hardware/wifi/SPECSHEET.md §4, §7
 * References:   RT2800 register map (Linux GPL-2.0+, IP ancestor)
 *               docs/hardware/AC791N_IP_ANCESTRY.md §3
 *
 * Results are tagged [M:hw] and go into SPECSHEET.md via the analyst.
 * The probe code itself is implementer-side (GPL-3.0), designed by the
 * analyst's probe specification in docs/hardware/wifi/Taskfile.yml. */

#ifdef CONFIG_WIFI_PROBES

/* ---- probe register definitions ----
 * From SPECSHEET §4.2 (clock/reset), §7.1 (DMA), §7.2 (MAC control).
 * Names from RT2800 public headers [IP:RT2x00]. */

/* Clock and reset (SPECSHEET §4.2, confirmed [M:t]) */
#define PROBE_WL_CLK0       (*(volatile uint32_t *)0x14040u)
#define PROBE_WL_CLK1       (*(volatile uint32_t *)0x14044u)
#define PROBE_WL_CLK2       (*(volatile uint32_t *)0x14048u)
#define PROBE_WL_CLK3       (*(volatile uint32_t *)0x1404Cu)
#define PROBE_WL_CLK4       (*(volatile uint32_t *)0x14050u)
#define PROBE_WL_RST        (*(volatile uint32_t *)0x1405Cu)

/* DMA / interrupt engine (SPECSHEET §7.1, [M:s, IP:RT2x00]) */
#define PROBE_INT_SOURCE_CSR (*(volatile uint32_t *)0x0200u)
#define PROBE_INT_MASK_CSR   (*(volatile uint32_t *)0x0204u)
#define PROBE_WPDMA_GLO_CFG  (*(volatile uint32_t *)0x0208u)
#define PROBE_WPDMA_RST_IDX  (*(volatile uint32_t *)0x020Cu)
#define PROBE_DELAY_INT_CFG  (*(volatile uint32_t *)0x0210u)
#define PROBE_TX_BASE_PTR0   (*(volatile uint32_t *)0x0230u)
#define PROBE_TX_MAX_CNT0    (*(volatile uint32_t *)0x0234u)
#define PROBE_RX_BASE_PTR    (*(volatile uint32_t *)0x0290u)
#define PROBE_RX_MAX_CNT     (*(volatile uint32_t *)0x0294u)
#define PROBE_RX_CRX_IDX     (*(volatile uint32_t *)0x0298u)
#define PROBE_RX_DRX_IDX     (*(volatile uint32_t *)0x029Cu)

/* MAC control (SPECSHEET §7.2, 0x30000 + RT2800 offset) */
#define PROBE_MAC_SYS_CTRL   (*(volatile uint32_t *)0x31004u)
#define PROBE_BBP_CSR_CFG    (*(volatile uint32_t *)0x3101Cu)
#define PROBE_XIFS_TIME_CFG  (*(volatile uint32_t *)0x31100u)
#define PROBE_BKOFF_SLOT_CFG (*(volatile uint32_t *)0x31104u)
#define PROBE_TX_SW_CFG0     (*(volatile uint32_t *)0x31330u)

/* GPIO Port A (SPECSHEET §7 ERRATA: these are NOT WiFi) */
#define PROBE_GPIO_PA_OUT    (*(volatile uint32_t *)0x50000u)
#define PROBE_GPIO_PA_DIR    (*(volatile uint32_t *)0x50008u)
#define PROBE_GPIO_PA_DIE    (*(volatile uint32_t *)0x5000Cu)

/* ---- probe result storage ---- */

#define PROBE_MAX_RESULTS 24

struct probe_result {
    uint32_t address;
    uint32_t value;
    uint8_t  phase;     /* 0=pre-clock, 1=post-clock, 2=post-reset */
    uint8_t  rw;        /* 'R'=read, 'W'=write-then-read */
};

static struct {
    struct probe_result results[PROBE_MAX_RESULTS];
    uint32_t count;
    uint8_t  done;
    uint8_t  clock_ok;
    uint8_t  reset_ok;
} wifi_probes;

/* ---- probe helpers ---- */

static void probe_read(uint32_t addr, uint8_t phase)
{
    if (wifi_probes.count >= PROBE_MAX_RESULTS)
        return;
    volatile uint32_t *reg = (volatile uint32_t *)addr;
    struct probe_result *r = &wifi_probes.results[wifi_probes.count++];
    r->address = addr;
    r->value   = *reg;
    r->phase   = phase;
    r->rw      = 'R';
}

static void probe_log_result(const char *name, uint32_t addr, uint32_t val)
{
    char line[48];
    uint32_t i = 0;
    static const char hex[] = "0123456789ABCDEF";

    while (name[i] && i < 20) { line[i] = name[i]; i++; }
    line[i++] = ' ';
    line[i++] = '0'; line[i++] = 'x';
    line[i++] = hex[(addr >> 16) & 0xF];
    line[i++] = hex[(addr >> 12) & 0xF];
    line[i++] = hex[(addr >>  8) & 0xF];
    line[i++] = hex[(addr >>  4) & 0xF];
    line[i++] = hex[(addr      ) & 0xF];
    line[i++] = '=';
    line[i++] = '0'; line[i++] = 'x';
    { uint32_t j; for (j = 28; j < 32u; j -= 4) line[i++] = hex[(val >> j) & 0xF]; }
    { uint32_t j; for (j = 0; j < 4u; j++) line[i++] = hex[(val >> (24 - j*4)) & 0xF]; }
    line[i] = 0;
    cfm1_log(line);
}

/* ---- main probe sequence ---- */

static void wifi_probes_init(void)
{
    wifi_probes.count = 0;
    wifi_probes.done = 0;
    wifi_probes.clock_ok = 0;
    wifi_probes.reset_ok = 0;

    cfm1_log("probe: === WiFi HW probes v23c (reads only) ===");

    /* Phase 0: clock+reset + bus gate (proven from v18) */
    PROBE_WL_CLK0 |= 0x01u;
    PROBE_WL_CLK4 |= 0x01u;
    fm1_delay_us(50);
    PROBE_WL_RST = 0x00u;
    fm1_delay_us(10);
    PROBE_WL_RST = 0x01u;
    fm1_delay_us(100);
    wifi_probes.clock_ok = 1;
    *(volatile uint32_t *)0x14000u = 0x0000FFFFu;
    /* Do NOT write 0x14008 (WL_CTL8): kills input shift register GPIO.
     * WL_CTL0 alone is sufficient for MAC/DMA bus gating. */
    fm1_delay_us(100);

    /* v23b faulted. Suspect: write-readback tests (Group 7).
     * v23c: ALL reads, NO writes. If this boots, writes caused the crash. */

    /* DMA control (0x40200-0x40214) */
    cfm1_log("probe: DMA ctrl");
    probe_read(0x40200u, 1);  /* INT_SOURCE_CSR */
    probe_read(0x40204u, 1);  /* INT_MASK_CSR */
    probe_read(0x40208u, 1);  /* WPDMA_GLO_CFG */
    probe_read(0x4020Cu, 1);  /* WPDMA_RST_IDX */
    probe_read(0x40210u, 1);  /* DELAY_INT_CFG */
    /* 5 */

    /* TX queue 0 */
    cfm1_log("probe: TX0");
    probe_read(0x40230u, 1);
    probe_read(0x40234u, 1);
    probe_read(0x40238u, 1);
    probe_read(0x4023Cu, 1);
    /* 9 */

    /* RX queue */
    cfm1_log("probe: RX");
    probe_read(0x40290u, 1);
    probe_read(0x40294u, 1);
    probe_read(0x40298u, 1);
    probe_read(0x4029Cu, 1);
    /* 13 */

    /* TX1 + boundary */
    cfm1_log("probe: TX1+bound");
    probe_read(0x40240u, 1);  /* TX1 base */
    probe_read(0x40300u, 1);  /* past DMA */
    /* 15 */

    /* MAC STA registers (proven safe from v19/v20) */
    cfm1_log("probe: MAC STA");
    probe_read(0x31000u, 1);  /* CHIP_VERSION */
    probe_read(0x31004u, 1);  /* MAC_SYS_CTRL */
    probe_read(0x30300u, 1);  /* MAC_ADDR_DW0 */
    probe_read(0x30304u, 1);  /* MAC_ADDR_DW1 */
    probe_read(0x31100u, 1);  /* XIFS_TIME_CFG */
    probe_read(0x31104u, 1);  /* BKOFF_SLOT_CFG */
    probe_read(0x31330u, 1);  /* TX_SW_CFG0 */
    probe_read(0x3101Cu, 1);  /* BBP_CSR_CFG */
    probe_read(0x30308u, 1);  /* MAC config */
    /* 24 */

    wifi_probes.done = 1;
    wifi_probes.reset_ok = 1;
    cfm1_log_int("probe: total=", (int32_t)wifi_probes.count);
    cfm1_log("probe: done");
}

/* ---- DAP query handler ----
 * Binary format: [status] [count] then per result:
 *   [addr_31:24] [addr_23:16] [addr_15:8] [addr_7:0]
 *   [val_31:24] [val_23:16] [val_15:8] [val_7:0]
 *   [phase] [rw]
 * Total: 2 + count*10 bytes. Fits 25 results in 256 bytes. */

static uint32_t wifi_probes_fill(uint8_t *b, uint32_t max)
{
    uint32_t off = 2, i;
    b[0] = 0; /* status OK */
    b[1] = (uint8_t)wifi_probes.count;
    for (i = 0; i < wifi_probes.count && off + 10u <= max; i++) {
        struct probe_result *r = &wifi_probes.results[i];
        b[off++] = (uint8_t)(r->address >> 24);
        b[off++] = (uint8_t)(r->address >> 16);
        b[off++] = (uint8_t)(r->address >>  8);
        b[off++] = (uint8_t)(r->address);
        b[off++] = (uint8_t)(r->value >> 24);
        b[off++] = (uint8_t)(r->value >> 16);
        b[off++] = (uint8_t)(r->value >>  8);
        b[off++] = (uint8_t)(r->value);
        b[off++] = r->phase;
        b[off++] = r->rw;
    }
    return off;
}

#endif /* CONFIG_WIFI_PROBES */
