/* SPDX-License-Identifier: GPL-3.0-only */
/* Clean-room WiFi bus driver for AC791N (WL82) using JieLi WDMA engine.
 *
 * From docs/hardware/wifi/SPECSHEET.md §4.2 (bus gate, clock/reset),
 * §7.4 (init sequence), §7.5a/§7.5b (TXWI/RXWI format),
 * §7.10 (MAC config registers), §7.11 (WDMA engine).
 * RT2800 MAC register layout from Linux GPL-2.0+ [IP:RT2x00].
 *
 * No vendor code, IR or disassembly. */

/* ---- WDMA registers [SPECSHEET §7.11.1] ---- */

#define WDMA_CLK_GATE  (*(volatile uint32_t *)0x1404Cu)
#define WDMA_CFG0      (*(volatile uint32_t *)0x30F00u)
#define WDMA_CTL       (*(volatile uint32_t *)0x30F04u)
#define WDMA_TX_ADR    (*(volatile uint32_t *)0x30F10u)
#define WDMA_TX_LEN    (*(volatile uint32_t *)0x30F14u)
#define WDMA_RX_ADR    (*(volatile uint32_t *)0x30F18u)
#define WDMA_RX_LEN    (*(volatile uint32_t *)0x30F1Cu)

/* WDMA_CTL bits [SPECSHEET §7.11.3] */
#define WDMA_CTL_TX_ACK   (1u << 10)
#define WDMA_CTL_RX_ACK   (1u << 11)
#define WDMA_CTL_RX_AVAIL (1u << 17)
#define WDMA_CTL_TX_DONE  (1u << 20)
#define WDMA_CTL_RX_DONE  (1u << 21)

/* WDMA_CFG0 init value [SPECSHEET §7.11.2]: 0x46F */
#define WDMA_CFG0_INIT 0x046Fu

/* MAC RX notification enable [SPECSHEET §7.11.4 step 4] */
#define WIFI_MAC_IRQ_EN    (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x0328u))

/* SoC WiFi interrupt enable [SPECSHEET §7.11.4 step 5] */
#define WIFI_SOC_IRQ_EN    (*(volatile uint32_t *)0x14050u)

/* ---- MAC registers: 0x30000 + RT2800 offset [SPECSHEET §7.2] ---- */

#define WIFI_MAC_BASE          0x30000u

#define WIFI_MAC_SYS_CTRL      (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1004u))
#define WIFI_MAC_ADDR_DW0      (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x0300u))
#define WIFI_MAC_ADDR_DW1      (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x0304u))

#define WIFI_MAC_ADDR_HI0      (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1008u))
#define WIFI_MAC_ADDR_HI1      (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x100Cu))
#define WIFI_MAC_BSSID_DW0     (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1010u))
#define WIFI_MAC_BSSID_DW1     (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1014u))
#define WIFI_MAX_LEN_CFG       (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1018u))
#define WIFI_BCN_TIME_CFG      (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1114u))
#define WIFI_RX_FILTER_CFG     (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1400u))
#define WIFI_AUTO_RSP_CFG      (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1404u))
#define WIFI_LEGACY_BASIC_RATE (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1408u))
#define WIFI_HT_BASIC_RATE     (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x140Cu))

/* Statistics counters (read-to-clear) [SPECSHEET §7.10] */
#define WIFI_RX_STA_CNT0       (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1700u))
#define WIFI_RX_STA_CNT1       (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1704u))
#define WIFI_RX_STA_CNT2       (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1708u))
#define WIFI_TX_STA_CNT0       (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x170Cu))
#define WIFI_TX_STA_CNT1       (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1710u))
#define WIFI_TX_STA_CNT2       (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1714u))

/* MAC behavioural diagnostics [IP:RT2x00, rt2800.h lines 613, 1012-1022, 1029, 1931].
 * Read-only; used to tell "frame never entered the MAC" from "MAC defers on CCA". */
#define WIFI_TXRXQ_PCNT        (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x0438u)) /* [7:0] TX0Q [15:8] TX1Q [23:16] TX2Q [31:24] RX0Q */
#define WIFI_CH_IDLE_STA       (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1130u)) /* channel idle time */
#define WIFI_CH_BUSY_STA       (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1134u)) /* channel busy time (primary) */
#define WIFI_CH_BUSY_SEC       (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1138u)) /* channel busy time (secondary) */
#define WIFI_MAC_STATUS        (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1200u)) /* MAC_STATUS_CFG [0] BBP/RF busy TX [1] busy RX */

/* TX readiness gate and front end [SPECSHEET §7.11.5, §4.2.1 C2-C3, M:s 2026-11-04] */
#define WIFI_PBF_MAX_PCNT      (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x040Cu)) /* byte 1 = TX queue 2 page limit */
#define WIFI_TXQ_STA           (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x0434u)) /* bit 18 = queue 2 busy/full [I] */
#define WIFI_PBF_DBG           (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x043Cu)) /* [7:5] non-zero = packet buffer ready */
#define WIFI_TX_PIN_CFG        (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1328u)) /* PA/LNA/RF switch enables [IP:RT2x00] */
#define WIFI_TX_BAND_CFG       (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x132Cu)) /* band select [IP:RT2x00] */
#define WIFI_TX_STA_FIFO       (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x1718u)) /* [0] valid [5] success [15:8] WCID [22:16] MCS */

/* Pre-RX MAC register [SPECSHEET §7.11.6]: vendor writes 2 before RX.
 * Purpose unknown; included for compatibility. */
#define WIFI_MAC_RX_PREP       (*(volatile uint32_t *)(WIFI_MAC_BASE + 0x0324u))

/* MAC_SYS_CTRL bits [SPECSHEET §7.4 step 6] */
#define MAC_RESET_CSR  (1u << 0)
#define MAC_RESET_BBP  (1u << 1)
#define MAC_TX_EN      (1u << 2)
#define MAC_RX_EN      (1u << 3)

/* ---- TXWI [SPECSHEET §7.5a] ---- */

struct wifi_txwi {
    uint32_t word0;
    uint32_t word1;
    uint32_t iv;
    uint32_t eiv;
};

#define TXWI_W0_DENSITY_SHIFT  5u   /* [7:5] MPDU density */
#define TXWI_W0_PHYMODE_SHIFT  30u
#define TXWI_W0_MCS_SHIFT      16u
#define TXWI_W1_ACK            (1u << 0)
#define TXWI_W1_NSEQ           (1u << 1)
#define TXWI_W1_BW_WIN_SHIFT   2u   /* [7:2] block-ack window size */
#define TXWI_W1_WCID_SHIFT     8u
#define TXWI_W1_MPDU_LEN_SHIFT 16u

