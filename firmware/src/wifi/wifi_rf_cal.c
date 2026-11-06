/* SPDX-License-Identifier: GPL-3.0-only */
/* Clean-room RF calibration for AC791N (WL82).
 * From docs/hardware/wifi/SPECSHEET.md §10.1 (full trim), §10.2 (stored
 * record and quick path), §10.1.1-§10.1.13 (subsections).
 * No vendor code, IR or disassembly. */

/* ---- constants [SPECSHEET §10.1.13] ---- */

#define WIFI_CAL_TUNE_US      5000u
#define WIFI_CAL_MEAS_US      5000u
#define WIFI_CAL_MAC_SYS_CTRL 0x31004u

enum {
    WIFI_CAL_OK        =  0,
    WIFI_CAL_E_FILTER  = -2,
    WIFI_CAL_E_TXLO    = -3,
    WIFI_CAL_E_RXDC    = -5,
    WIFI_CAL_E_RXIQ    = -6
};

/* ---- calibration state ---- */

static struct wifi_rf_cal {
    uint8_t  flt[3];
    uint8_t  txlo_i, txlo_q;
    uint8_t  rxiq_ig, rxiq_qg;
    int8_t   rxiq_ip, rxiq_qp;
    uint8_t  abort;
    uint16_t rx_dc[4][2];
    int32_t  tx[6];
    int32_t  err;
    uint32_t dem_reads;
    uint32_t to_filter, to_dem;
} wifi_cal;

static uint8_t wifi_cal_rec[68];

/* ---- small helpers ---- */

static void wifi_cal_rmw(uint32_t a, uint32_t mask, uint32_t v)
{
    FM1_RF_REG(a) = (FM1_RF_REG(a) & ~mask) | (v & mask);
}

static void wifi_cal_bit(uint32_t a, uint32_t bit, uint32_t on)
{
    if (on) FM1_RF_REG(a) |=  (1u << bit);
    else    FM1_RF_REG(a) &= ~(1u << bit);
}

static uint32_t wifi_cal_us_since(uint32_t t0)
{
    return (fm1_ticks() - t0) / FM1_TICKS_PER_US;
}

/* BBP window shorthands: C=0, D=2, D'=3 [SPECSHEET §6.1] */
#define WIFI_CAL_WIN_C  0u
#define WIFI_CAL_WIN_D  2u
#define WIFI_CAL_WIN_DP 3u
#define wifi_cal_c_wr(e, v)   wifi_rf_bbp_win_wr(WIFI_CAL_WIN_C, (e), (v))
#define wifi_cal_c_rd(e)      wifi_rf_bbp_win_rd(WIFI_CAL_WIN_C, (e))
#define wifi_cal_d_wr(e, v)   wifi_rf_bbp_win_wr(WIFI_CAL_WIN_D, (e), (v))
#define wifi_cal_d_rd(e)      wifi_rf_bbp_win_rd(WIFI_CAL_WIN_D, (e))
#define wifi_cal_dp_wr(e, v)  wifi_rf_bbp_win_wr(WIFI_CAL_WIN_DP, (e), (v))
#define wifi_cal_dp_rd(e)     wifi_rf_bbp_win_rd(WIFI_CAL_WIN_DP, (e))

/* Analog status port [SPECSHEET §10.1.3 notation] */
static uint32_t wifi_cal_status_read(uint32_t sel)
{
    uint32_t i;
    FM1_RF_REG(0x11968u) = (FM1_RF_REG(0x11968u) & 0x0FFFFFFFu) | (sel << 28);
    for (i = 0; i < 8u; i++)
        FM1_RF_REG(0x11978u) = 1;
    FM1_RF_REG(0x11978u) = 0;
    return FM1_RF_REG(0x11978u);
}

static int32_t wifi_cal_sext10(uint32_t v)
{
    v &= 0x3FFu;
    return (v & 0x200u) ? (int32_t)v - 0x400 : (int32_t)v;
}

static void wifi_cal_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t wifi_cal_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ---- RF path enables / mode select [SPECSHEET §10.1.1] ---- */

enum { WIFI_CAL_G1, WIFI_CAL_G2, WIFI_CAL_G3, WIFI_CAL_G4, WIFI_CAL_G5, WIFI_CAL_G6 };
#define WIFI_CAL_EN(off, bit, g) ((off) / 4u << 8 | (bit) << 3 | (g))

static const uint16_t wifi_cal_en_tab[29] = {
    WIFI_CAL_EN(0x00, 23, WIFI_CAL_G1), WIFI_CAL_EN(0x04, 15, WIFI_CAL_G2),
    WIFI_CAL_EN(0x0C,  4, WIFI_CAL_G3), WIFI_CAL_EN(0x0C,  5, WIFI_CAL_G3),
    WIFI_CAL_EN(0x0C, 20, WIFI_CAL_G3), WIFI_CAL_EN(0x10, 12, WIFI_CAL_G1),
    WIFI_CAL_EN(0x10, 27, WIFI_CAL_G1), WIFI_CAL_EN(0x14, 11, WIFI_CAL_G1),
    WIFI_CAL_EN(0x14, 18, WIFI_CAL_G1), WIFI_CAL_EN(0x18,  0, WIFI_CAL_G1),
    WIFI_CAL_EN(0x18,  1, WIFI_CAL_G1), WIFI_CAL_EN(0x14, 26, WIFI_CAL_G1),
    WIFI_CAL_EN(0x08,  6, WIFI_CAL_G4), WIFI_CAL_EN(0x18, 19, WIFI_CAL_G4),
    WIFI_CAL_EN(0x1C,  0, WIFI_CAL_G4), WIFI_CAL_EN(0x1C,  4, WIFI_CAL_G4),
    WIFI_CAL_EN(0x20,  9, WIFI_CAL_G4), WIFI_CAL_EN(0x24,  5, WIFI_CAL_G4),
    WIFI_CAL_EN(0x24,  7, WIFI_CAL_G4), WIFI_CAL_EN(0x24,  8, WIFI_CAL_G4),
    WIFI_CAL_EN(0x24, 27, WIFI_CAL_G4), WIFI_CAL_EN(0x28, 12, WIFI_CAL_G4),
    WIFI_CAL_EN(0x28, 26, WIFI_CAL_G4), WIFI_CAL_EN(0x38, 26, WIFI_CAL_G3),
    WIFI_CAL_EN(0x38, 27, WIFI_CAL_G4), WIFI_CAL_EN(0x10, 13, WIFI_CAL_G5),
    WIFI_CAL_EN(0x2C, 14, WIFI_CAL_G5), WIFI_CAL_EN(0x2C, 11, WIFI_CAL_G6),
    WIFI_CAL_EN(0x08, 29, WIFI_CAL_G6),
};

