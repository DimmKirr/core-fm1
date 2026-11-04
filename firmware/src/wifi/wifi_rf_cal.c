/* SPDX-License-Identifier: GPL-3.0-only */
/* Clean-room RF calibration for AC791N (WL82).
 * From docs/hardware/wifi/SPECSHEET.md §10.1 (register map, calibration procedure),
 * and fm1-base bsp/chip/fm1_rf_cal.h (Apache-2.0, charlesvestal/fm1-base).
 * No vendor code, IR or disassembly. */

/* ---- constants ---- */

#define WCAL_TUNE_US      5000u   /* RC tune engine timeout per run */
#define WCAL_DEM_US       5000u   /* estimator read timeout */
#define WCAL_TOP_FALLBACK 63u     /* VCO scan top when no mid bank found */
#define WCAL_MAC_1004     0x31004u

enum {
    WCAL_OK        =  0,
    WCAL_E_MIDBANK = -1,
    WCAL_E_FILTER  = -2,
    WCAL_E_TXLO    = -3,
    WCAL_E_RXDC    = -5
};

/* ---- calibration state ---- */

static struct wifi_rf_cal {
    uint8_t  limit[48];
    uint8_t  top, mid, mid_found, bank121;
    uint8_t  flt[3];
    uint8_t  txlo_i, txlo_q;
    uint8_t  abort;
    uint16_t rx_dc[4][2];
    int32_t  tx[6];
    int32_t  err;
    uint32_t us_scan, us_filter, us_txlo, us_rxdc, us_total;
    uint32_t to_filter, to_dem;
    uint32_t dem_reads;
    uint32_t bbp_timeouts;
} wifi_cal;

static uint8_t wifi_cal_x187[68];

/* ---- small helpers ---- */

static void wcal_rmw(uint32_t a, uint32_t mask, uint32_t v)
{
    FM1_RF_REG(a) = (FM1_RF_REG(a) & ~mask) | (v & mask);
}

static void wcal_bit(uint32_t a, uint32_t bit, uint32_t on)
{
    if (on) FM1_RF_REG(a) |= 1u << bit;
    else    FM1_RF_REG(a) &= ~(1u << bit);
}

static uint32_t wcal_us_since(uint32_t t0)
{
    return (fm1_ticks() - t0) / FM1_TICKS_PER_US;
}

/* BBP window shorthands (CCA=0, D=2, D'=3) */
#define WCAL_C  0u
#define WCAL_D  2u
#define WCAL_DP 3u
#define wcal_c_wr(e, v)   wifi_rf_bbp_win_wr(WCAL_C, (e), (v))
#define wcal_c_rd(e)      wifi_rf_bbp_win_rd(WCAL_C, (e))
#define wcal_d_wr(e, v)   wifi_rf_bbp_win_wr(WCAL_D, (e), (v))
#define wcal_d_rd(e)      wifi_rf_bbp_win_rd(WCAL_D, (e))
#define wcal_dp_wr(e, v)  wifi_rf_bbp_win_wr(WCAL_DP, (e), (v))
#define wcal_dp_rd(e)     wifi_rf_bbp_win_rd(WCAL_DP, (e))

/* analog read port: sel into 0x11968 [31:28], strobe 0x11978 8x1 then 0, read */
static uint32_t wcal_get(uint32_t sel)
{
    uint32_t i;
    FM1_RF_REG(0x11968u) = (FM1_RF_REG(0x11968u) & 0x0FFFFFFFu) | sel << 28;
    for (i = 0; i < 8u; i++)
        FM1_RF_REG(0x11978u) = 1;
    FM1_RF_REG(0x11978u) = 0;
    return FM1_RF_REG(0x11978u);
}

static int32_t wcal_sext10(uint32_t v)
{
    v &= 0x3FFu;
    return (v & 0x200u) ? (int32_t)v - 0x400 : (int32_t)v;
}