#define PHYMODE_CCK  0u
#define PHYMODE_OFDM 1u

/* ---- RXWI [SPECSHEET §7.5b] ---- */

#define RXWI_SIZE              16u
#define RXWI_W0_MPDU_LEN_MASK  0x0FFF0000u
#define RXWI_W0_MPDU_LEN_SHIFT 16u

/* ---- buffer sizes ---- */

#define WIFI_TX_MAX_FRAME 1600u
#define WIFI_RX_BUF_SIZE  1624u  /* 4-byte length + RXWI(16) + max frame(1600) + 4 */

/* WDMA TX buffer, layout measured on the vendor build [SPECSHEET §7.11.5
 * measured layout, 2026-11-02]:
 * [0..3]  total length = 16 + pad4(frame) + 4
 * [4..7]  4 (vendor writes 4 for data frames, 5 for QoS null)
 * [8..23] TXWI (16 bytes, DMA starts here)
 * [24..]  802.11 frame, zero-padded to 4 bytes, then one zero word */
#define WIFI_TX_HDR       8u
#define WIFI_TX_BUF_SIZE  (WIFI_TX_HDR + 16u + WIFI_TX_MAX_FRAME + 8u)  /* pad + terminator */

/* ---- WiFi IRQ handler [SPECSHEET §7.11.4] ----
 *
 * The vendor registers IRQ 52 with a handler that reads 0x30328, checks
 * bit 0 (frame arrived), re-writes 0x30328=2 to re-arm the notification,
 * and wakes the RX thread.  Without this ISR, the MAC notification
 * deadlocks: 0x30328 bit 0 stays asserted, and WDMA_CTL bit 17 never
 * appears for polling.
 *
 * Our ISR re-arms the notification and sets a volatile flag; the poll
 * loop checks it alongside WDMA_CTL bit 17. */

static volatile uint32_t wifi_irq_pending;
static volatile uint32_t wifi_isr_count;

/* Interrupt 52 handler, as the vendor's [SPECSHEET §7.11.9, §7.11.6 step 1,
 * M:s]: mask the source (WF_CON4 0x14050 bit 0) and flag the receive path;
 * no MAC register access. The mask is re-opened by wifi_sdio_poll_irq() once
 * WDMA is drained. The previous handler left the source unmasked and wrote
 * MAC+0x328 from interrupt context; the first real reception (12n) then kept
 * the CPU busy until the watchdog reset it. */
void fm1_wifi_irq(void)
{
    WIFI_SOC_IRQ_EN &= ~1u;
    wifi_irq_pending = 1;
    wifi_isr_count++;
}

__asm__(".section .text.isr_wifi,\"ax\",@progbits\n"
        "\t.globl isr_wifi\n"
        "isr_wifi:\n"
        "\t[--sp] = {psr, rets, reti}\n"
        "\t[--sp] = {r3-r0}\n"
        "\tcall fm1_wifi_irq\n"
        "\t{r3-r0} = [sp++]\n"
        "\t{psr, rets, reti} = [sp++]\n"
        "\tcsync\n"
        "\trti\n"
        "\t.previous\n");
extern void isr_wifi(void);

/* ---- static allocations ---- */

static uint8_t wifi_tx_buf[WIFI_TX_BUF_SIZE] __attribute__((aligned(4)));
static uint8_t wifi_rx_buf[WIFI_RX_BUF_SIZE] __attribute__((aligned(4)));

static struct {
    uint8_t  initialized;
    uint8_t  dma_active;
    uint8_t  error_count;
    uint32_t rx_count;
    uint32_t tx_qpcnt;   /* TXRXQ_PCNT sampled before the last TX ack (diagnostic) */
    uint8_t  tx_gate;    /* 1 = readiness gate passed before the last TX (diagnostic) */
    uint32_t tx_gate_pcnt, tx_gate_qsta, tx_gate_dbg;   /* the three gate reads, last attempt */
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

/* ---- MAC reset [SPECSHEET §7.4 step 6] ---- */

/* ---- "Running" indicator rows [SPECSHEET §4.2, observations 2026-11-03/04] ----
 * Five read-only bits differ between the running vendor radio and core-fm1
 * and refuse writes; sampled at every init step boundary so a transient
 * assert cannot hide. Two packed hex rows per sample (CFM1_LOG_LINE_LEN 40:
 * id, space, 4-char tag, ':', 32 hex digits, NUL):
 *   A tag: WL_CTL0 0x14000 | WL_ID 0x1400C | WL_CLK4 0x14050 | CFG58 0x14058
 *   B tag: CFG60 0x14060 | CFG90 0x14090 | WDMA_CTL 0x30F04 | MAC_SYS_CTRL
 * The MAC words read as 0 while mac_ok is 0 (before the bus gate, MAC reads
 * fault). Decoder: tools/wifi_ind_timeline.py (task debug:ind).
 * NOINIT marker bit 1 is set while the init-time samples run: if a sample
 * ever faults, the next boot sees the bit and skips sampling, so the device
 * comes back with USB and the DAP alive. */
static uint8_t wifi_ind_off;

static void wifi_ind_arm(void)
{
#ifdef CONFIG_DEBUG_DAP
    if (cfm1_dbg_nv.probe & 2u) {
        wifi_ind_off = 1;
        cfm1_log("wifi: ind rows skipped: crashed last boot");
        return;
    }
    cfm1_dbg_nv.probe |= 2u;
#endif
}

static void wifi_ind_done(void)
{
#ifdef CONFIG_DEBUG_DAP
    cfm1_dbg_nv.probe &= ~2u;
#endif
}

static void wifi_ind_row(char id, const char *tag, const uint32_t *w)
{
    static const char hex[] = "0123456789ABCDEF";
    char line[CFM1_LOG_LINE_LEN];
    uint32_t i = 0, k, j;
    line[i++] = id;
    line[i++] = ' ';
    for (k = 0; k < 4u && tag[k]; k++)
        line[i++] = tag[k];
    line[i++] = ':';
    for (k = 0; k < 4u; k++)
        for (j = 0; j < 8u; j++)
            line[i++] = hex[(w[k] >> (28u - 4u * j)) & 0xFu];
    line[i] = 0;
    cfm1_log(line);
}

static void wifi_ind_log(const char *tag, int mac_ok)
{
    uint32_t w[4];
    if (wifi_ind_off)
        return;
    w[0] = *(volatile uint32_t *)0x14000u;
    w[1] = *(volatile uint32_t *)0x1400Cu;
    w[2] = *(volatile uint32_t *)0x14050u;
    w[3] = *(volatile uint32_t *)0x14058u;
    wifi_ind_row('A', tag, w);
    w[0] = *(volatile uint32_t *)0x14060u;
    w[1] = *(volatile uint32_t *)0x14090u;
    w[2] = mac_ok ? WDMA_CTL : 0u;
    w[3] = mac_ok ? WIFI_MAC_SYS_CTRL : 0u;
    wifi_ind_row('B', tag, w);
}

static void wifi_hw_reset(void)
{
    uint32_t val;
    cfm1_log("wifi: hw_reset");

    val = WIFI_MAC_SYS_CTRL;
    WIFI_MAC_SYS_CTRL = val | (MAC_RESET_CSR | MAC_RESET_BBP);
    fm1_delay_us(10);
    wifi_ind_log("inrs", 1);
    WIFI_MAC_SYS_CTRL = 0;
    fm1_delay_us(10);

    cfm1_log("wifi: hw_reset done");
}

/* ---- WDMA init [SPECSHEET §7.11.4] ---- */

static void wdma_init(void)
{
    cfm1_log("wifi: wdma_init");

    /* Step 1: clock gate bit 24 [SPECSHEET §7.11.4 step 1] */
    WDMA_CLK_GATE |= (1u << 24);

    /* Step 2: WDMA_CTL [SPECSHEET §7.11.4 step 2]
     * Vendor value 0x00010037: b0-2,4,5,16. Single write to ensure b16 sticks.
     * b9-11 (ACK bits) may auto-clear; write them anyway per specsheet. */
    WDMA_CTL = 0x00010E37u;

    /* Step 3: WDMA_CFG0 [SPECSHEET §7.11.4 step 3] */
    WDMA_CFG0 = WDMA_CFG0_INIT;

    cfm1_log_int("wifi: wdma_ctl=", (int32_t)WDMA_CTL);
    cfm1_log_int("wifi: wdma_cfg0=", (int32_t)WDMA_CFG0);

    wifi_sdio_state.dma_active = 1;
    cfm1_log("wifi: wdma_init done");
}

/* ---- PHY/MAC hardware init [SPECSHEET §11.2] ----
 * Minimum viable init: configures the digital baseband for 802.11 frame
 * processing.  Without this, the radio can transmit raw frames via WDMA
 * but the receiver cannot detect or demodulate any WiFi signal.
 * Sources: docs/hardware/wifi/SPECSHEET.md §11.2.2-§11.2.8b, §11.2.12.
 * No vendor code, IR or disassembly. */

/* PHY-only reset [SPECSHEET §11.2.2] */
static void wifi_phy_rst(void)
{
    WIFI_MAC_SYS_CTRL = 0x0Cu;
    wifi_bbp_write(22, 0x00);
    wifi_bbp_write(21, 0x31);
    wifi_bbp_write(21, 0x30);
    wifi_bbp_write(25, 0x00);
}

/* Full PHY+MAC reset [SPECSHEET §11.2.2] */
static void wifi_phy_mac_rst(void)
{
    WIFI_MAC_SYS_CTRL = 0x03u;
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x030Cu) = 0x1Eu;
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x030Cu) = 0x00u;
    wifi_phy_rst();
}