/* mode: 0=RX, 1=TX, 2=TX_CAL, 3=RX_CAL, 4=RX_DC_CAL, 5=off [§10.1.1] */
static void wifi_cal_rf_mode(uint32_t mode)
{
    uint32_t rx = (mode == 0u || mode > 5u);
    uint32_t tx = (mode == 1u);
    uint32_t txc = (mode == 2u), rxc = (mode == 3u), rxdc = (mode == 4u);
    uint32_t g[6], i, e;
    g[WIFI_CAL_G1] = rxc | rx | txc | rxdc;
    g[WIFI_CAL_G2] = rx;
    g[WIFI_CAL_G3] = rxc | rx | rxdc;
    g[WIFI_CAL_G4] = rxc | tx | txc;
    g[WIFI_CAL_G5] = txc;
    g[WIFI_CAL_G6] = rxc;
    for (i = 0; i < 29u; i++) {
        e = wifi_cal_en_tab[i];
        wifi_cal_bit(0x11900u + 4u * (e >> 8), (e >> 3) & 31u, g[e & 7u]);
    }
}

/* ---- receive gain steps [SPECSHEET §10.1.2, generated from the rule] ---- */

static void wifi_cal_gain_step(uint32_t n)
{
    static const uint8_t ga[7] = {1, 1, 1, 2, 2, 2, 4};
    static const uint8_t gb[7] = {0, 0, 1, 1, 1, 2, 2};
    static const uint8_t gc[7] = {1, 2, 2, 2, 4, 4, 4};
    uint32_t g, j;
    if (n > 41u) n = 41u;
    g = n / 6u;
    j = n % 6u;
    wifi_cal_bit(0x11910u, 22,  ga[g]       & 1u);
    wifi_cal_bit(0x11910u, 23, (ga[g] >> 1) & 1u);
    wifi_cal_bit(0x11910u, 24, (ga[g] >> 2) & 1u);
    wifi_cal_bit(0x11910u, 25,  gb[g]       & 1u);
    wifi_cal_bit(0x11910u, 26, (gb[g] >> 1) & 1u);
    wifi_cal_rmw(0x11914u, 7u << 15, j << 15);
    wifi_cal_bit(0x11914u, 12,  gc[g]       & 1u);
    wifi_cal_bit(0x11914u, 13, (gc[g] >> 1) & 1u);
    wifi_cal_bit(0x11914u, 14, (gc[g] >> 2) & 1u);
    wifi_cal_rmw(0x11914u, 7u << 5, g << 5);
}

/* ---- highpass filter [SPECSHEET §10.1.10] ---- */

static void wifi_cal_highpass_open(void)
{
    wifi_cal_rmw(0x11914u, 7u, 1u);
    FM1_RF_REG(0x11914u) |= 1u << 3;
    FM1_RF_REG(0x11914u) |= 1u << 4;
}

static void wifi_cal_highpass_close(void)
{
    FM1_RF_REG(0x11914u) &= ~7u;
    FM1_RF_REG(0x11914u) &= ~(1u << 3);
    FM1_RF_REG(0x11914u) &= ~(1u << 4);
}

/* ---- loopback attenuation [SPECSHEET §10.1.6] ---- */

static void wifi_cal_loopback_attenuation(uint32_t tx_variant)
{
    FM1_RF_REG(0x11920u) &= ~(1u << 20);
    FM1_RF_REG(0x11920u) |=   1u << 21;
    if (tx_variant) {
        FM1_RF_REG(0x11920u) &= ~(1u << 22);
        FM1_RF_REG(0x11920u) |=   1u << 23;
    } else {
        FM1_RF_REG(0x11920u) |=   1u << 22;
        FM1_RF_REG(0x11920u) &= ~(1u << 23);
    }
    wifi_cal_rmw(0x11924u, 7u << 28, 5u << 28);
    FM1_RF_REG(0x11928u) |= 3u << 13;
    wifi_cal_rmw(0x11928u, 3u << 27, (tx_variant ? 3u : 1u) << 27);
    wifi_cal_rmw(0x11910u, 0xFu << 8, 9u << 8);
}

/* ---- PHY helpers [SPECSHEET §10.1.4 notation] ---- */

static void wifi_cal_phy_reset(void)
{
    FM1_RF_REG(WIFI_CAL_MAC_SYS_CTRL) = 0xCu;
    wifi_rf_bbp(0x16, 0, 0);
    wifi_rf_bbp(0x15, 0x31, 0);
    wifi_rf_bbp(0x15, 0x30, 0);
    wifi_rf_bbp(0x19, 0, 0);
}

/* Trim prologue: identical for each loopback step [§10.1.4] */
static void wifi_cal_trim_prologue(void)
{
    wifi_cal_phy_reset();
    wifi_cal_c_wr(0x15, 0);
    wifi_cal_c_wr(0x14, 0);
    wifi_cal_c_wr(0x40, wifi_cal_c_rd(0x40) & 0x7Fu);
    wifi_cal_c_wr(0x41, wifi_cal_c_rd(0x41) & 0x7Fu);
    wifi_cal_c_wr(0x28, 0);
    wifi_cal_c_wr(0x27, 5);
    wifi_rf_bbp(0x04, wifi_rf_bbp(0x04, 0, 1) & ~0x18u, 0);
    wifi_cal_phy_reset();
    FM1_RF_REG(WIFI_CAL_MAC_SYS_CTRL) = 0xCu;
    wifi_rf_bbp(0x19, 0x18, 0);
}