static void wcal_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t wcal_u32(const uint8_t *p)
{
    return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* ---- RF enable table (SPECSHEET §10.1, fm1-base fm1_rfc_en_tab) ---- */

enum { WCAL_F_A, WCAL_F_RX, WCAL_F_R, WCAL_F_T, WCAL_F_TXC, WCAL_F_RXC };
#define WCAL_EN(off, bit, f) ((off) / 4u << 8 | (bit) << 3 | (f))

static const uint16_t wcal_en_tab[29] = {
    WCAL_EN(0x00, 23, WCAL_F_A),  WCAL_EN(0x04, 15, WCAL_F_RX), WCAL_EN(0x0C, 4, WCAL_F_R),
    WCAL_EN(0x0C, 5, WCAL_F_R),   WCAL_EN(0x0C, 20, WCAL_F_R),  WCAL_EN(0x10, 12, WCAL_F_A),
    WCAL_EN(0x10, 27, WCAL_F_A),  WCAL_EN(0x14, 11, WCAL_F_A),  WCAL_EN(0x14, 18, WCAL_F_A),
    WCAL_EN(0x18, 0, WCAL_F_A),   WCAL_EN(0x18, 1, WCAL_F_A),   WCAL_EN(0x14, 26, WCAL_F_A),
    WCAL_EN(0x08, 6, WCAL_F_T),   WCAL_EN(0x18, 19, WCAL_F_T),  WCAL_EN(0x1C, 0, WCAL_F_T),
    WCAL_EN(0x1C, 4, WCAL_F_T),   WCAL_EN(0x20, 9, WCAL_F_T),   WCAL_EN(0x24, 5, WCAL_F_T),
    WCAL_EN(0x24, 7, WCAL_F_T),   WCAL_EN(0x24, 8, WCAL_F_T),   WCAL_EN(0x24, 27, WCAL_F_T),
    WCAL_EN(0x28, 12, WCAL_F_T),  WCAL_EN(0x28, 26, WCAL_F_T),  WCAL_EN(0x38, 26, WCAL_F_R),
    WCAL_EN(0x38, 27, WCAL_F_T),  WCAL_EN(0x10, 13, WCAL_F_TXC), WCAL_EN(0x2C, 14, WCAL_F_TXC),
    WCAL_EN(0x2C, 11, WCAL_F_RXC), WCAL_EN(0x08, 29, WCAL_F_RXC),
};

/* mode: 0=RX, 1=TX, 2=TX_CAL, 3=RX_CAL, 4=RX_DC_CAL, 5=synth only */
static void wcal_en_set(uint32_t mode)
{
    uint32_t rx = mode == 0 || mode > 5, tx = mode == 1;
    uint32_t txc = mode == 2, rxc = mode == 3, rxdc = mode == 4;
    uint32_t f[6], i, e;
    f[WCAL_F_A]   = rxc | rx | txc | rxdc;
    f[WCAL_F_RX]  = rx;
    f[WCAL_F_R]   = rxc | rx | rxdc;
    f[WCAL_F_T]   = rxc | tx | txc;
    f[WCAL_F_TXC] = txc;
    f[WCAL_F_RXC] = rxc;
    for (i = 0; i < 29u; i++) {
        e = wcal_en_tab[i];
        wcal_bit(0x11900u + 4u * (e >> 8), e >> 3 & 31u, f[e & 7u]);
    }
}

/* ---- VGA gain table (42 steps, SPECSHEET §10.1, fm1-base fm1_rfc_vga) ---- */

static const uint8_t wcal_vga[42][4] = {
    {1, 0, 0x10, 0}, {1, 0, 0x11, 0}, {1, 0, 0x12, 0}, {1, 0, 0x13, 0}, {1, 0, 0x14, 0}, {1, 0, 0x15, 0},
    {1, 0, 0x20, 1}, {1, 0, 0x21, 1}, {1, 0, 0x22, 1}, {1, 0, 0x23, 1}, {1, 0, 0x24, 1}, {1, 0, 0x25, 1},
    {1, 1, 0x20, 2}, {1, 1, 0x21, 2}, {1, 1, 0x22, 2}, {1, 1, 0x23, 2}, {1, 1, 0x24, 2}, {1, 1, 0x25, 2},
    {2, 1, 0x20, 3}, {2, 1, 0x21, 3}, {2, 1, 0x22, 3}, {2, 1, 0x23, 3}, {2, 1, 0x24, 3}, {2, 1, 0x25, 3},
    {2, 1, 0x40, 4}, {2, 1, 0x41, 4}, {2, 1, 0x42, 4}, {2, 1, 0x43, 4}, {2, 1, 0x44, 4}, {2, 1, 0x45, 4},
    {2, 2, 0x40, 5}, {2, 2, 0x41, 5}, {2, 2, 0x42, 5}, {2, 2, 0x43, 5}, {2, 2, 0x44, 5}, {2, 2, 0x45, 5},
    {4, 2, 0x40, 6}, {4, 2, 0x41, 6}, {4, 2, 0x42, 6}, {4, 2, 0x43, 6}, {4, 2, 0x44, 6}, {4, 2, 0x45, 6},
};

static void wcal_vga_gain(uint32_t g)
{
    const uint8_t *v = wcal_vga[g < 42u ? g : 41u];
    wcal_bit(0x11910u, 22, v[0]);
    wcal_bit(0x11910u, 23, v[0] >> 1);
    wcal_bit(0x11910u, 24, v[0] >> 2);
    wcal_bit(0x11910u, 25, v[1]);
    wcal_bit(0x11910u, 26, v[1] >> 1);
    wcal_rmw(0x11914u, 7u << 15, (uint32_t)v[2] << 15);
    wcal_bit(0x11914u, 12, v[2] >> 4);
    wcal_bit(0x11914u, 13, v[2] >> 5);
    wcal_bit(0x11914u, 14, v[2] >> 6);
    wcal_rmw(0x11914u, 7u << 5, (uint32_t)v[3] << 5);
}

/* ---- highpass filter ---- */

static void wcal_hp_open(void)
{
    wcal_rmw(0x11914u, 7u, 1u);
    FM1_RF_REG(0x11914u) |= 1u << 3;
    FM1_RF_REG(0x11914u) |= 1u << 4;
}

static void wcal_hp_close(void)
{
    FM1_RF_REG(0x11914u) &= ~7u;
    FM1_RF_REG(0x11914u) &= ~(1u << 3);
    FM1_RF_REG(0x11914u) &= ~(1u << 4);
}

/* ---- loopback attenuation ---- */

static void wcal_cal_att(uint32_t t)
{
    FM1_RF_REG(0x11920u) &= ~(1u << 20);
    FM1_RF_REG(0x11920u) |= 1u << 21;
    if (t == 1u) {
        FM1_RF_REG(0x11920u) &= ~(1u << 22);
        FM1_RF_REG(0x11920u) |= 1u << 23;
    } else {
        FM1_RF_REG(0x11920u) |= 1u << 22;
        FM1_RF_REG(0x11920u) &= ~(1u << 23);
    }
    wcal_rmw(0x11924u, 7u << 28, 5u << 28);
    FM1_RF_REG(0x11928u) |= 3u << 13;
    wcal_rmw(0x11928u, 3u << 27, (t == 1u ? 3u : 1u) << 27);
    wcal_rmw(0x11910u, 0xFu << 8, 9u << 8);
}

/* ---- PHY helpers for calibration ---- */

static void wcal_phy_rst(void)
{
    FM1_RF_REG(WCAL_MAC_1004) = 12;
    wifi_rf_bbp(0x16, 0, 0);
    wifi_rf_bbp(0x15, 0x31, 0);
    wifi_rf_bbp(0x15, 0x30, 0);
    wifi_rf_bbp(0x19, 0, 0);
}

static void wcal_loop_test(void)
{
    wcal_phy_rst();
    FM1_RF_REG(WCAL_MAC_1004) = 12;
    wifi_rf_bbp(0x19, 0x18, 0);
}

static void wcal_cca_for_trim(void)
{
    wcal_c_wr(0x40, wcal_c_rd(0x40) & 0x7Fu);
    wcal_c_wr(0x41, wcal_c_rd(0x41) & 0x7Fu);
    wcal_c_wr(0x28, 0);
    wcal_c_wr(0x27, 5);
    wifi_rf_bbp(0x04, wifi_rf_bbp(0x04, 0, 1) & ~0x18u, 0);
}

/* ---- estimator (dem_filter_dc) ---- */

static int32_t wcal_dem(struct wifi_rf_cal *c, uint32_t sel)
{
    uint32_t t0, a, b, hi, mid;
    int32_t i, q;
    c->dem_reads++;
    wcal_dp_wr(0x15, 1);
    wcal_dp_wr(0x16, 1);
    wcal_dp_wr(0x15, 3);
    wcal_dp_wr(0x16, 3);
    t0 = fm1_ticks();
    while (wcal_dp_rd(0x8D) & 3u)
        if (fm1_ticks() - t0 > WCAL_DEM_US * FM1_TICKS_PER_US) {
            c->to_dem++;
            c->abort = 1;
            return 0;
        }
    if (sel == 1u || sel == 2u) {
        a = sel == 1u ? 0x81u : 0x8Au;
        hi = wcal_dp_rd(a);
        mid = wcal_dp_rd(a + 1u);
        b = wcal_dp_rd(a + 2u);
        return (int32_t)(hi << 24 | mid << 16 | b << 8) / 256;
    }
    a = sel == 0u || sel == 3u ? 0x81u : 0x87u;
    b = sel == 0u || sel == 3u ? 0x84u : 0x8Au;
    hi = wcal_dp_rd(a);
    i = (int16_t)(hi << 8 | wcal_dp_rd(a + 1u));
    hi = wcal_dp_rd(b);
    q = (int16_t)(hi << 8 | wcal_dp_rd(b + 1u));
    return (int32_t)((uint32_t)(i * i) + (uint32_t)(q * q));
}

/* ---- ADC peak / auto-tune ---- */

static uint32_t wcal_adc_peak(void)
{
    uint32_t p;
    wcal_dp_wr(0x17, 0x82);
    fm1_delay_us(1000);
    p = wcal_dp_rd(0x80);
    wcal_dp_wr(0x17, 2);
    return p;
}

static uint32_t wcal_adc_pp_tune(void)
{
    uint32_t g = 0, p;
    wcal_vga_gain(0);
    wcal_adc_peak();
    p = wcal_adc_peak();
    while (p <= 47u && g <= 35u) {
        g++;
        wcal_vga_gain(g);
        wcal_adc_peak();
        p = wcal_adc_peak();
    }
    wcal_vga_gain(g);
    return g;
}

/* ---- TX LO trim helpers ---- */

static void wcal_txlo_i(uint32_t v) { wcal_rmw(0x1191Cu, 0x7Fu << 8, (v & 0x7Fu) << 8); }
static void wcal_txlo_q(uint32_t v) { wcal_rmw(0x11920u, 0x7Fu, v & 0x7Fu); }

/* ---- analog RX DC trims ---- */

static uint32_t wcal_anl_rxdc(uint32_t shift, uint32_t sbit, int32_t dc)
{
    int16_t d = (int16_t)dc;
    if (d >= 0) {
        wcal_rmw(0x11908u, 0x1FFu << shift, (uint32_t)d << shift);
        FM1_RF_REG(0x11908u) |= 1u << sbit;
        return (uint16_t)(d | 0x200);
    }
    wcal_rmw(0x11908u, 0x1FFu << shift, (uint32_t)(-d) << shift);
    FM1_RF_REG(0x11908u) &= ~(1u << sbit);
    return (uint16_t)-d;
}

#define wcal_anl_rxdc_i(dc) wcal_anl_rxdc(7, 16, (dc))
#define wcal_anl_rxdc_q(dc) wcal_anl_rxdc(17, 26, (dc))

/* ---- TX window-D helpers ---- */

enum { WCAL_TX_IG, WCAL_TX_IP, WCAL_TX_QG, WCAL_TX_QP, WCAL_TX_IDC, WCAL_TX_QDC };

static void wcal_tx_set(struct wifi_rf_cal *c, uint32_t k, int32_t v)
{
    uint32_t e = k < 4u ? 0x61u : 0x62u, s = 2u * (k & 3u), u = (uint32_t)v, r;
    wcal_d_wr(0x04u + k, u >> 2 & 0xFFu);
    r = wcal_d_rd(e);
    wcal_d_wr(e, (r & ~(3u << s)) | (u & 3u) << s);
    c->tx[k] = v;
}

static void wcal_tx_neutral(struct wifi_rf_cal *c)
{
    wcal_tx_set(c, WCAL_TX_IG, 1023);
    wcal_tx_set(c, WCAL_TX_QG, 1023);
    wcal_tx_set(c, WCAL_TX_IP, 0);
    wcal_tx_set(c, WCAL_TX_QP, 0);
    wcal_tx_set(c, WCAL_TX_IDC, 0);
    wcal_tx_set(c, WCAL_TX_QDC, 0);
}

/* ---- VCO bank scan (SPECSHEET §10.1 / §11.1) ---- */

static void wcal_fbds(uint32_t fbds) { wcal_rmw(0x1193Cu, 0xFFu << 5, fbds << 5); }
static void wcal_cap(uint32_t cap)   { wcal_rmw(0x11938u, 0x7Fu << 19, (cap & 0x7Fu) << 19); }

static void wcal_rn_en(void)
{
    FM1_RF_REG(0x11938u) &= ~(1u << 16);
    FM1_RF_REG(0x11938u) &= ~(1u << 18);
    FM1_RF_REG(0x11934u) &= ~(1u << 24);
    fm1_delay_us(5);
    FM1_RF_REG(0x11938u) |= 1u << 16;
    fm1_delay_us(5);
    FM1_RF_REG(0x11934u) |= 1u << 24;
    FM1_RF_REG(0x11938u) |= 1u << 18;
    fm1_delay_us(100);
}

static uint32_t wcal_vc_res(void)
{
    uint32_t r = wcal_get(1);
    return (r >> 18 & 1u) | (r >> 16 & 2u);
}

static void wcal_vc_det(uint32_t on) { wcal_bit(0x11940u, 24, on); }

static uint32_t wcal_midbank(struct wifi_rf_cal *c)
{
    uint32_t mid, m, l, h;
    wcal_fbds(73);
    for (mid = 7; mid < 120u; mid += 16u) {
        wcal_cap(mid);
        wcal_rn_en();
        m = wcal_vc_res();
        wcal_cap(mid - 7u);
        wcal_rn_en();
        l = wcal_vc_res();
        wcal_cap(mid + 8u);
        wcal_rn_en();
        h = wcal_vc_res();
        if (h != m || l != m) {
            c->mid = (uint8_t)mid;
            c->mid_found = 1;
            return mid | 15u;
        }
    }
    c->mid = 0;
    c->mid_found = 0;
    return WCAL_TOP_FALLBACK;
}

static uint32_t wcal_bank(const struct wifi_rf_cal *c, uint32_t fbds)
{
    uint32_t i, f = fbds & 0xFFu;
    for (i = 1; i < 48u; i++)
        if (c->limit[i] > f)
            return (uint8_t)(c->limit[47] - (i - 1u));
    return (uint8_t)(c->limit[47] - 47u);
}

static int wcal_vco_scan(struct wifi_rf_cal *c)
{
    uint32_t t0 = fm1_ticks(), top, prev_h = 73, lo = 73, cur_lo, h, f, r, i;
    wcal_vc_det(1);
    top = wcal_midbank(c);
    c->top = (uint8_t)top;
    c->limit[47] = (uint8_t)top;
    for (i = 0; i < 47u; i++) {
        wcal_cap((top - i) & 0xFFu);
        f = cur_lo = lo;
        for (;;) {
            wcal_fbds(f);
            wcal_rn_en();
            r = wcal_vc_res();
            if (r == 1u) {
                h = f;
                break;
            }
            if (r == 2u)
                cur_lo = f;
            if (f + 1u > 171u) {
                h = 171;
                break;
            }
            f++;
        }
        c->limit[i] = (uint8_t)((prev_h + cur_lo - 3u) / 2u);
        prev_h = h;
        lo = cur_lo;
    }
    wcal_fbds(121);
    c->bank121 = (uint8_t)wcal_bank(c, 121);
    wcal_cap(c->bank121);
    wcal_vc_det(0);
    c->us_scan = wcal_us_since(t0);
    return c->mid_found ? WCAL_OK : WCAL_E_MIDBANK;
}

/* ---- RC filter tune (SPECSHEET §10.1 step 1) ---- */

static int wcal_rx_tune(struct wifi_rf_cal *c)
{
    static const uint16_t code[3] = {0x1FC, 0x122, 0x0A0};
    uint32_t k, t0, r6, r10, r20;
    wcal_rmw(0x11904u, 0xFu << 8, 8u << 8);
    FM1_RF_REG(0x11950u) |= 1u << 27;
    for (k = 0; k < 3u; k++) {
        wcal_rmw(0x11968u, 0x3FEu, code[k]);
        wcal_rmw(0x11968u, 0x7Fu << 11, 15u << 11);
        FM1_RF_REG(0x11968u) &= ~(0x1Fu << 18);
        FM1_RF_REG(0x11968u) &= ~(1u << 23);
        FM1_RF_REG(0x11968u) |= 1u;
        t0 = fm1_ticks();
        while (!(wcal_get(1) & 0x20u))
            if (fm1_ticks() - t0 > WCAL_TUNE_US * FM1_TICKS_PER_US) {
                c->to_filter++;
                FM1_RF_REG(0x11968u) &= ~1u;
                FM1_RF_REG(0x11950u) &= ~(1u << 27);
                return WCAL_E_FILTER;
            }
        c->flt[k] = (uint8_t)(wcal_get(1) >> 8);
        FM1_RF_REG(0x11968u) &= ~1u;
    }
    r6 = c->flt[0];
    r10 = c->flt[1];
    r20 = c->flt[2];
    FM1_RF_REG(0x11950u) &= ~(1u << 27);
    wcal_rmw(0x11920u, 0xFFu << 10, r10 << 10);
    wcal_rmw(0x11910u, 0xFFu << 14, r10 << 14);
    FM1_RF_REG(0x1195Cu) |= 0xFFu;
    wcal_rmw(0x1195Cu, 0xFFu << 8, r6 << 8);
    wcal_rmw(0x1195Cu, 0xFFu << 16, r10 << 16);
    wcal_rmw(0x1195Cu, 0xFFu << 24, r20 << 24);
    FM1_RF_REG(0x11960u) |= 0xFFu;
    wcal_rmw(0x11960u, 0xFFu << 8, r6 << 8);
    wcal_rmw(0x11960u, 0xFFu << 16, r10 << 16);
    wcal_rmw(0x11960u, 0xFFu << 24, r20 << 24);
    return WCAL_OK;
}

/* ---- TX LO search (SPECSHEET §10.1 step 2) ---- */

static int32_t wcal_txlo_search(struct wifi_rf_cal *c, void (*set)(uint32_t))
{
    int32_t best, m;
    uint32_t i, b = 64;
    set(64);
    best = wcal_dem(c, 0);
    for (i = 0; i < 128u && !c->abort; i++) {
        set(i);
        m = wcal_dem(c, 0);
        if (m < best) {
            best = m;
            b = i;
        }
    }
    if (c->abort)
        return -1;
    set(b);
    return (int32_t)b;
}

static int wcal_txiq_tune(struct wifi_rf_cal *c)
{
    uint32_t t0 = fm1_ticks();
    int32_t r;
    wcal_phy_rst();
    wcal_c_wr(0x15, 0);
    wcal_c_wr(0x14, 0);
    wcal_cca_for_trim();
    wcal_loop_test();
    wcal_d_wr(0x00, 3);
    wcal_d_wr(0x5F, 16);
    wcal_d_wr(0x01, 20);
    wcal_d_wr(0x02, 0);
    wcal_d_wr(0x03, 0);
    wcal_dp_wr(0x00, 62);
    wcal_dp_wr(0x13, 30);
    wcal_dp_wr(0x14, 56);
    wcal_dp_wr(0x14, 52);
    wcal_tx_neutral(c);
    wcal_txlo_i(64);
    wcal_txlo_q(64);
    wcal_adc_pp_tune();
    c->abort = 0;
    r = wcal_txlo_search(c, wcal_txlo_i);
    if (r >= 0) {
        c->txlo_i = (uint8_t)r;
        r = wcal_txlo_search(c, wcal_txlo_q);
        c->txlo_q = (uint8_t)r;
    }
    c->us_txlo = wcal_us_since(t0);
    if (r < 0) {
        wcal_txlo_i(64);
        wcal_txlo_q(64);
        c->txlo_i = c->txlo_q = 64;
        return WCAL_E_TXLO;
    }
    return WCAL_OK;
}

/* ---- RX DC successive approximation (SPECSHEET §10.1 step 3) ---- */

static int wcal_rxdc_tune(struct wifi_rf_cal *c)
{
    static const uint8_t gs[4][3] = {{0, 0, 127}, {1, 0, 127}, {1, 3, 69}, {1, 9, 14}};
    uint32_t t0 = fm1_ticks(), g, j;
    int32_t bi, bq, s, m;
    wcal_phy_rst();
    wcal_c_wr(0x15, 0);
    wcal_c_wr(0x14, 0);
    wcal_cca_for_trim();
    wcal_loop_test();
    wcal_d_wr(0x00, 3);
    wcal_d_wr(0x5F, 16);
    wcal_dp_wr(0x00, 62);
    wcal_dp_wr(0x1B, 56);
    wcal_dp_wr(0x08, 0xC0);
    wcal_dp_wr(0x13, 0);
    wcal_dp_wr(0x14, 56);
    wcal_dp_wr(0x14, 52);
    wcal_vga_gain(40);
    c->abort = 0;
    for (g = 0; g < 4u && !c->abort; g++) {
        wcal_rmw(0x1190Cu, 3u << 6, (uint32_t)gs[g][0] << 6);
        wcal_rmw(0x11910u, 0xFu << 8, (uint32_t)gs[g][1] << 8);
        wcal_rmw(0x11910u, 0x7Fu, gs[g][2]);
        wcal_anl_rxdc_i(0);
        wcal_anl_rxdc_q(0);
        bi = bq = 0;
        s = 256;
        for (j = 0; j < 12u && !c->abort; j++) {
            m = wcal_dem(c, 1);
            if (c->abort)
                break;
            if (m > 0 && bi > -510)
                bi -= s;
            else if (m < 0 && bi < 510)
                bi += s;
            wcal_anl_rxdc_i(bi);
            m = wcal_dem(c, 2);
            if (m > 0 && bq > -510)
                bq -= s;
            else if (m < 0 && bq < 510)
                bq += s;
            wcal_anl_rxdc_q(bq);
            s = s > 1 ? s >> 1 : 1;
        }
        if (c->abort)
            break;
        c->rx_dc[g][0] = (uint16_t)wcal_anl_rxdc_i(bi);
        c->rx_dc[g][1] = (uint16_t)wcal_anl_rxdc_q(bq);
    }
    c->us_rxdc = wcal_us_since(t0);
    if (c->abort) {
        wcal_anl_rxdc_i(0);
        wcal_anl_rxdc_q(0);
        for (; g < 4u; g++)
            c->rx_dc[g][0] = c->rx_dc[g][1] = 0x200;
        return WCAL_E_RXDC;
    }
    return WCAL_OK;
}

/* ---- record build (SPECSHEET §10.1 step 4) ---- */

static uint16_t wcal_crc16(const uint8_t *p, uint32_t n)
{
    uint32_t crc = 0, i, b;
    for (i = 0; i < n; i++) {
        crc ^= (uint32_t)p[i] << 8;
        for (b = 0; b < 8u; b++)
            crc = crc & 0x8000u ? (crc << 1 ^ 0x1021u) & 0xFFFFu : (crc << 1) & 0xFFFFu;
    }
    return (uint16_t)crc;
}

static void wcal_record(const struct wifi_rf_cal *c, uint8_t x187[68])
{
    uint32_t k, r, mag;
    for (k = 0; k < 68u; k++)
        x187[k] = 0;
    for (k = 0; k < 6u; k++) {
        r = (uint32_t)c->tx[k] & 0x3FFu;
        wcal_put32(x187 + 4u * k, (k == WCAL_TX_IG || k == WCAL_TX_QG) ? r : (uint32_t)wcal_sext10(r));
    }
    x187[24] = (uint8_t)(FM1_RF_REG(0x1191Cu) >> 8 & 0x7Fu);
    x187[25] = (uint8_t)(FM1_RF_REG(0x11920u) & 0x7Fu);
    x187[26] = 255;
    x187[27] = 255;
    r = FM1_RF_REG(0x11908u);
    mag = r >> 7 & 0x1FFu;
    wcal_put32(x187 + 32, r & 1u << 16 ? mag : 0u - mag);
    mag = r >> 17 & 0x1FFu;
    wcal_put32(x187 + 36, r & 1u << 26 ? mag : 0u - mag);
    wcal_put32(x187 + 40, FM1_RF_REG(0x1195Cu));
    wcal_put32(x187 + 44, FM1_RF_REG(0x11960u));
    for (k = 0; k < 8u; k++) {
        x187[48u + 2u * k] = (uint8_t)c->rx_dc[k >> 1][k & 1u];
        x187[49u + 2u * k] = (uint8_t)(c->rx_dc[k >> 1][k & 1u] >> 8);
    }
    r = wcal_crc16(x187, 64);
    x187[64] = (uint8_t)r;
    x187[65] = (uint8_t)(r >> 8);
}

/* ---- main trim (SPECSHEET §10.1: filter, TX LO, RX DC) ---- */

static int wcal_trim(struct wifi_rf_cal *c, uint8_t x187[68])
{
    uint32_t t0 = fm1_ticks(), t, g;
    int r;
    c->err = WCAL_OK;
    c->dem_reads = 0;
    c->abort = 0;
    c->txlo_i = c->txlo_q = 64;
    for (g = 0; g < 4u; g++)
        c->rx_dc[g][0] = c->rx_dc[g][1] = 0x200;
    c->tx[WCAL_TX_IG] = c->tx[WCAL_TX_QG] = 1023;
    c->tx[WCAL_TX_IP] = c->tx[WCAL_TX_QP] = c->tx[WCAL_TX_IDC] = c->tx[WCAL_TX_QDC] = 0;

    t = fm1_ticks();
    r = wcal_rx_tune(c);
    c->us_filter = wcal_us_since(t);
    if (r && !c->err)
        c->err = r;

    wcal_en_set(2);
    wcal_cal_att(1);
    r = wcal_txiq_tune(c);
    if (r && !c->err)
        c->err = r;

    wcal_hp_close();
    wcal_en_set(0);
    FM1_RF_REG(0x11904u) &= ~(1u << 15);
    r = wcal_rxdc_tune(c);
    if (r && !c->err)
        c->err = r;

    wcal_hp_open();
    wcal_en_set(3);
    wcal_cal_att(0);

    wcal_record(c, x187);
    c->us_total = wcal_us_since(t0);
    return (int)c->err;
}

/* ---- apply measured calibration values to wifi-mode registers ---- */

static void wcal_apply(const struct wifi_rf_cal *c)
{
    uint32_t r6 = c->flt[0], r10 = c->flt[1], r20 = c->flt[2];

    /* RC filter bandwidth codes (same writes as wcal_rx_tune epilog) */
    wcal_rmw(0x11920u, 0xFFu << 10, r10 << 10);
    wcal_rmw(0x11910u, 0xFFu << 14, r10 << 14);
    FM1_RF_REG(0x1195Cu) |= 0xFFu;
    wcal_rmw(0x1195Cu, 0xFFu << 8, r6 << 8);
    wcal_rmw(0x1195Cu, 0xFFu << 16, r10 << 16);
    wcal_rmw(0x1195Cu, 0xFFu << 24, r20 << 24);
    FM1_RF_REG(0x11960u) |= 0xFFu;
    wcal_rmw(0x11960u, 0xFFu << 8, r6 << 8);
    wcal_rmw(0x11960u, 0xFFu << 16, r10 << 16);
    wcal_rmw(0x11960u, 0xFFu << 24, r20 << 24);

    /* TX LO leakage */
    wcal_rmw(0x1191Cu, 0x7Fu << 8, (uint32_t)c->txlo_i << 8);
    wcal_rmw(0x11920u, 0x7Fu, c->txlo_q);

    /* RX DC: step 0 only (the step used for normal WiFi gain) */
    if (c->rx_dc[0][0] != 0x200u || c->rx_dc[0][1] != 0x200u) {
        uint16_t ri = c->rx_dc[0][0], rq = c->rx_dc[0][1];
        uint32_t v = FM1_RF_REG(0x11908u);
        v = (v & ~(0x1FFu << 7)) | ((uint32_t)(ri & 0x1FFu) << 7);
        if (ri & 0x200u) v |= 1u << 16; else v &= ~(1u << 16);
        v = (v & ~(0x1FFu << 17)) | ((uint32_t)(rq & 0x1FFu) << 17);
        if (rq & 0x200u) v |= 1u << 26; else v &= ~(1u << 26);
        FM1_RF_REG(0x11908u) = v;
    }
}

/* ---- entry point: called by wifi_rf_init when VM 187 is absent ---- */

static void wifi_rf_calibrate(void)
{
    int rc;

    cfm1_log("wifi: rf cal start");
    cfm1_log_int("wifi: cal pll1 pre=", (int32_t)FM1_RF_REG(0x11938u));

    /* VCO bank scan skipped: it requires BLE PLL state (before wifi_mode)
     * and corrupts the WiFi PLL bank register when run after wifi_mode.
     * The bank table is only needed for LUT patching (future work). */

    rc = wcal_trim(&wifi_cal, wifi_cal_x187);
    cfm1_log_int("wifi: cal flt6=", wifi_cal.flt[0]);
    cfm1_log_int("wifi: cal flt10=", wifi_cal.flt[1]);
    cfm1_log_int("wifi: cal flt20=", wifi_cal.flt[2]);
    cfm1_log_int("wifi: cal txlo_i=", wifi_cal.txlo_i);
    cfm1_log_int("wifi: cal txlo_q=", wifi_cal.txlo_q);
    cfm1_log_int("wifi: cal rxdc0i=", wifi_cal.rx_dc[0][0]);
    cfm1_log_int("wifi: cal rxdc0q=", wifi_cal.rx_dc[0][1]);
    cfm1_log_int("wifi: cal rxdc1i=", wifi_cal.rx_dc[1][0]);
    cfm1_log_int("wifi: cal rxdc2i=", wifi_cal.rx_dc[2][0]);
    cfm1_log_int("wifi: cal rxdc3i=", wifi_cal.rx_dc[3][0]);
    cfm1_log_int("wifi: cal dem_n=", (int32_t)wifi_cal.dem_reads);
    cfm1_log_int("wifi: cal err=", (int32_t)wifi_cal.err);
    cfm1_log_int("wifi: cal us=", (int32_t)wifi_cal.us_total);
    cfm1_log_int("wifi: cal pll1 post=", (int32_t)FM1_RF_REG(0x11938u));

    /* The trim modified analog registers (en_set, cal_att, vga_gain, etc.)
     * leaving them in calibration mode. Re-run wifi_mode to restore ALL
     * analog registers to known WiFi values, then re-apply only the
     * measured calibration results via RMW. */
    wifi_rf_wifi_mode();
    wcal_apply(&wifi_cal);

    cfm1_log_int("wifi: cal pll1 final=", (int32_t)FM1_RF_REG(0x11938u));
    cfm1_log("wifi: rf cal done");
}