/* SIFS timing [SPECSHEET §11.2.3] */
static void wifi_sifs_reset(void)
{
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1104u) = 0x00140314u;
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1100u) = 0x33A40C0Bu;
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1330u) = 0x00040804u;
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1334u) = 0x000C0808u;
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1338u) = 0x000C0104u;
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1348u) = 0x000F1290u;
}

/* CCA register table [SPECSHEET §11.2.6], window C (w=0).
 * Pairs of (index, value).  Detection thresholds from SDK defaults
 * [SPECSHEET §11.2.6 calibration table]. */
static const uint8_t wifi_cca_tbl[] = {
      0,0x8A,   1,0x20,   2,0x04,   3,0x08,
      4,0x0C,   5,0x10,   6,0xB0,   7,0xA0,
      8,0x70,   9,0x32,  10,0x0C,  11,0xE0,
     12,0x01,  13,0x00,  14,0xB5,  15,0x91,
     16,0x44,  17,0x43,  18,0x44,  19,0x04,
     20,0x7F,  21,0x0F,  22,0x00,  23,0x05,
     24,0x77,  25,0x80,  26,0x12,  27,0xBE,
     28,0x35,  29,0xEF,  30,0x0F,  31,0x0F,
     32,0x4F,  33,0x05,  34,0x05,  35,0x93,
     36,0x04,  37,0x0D,  38,0x03,  39,0x84,
     40,0x1E,  41,0x0A,  42,0x94,
     48,0x20,  49,0x06,  50,0x3F,  51,0x0C,
     52,0x0C,  53,0x04,  54,0x0C,  55,0x04,
     56,0x10,  57,0x10,  58,0x20,  59,0xC0,
     60,0xF0,  61,0xD0,  62,0x80,  63,0xD0,
     64,0xE7,  65,0x87,  66,0x18,  67,0xE8,
     68,0x30,  69,0x30,  70,0x07,  71,0x00,
     72,0xBE,  73,0x96,
     75,0x70,  76,0x50,
     79,0x8C,
     80,0x80,  81,0x90,  82,0xFF,  83,0x03,
     84,0x00,  85,0x01,  86,0x01,  87,0x27,
     88,0x03,  89,0x28,  90,0xD8,  91,0x0C,
     92,0x01,  93,0x05,  94,0x50,  95,0x80,
     96,0x80,  97,0xDD,  98,0x20,  99,0xFE,
    100,0x50, 101,0x50, 102,0x50, 103,0x00,
    104,0x00, 105,0x64, 106,0x84, 107,0xDF,
    108,0x47, 109,0x40, 110,0x2C, 111,0x70,
    112,0x50, 113,0xE0, 114,0x01, 115,0x9C,
    117,0x00, 118,0x03, 119,0x49, 120,0x00,
    121,0x04, 122,0xD0,
};

/* RX frontend register table [SPECSHEET §11.2.7], window D' (w=3).
 * Pairs of (index, value). */
static const uint8_t wifi_rxfe_tbl[] = {
     0,0x3A,   3,0x15,   4,0x6A,   5,0x00,
     6,0x00,   7,0xB5,   8,0xC1,  13,0x2B,
    14,0xA0,  15,0xC4,  16,0x04,  17,0xA0,
    18,0x2B,  19,0x00,  20,0x00,  21,0x00,
    22,0x00,  23,0x00,  24,0x00,  25,0x39,
    26,0x6A,  27,0x38,  28,0x00,
};