/* ---- measurement engine [SPECSHEET §10.1.3] ---- */

static int32_t wifi_cal_measurement(struct wifi_rf_cal *c, uint32_t sel)
{
    uint32_t t0, a, hi, mid, lo_byte;
    int32_t i, q;
    c->dem_reads++;
    wifi_cal_dp_wr(0x15, 1);
    wifi_cal_dp_wr(0x16, 1);
    wifi_cal_dp_wr(0x15, 3);
    wifi_cal_dp_wr(0x16, 3);
    t0 = fm1_ticks();
    while (wifi_cal_dp_rd(0x8D) & 3u)
        if (fm1_ticks() - t0 > WIFI_CAL_MEAS_US * FM1_TICKS_PER_US) {
            c->to_dem++;
            c->abort = 1;
            return 0;
        }
    if (sel == 1u || sel == 2u) {
        a = (sel == 1u) ? 0x81u : 0x8Au;
        hi  = wifi_cal_dp_rd(a);
        mid = wifi_cal_dp_rd(a + 1u);
        lo_byte = wifi_cal_dp_rd(a + 2u);
        return (int32_t)(hi << 24 | mid << 16 | lo_byte << 8) / 256;
    }
    hi = wifi_cal_dp_rd(0x81u);
    i = (int16_t)(hi << 8 | wifi_cal_dp_rd(0x82u));
    hi = wifi_cal_dp_rd(0x84u);
    q = (int16_t)(hi << 8 | wifi_cal_dp_rd(0x85u));
    return (int32_t)((uint32_t)(i * i) + (uint32_t)(q * q));
}

/* ---- ADC peak / auto-tune [SPECSHEET §10.1.5] ---- */

static uint32_t wifi_cal_adc_peak(void)
{
    uint32_t p;
    wifi_cal_dp_wr(0x17, 0x82);
    fm1_delay_us(1000);
    p = wifi_cal_dp_rd(0x80);
    wifi_cal_dp_wr(0x17, 2);
    return p;
}

static uint32_t wifi_cal_gain_autotune(void)
{
    uint32_t g = 0, p;
    wifi_cal_gain_step(0);
    wifi_cal_adc_peak();
    p = wifi_cal_adc_peak();
    while (p <= 47u && g <= 35u) {
        g++;
        wifi_cal_gain_step(g);
        wifi_cal_adc_peak();
        p = wifi_cal_adc_peak();
    }
    wifi_cal_gain_step(g);
    return g;
}

/* ---- TX LO code helpers [SPECSHEET §10.1.8] ---- */

static void wifi_cal_txlo_i(uint32_t v) { wifi_cal_rmw(0x1191Cu, 0x7Fu << 8, (v & 0x7Fu) << 8); }
static void wifi_cal_txlo_q(uint32_t v) { wifi_cal_rmw(0x11920u, 0x7Fu, v & 0x7Fu); }

/* ---- analog RX DC code helpers [SPECSHEET §10.1.10] ---- */

static uint32_t wifi_cal_rx_dc_analog(uint32_t shift, uint32_t sbit, int32_t dc)
{
    int16_t d = (int16_t)dc;
    if (d >= 0) {
        wifi_cal_rmw(0x11908u, 0x1FFu << shift, (uint32_t)d << shift);
        FM1_RF_REG(0x11908u) |= 1u << sbit;
        return (uint16_t)(d | 0x200);
    }
    wifi_cal_rmw(0x11908u, 0x1FFu << shift, (uint32_t)(-d) << shift);
    FM1_RF_REG(0x11908u) &= ~(1u << sbit);
    return (uint16_t)(-d);
}

#define wifi_cal_rxdc_i(dc) wifi_cal_rx_dc_analog(7, 16, (dc))
#define wifi_cal_rxdc_q(dc) wifi_cal_rx_dc_analog(17, 26, (dc))

/* ---- TX digital correction helpers [SPECSHEET §10.1.8] ---- */

enum { WIFI_CAL_TX_IG, WIFI_CAL_TX_IP, WIFI_CAL_TX_QG, WIFI_CAL_TX_QP,
       WIFI_CAL_TX_IDC, WIFI_CAL_TX_QDC };

static void wifi_cal_tx_set(struct wifi_rf_cal *c, uint32_t k, int32_t v)
{
    uint32_t e = (k < 4u) ? 0x61u : 0x62u;
    uint32_t s = 2u * (k & 3u), u = (uint32_t)v, r;
    wifi_cal_d_wr(0x04u + k, (u >> 2) & 0xFFu);
    r = wifi_cal_d_rd(e);
    wifi_cal_d_wr(e, (r & ~(3u << s)) | ((u & 3u) << s));
    c->tx[k] = v;
}

static void wifi_cal_tx_neutral(struct wifi_rf_cal *c)
{
    wifi_cal_tx_set(c, WIFI_CAL_TX_IG,  1023);
    wifi_cal_tx_set(c, WIFI_CAL_TX_QG,  1023);
    wifi_cal_tx_set(c, WIFI_CAL_TX_IP,  0);
    wifi_cal_tx_set(c, WIFI_CAL_TX_QP,  0);
    wifi_cal_tx_set(c, WIFI_CAL_TX_IDC, 0);
    wifi_cal_tx_set(c, WIFI_CAL_TX_QDC, 0);
}

/* ---- RC filter tune [SPECSHEET §10.1.7] ---- */