/* RX 11n register table [SPECSHEET §11.2.8], window C' (w=1).
 * Dense array: index 0..71. */
static const uint8_t wifi_rx11n_tbl[72] = {
    0x62, 0x58, 0x45, 0xE5, 0x00, 0x0A, 0x12, 0x04,
    0x00, 0x70, 0x34, 0x00, 0x00, 0x9A, 0xA6, 0x33,
    0x9A, 0xA6, 0x33, 0x9A, 0xA6, 0x33, 0x9A, 0xA6,
    0x33, 0x9A, 0xA6, 0x33, 0x08, 0x0F, 0x10, 0x10,
    0x10, 0x24, 0x25, 0x24, 0x25, 0x25, 0x25, 0x25,
    0x25, 0x25, 0x25, 0x25, 0x25, 0x1D, 0x25, 0x1C,
    0x25, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
    0x07, 0x2E, 0x08, 0x20, 0xFF, 0x08, 0x00, 0x04,
    0x00, 0x60, 0x0F, 0x00, 0x04, 0x00, 0x00, 0x00,
};

/* TX backend register table parts 2+3 [SPECSHEET §11.2.8b], window D (w=2).
 * Part 1 (per-rate gain, indices 11-34) is generated in code.
 * Pairs of (index, value). */
static const uint8_t wifi_txbe_tbl[] = {
    /* Part 2: TX signal processing (indices 35-76) */
     35,0x03,  36,0x32,  37,0x00,  38,0x3F,
     39,0xE5,  40,0x00,  41,0xCC,  42,0x01,
     43,0x8A,  44,0x02,  45,0xCA,  46,0x01,
     47,0x88,  48,0x02,  49,0x06,  50,0x04,
     51,0x19,  52,0xB6,  53,0x00,  54,0x99,
     55,0x01,  56,0x42,  57,0x02,  58,0xE6,
     59,0x01,  60,0xAF,  61,0x02,  62,0x04,
     63,0x04,  64,0x44,  65,0x5B,  66,0x00,
     67,0xCC,  68,0x01,  69,0x8A,  70,0x02,
     71,0x99,  72,0x01,  73,0x43,  74,0x02,
     75,0x06,  76,0x05,
    /* Part 3: TX filter/calibration (indices 80-95) */
     80,0x00,  81,0x4F,  82,0xA4,  83,0x90,
     84,0xED,  85,0xDA,  86,0x54,  87,0x6B,
     88,0x00,  89,0x10,  90,0xD6,  91,0x90,
     92,0xC0,  93,0x00,  95,0x40,
};

/* AGC gain table [SPECSHEET §11.2.5a] */
static const uint32_t wifi_agc_tbl[128] = {
    0xFFB000ECu, 0xFFB801EDu, 0xFFC002EEu, 0xFFC803EFu,
    0xFFD004F0u, 0xFFD805F1u, 0xFFE006F2u, 0xFFE807F3u,
    0xFFF008F4u, 0xFFF809F5u, 0x00000AF6u, 0x00080BF7u,
    0x00100CF8u, 0x00180DF9u, 0x00200EFAu, 0x00280FFBu,
    0x003010FCu, 0x003811FDu, 0x004012FEu, 0x004813FFu,
    0x00501400u, 0x00581501u, 0x00601602u, 0x00681703u,
    0x00701804u, 0x00781905u, 0x00801A06u, 0x00881B07u,
    0x00901C08u, 0x00981D09u, 0x00A01E0Au, 0x00A81F0Bu,
    0x00B0200Cu, 0x00B8210Du, 0x00C0220Eu, 0x00C8230Fu,
    0x00D02410u, 0x00D82511u, 0x00E02612u, 0x00E82713u,
    0x00F02814u, 0x00F82915u, 0x01002A16u, 0x01082B17u,
    0x01102C18u, 0x01182D19u, 0x01202E1Au, 0x01282F1Bu,
    0x0130301Cu, 0x0138311Du, 0x0140321Eu, 0x0148331Fu,
    0x01503420u, 0x01583521u, 0x01603622u, 0x01683723u,
    0x01703824u, 0x01783925u, 0x01803A26u, 0x01883B27u,
    0x01903C28u, 0x01983D29u, 0x01A03E2Au, 0x01A83F2Bu,
    0x01B0402Cu, 0x01B8412Du, 0x01C0422Eu, 0x01C8432Fu,
    0x01D04430u, 0x01D84531u, 0x01E04632u, 0x01E84733u,
    0x01F04834u, 0x01F84935u, 0x02004A36u, 0x02084B37u,
    0x02104C38u, 0x02184D39u, 0x02204E3Au, 0x02284F3Bu,
    0x0230503Cu, 0x0238513Du, 0x0240523Eu, 0x0248533Fu,
    0x02505440u, 0x02585541u, 0x02605642u, 0x02685743u,
    0x02705844u, 0x02785945u, 0x02805A46u, 0x02885B47u,
    0x02905C48u, 0x02985D49u, 0x02A05E4Au, 0x02A85F4Bu,
    0x02B0604Cu, 0x02B8614Du, 0x02C0624Eu, 0x02C8634Fu,
    0x02D06450u, 0x02D86551u, 0x02E06652u, 0x02E86753u,
    0x02F06854u, 0x02F86955u, 0x03006A56u, 0x03086B57u,
    0x03106C58u, 0x03186D59u, 0x03206E5Au, 0x03286F5Bu,
    0x0330705Cu, 0x0338715Du, 0x0340725Eu, 0x0348735Fu,
    0x03507460u, 0x03587561u, 0x03607662u, 0x03687763u,
    0x03707864u, 0x03787965u, 0x03807A66u, 0x03887B67u,
    0x03907C68u, 0x03987D69u, 0x03A07E6Au, 0x03A87F6Bu,
};

/* AGC table load [SPECSHEET §11.2.5a] */
static void wifi_agc_load(void)
{
    uint32_t i;
    for (i = 0; i < 128u; i++) {
        uint32_t v = wifi_agc_tbl[i];
        int32_t b0 = (int32_t)(int8_t)(v & 0xFFu);
        uint8_t b1 = (v >> 8) & 0xFFu;
        uint8_t b2 = (v >> 16) & 0xFFu;
        uint8_t b3 = (v >> 24) & 0xFFu;
        if (b0 > 63) b0 = 63;
        if (b0 < -64) b0 = -64;
        wifi_bbp_win_write(0, 47, i & 0xFFu);
        wifi_bbp_win_write(0, 43, (uint8_t)b0);
        wifi_bbp_win_write(0, 78, b1);
        wifi_bbp_win_write(0, 84, b2);
        wifi_bbp_win_write(0, 85, b3);
        wifi_bbp_win_write(0, 47, (i & 0xFFu) | 0x80u);
    }
    wifi_bbp_win_write(0, 47, 0);
    wifi_bbp_win_write(0, 84, 0xC0u);
    wifi_bbp_win_write(0, 85, 1);
    wifi_bbp_win_write(0, 78, 66);
    wifi_bbp_win_write(0, 43, 66);
    wifi_bbp_win_write(0, 44, 66);
    wifi_bbp_win_write(0, 121, 0);
    wifi_bbp_win_write(0, 122, 32);
}