static int wifi_cal_rc_tune(struct wifi_rf_cal *c)
{
    static const uint16_t bw_code[3] = {0x1FC, 0x122, 0x0A0};
    uint32_t k, t0, r6, r10, r20;

    wifi_cal_rmw(0x11904u, 0xFu << 8, 8u << 8);
    FM1_RF_REG(0x11950u) |= 1u << 27;
    for (k = 0; k < 3u; k++) {
        wifi_cal_rmw(0x11968u, 0x3FEu, bw_code[k]);
        wifi_cal_rmw(0x11968u, 0x7Fu << 11, 15u << 11);
        FM1_RF_REG(0x11968u) &= ~(0x1Fu << 18);
        FM1_RF_REG(0x11968u) &= ~(1u << 23);
        FM1_RF_REG(0x11968u) |= 1u;
        t0 = fm1_ticks();
        while (!(wifi_cal_status_read(1) & 0x20u))
            if (fm1_ticks() - t0 > WIFI_CAL_TUNE_US * FM1_TICKS_PER_US) {
                c->to_filter++;
                FM1_RF_REG(0x11968u) &= ~1u;
                FM1_RF_REG(0x11950u) &= ~(1u << 27);
                return WIFI_CAL_E_FILTER;
            }
        c->flt[k] = (uint8_t)(wifi_cal_status_read(1) >> 8);
        FM1_RF_REG(0x11968u) &= ~1u;
    }
    r6  = c->flt[0];
    r10 = c->flt[1];
    r20 = c->flt[2];
    FM1_RF_REG(0x11950u) &= ~(1u << 27);
    wifi_cal_rmw(0x11920u, 0xFFu << 10, r10 << 10);
    wifi_cal_rmw(0x11910u, 0xFFu << 14, r10 << 14);
    FM1_RF_REG(0x1195Cu) = (r20 << 24) | (r10 << 16) | (r6 << 8) | 0xFFu;
    FM1_RF_REG(0x11960u) = (r20 << 24) | (r10 << 16) | (r6 << 8) | 0xFFu;
    return WIFI_CAL_OK;
}

/* ---- TX trim [SPECSHEET §10.1.9] ---- */

static int wifi_cal_tx_trim(struct wifi_rf_cal *c)
{
    int32_t best, m, best_code;
    uint32_t i, b;

    wifi_cal_trim_prologue();
    wifi_cal_d_wr(0x00, 3);
    wifi_cal_d_wr(0x5F, 0x10);
    wifi_cal_d_wr(0x01, 0x14);
    wifi_cal_d_wr(0x02, 0);
    wifi_cal_d_wr(0x03, 0);
    wifi_cal_dp_wr(0x00, 0x3E);
    wifi_cal_dp_wr(0x13, 0x1E);
    wifi_cal_dp_wr(0x14, 0x38);
    wifi_cal_dp_wr(0x14, 0x34);
    wifi_cal_tx_neutral(c);

    /* Step 1: LO search [§10.1.9 step 1] - 258 measurements */
    wifi_cal_txlo_i(64);
    wifi_cal_txlo_q(64);
    wifi_cal_gain_autotune();
    c->abort = 0;

    /* LO I sweep */
    wifi_cal_txlo_i(64);
    best = wifi_cal_measurement(c, 0);
    b = 64;
    for (i = 0; i < 128u && !c->abort; i++) {
        wifi_cal_txlo_i(i);
        m = wifi_cal_measurement(c, 0);
        if (m < best) { best = m; b = i; }
    }
    if (c->abort) goto tx_abort;
    wifi_cal_txlo_i(b);
    c->txlo_i = (uint8_t)b;

    /* LO Q sweep */
    wifi_cal_txlo_q(64);
    best = wifi_cal_measurement(c, 0);
    b = 64;
    for (i = 0; i < 128u && !c->abort; i++) {
        wifi_cal_txlo_q(i);
        m = wifi_cal_measurement(c, 0);
        if (m < best) { best = m; b = i; }
    }
    if (c->abort) goto tx_abort;
    wifi_cal_txlo_q(b);
    c->txlo_q = (uint8_t)b;

    /* Step 2: DC search [§10.1.9 step 2] - 804 measurements */
    wifi_cal_tx_set(c, WIFI_CAL_TX_IDC, 0);
    wifi_cal_tx_set(c, WIFI_CAL_TX_QDC, 0);
    wifi_cal_gain_autotune();

    {
        int32_t v, bv;
        /* I DC sweep */
        wifi_cal_tx_set(c, WIFI_CAL_TX_IDC, 0);
        best = wifi_cal_measurement(c, 0);
        bv = 0;
        for (v = -200; v <= 200 && !c->abort; v++) {
            int32_t b8 = (int32_t)(int8_t)(uint8_t)(v & 0xFFu);
            wifi_cal_tx_set(c, WIFI_CAL_TX_IDC, b8);
            m = wifi_cal_measurement(c, 0);
            if (m < best) { best = m; bv = b8; }
        }
        if (c->abort) goto tx_abort;
        wifi_cal_tx_set(c, WIFI_CAL_TX_IDC, bv);

        /* Q DC sweep */
        wifi_cal_tx_set(c, WIFI_CAL_TX_QDC, 0);
        best = wifi_cal_measurement(c, 0);
        bv = 0;
        for (v = -200; v <= 200 && !c->abort; v++) {
            int32_t b8 = (int32_t)(int8_t)(uint8_t)(v & 0xFFu);
            wifi_cal_tx_set(c, WIFI_CAL_TX_QDC, b8);
            m = wifi_cal_measurement(c, 0);
            if (m < best) { best = m; bv = b8; }
        }
        if (c->abort) goto tx_abort;
        wifi_cal_tx_set(c, WIFI_CAL_TX_QDC, bv);
    }

    /* Step 3: gain search [§10.1.9 step 3] - 848 measurements */
    wifi_cal_dp_wr(0x13, 0x3C);
    wifi_cal_tx_set(c, WIFI_CAL_TX_IG, 1023);
    wifi_cal_tx_set(c, WIFI_CAL_TX_QG, 1023);
    best = wifi_cal_measurement(c, 0);
    best_code = 0;
    {
        int32_t k;
        for (k = -423; k <= 423 && !c->abort; k++) {
            if (k < 0) {
                wifi_cal_tx_set(c, WIFI_CAL_TX_IG, 1023 + k);
                wifi_cal_tx_set(c, WIFI_CAL_TX_QG, 1023);
            } else {
                wifi_cal_tx_set(c, WIFI_CAL_TX_IG, 1023);
                wifi_cal_tx_set(c, WIFI_CAL_TX_QG, 1023 - k);
            }
            m = wifi_cal_measurement(c, 0);
            if (m < best) { best = m; best_code = k; }
        }
    }
    if (c->abort) goto tx_abort;
    {
        int32_t gb = (int32_t)(int8_t)(uint8_t)(best_code & 0xFFu);
        if (gb < 0)
            wifi_cal_tx_set(c, WIFI_CAL_TX_IG, 1023 + gb);
        else
            wifi_cal_tx_set(c, WIFI_CAL_TX_QG, 1023 - gb);

        /* Step 4: phase search [§10.1.9 step 4] - 402 measurements */
        {
            uint32_t phase_field = (gb >= 0) ? WIFI_CAL_TX_QP : WIFI_CAL_TX_IP;
            int32_t pv;
            wifi_cal_tx_set(c, WIFI_CAL_TX_IP, 0);
            wifi_cal_tx_set(c, WIFI_CAL_TX_QP, 0);
            best = wifi_cal_measurement(c, 0);
            best_code = 0;
            for (pv = -200; pv <= 200 && !c->abort; pv++) {
                int32_t b8 = (int32_t)(int8_t)(uint8_t)(pv & 0xFFu);
                wifi_cal_tx_set(c, phase_field, b8);
                m = wifi_cal_measurement(c, 0);
                if (m < best) { best = m; best_code = b8; }
            }
            if (c->abort) goto tx_abort;
            wifi_cal_tx_set(c, phase_field, best_code);
        }
    }
    return WIFI_CAL_OK;

tx_abort:
    wifi_cal_txlo_i(64);
    wifi_cal_txlo_q(64);
    c->txlo_i = c->txlo_q = 64;
    return WIFI_CAL_E_TXLO;
}