/* Minimum viable PHY init [SPECSHEET §11.2.12] */
static void wifi_phy_hw_init(void)
{
    uint32_t i, n;
    cfm1_log("wifi: phy init");

    /* 1. PHY+MAC reset [SPECSHEET §11.2.2] */
    wifi_phy_mac_rst();
    wifi_ind_log("pmrs", 1);

    /* 2. SIFS timing [SPECSHEET §11.2.3] */
    wifi_sifs_reset();

    /* 3. MAC mode register [SPECSHEET §11.2.4] */
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x0308u) = 0x9407u;

    /* 4. AGC table [SPECSHEET §11.2.5a] */
    wifi_agc_load();

    /* 5. CCA table, window C (w=0) [SPECSHEET §11.2.6] */
    n = sizeof(wifi_cca_tbl) / 2u;
    for (i = 0; i < n; i++)
        wifi_bbp_win_write(0, wifi_cca_tbl[i * 2u], wifi_cca_tbl[i * 2u + 1u]);
    cfm1_log_int("wifi: phy cca n=", (int32_t)n);

    /* 5. RX frontend table, window D' (w=3) [SPECSHEET §11.2.7] */
    n = sizeof(wifi_rxfe_tbl) / 2u;
    for (i = 0; i < n; i++)
        wifi_bbp_win_write(3, wifi_rxfe_tbl[i * 2u], wifi_rxfe_tbl[i * 2u + 1u]);

    /* 6. RX 11n table, window C' (w=1) [SPECSHEET §11.2.8] */
    for (i = 0; i < 72u; i++)
        wifi_bbp_win_write(1, i, wifi_rx11n_tbl[i]);

    /* 7. TX backend table, window D (w=2) [SPECSHEET §11.2.8b] */
    wifi_bbp_win_write(2, 0, 0x02u);
    /* Part 1: per-rate gain [SPECSHEET §10] */
    {
        static const uint8_t prg[12] = {
            0x20, 0x20, 0x20, 0x20, 0x20, 0x2B,
            0x26, 0x20, 0x20, 0x2B, 0x26, 0x20
        };
        uint32_t gi = 0;
        for (i = 11; i <= 34; i++) {
            if (i & 1u)
                wifi_bbp_win_write(2, i, prg[gi++]);
            else
                wifi_bbp_win_write(2, i, 0x00u);
        }
    }
    /* Parts 2+3 from table */
    n = sizeof(wifi_txbe_tbl) / 2u;
    for (i = 0; i < n; i++)
        wifi_bbp_win_write(2, wifi_txbe_tbl[i * 2u], wifi_txbe_tbl[i * 2u + 1u]);
    /* CFR Phase 0: initial filter coefficients [SPECSHEET §6] */
    wifi_bbp_win_write(2, 41, 0x6Cu);
    wifi_bbp_win_write(2, 42, 0x01u);
    wifi_bbp_win_write(2, 43, 0x02u);
    wifi_bbp_win_write(2, 44, 0x02u);
    wifi_bbp_win_write(2, 49, 0x03u);
    wifi_bbp_win_write(2, 77, 0x0Fu);
    wifi_bbp_win_write(2, 78, 0x0Fu);
    wifi_bbp_win_write(2, 79, 0x00u);
    wifi_bbp_win_write(2, 0, 0x02u);

    /* RX 11n[60] override: 0xFF -> 0x01 [SPECSHEET §11.2.5 step 10] */
    wifi_bbp_win_write(1, 60, 0x01u);

    /* 8. Final PHY reset [SPECSHEET §11.2.12 step 8].
     * Must come BEFORE direct BBP writes: the R21 toggle (soft reset)
     * clears some direct BBP registers (R66 confirmed cleared). */
    wifi_phy_rst();
    wifi_ind_log("prst", 1);

    /* Direct BBP writes: AC791N only implements BBP[84] and BBP[106]
     * from the RT2800 register set [DISCOVERY §11.2.12 step 10].
     * AGC is handled by the 128-entry digital AGC table (loaded above),
     * not direct R66 like real RT2800. */
    wifi_bbp_write(84, 0x02);
    wifi_bbp_write(106, 0x04);

    cfm1_log_int("wifi: phy bbp_to=", (int32_t)wifi_bbp_timeouts);
    /* H1/H2: verify key BBP registers after PHY init */
    cfm1_log_int("wifi: phy cca0=", (int32_t)wifi_bbp_win_read(0, 0));
    cfm1_log_int("wifi: phy cca6=", (int32_t)wifi_bbp_win_read(0, 6));
    cfm1_log_int("wifi: phy cca14=", (int32_t)wifi_bbp_win_read(0, 14));
    cfm1_log_int("wifi: phy cca72=", (int32_t)wifi_bbp_win_read(0, 72));
    cfm1_log_int("wifi: phy rxfe0=", (int32_t)wifi_bbp_win_read(3, 0));
    cfm1_log_int("wifi: phy bbp84=", (int32_t)wifi_bbp_read(84));
    cfm1_log_int("wifi: phy bbp106=", (int32_t)wifi_bbp_read(106));
    cfm1_log("wifi: phy init done");
}

/* ---- RAPP init [SPECSHEET §7] ----
 * Load 128-entry PA ramp lookup table into TX backend RAM.
 * D[92] bit 4: write pulse, D[93] [6:0]: address, D[94]: data lo,
 * D[92] bit 5: data bit 8. */

static void wifi_rapp_init(void)
{
    uint32_t i, v;

    cfm1_log("wifi: rapp init");

    for (i = 0; i < 128u; i++) {
        /* Linear ramp: 0..254 in steps of 2, clamped to 9-bit range.
         * Smooth ramp avoids abrupt PA transitions. [SPECSHEET §7] */
        v = i * 2u;

        wifi_bbp_win_write(2, 92, 0x00u);
        wifi_bbp_win_write(2, 93, (uint8_t)(i & 0x7Fu));
        wifi_bbp_win_write(2, 94, (uint8_t)(v & 0xFFu));
        wifi_bbp_win_write(2, 92, (uint8_t)(((v >> 8) & 1u) << 5));
        wifi_bbp_win_write(2, 92, (uint8_t)((((v >> 8) & 1u) << 5) | 0x10u));
    }

    /* Ramp thresholds [SPECSHEET §7]: 11g/n=160, 11b=160, BPSK=400 */
    wifi_bbp_win_write(2, 86, 160u);
    wifi_bbp_win_write(2, 87, 160u);
    wifi_bbp_win_write(2, 88, (uint8_t)(400u & 0xFFu));
    wifi_bbp_win_write(2, 89, (uint8_t)(400u >> 8));

    /* Enable RAPP: D[81] bits [1:0] = 2 [SPECSHEET §7] */
    v = wifi_bbp_win_read(2, 81);
    wifi_bbp_win_write(2, 81, (uint8_t)((v & ~0x03u) | 0x02u));

    cfm1_log("wifi: rapp done");
}

/* ---- Apply TX calibration to BBP [SPECSHEET §18, record 187] ----
 * After PHY init resets BBP, re-apply TX IQ gain/phase/DC from
 * wifi_cal to Window D, and RX IQ gain/phase to Window D'. */

static void wifi_cal_bbp_apply(void)
{
    uint32_t k, u, r;

    cfm1_log("wifi: cal bbp apply");

    /* TX IQ gain/phase/DC -> Window D entries D[0x04..0x09], D[0x61], D[0x62]
     * [SPECSHEET §18: bytes 0-23 of record 187] */
    for (k = 0; k < 6u; k++) {
        u = (uint32_t)wifi_cal.tx[k] & 0x3FFu;
        wifi_bbp_win_write(2, 0x04u + k, (uint8_t)(u >> 2));
        r = wifi_bbp_win_read(2, k < 4u ? 0x61u : 0x62u);
        r = (r & ~(3u << (2u * (k & 3u)))) | ((u & 3u) << (2u * (k & 3u)));
        wifi_bbp_win_write(2, k < 4u ? 0x61u : 0x62u, (uint8_t)r);
    }

    /* RX IQ gain/phase -> Window D' entries D'[0x09..0x0C]
     * [SPECSHEET §18: bytes 26-29 of record 187]
     * Use neutral values (gain=255, phase=0) since wifi_cal doesn't
     * measure RX IQ (no image rejection cal). */
    wifi_bbp_win_write(3, 0x09u, 0xFFu);
    wifi_bbp_win_write(3, 0x0Bu, 0xFFu);
    wifi_bbp_win_write(3, 0x0Au, 0x00u);
    wifi_bbp_win_write(3, 0x0Cu, 0x00u);

    cfm1_log_int("wifi: cal d04=", (int32_t)wifi_bbp_win_read(2, 0x04));
    cfm1_log_int("wifi: cal d61=", (int32_t)wifi_bbp_win_read(2, 0x61));
}

/* ---- init (called from wifi_api.c) ---- */

static void wifi_sdio_init(void)
{
    uint32_t i;
    cfm1_log("wifi: sdio init");

    for (i = 0; i < sizeof(wifi_sdio_state); i++)
        ((uint8_t *)&wifi_sdio_state)[i] = 0;

    /* Clock tree and PLL are configured by fm1_ble_rf_run() bytecode (group 3).
     * Verified on hardware: WL_CLK0=0x14F, WL_RST=0x47060C6B after bytecode.
     * Do NOT overwrite WL_RST: the bytecode value includes calibrated PLL
     * divider/multiplier from the full RF init, not the simple wf_clk_rst_init. */

    /* SoC clock select, matched to the running vendor radio [M:hw 2026-11-02,
     * SPECSHEET §4.5]: SYS_CLK2 bit 10 instead of bit 8. Does not start the
     * MAC/BBP by itself (iteration 12g); kept so every writable SoC register
     * equals the vendor's steady state. */
    {
        volatile uint32_t *sysclk2 = (volatile uint32_t *)0x10010u;
        *sysclk2 = (*sysclk2 & ~(1u << 8)) | (1u << 10);
        fm1_delay_us(100);
        cfm1_log_int("wifi: sysclk2=", (int32_t)*sysclk2);
    }

    /* WL_CTL0 (0x14000) is not written at all [SPECSHEET §4.2, §14 row 10,
     * M:s 2026-11-04]: it is a BT/WiFi arbitration control the vendor never
     * touches on a WiFi-only start, and the MAC is readable once the clock
     * tree is up. core-fm1 asserted it (0xFFFF) at boot until 12p, which
     * held the datapaths; 12i-12o needed a release before anything ran.
     * Do NOT write WL_CTL8 (0x14008) either: GPIO hazard (iteration 12f). */

    /* SYS_CLK1 bit 12 clear, as on the running vendor radio [M:hw 2026-11-03,
     * SPECSHEET §4.5]; mirrors into 0x14090 bit 4. The other three SoC
     * differences (0x1400C b29, 0x14058, 0x14060 b23) refuse writes: status,
     * not configuration (iteration 12h). */
    *(volatile uint32_t *)0x10008u &= ~(1u << 12);
    fm1_delay_us(100);
    cfm1_log_int("wifi: sysclk1=", (int32_t)(*(volatile uint32_t *)0x10008u));

    wifi_ind_log("gate", 1);

    /* MAC reset [SPECSHEET §7.4 step 6] */
    wifi_hw_reset();
    wifi_ind_log("rst", 1);

    /* WDMA init replaces WPDMA ring setup [SPECSHEET §7.11.4 steps 1-3] */
    wdma_init();
    wifi_ind_log("wdma", 1);

    /* PHY/MAC hardware init [SPECSHEET §11.2.12]: configure digital
     * baseband for 802.11 frame processing.  This is the "2nd PHY init"
     * (§17 step 19): reload BBP tables after RF calibration undid
     * loopback changes.  Must run after WDMA init and before IRQ. */
    wifi_phy_hw_init();
    wifi_ind_log("phy", 1);

    /* RAPP: PA ramp lookup table + thresholds [SPECSHEET §7] */
    wifi_rapp_init();

    /* Re-apply TX/RX calibration values to BBP windows [SPECSHEET §18].
     * PHY init reset BBP, wiping any cal values written during trim. */
    wifi_cal_bbp_apply();
    wifi_ind_log("cal", 1);

    /* Register WiFi ISR before enabling the interrupt [SPECSHEET §7.11.4]:
     * The vendor's wfhw_iqr_init registers IRQ 52, then enables 0x30328.
     * Without an ISR, the first frame notification deadlocks the MAC. */
    wifi_irq_pending = 0;
    fm1_irq_attach(FM1_IRQ_WIFI, isr_wifi, 2);

    /* Enable MAC RX notification [SPECSHEET §7.11.4 step 4]:
     * Without this, WDMA_CTL bit 17 (RX_AVAIL) never asserts. */
    WIFI_MAC_IRQ_EN = 0u;
    WIFI_MAC_IRQ_EN = 2u;

    /* Enable SoC WiFi interrupt path [SPECSHEET §7.11.4 step 5]: bit 0 only.
     * Bit 23 reads 1 on the running vendor radio and 0 here; it refuses
     * writes (iterations 12e, 12h): a "running" indicator, not a knob. */
    WIFI_SOC_IRQ_EN |= 1u;

    cfm1_log_int("wifi: irq_en=", (int32_t)WIFI_MAC_IRQ_EN);
    cfm1_log_int("wifi: soc_irq=", (int32_t)WIFI_SOC_IRQ_EN);
    wifi_ind_log("irq", 1);

    wifi_sdio_state.initialized = 1;
    cfm1_log("wifi: sdio init done");
}