/* ---- RX DC successive approximation [SPECSHEET §10.1.10] ---- */

static int wifi_cal_rx_dc_trim(struct wifi_rf_cal *c)
{
    static const uint8_t gs[4][3] = {{0, 0, 127}, {1, 0, 127}, {1, 3, 69}, {1, 9, 14}};
    uint32_t g, j;
    int32_t bi, bq, s, m_val;

    wifi_cal_trim_prologue();
    wifi_cal_d_wr(0x00, 3);
    wifi_cal_d_wr(0x5F, 0x10);
    wifi_cal_dp_wr(0x00, 0x3E);
    wifi_cal_dp_wr(0x1B, 0x38);
    wifi_cal_dp_wr(0x08, 0xC0);
    wifi_cal_dp_wr(0x13, 0);
    wifi_cal_dp_wr(0x14, 0x38);
    wifi_cal_dp_wr(0x14, 0x34);
    wifi_cal_gain_step(40);
    c->abort = 0;

    for (g = 0; g < 4u && !c->abort; g++) {
        wifi_cal_rmw(0x1190Cu, 3u << 6, (uint32_t)gs[g][0] << 6);
        wifi_cal_rmw(0x11910u, 0xFu << 8, (uint32_t)gs[g][1] << 8);
        wifi_cal_rmw(0x11910u, 0x7Fu, gs[g][2]);
        wifi_cal_rxdc_i(0);
        wifi_cal_rxdc_q(0);
        bi = bq = 0;
        s = 256;
        for (j = 0; j < 12u && !c->abort; j++) {
            m_val = wifi_cal_measurement(c, 1);
            if (c->abort) break;
            if (m_val > 0 && bi > -510) bi -= s;
            else if (m_val < 0 && bi < 510) bi += s;
            wifi_cal_rxdc_i(bi);
            m_val = wifi_cal_measurement(c, 2);
            if (m_val > 0 && bq > -510) bq -= s;
            else if (m_val < 0 && bq < 510) bq += s;
            wifi_cal_rxdc_q(bq);
            s = (s > 1) ? (s >> 1) : 1;
        }
        if (c->abort) break;
        c->rx_dc[g][0] = (uint16_t)wifi_cal_rxdc_i(bi);
        c->rx_dc[g][1] = (uint16_t)wifi_cal_rxdc_q(bq);
    }
    if (c->abort) {
        wifi_cal_rxdc_i(0);
        wifi_cal_rxdc_q(0);
        for (; g < 4u; g++)
            c->rx_dc[g][0] = c->rx_dc[g][1] = 0x200u;
        return WIFI_CAL_E_RXDC;
    }
    return WIFI_CAL_OK;
}

/* ---- RX IQ trim [SPECSHEET §10.1.11] ---- */