/* ---- TX ---- */

static void wifi_build_txwi(struct wifi_txwi *txwi, uint32_t frame_len)
{
    /* 1 Mbps CCK, broadcast (WCID=0xFF, no ACK). Mirrors the vendor TXWI
     * measured on hardware [SPECSHEET §7.5a, §7.11.5 measured layout]:
     * MPDU_DENSITY=5, BW_WIN_SIZE=7, software sequence numbers (NSEQ
     * clear: the 802.11 header already carries one). */
    txwi->word0 = (PHYMODE_CCK << TXWI_W0_PHYMODE_SHIFT)
                | (0u << TXWI_W0_MCS_SHIFT)
                | (5u << TXWI_W0_DENSITY_SHIFT);
    txwi->word1 = (7u << TXWI_W1_BW_WIN_SHIFT)
                | (0xFFu << TXWI_W1_WCID_SHIFT)
                | ((frame_len & 0xFFFu) << TXWI_W1_MPDU_LEN_SHIFT);
    txwi->iv  = 0;
    txwi->eiv = 0;
}

static int wifi_sdio_tx(const uint8_t *frame, uint32_t len)
{
    uint32_t wait, i;
    static uint32_t tx_dbg;

    if (!frame || len == 0 || len > WIFI_TX_MAX_FRAME)
        return -1;
    if (!wifi_sdio_state.initialized || !wifi_sdio_state.dma_active) {
        if (tx_dbg < 3u) cfm1_log("wifi: tx not active");
        tx_dbg++;
        return -1;
    }

    /* Staging buffer in the vendor's layout [SPECSHEET §7.11.5 revised
     * 2026-11-04, M:s]: [0..3] software length word (MPDU + 22, NOT
     * transferred), [4..7] queue word (bit 0 WIV, bits [2:1] QSEL = 2: 5 for
     * management, 4 for data), [8..23] TXWI, then the frame unpadded, two
     * zero bytes and four more bytes. The transfer starts at the queue word
     * and is MPDU + 26 bytes long. Iterations 12c/12d started at the TXWI or
     * at the length word (whose low bits select queue 0): neither queued. */
    *(uint32_t *)&wifi_tx_buf[0] = len + 22u;
    *(uint32_t *)&wifi_tx_buf[4] = ((frame[0] & 0x0Cu) == 0u) ? 5u : 4u;
    wifi_build_txwi((struct wifi_txwi *)&wifi_tx_buf[WIFI_TX_HDR], len);
    for (i = 0; i < len; i++)
        wifi_tx_buf[WIFI_TX_HDR + sizeof(struct wifi_txwi) + i] = frame[i];
    for (; i < len + 6u; i++)                          /* 2 + 4 trailing bytes */
        wifi_tx_buf[WIFI_TX_HDR + sizeof(struct wifi_txwi) + i] = 0;

    if (tx_dbg < 3u)
        cfm1_log_int("wifi: tx len=", (int32_t)len);

    /* TX readiness gate [SPECSHEET §7.11.5, M:s]: queue-2 pages (TXRXQ_PCNT
     * byte 2) below the limit (PBF_MAX_PCNT byte 1) with room for the MPDU,
     * 0x30434 bit 18 clear, PBF_DBG [7:5] non-zero. The vendor retries
     * without bound; here the gate is diagnostic (iteration 12l): a few
     * retries, the reads are kept for the STA log, then transmit anyway. */
    {
        static uint32_t tx_gate_dbg;
        uint32_t pcnt = 0, qsta = 0, dbg = 0, limit = (WIFI_PBF_MAX_PCNT >> 8) & 0xFFu;
        wifi_sdio_state.tx_gate = 0;
        for (wait = 0; wait < 5u; wait++) {
            uint32_t pages;
            pcnt = WIFI_TXRXQ_PCNT;
            qsta = WIFI_TXQ_STA;
            dbg = WIFI_PBF_DBG;
            pages = (pcnt >> 16) & 0xFFu;
            if (pages < limit && (limit - pages) * 128u > len &&
                !(qsta & (1u << 18)) && (dbg & 0xE0u)) {
                wifi_sdio_state.tx_gate = 1;
                break;
            }
            fm1_delay_ms(10);
        }
        wifi_sdio_state.tx_gate_pcnt = pcnt;
        wifi_sdio_state.tx_gate_qsta = qsta;
        wifi_sdio_state.tx_gate_dbg = dbg;
        if (!wifi_sdio_state.tx_gate && tx_gate_dbg < 3u) {
            cfm1_log("wifi: tx gate not ready, sending anyway");
            tx_gate_dbg++;
        }
    }

    /* WDMA synchronous TX [SPECSHEET §7.11.5 steps 1-7] */
    wifi_sdio_claim();

    WDMA_CTL |= WDMA_CTL_TX_ACK;                     /* 1: ack/clear   */
    __asm__ volatile("csync" ::: "memory");            /* 2: barrier     */
    /* 3-4: source = the queue word, MPDU + 26 bytes [M:s] */
    WDMA_TX_ADR = (uint32_t)&wifi_tx_buf[4];
    WDMA_TX_LEN = len + 26u;                           /* triggers TX */
    __asm__ volatile("csync" ::: "memory");            /* 5: barrier     */

    for (wait = 0; wait < 10000u; wait++) {            /* 6: poll done   */
        if (WDMA_CTL & WDMA_CTL_TX_DONE)
            break;
        fm1_delay_us(1);
    }
    /* Queue depth while the frame can still be queued [IP:RT2x00 TXRXQ_PCNT]:
     * 0 here means the MAC never took the frame from WDMA. Logged by wifi_sta.c. */
    wifi_sdio_state.tx_qpcnt = WIFI_TXRXQ_PCNT;
    WDMA_CTL |= WDMA_CTL_TX_ACK;                     /* 7: ack done    */

    wifi_sdio_release();

    if (wait >= 10000u) {
        if (tx_dbg < 3u)
            cfm1_log("wifi: wdma tx timeout");
        tx_dbg++;
        if (wifi_sdio_state.error_count < 255u)
            wifi_sdio_state.error_count++;
        return -1;
    }
    return 0;
}