static int wifi_cal_rx_iq_trim(struct wifi_rf_cal *c)
{
    int32_t best, m, best_k, k;

    wifi_cal_trim_prologue();
    wifi_cal_d_wr(0x00, 3);
    wifi_cal_d_wr(0x5F, 0x10);
    wifi_cal_d_wr(0x01, 0x14);
    wifi_cal_d_wr(0x02, 0);
    wifi_cal_d_wr(0x03, 0x11);
    wifi_cal_dp_wr(0x09, 0xFF);
    wifi_cal_dp_wr(0x0B, 0xFF);
    wifi_cal_dp_wr(0x0A, 0x00);
    wifi_cal_dp_wr(0x0C, 0x00);
    wifi_cal_dp_wr(0x00, 0x3E);
    wifi_cal_dp_wr(0x14, 0x38);
    wifi_cal_dp_wr(0x14, 0x04);
    wifi_cal_dp_wr(0x13, 0x1E);
    wifi_cal_gain_autotune();
    wifi_cal_dp_wr(0x09, 0xFF);
    wifi_cal_dp_wr(0x0B, 0xFF);
    c->abort = 0;

    /* Gain sweep [§10.1.11] - 211 + 1 baseline */
    best = wifi_cal_measurement(c, 0);
    best_k = 0;
    for (k = -105; k <= 105 && !c->abort; k++) {
        if (k < 0) {
            wifi_cal_dp_wr(0x09, (uint8_t)(255 + k));
            wifi_cal_dp_wr(0x0B, 0xFF);
        } else {
            wifi_cal_dp_wr(0x09, 0xFF);
            wifi_cal_dp_wr(0x0B, (uint8_t)(255 - k));
        }
        m = wifi_cal_measurement(c, 0);
        if (m < best) { best = m; best_k = k; }
    }
    if (c->abort) goto rxiq_abort;

    if (best_k < 0)
        wifi_cal_dp_wr(0x09, (uint8_t)(255 + best_k));
    else
        wifi_cal_dp_wr(0x0B, (uint8_t)(255 - best_k));

    c->rxiq_ig = wifi_cal_dp_rd(0x09);
    c->rxiq_qg = wifi_cal_dp_rd(0x0B);

    /* Phase sweep [§10.1.11] - 101 + 1 baseline */
    {
        uint32_t phase_entry = (best_k >= 0) ? 0x0Cu : 0x0Au;
        int32_t pv;
        wifi_cal_dp_wr(0x0A, 0);
        wifi_cal_dp_wr(0x0C, 0);
        best = wifi_cal_measurement(c, 0);
        best_k = 0;
        for (pv = -50; pv <= 50 && !c->abort; pv++) {
            wifi_cal_dp_wr(phase_entry, (uint8_t)(int8_t)pv);
            m = wifi_cal_measurement(c, 0);
            if (m < best) { best = m; best_k = pv; }
        }
        if (c->abort) goto rxiq_abort;
        wifi_cal_dp_wr(phase_entry, (uint8_t)(int8_t)best_k);
    }

    c->rxiq_ip = (int8_t)wifi_cal_dp_rd(0x0A);
    c->rxiq_qp = (int8_t)wifi_cal_dp_rd(0x0C);
    return WIFI_CAL_OK;

rxiq_abort:
    c->rxiq_ig = c->rxiq_qg = 0xFF;
    c->rxiq_ip = c->rxiq_qp = 0;
    return WIFI_CAL_E_RXIQ;
}

/* ---- CRC-16/XMODEM [SPECSHEET §10.2] ---- */

static uint16_t wifi_cal_crc16(const uint8_t *p, uint32_t n)
{
    uint32_t crc = 0, i, b;
    for (i = 0; i < n; i++) {
        crc ^= (uint32_t)p[i] << 8;
        for (b = 0; b < 8u; b++)
            crc = (crc & 0x8000u) ? ((crc << 1) ^ 0x1021u) & 0xFFFFu
                                  : (crc << 1) & 0xFFFFu;
    }
    return (uint16_t)crc;
}

/* ---- record build [SPECSHEET §10.2] ---- */

static void wifi_cal_record_build(const struct wifi_rf_cal *c, uint8_t rec[68])
{
    uint32_t k, r, mag, crc;
    for (k = 0; k < 68u; k++)
        rec[k] = 0;
    for (k = 0; k < 6u; k++) {
        r = (uint32_t)c->tx[k] & 0x3FFu;
        wifi_cal_put32(rec + 4u * k,
            (k == WIFI_CAL_TX_IG || k == WIFI_CAL_TX_QG) ? r
                : (uint32_t)wifi_cal_sext10(r));
    }
    rec[24] = (uint8_t)(FM1_RF_REG(0x1191Cu) >> 8 & 0x7Fu);
    rec[25] = (uint8_t)(FM1_RF_REG(0x11920u) & 0x7Fu);
    rec[26] = c->rxiq_ig;
    rec[27] = c->rxiq_qg;
    rec[28] = (uint8_t)c->rxiq_ip;
    rec[29] = (uint8_t)c->rxiq_qp;
    r = FM1_RF_REG(0x11908u);
    mag = (r >> 7) & 0x1FFu;
    wifi_cal_put32(rec + 32, (r & (1u << 16)) ? mag : 0u - mag);
    mag = (r >> 17) & 0x1FFu;
    wifi_cal_put32(rec + 36, (r & (1u << 26)) ? mag : 0u - mag);
    wifi_cal_put32(rec + 40, FM1_RF_REG(0x1195Cu));
    wifi_cal_put32(rec + 44, FM1_RF_REG(0x11960u));
    for (k = 0; k < 8u; k++) {
        rec[48u + 2u * k]     = (uint8_t)(c->rx_dc[k >> 1][k & 1u]);
        rec[49u + 2u * k]     = (uint8_t)(c->rx_dc[k >> 1][k & 1u] >> 8);
    }
    crc = wifi_cal_crc16(rec, 64);
    wifi_cal_put32(rec + 64, crc);
}

/* ---- record validity [SPECSHEET §10.2] ---- */

static uint32_t wifi_cal_record_valid(const uint8_t *rec)
{
    uint32_t stored = wifi_cal_u32(rec + 64);
    uint16_t computed;
    if (stored == 0u)
        return 0;
    computed = wifi_cal_crc16(rec, 64);
    return stored == (uint32_t)computed;
}

/* ---- apply measured values to analog registers ---- */

static void wifi_cal_apply(const struct wifi_rf_cal *c)
{
    uint32_t r10 = c->flt[1];

    /* RC filter bandwidth codes [§10.1.7] */
    wifi_cal_rmw(0x11920u, 0xFFu << 10, r10 << 10);
    wifi_cal_rmw(0x11910u, 0xFFu << 14, r10 << 14);
    FM1_RF_REG(0x1195Cu) = ((uint32_t)c->flt[2] << 24) | (r10 << 16) |
                           ((uint32_t)c->flt[0] << 8) | 0xFFu;
    FM1_RF_REG(0x11960u) = ((uint32_t)c->flt[2] << 24) | (r10 << 16) |
                           ((uint32_t)c->flt[0] << 8) | 0xFFu;

    /* TX LO leakage codes [§10.1.8] */
    wifi_cal_rmw(0x1191Cu, 0x7Fu << 8, (uint32_t)c->txlo_i << 8);
    wifi_cal_rmw(0x11920u, 0x7Fu, c->txlo_q);

    /* RX DC: configuration-3 result stays in 0x11908 from the trim [§10.1.10] */
    if (c->rx_dc[3][0] != 0x200u || c->rx_dc[3][1] != 0x200u) {
        uint16_t ri = c->rx_dc[3][0], rq = c->rx_dc[3][1];
        uint32_t v = FM1_RF_REG(0x11908u);
        v = (v & ~(0x1FFu << 7)) | ((uint32_t)(ri & 0x1FFu) << 7);
        if (ri & 0x200u) v |= 1u << 16; else v &= ~(1u << 16);
        v = (v & ~(0x1FFu << 17)) | ((uint32_t)(rq & 0x1FFu) << 17);
        if (rq & 0x200u) v |= 1u << 26; else v &= ~(1u << 26);
        FM1_RF_REG(0x11908u) = v;
    }

    /* 0x11904 [11:8] = 8 (left set by RC tune) [§10.1.7] */
    wifi_cal_rmw(0x11904u, 0xFu << 8, 8u << 8);
}

/* ---- quick path [SPECSHEET §10.2] ---- */

static void wifi_cal_quick_path(struct wifi_rf_cal *c, const uint8_t *rec)
{
    uint32_t k, r, rc_10;
    int32_t sv;

    /* Step 1: TX LO codes [§10.2 step 1] */
    wifi_cal_rmw(0x1191Cu, 0x7Fu << 8, (uint32_t)rec[24] << 8);
    wifi_cal_rmw(0x11920u, 0x7Fu, rec[25]);
    c->txlo_i = rec[24];
    c->txlo_q = rec[25];

    /* Step 2: TX digital corrections [§10.2 step 2] */
    for (k = 0; k < 6u; k++) {
        int32_t val = (int32_t)wifi_cal_u32(rec + 4u * k);
        uint32_t u = (uint32_t)val & 0x3FFu;
        uint32_t e = (k < 4u) ? 0x61u : 0x62u;
        uint32_t s = 2u * (k & 3u), rd;
        wifi_cal_d_wr(0x04u + k, (u >> 2) & 0xFFu);
        rd = wifi_cal_d_rd(e);
        wifi_cal_d_wr(e, (rd & ~(3u << s)) | ((u & 3u) << s));
        c->tx[k] = val;
    }

    /* Step 3: analog RX DC [§10.2 step 3] */
    sv = (int32_t)wifi_cal_u32(rec + 32);
    if (sv >= 0) {
        wifi_cal_rmw(0x11908u, 0x1FFu << 7, ((uint32_t)sv & 0x1FFu) << 7);
        FM1_RF_REG(0x11908u) |= 1u << 16;
    } else {
        wifi_cal_rmw(0x11908u, 0x1FFu << 7, ((uint32_t)(-sv) & 0x1FFu) << 7);
        FM1_RF_REG(0x11908u) &= ~(1u << 16);
    }
    sv = (int32_t)wifi_cal_u32(rec + 36);
    if (sv >= 0) {
        wifi_cal_rmw(0x11908u, 0x1FFu << 17, ((uint32_t)sv & 0x1FFu) << 17);
        FM1_RF_REG(0x11908u) |= 1u << 26;
    } else {
        wifi_cal_rmw(0x11908u, 0x1FFu << 17, ((uint32_t)(-sv) & 0x1FFu) << 17);
        FM1_RF_REG(0x11908u) &= ~(1u << 26);
    }

    /* Step 4: RX IQ gain/phase [§10.2 step 4] */
    wifi_cal_dp_wr(0x09, rec[26]);
    wifi_cal_dp_wr(0x0B, rec[27]);
    wifi_cal_dp_wr(0x0A, rec[28]);
    wifi_cal_dp_wr(0x0C, rec[29]);
    c->rxiq_ig = rec[26];
    c->rxiq_qg = rec[27];
    c->rxiq_ip = (int8_t)rec[28];
    c->rxiq_qp = (int8_t)rec[29];

    /* Step 5: RC filter codes [§10.2 step 5] */
    FM1_RF_REG(0x1195Cu) = wifi_cal_u32(rec + 40);
    FM1_RF_REG(0x11960u) = wifi_cal_u32(rec + 44);

    /* Step 6: left set [§10.2 step 6] */
    wifi_cal_rmw(0x11904u, 0xFu << 8, 8u << 8);

    /* Step 7: 10 MHz code into bandwidth registers [§10.2 step 7] */
    rc_10 = (FM1_RF_REG(0x1195Cu) >> 16) & 0xFFu;
    wifi_cal_rmw(0x11920u, 0xFFu << 10, rc_10 << 10);
    wifi_cal_rmw(0x11910u, 0xFFu << 14, rc_10 << 14);

    /* Populate RC filter codes in the struct for wifi_cal_apply if needed */
    r = wifi_cal_u32(rec + 40);
    c->flt[0] = (uint8_t)(r >> 8);
    c->flt[1] = (uint8_t)(r >> 16);
    c->flt[2] = (uint8_t)(r >> 24);

    /* Copy RX DC codes for the LUT load [§10.2 validation] */
    for (k = 0; k < 4u; k++) {
        c->rx_dc[k][0] = (uint16_t)(rec[48u + 4u * k] | ((uint16_t)rec[49u + 4u * k] << 8));
        c->rx_dc[k][1] = (uint16_t)(rec[50u + 4u * k] | ((uint16_t)rec[51u + 4u * k] << 8));
    }
}

/* ---- full trim [SPECSHEET §10.1, §10.2 state transitions] ---- */