/* ---- CMD53 for MAC addr read ---- */

static int wifi_sdio_cmd53_read(uint8_t fn, uint32_t addr, uint8_t *dst,
                                uint32_t len, int incr)
{
    (void)fn;
    (void)incr;

    /* MAC address from MAC_ADDR_DW0/DW1 [SPECSHEET §7.2] */
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

/* ---- WDMA RX DMA phase helper ---- */

static int wdma_rx_phase(uint32_t dst, uint32_t nbytes, uint32_t timeout_us)
{
    uint32_t wait;

    WDMA_CTL |= WDMA_CTL_RX_ACK;
    __asm__ volatile("csync" ::: "memory");
    WDMA_RX_ADR = dst;
    WDMA_RX_LEN = nbytes;
    __asm__ volatile("csync" ::: "memory");

    for (wait = 0; wait < timeout_us; wait++) {
        if (WDMA_CTL & WDMA_CTL_RX_DONE)
            break;
        fm1_delay_us(1);
    }
    WDMA_CTL |= WDMA_CTL_RX_ACK;
    return (wait < timeout_us) ? 0 : -1;
}

/* ---- poll (called from wifi_api.c) ---- */

static void wifi_sdio_poll_irq(void)
{
    uint32_t rx_len, frame_len, ctl, n;

    if (!wifi_sdio_state.initialized || !wifi_sdio_state.dma_active)
        return;

    /* The ISR sets wifi_irq_pending when 0x30328 bit 0 fires.
     * Clear it and check WDMA_CTL: the MAC may have already posted
     * the frame to the WDMA engine by the time we poll. */
    if (wifi_irq_pending) {
        wifi_irq_pending = 0;
    }

    ctl = WDMA_CTL;

    /* Drain available RX frames [SPECSHEET §7.11.6], at most a few per poll
     * so the cooperative scheduler (and the watchdog feed) keep running. */
    for (n = 0; n < 8u && (ctl & WDMA_CTL_RX_AVAIL); n++) {

        /* Pre-RX MAC write [SPECSHEET §7.11.6]: purpose unknown */
        WIFI_MAC_RX_PREP = 2u;

        /* Phase 1: 4-byte RX length [SPECSHEET §7.11.6 phase 1] */
        if (wdma_rx_phase((uint32_t)&wifi_rx_buf[0], 4u, 1000u) < 0) {
            cfm1_log("wifi: rx phase1 timeout");
            break;
        }

        /* Byte-swap: big-endian from MAC [SPECSHEET §7.11.6] */
        rx_len = __builtin_bswap32(*(uint32_t *)&wifi_rx_buf[0]);
        if (rx_len == 0 || rx_len >= 4089u) {
            if (wifi_sdio_state.rx_count < 5u)
                cfm1_log_int("wifi: rx bad len=", (int32_t)rx_len);
            break;
        }
        if (rx_len + 4u > WIFI_RX_BUF_SIZE - 4u)
            break;

        /* Phase 2: RXWI + frame [SPECSHEET §7.11.6 phase 2] */
        if (wdma_rx_phase((uint32_t)&wifi_rx_buf[4], rx_len + 4u, 1000u) < 0) {
            cfm1_log("wifi: rx phase2 timeout");
            break;
        }

        /* MPDU length from RXWI word 0 [SPECSHEET §7.5b] */
        frame_len = (*(uint32_t *)&wifi_rx_buf[4] & RXWI_W0_MPDU_LEN_MASK)
                  >> RXWI_W0_MPDU_LEN_SHIFT;

        wifi_sdio_state.rx_count++;
        if (wifi_sdio_state.rx_count == 1u)
            cfm1_log("wifi: rx first");
        if (wifi_sdio_state.rx_count <= 5u)
            cfm1_log_int("wifi: rx len=", (int32_t)frame_len);

        if (frame_len >= 24 && frame_len <= (WIFI_RX_BUF_SIZE - 4u - RXWI_SIZE))
            wifi_mac_rx(wifi_rx_buf + 4 + RXWI_SIZE, frame_len);

        ctl = WDMA_CTL;
    }

    /* Re-open the SoC interrupt mask the handler closed [§7.11.6 step 7]:
     * the single receive buffer is free again once the loop above returns. */
    WIFI_SOC_IRQ_EN |= 1u;
}

/* ---- BBP snapshot for the vendor comparison [SPECSHEET §6.1, §6.3] ----
 * One DAP line per 16 registers, packed as 32 hex digits with no spaces so
 * the row fits the smallest CFM1_LOG_LINE_LEN (40): 4-char tag + 2-digit
 * base + ':' + 32 digits + NUL. Covers everything the hardware can read
 * back: direct R0-R127, window C (CCA) 0x00-0x7F, window D (TX backend)
 * 0x00-0x5F. Windows C' and D' have no read-back register. */
static void wifi_bbp_row(const char *tag, uint32_t base, uint32_t w, int direct)
{
    static const char hex[] = "0123456789ABCDEF";
    char line[40];
    uint32_t i, v;

    for (i = 0; i < 4u; i++)
        line[i] = tag[i];
    line[4] = hex[(base >> 4) & 0xFu];
    line[5] = hex[base & 0xFu];
    line[6] = ':';
    for (i = 0; i < 16u; i++) {
        v = direct ? wifi_bbp_read(base + i) : wifi_bbp_win_read(w, base + i);
        line[7u + 2u * i] = hex[(v >> 4) & 0xFu];
        line[8u + 2u * i] = hex[v & 0xFu];
    }
    line[39] = 0;
    cfm1_log(line);
}

static void wifi_bbp_snapshot(void)
{
    uint32_t b;
    cfm1_log("wifi: bbp snapshot");
    for (b = 0; b < 128u; b += 16u) wifi_bbp_row("bbpR", b, 0, 1);  /* direct */
    for (b = 0; b < 0x80u; b += 16u) wifi_bbp_row("bbpC", b, 0, 0);  /* window C */
    for (b = 0; b < 0x60u; b += 16u) wifi_bbp_row("bbpD", b, 2, 0);  /* window D */
}