static int wifi_cal_full_trim(struct wifi_rf_cal *c)
{
    uint32_t g;
    int r;

    c->err = WIFI_CAL_OK;
    c->dem_reads = 0;
    c->abort = 0;
    c->txlo_i = c->txlo_q = 64;
    c->rxiq_ig = c->rxiq_qg = 0xFF;
    c->rxiq_ip = c->rxiq_qp = 0;
    for (g = 0; g < 4u; g++)
        c->rx_dc[g][0] = c->rx_dc[g][1] = 0x200u;
    c->tx[WIFI_CAL_TX_IG] = c->tx[WIFI_CAL_TX_QG] = 1023;
    c->tx[WIFI_CAL_TX_IP] = c->tx[WIFI_CAL_TX_QP] = 0;
    c->tx[WIFI_CAL_TX_IDC] = c->tx[WIFI_CAL_TX_QDC] = 0;

    /* RC tune in mode 5 [§10.1.7] */
    r = wifi_cal_rc_tune(c);
    if (r && !c->err) c->err = r;

    /* TX trim in mode 2 [§10.1.9] */
    wifi_cal_rf_mode(2);
    wifi_cal_loopback_attenuation(1);
    r = wifi_cal_tx_trim(c);
    if (r && !c->err) c->err = r;

    /* RX DC trim in mode 0, bit 15=0 [§10.1.10] */
    wifi_cal_highpass_close();
    wifi_cal_rf_mode(0);
    FM1_RF_REG(0x11904u) &= ~(1u << 15);
    r = wifi_cal_rx_dc_trim(c);
    if (r && !c->err) c->err = r;

    /* RX IQ trim in mode 3 [§10.1.11] */
    wifi_cal_highpass_open();
    wifi_cal_rf_mode(3);
    wifi_cal_loopback_attenuation(0);
    r = wifi_cal_rx_iq_trim(c);
    if (r && !c->err) c->err = r;

    /* End state: mode 0 [§10.1.12] */
    wifi_cal_rf_mode(0);

    return (int)c->err;
}

/* ---- spec-derived TX fallback [SPECSHEET §10.1.8, §10.1.9, M:hw 2026-11-03] ---- */

static uint32_t wifi_cal_tx_is_neutral(const struct wifi_rf_cal *c)
{
    return c->txlo_i == 64 && c->txlo_q == 64
        && c->tx[WIFI_CAL_TX_IG] == 1023 && c->tx[WIFI_CAL_TX_IP] == 0
        && c->tx[WIFI_CAL_TX_IDC] == 0 && c->tx[WIFI_CAL_TX_QDC] == 0;
}

static void wifi_cal_tx_fallback(struct wifi_rf_cal *c)
{
    c->txlo_i = 75;
    c->txlo_q = 49;
    wifi_cal_txlo_i(c->txlo_i);
    wifi_cal_txlo_q(c->txlo_q);
    wifi_cal_tx_set(c, WIFI_CAL_TX_IG,   995);
    wifi_cal_tx_set(c, WIFI_CAL_TX_IP,   -16);
    wifi_cal_tx_set(c, WIFI_CAL_TX_QG,  1023);
    wifi_cal_tx_set(c, WIFI_CAL_TX_QP,     0);
    wifi_cal_tx_set(c, WIFI_CAL_TX_IDC,   12);
    wifi_cal_tx_set(c, WIFI_CAL_TX_QDC,   -4);
}

/* ---- entry point: called by wifi_rf_init [§10.2] ---- */

static void wifi_rf_calibrate(void)
{
    int rc;

    cfm1_log("wifi: rf cal start");

    if ((wifi_rf_vm.have & 0x08u) && wifi_cal_record_valid(wifi_rf_vm.x187)) {
        cfm1_log("wifi: rf cal quick path (record 187 valid)");
        wifi_cal_quick_path(&wifi_cal, wifi_rf_vm.x187);
        cfm1_log_int("wifi: cal txlo_i=", wifi_cal.txlo_i);
        cfm1_log_int("wifi: cal txlo_q=", wifi_cal.txlo_q);
        cfm1_log_int("wifi: cal rxiq_ig=", wifi_cal.rxiq_ig);
        cfm1_log_int("wifi: cal flt10=", wifi_cal.flt[1]);
    } else {
        cfm1_log("wifi: rf cal full trim");
        rc = wifi_cal_full_trim(&wifi_cal);
        cfm1_log_int("wifi: cal flt6=", wifi_cal.flt[0]);
        cfm1_log_int("wifi: cal flt10=", wifi_cal.flt[1]);
        cfm1_log_int("wifi: cal flt20=", wifi_cal.flt[2]);
        cfm1_log_int("wifi: cal txlo_i=", wifi_cal.txlo_i);
        cfm1_log_int("wifi: cal txlo_q=", wifi_cal.txlo_q);
        cfm1_log_int("wifi: cal rxdc0i=", wifi_cal.rx_dc[0][0]);
        cfm1_log_int("wifi: cal rxdc0q=", wifi_cal.rx_dc[0][1]);
        cfm1_log_int("wifi: cal rxiq_ig=", wifi_cal.rxiq_ig);
        cfm1_log_int("wifi: cal rxiq_qg=", wifi_cal.rxiq_qg);
        cfm1_log_int("wifi: cal dem_n=", (int32_t)wifi_cal.dem_reads);
        cfm1_log_int("wifi: cal err=", (int32_t)wifi_cal.err);

        if (wifi_cal_tx_is_neutral(&wifi_cal)) {
            cfm1_log("wifi: cal TX neutral, applying spec fallback");
            wifi_cal_tx_fallback(&wifi_cal);
            cfm1_log_int("wifi: cal fb txlo_i=", wifi_cal.txlo_i);
            cfm1_log_int("wifi: cal fb txlo_q=", wifi_cal.txlo_q);
            cfm1_log_int("wifi: cal fb tx_ig=", wifi_cal.tx[WIFI_CAL_TX_IG]);
            cfm1_log_int("wifi: cal fb tx_ip=", wifi_cal.tx[WIFI_CAL_TX_IP]);
            cfm1_log_int("wifi: cal fb tx_idc=", wifi_cal.tx[WIFI_CAL_TX_IDC]);
            cfm1_log_int("wifi: cal fb tx_qdc=", wifi_cal.tx[WIFI_CAL_TX_QDC]);
        }
        (void)rc;

        wifi_rf_wifi_mode();
        wifi_cal_apply(&wifi_cal);
        wifi_cal_record_build(&wifi_cal, wifi_cal_rec);
        /* TODO: write record 187 to flash after a 1 s delay [§10.2] */
    }

    cfm1_log("wifi: rf cal done");
}
