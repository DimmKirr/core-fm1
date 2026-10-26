/* SPDX-License-Identifier: GPL-3.0-only */
/* WiFi RF HAL: init, calibration, channel set, power control.
 * Clean-room reverse-engineered from LLVM IR in .scratch/examples/wifi/ir/wl_rf_common/.
 * Source IR files: wf_rf_init.c, wf_rf_cal.c, wf_cfr_init.c, wf_soft_adj.c,
 * wf_pa_dpd.c, wf_phy_mac_lib.c.
 *
 * Register addresses decoded from IR inttoptr(i32 N) patterns and cross-referenced
 * with JL_WL_TypeDef debug info (base 0x11900). JieLi SDK register names preserved
 * in comments for traceability. */

#include "../hal/fm1_wifi_rf.h"
#include "../hal/fm1_wifi_bbp.h"
#include "../hal/wifi_rf_tables.h"

/* ======== external declarations ======== */

extern int clk_get(const char *name);
extern void delay_us(int us);
extern int printf(const char *fmt, ...);

/* ======== BBP indirect register access ======== */
/* From wf_phy_mac_lib.c.ll. These use the BBP port (0x3101C) as a front-end
 * to access deeper baseband sub-registers via indirect addressing. */

static void cca_reg_set(uint8_t addr, uint8_t dat)
{
    fm1_wf_bbp_write(0xC9, addr);
    fm1_wf_bbp_write(0xCA, dat);
    fm1_wf_bbp_write(0xC8, 1);
}

static void rx11n_reg_set(uint8_t addr, uint8_t dat)
{
    fm1_wf_bbp_write(0xCD, addr);
    fm1_wf_bbp_write(0xCE, dat);
    fm1_wf_bbp_write(0xCC, 1);
}

static void tx_backend_wr(uint8_t addr, uint8_t dat)
{
    fm1_wf_bbp_write(0xD1, addr);
    fm1_wf_bbp_write(0xD2, dat);
    fm1_wf_bbp_write(0xD0, 1);
}

static uint8_t tx_backend_rd(uint8_t addr)
{
    fm1_wf_bbp_write(0xD1, addr);
    fm1_wf_bbp_write(0xD0, 2);
    return fm1_wf_bbp_read(0xD3);
}

static void rx_frontend_wr(uint8_t addr, uint8_t dat)
{
    fm1_wf_bbp_write(0xD5, addr);
    fm1_wf_bbp_write(0xD6, dat);
    fm1_wf_bbp_write(0xD4, 1);
}

static uint8_t rx_frontend_rd(uint8_t addr)
{
    fm1_wf_bbp_write(0xD5, addr);
    fm1_wf_bbp_write(0xD4, 2);
    return fm1_wf_bbp_read(0xD7);
}

/* ======== PHY/MAC reset (wf_phy_mac_lib.c.ll) ======== */

static void wf_phy_rst(void)
{
    FM1_WF_MAC_REG(0x1004) = 12;
    fm1_wf_bbp_write(22, 0);
    fm1_wf_bbp_write(21, 49);
    fm1_wf_bbp_write(21, 48);
    fm1_wf_bbp_write(25, 0);
}

static void wf_phy_mac_rst(void)
{
    FM1_WF_MAC_REG(0x1004) = 3;
    FM1_WF_MAC_REG(0x030C) = 30;
    FM1_WF_MAC_REG(0x030C) = 0;
    wf_phy_rst();
}

/* ======== analog register state ======== */

static uint32_t wf_wla_con[WF_WLA_CON_SIZE];
static uint32_t wfrf_wla_con7;
static uint32_t wfrf_wla_con8;
static uint8_t wifi_xosc_is_40m;

/* ======== register helpers (wf_rf_init.c.ll) ======== */

static uint32_t wlrf_get_(uint8_t idx)
{
    /* IR: set bits [31:28] to idx, loop 8x writing 1/0 to 0x11978, read back.
     * Simplified to: write index to CAL_START[31:28], brief wait, read result. */
    uint32_t v = FM1_WF_CAL_START;
    v = (v & 0x0FFFFFFFu) | ((uint32_t)idx << 28);
    FM1_WF_CAL_START = v;
    for (volatile int i = 0; i < 8; i++) {
        FM1_WF_CON30 = 1;
    }
    FM1_WF_CON30 = 0;
    return FM1_WF_CON30;
}

static void wf_wla_set_con(uint8_t idx, uint32_t val)
{
    if (idx < WF_WLA_CON_SIZE) {
        wf_wla_con[idx] = val;
        FM1_RF_REG(0x11900u + idx * 4) = val;
    }
}

static void wfrf_wla_setbits(uint32_t addr, uint32_t mask, uint32_t val)
{
    uint32_t v = FM1_RF_REG(addr);
    v = (v & ~mask) | (val & mask);
    FM1_RF_REG(addr) = v;
}

/* ======== crystal oscillator detection ======== */

static uint8_t wifi_xosc_val[2];

static void wifi_get_xosc(void)
{
    /* IR: reads xosc trim from nonvolatile store.
     * xosc_val[0] = coarse, xosc_val[1] = fine. */
    wifi_xosc_is_40m = (clk_get("sys_timer") == 40000000) ? 1 : 0;
    wifi_xosc_val[0] = 0x0A;
    wifi_xosc_val[1] = 0x0B;
}

/* ======== RF enable modes (wf_rf_init.c.ll: wf_rf_en_set) ======== */
/* mode 1-5: sets different combinations of rf_en, lna_en, rx_en, tx_en, pa_en
 * across 13+ analog registers. Translated from the IR switch/case. */

static void wf_rf_en_set(uint8_t mode)
{
    /* 0x11900 WL_CON0: rf_en at bit 0 */
    /* 0x11904 WL_CON1: lna_en at bit 0 */
    /* 0x11908 WL_CON2: rx_en at bit 0 */
    /* 0x1190C WL_CON3: bits for mixer */
    /* 0x11910 WL_CON4: rx filter/vga bits */
    /* 0x11914 WL_CON5: more rx path bits */
    /* 0x11920 LOFC_CON: LO frequency bits */
    /* 0x11924 LOFC_RES: LO result */
    /* 0x11928 WL_ANL_CON0: tx path bits */
    /* 0x1192C WL_ANL_CON1: pa bits */
    /* 0x11930 WL_ANL_CON2: pll analog bits */
    /* 0x11934 WL_ANL_CON3 / VCO_EN: vco enable */
    /* 0x11938 VCO_BAND: band select */

    switch (mode) {
    case 0: /* all off */
        FM1_WF_CON0 &= ~1u;
        FM1_WF_CON1 &= ~1u;
        FM1_WF_CON2 &= ~1u;
        FM1_WF_CON3 &= ~1u;
        FM1_WF_CON4 &= ~1u;
        FM1_WF_CON5 &= ~1u;
        FM1_WF_CON8 &= ~1u;
        FM1_WF_CON9 &= ~1u;
        FM1_WF_CON10 &= ~1u;
        FM1_WF_CON11 &= ~1u;
        FM1_WF_CON12 &= ~1u;
        FM1_WF_VCO_EN &= ~1u;
        FM1_WF_VCO_BAND &= ~1u;
        break;

    case 1: /* rf_en only (PLL warmup) */
        FM1_WF_CON0 |= 1u;
        FM1_WF_CON1 &= ~1u;
        FM1_WF_CON2 &= ~1u;
        FM1_WF_CON3 &= ~1u;
        FM1_WF_CON4 &= ~1u;
        FM1_WF_CON5 &= ~1u;
        FM1_WF_CON8 &= ~1u;
        FM1_WF_CON9 &= ~1u;
        FM1_WF_CON10 &= ~1u;
        FM1_WF_CON11 &= ~1u;
        FM1_WF_CON12 |= 1u;
        FM1_WF_VCO_EN |= 1u;
        FM1_WF_VCO_BAND |= 1u;
        break;

    case 2: /* rf_en + lna_en + rx_en (receive) */
        FM1_WF_CON0 |= 1u;
        FM1_WF_CON1 |= 1u;
        FM1_WF_CON2 |= 1u;
        FM1_WF_CON3 |= 1u;
        FM1_WF_CON4 |= 1u;
        FM1_WF_CON5 |= 1u;
        FM1_WF_CON8 |= 1u;
        FM1_WF_CON9 &= ~1u;
        FM1_WF_CON10 &= ~1u;
        FM1_WF_CON11 &= ~1u;
        FM1_WF_CON12 |= 1u;
        FM1_WF_VCO_EN |= 1u;
        FM1_WF_VCO_BAND |= 1u;
        break;

    case 3: /* rf_en + tx_en (transmit, no PA) */
        FM1_WF_CON0 |= 1u;
        FM1_WF_CON1 &= ~1u;
        FM1_WF_CON2 &= ~1u;
        FM1_WF_CON3 &= ~1u;
        FM1_WF_CON4 &= ~1u;
        FM1_WF_CON5 &= ~1u;
        FM1_WF_CON8 &= ~1u;
        FM1_WF_CON9 |= 1u;
        FM1_WF_CON10 |= 1u;
        FM1_WF_CON11 &= ~1u;
        FM1_WF_CON12 |= 1u;
        FM1_WF_VCO_EN |= 1u;
        FM1_WF_VCO_BAND |= 1u;
        break;

    case 4: /* rf_en + tx_en + pa_en (transmit with PA) */
        FM1_WF_CON0 |= 1u;
        FM1_WF_CON1 &= ~1u;
        FM1_WF_CON2 &= ~1u;
        FM1_WF_CON3 &= ~1u;
        FM1_WF_CON4 &= ~1u;
        FM1_WF_CON5 &= ~1u;
        FM1_WF_CON8 &= ~1u;
        FM1_WF_CON9 |= 1u;
        FM1_WF_CON10 |= 1u;
        FM1_WF_CON11 |= 1u;
        FM1_WF_CON12 |= 1u;
        FM1_WF_VCO_EN |= 1u;
        FM1_WF_VCO_BAND |= 1u;
        break;

    case 5: /* calibration mode: rf_en + special loopback */
        FM1_WF_CON0 |= 1u;
        FM1_WF_CON1 &= ~1u;
        FM1_WF_CON2 |= 1u;
        FM1_WF_CON3 |= 1u;
        FM1_WF_CON4 |= 1u;
        FM1_WF_CON5 &= ~1u;
        FM1_WF_CON8 &= ~1u;
        FM1_WF_CON9 |= 1u;
        FM1_WF_CON10 |= 1u;
        FM1_WF_CON11 &= ~1u;
        FM1_WF_CON12 |= 1u;
        FM1_WF_VCO_EN |= 1u;
        FM1_WF_VCO_BAND |= 1u;
        break;
    }
}

/* ======== RX filter/VGA gain (wf_rf_init.c.ll) ======== */

static void wfrf_flt_vga_gain(uint8_t idx)
{
    if (idx >= 42) return;
    const uint8_t *e = wifi_rx_flt_vga_gain[idx];

    /* e[0] -> CON4 bits [25:22] (VGA coarse gain) */
    uint32_t v = FM1_WF_CON4;
    v = (v & ~(0xFu << 22)) | (((uint32_t)e[0] & 0xF) << 22);
    FM1_WF_CON4 = v;

    /* e[1] -> CON4 bit 25 (bypass flag) */
    v = FM1_WF_CON4;
    v = (v & ~(1u << 25)) | (((uint32_t)e[1] & 1) << 25);
    FM1_WF_CON4 = v;

    /* e[1] -> CON4 bit 26 */
    v = FM1_WF_CON4;
    v = (v & ~(1u << 26)) | (((uint32_t)e[1] & 1) << 26);
    FM1_WF_CON4 = v;

    /* e[2] bits [2:0] -> CON5 bits [17:15] (fine gain) */
    v = FM1_WF_CON5;
    v = (v & ~(7u << 15)) | (((uint32_t)e[2] & 7) << 15);
    FM1_WF_CON5 = v;

    /* e[2] bit 4 -> CON5 bit 12 */
    v = FM1_WF_CON5;
    v = (v & ~(1u << 12)) | ((((uint32_t)(int8_t)e[2] >> 4) & 1u) << 12);
    FM1_WF_CON5 = v;

    /* e[2] bit 5 -> CON5 bit 13 */
    v = FM1_WF_CON5;
    v = (v & ~(1u << 13)) | ((((uint32_t)(int8_t)e[2] >> 5) & 1u) << 13);
    FM1_WF_CON5 = v;

    /* e[2] bit 6 -> CON5 bit 14 */
    v = FM1_WF_CON5;
    v = (v & ~(1u << 14)) | ((((uint32_t)(int8_t)e[2] >> 6) & 1u) << 14);
    FM1_WF_CON5 = v;

    /* e[3] -> CON5 bits [7:5] (VGA mode) */
    v = FM1_WF_CON5;
    v = (v & ~(7u << 5)) | (((uint32_t)e[3] << 5) & (7u << 5));
    FM1_WF_CON5 = v;
}

/* ======== RX AGC gain from LUT (wf_rf_init.c.ll) ======== */

extern const uint32_t wl_rfd_ram_lut[256][2];
extern const uint32_t wl_rfd_ram_lut_40M[256][2];

static void wfrf_rx_agc_gain(int8_t gain)
{
    const uint32_t (*lut)[2] = wifi_xosc_is_40m ? wl_rfd_ram_lut : wl_rfd_ram_lut_40M;
    uint32_t packed = lut[(uint8_t)(gain + 128)][1];

    /* bits [22:26] from packed[7+n] -> CON4 bits [22:26] */
    uint32_t v = FM1_WF_CON4;
    v = (v & ~(1u << 22)) | (((packed >> 7) & 1) << 22);
    FM1_WF_CON4 = v;

    v = FM1_WF_CON4;
    v = (v & ~(1u << 23)) | (((packed >> 7) & 2) << 22);
    FM1_WF_CON4 = v;

    v = FM1_WF_CON4;
    v = (v & ~(1u << 24)) | (((packed >> 7) & 4) << 22);
    FM1_WF_CON4 = v;

    v = FM1_WF_CON4;
    v = (v & ~(1u << 25)) | (((packed >> 7) & 8) << 22);
    FM1_WF_CON4 = v;

    v = FM1_WF_CON4;
    v = (v & ~(1u << 26)) | (((packed >> 7) & 16) << 22);
    FM1_WF_CON4 = v;

    /* packed >> 11 -> CON5 bits [17:15, 14:12, 7:5] */
    uint32_t shifted = packed >> 11;
    v = FM1_WF_CON5;
    v = (v & ~(7u << 15)) | ((shifted & 7) << 15);
    FM1_WF_CON5 = v;

    v = FM1_WF_CON5;
    v = (v & ~(1u << 12)) | ((shifted & (1u << 12)));
    FM1_WF_CON5 = v;

    v = FM1_WF_CON5;
    v = (v & ~(1u << 13)) | ((shifted & (1u << 13)));
    FM1_WF_CON5 = v;

    v = FM1_WF_CON5;
    v = (v & ~(1u << 14)) | ((shifted & (1u << 14)));
    FM1_WF_CON5 = v;

    v = FM1_WF_CON5;
    v = (v & ~(7u << 5)) | (((packed >> 15) & 7) << 5);
    FM1_WF_CON5 = v;

    /* packed bits [1:0] -> CON16 bits [17:16] (LNA gain) */
    v = FM1_WF_CON16;
    v = (v & ~(3u << 16)) | (((packed & 3) << 16));
    FM1_WF_CON16 = v;

    /* packed bits [3:2] -> CON18 bits [7:6] */
    v = FM1_WF_CON18;
    v = (v & ~(3u << 6)) | ((packed << 4) & (3u << 6));
    FM1_WF_CON18 = v;

    /* packed bits [14:8] >> 3 -> CON4 bits [11:8] */
    v = FM1_WF_CON4;
    v = (v & ~(0xFu << 8)) | (((packed >> 3) & 0xF) << 8);
    FM1_WF_CON4 = v;

    /* packed bits [10:4] >> 4 -> CON4 bits [6:0] */
    v = FM1_WF_CON4;
    v = (v & ~0x7Fu) | ((packed >> 4) & 0x7F);
    FM1_WF_CON4 = v;
}

/* ======== high-pass filter control (wf_rf_init.c.ll) ======== */

static void wfrf_hp_open(void)
{
    FM1_WF_CON5 = (FM1_WF_CON5 & ~7u) | 1u;
    FM1_WF_CON5 |= 8u;
    FM1_WF_CON5 |= 16u;
}

static void wfrf_hp_close(void)
{
    FM1_WF_CON5 &= ~7u;
    FM1_WF_CON5 &= ~8u;
    FM1_WF_CON5 &= ~16u;
}

/* ======== calibration attenuation setup (wf_rf_init.c.ll) ======== */

static void rx_tx_cal_attenuation(int8_t is_tx)
{
    uint32_t v;

    /* LOFC_CON (0x11920): clear bit 20, set bit 21 */
    v = FM1_WF_CON8;
    v &= ~(1u << 20);
    FM1_WF_CON8 = v;

    v = FM1_WF_CON8;
    v |= (1u << 21);
    FM1_WF_CON8 = v;

    v = FM1_WF_CON8;
    if (is_tx == 1) {
        /* TX cal: clear bit 22, set bit 23 */
        v &= ~(1u << 22);
        FM1_WF_CON8 = v;
        v = FM1_WF_CON8;
        v |= (1u << 23);
    } else {
        /* RX cal: set bit 22, clear bit 23 */
        v |= (1u << 22);
        FM1_WF_CON8 = v;
        v = FM1_WF_CON8;
        v &= ~(1u << 23);
    }
    FM1_WF_CON8 = v;

    /* LOFC_RES (0x11924): bits [30:29] = 0b10, bit 31 varies */
    v = FM1_WF_CON9;
    v = (v & ~(7u << 29)) | (2u << 29);
    FM1_WF_CON9 = v;

    /* WL_ANL_CON0 (0x11928): set bits [14:13] */
    v = FM1_WF_CON10;
    v |= (3u << 13);
    FM1_WF_CON10 = v;

    /* attenuation path routing */
    v = FM1_WF_CON10;
    v = (v & ~(3u << 27));
    if (is_tx == 1) {
        v |= (3u << 27);
    } else {
        v |= (1u << 27);
    }
    FM1_WF_CON10 = v;

    /* CON4 bits [11:8] = 0x09 (attenuation level for cal) */
    v = FM1_WF_CON4;
    v = (v & ~(0xFu << 8)) | (9u << 8);
    FM1_WF_CON4 = v;
}

/* ======== PA tune gain (wf_rf_init.c.ll) ======== */

static void pa_tune_gain(int8_t idx)
{
    (void)idx;
    uint32_t v;

    /* LOFC_CON (0x11920) */
    v = FM1_WF_CON8;
    v &= ~(1u << 20);
    FM1_WF_CON8 = v;
    v = FM1_WF_CON8;
    v |= (1u << 21);
    FM1_WF_CON8 = v;
    v = FM1_WF_CON8;
    v &= ~(1u << 22);
    FM1_WF_CON8 = v;
    v = FM1_WF_CON8;
    v |= (1u << 23);
    FM1_WF_CON8 = v;

    /* LOFC_RES (0x11924) */
    v = FM1_WF_CON9;
    v = (v & ~(7u << 24)) | (1u << 24);
    FM1_WF_CON9 = v;
    v = FM1_WF_CON9;
    v |= (7u << 29);
    FM1_WF_CON9 = v;

    /* WL_ANL_CON0 (0x11928) */
    v = FM1_WF_CON10;
    v = (v & ~7u) | 3u;
    FM1_WF_CON10 = v;
    v = FM1_WF_CON10;
    v = (v & ~(7u << 9)) | (3u << 9);
    FM1_WF_CON10 = v;
    v = FM1_WF_CON10;
    v |= (3u << 13);
    FM1_WF_CON10 = v;
    v = FM1_WF_CON10;
    v = (v & ~(7u << 15)) | (2u << 15);
    FM1_WF_CON10 = v;
    v = FM1_WF_CON10;
    v = (v & ~(3u << 21)) | (3u << 21);
    FM1_WF_CON10 = v;
    v = FM1_WF_CON10;
    v &= ~(3u << 27);
    FM1_WF_CON10 = v;

    /* WL_ANL_CON1 (0x1192C) */
    v = FM1_WF_CON11;
    v &= ~7u;
    FM1_WF_CON11 = v;

    /* CON4 bits [11:8] = 0 */
    v = FM1_WF_CON4;
    v &= ~(0xFu << 8);
    FM1_WF_CON4 = v;
}

/* ======== PLL control (wf_rf_cal.c.ll) ======== */

static uint8_t wfa_pll_bank_limit[48];

static void set_pll_fbds(uint8_t val)
{
    /* 71996 = 0x1193C = VCO_FINE: bits [12:5] = val */
    uint32_t v = FM1_WF_VCO_FINE;
    v = (v & ~(0xFFu << 5)) | ((uint32_t)val << 5);
    FM1_WF_VCO_FINE = v;
}

static void set_pll_cap(uint8_t val)
{
    /* 71992 = 0x11938 = VCO_BAND: bits [25:19] = val & 0x7F */
    uint32_t v = FM1_WF_VCO_BAND;
    v = (v & ~(0x7Fu << 19)) | (((uint32_t)(val & 0x7F)) << 19);
    FM1_WF_VCO_BAND = v;
}

static void set_pll_rn_en(void)
{
    /* 71992 = 0x11938 = VCO_BAND: clear bit 16, clear bit 18 */
    uint32_t v = FM1_WF_VCO_BAND;
    v &= ~(1u << 16);
    FM1_WF_VCO_BAND = v;

    v = FM1_WF_VCO_BAND;
    v &= ~(1u << 18);
    FM1_WF_VCO_BAND = v;

    /* 71988 = 0x11934 = VCO_EN: clear bit 24 */
    v = FM1_WF_VCO_EN;
    v &= ~(1u << 24);
    FM1_WF_VCO_EN = v;

    delay_us(5);

    /* VCO_BAND: set bit 16 */
    v = FM1_WF_VCO_BAND;
    v |= (1u << 16);
    FM1_WF_VCO_BAND = v;

    delay_us(5);

    /* VCO_EN: set bit 24 */
    v = FM1_WF_VCO_EN;
    v |= (1u << 24);
    FM1_WF_VCO_EN = v;

    /* VCO_BAND: set bit 18 */
    v = FM1_WF_VCO_BAND;
    v |= (1u << 18);
    FM1_WF_VCO_BAND = v;

    delay_us(100);
}

static uint8_t get_pll_vc_res(void)
{
    uint32_t v = wlrf_get_(1);
    uint8_t bit18 = (v >> 18) & 1;
    uint8_t bit17 = (v >> 16) & 2;
    return (uint8_t)(bit18 | bit17);
}

static void open_rf_pll_vc_det(void)
{
    /* 72000 = 0x11940 = CON16 (WF_CON0): set bit 24 */
    FM1_WF_CON16 |= (1u << 24);
}

static void close_rf_pll_vc_det(void)
{
    FM1_WF_CON16 &= ~(1u << 24);
}

static uint8_t get_wfa_pll_midbank(void)
{
    set_pll_fbds(73);
    uint8_t res0 = 0, res1 = 0, res2 = 0;
    uint8_t cap = 7;

    while (cap < 120) {
        for (uint8_t round = 0; round < 3; round++) {
            if (round == 0) {
                set_pll_cap(cap);
            } else if (round == 1) {
                set_pll_cap(cap - 7);
            } else {
                set_pll_cap(cap + 8);
            }
            set_pll_rn_en();
            uint8_t r = get_pll_vc_res();
            if (round == 0)      res0 = r;
            else if (round == 1) res1 = r;
            else                 res2 = r;
        }
        if (!(res1 == res0 && res2 == res0))
            break;
        cap += 16;
    }
    printf("\n %d bank: %x \n", 2394, (unsigned)res0);
    return res0 | 0x0F;
}

static uint8_t get_wfa_pll_bank(uint8_t fbds)
{
    for (int8_t i = 1; i < 48; i++) {
        if (wfa_pll_bank_limit[i] > fbds) {
            return wfa_pll_bank_limit[47] - (uint8_t)(i - 1);
        }
    }
    return wfa_pll_bank_limit[47] - 47;
}

static uint8_t wfa_pll_bank_set(uint8_t val)
{
    return wfa_pll_bank_limit[47] - val;
}

static void wfa_pll_bank_scan(uint8_t restore_fbds)
{
    open_rf_pll_vc_det();
    uint8_t mid = get_wfa_pll_midbank();
    wfa_pll_bank_limit[47] = mid;

    for (int ch = 0; ch < 47; ch++) {
        uint8_t bank_lo = wfa_pll_bank_limit[47];
        uint8_t lo_fbds = (uint8_t)(ch + 73);
        uint8_t hi_fbds = lo_fbds;
        uint8_t lo_res, hi_res;

        for (;;) {
            set_pll_fbds(lo_fbds);
            set_pll_rn_en();
            uint8_t r = get_pll_vc_res();
            if (r == 1) {
                hi_res = lo_fbds;
                break;
            }
            if (r == 2) {
                lo_fbds++;
                if (lo_fbds > 171) {
                    lo_res = 171;
                    hi_res = 171;
                    goto store;
                }
                continue;
            }
            lo_fbds++;
            if (lo_fbds > 171) {
                hi_res = 171;
                break;
            }
        }
        lo_res = lo_fbds;
store:
        wfa_pll_bank_limit[ch] = (uint8_t)((bank_lo - 3 + lo_res) / 2);
    }

    set_pll_fbds(restore_fbds);
    uint8_t bk = get_wfa_pll_bank(restore_fbds);
    set_pll_cap(bk);
    printf("\nsetbak:%x", (unsigned)bk);
    close_rf_pll_vc_det();
}

/* ======== VCO scan ======== */

static int wf_vco_scan(void)
{
    for (int band = 0; band < 128; band++) {
        uint32_t v = FM1_WF_VCO_BAND;
        v = (v & ~(0x7Fu << 19)) | ((uint32_t)band << 19);
        FM1_WF_VCO_BAND = v;

        FM1_WF_VCO_EN |= (1u << 24);
        for (volatile int d = 0; d < 20; d++) ;
        FM1_WF_VCO_EN &= ~(1u << 24);

        FM1_WF_VCO_BAND |= (1u << 16) | (1u << 18);
        for (volatile int d = 0; d < 20; d++) ;
        FM1_WF_VCO_BAND &= ~((1u << 16) | (1u << 18));

        uint32_t cal = FM1_WF_CAL_START;
        if (cal & (1u << 28))
            return band;
    }
    return -1;
}

/* ======== RX tune / IQ calibration (wf_rf_cal.c.ll) ======== */

static void txlo_cali_set(uint8_t val)
{
    /* 71964 = 0x1191C = CON7: bits [14:8] */
    uint32_t v = FM1_WF_CON7;
    v = (v & ~(0x7Fu << 8)) | (((uint32_t)(val & 0x7F)) << 8);
    FM1_WF_CON7 = v;
}

static void txlo_calq_set(uint8_t val)
{
    /* 71968 = 0x11920 = CON8 / LOFC_CON: bits [6:0] */
    uint32_t v = FM1_WF_CON8;
    v = (v & ~0x7Fu) | (val & 0x7F);
    FM1_WF_CON8 = v;
}

static int16_t set_anl_rxdc_i(int16_t val)
{
    uint32_t v = FM1_WF_CON8;
    v = (v & ~(0x1FFu << 7)) | (((uint32_t)(val & 0x1FF)) << 7);
    FM1_WF_CON8 = v;
    return val;
}

static int16_t set_anl_rxdc_q(int16_t val)
{
    uint32_t v = FM1_WF_CON8;
    v = (v & ~(0x1FFu << 16)) | (((uint32_t)(val & 0x1FF)) << 16);
    FM1_WF_CON8 = v;
    return val;
}

static int32_t dem_filter_dc(int8_t mode)
{
    rx_frontend_wr(21, 1);
    rx_frontend_wr(22, 1);
    rx_frontend_wr(21, 3);
    rx_frontend_wr(22, 3);

    while ((rx_frontend_rd(0x8D) & 3) != 0)
        ;

    int32_t result = 0;
    switch (mode) {
    case 0: case 3: {
        int16_t i_val = ((int16_t)rx_frontend_rd(0x81) << 8) | rx_frontend_rd(0x82);
        int16_t q_val = ((int16_t)rx_frontend_rd(0x84) << 8) | rx_frontend_rd(0x85);
        result = (int32_t)i_val * i_val + (int32_t)q_val * q_val;
        break;
    }
    case 1: {
        int32_t v = ((int32_t)rx_frontend_rd(0x81) << 24)
                  | ((int32_t)rx_frontend_rd(0x82) << 16)
                  | ((int32_t)rx_frontend_rd(0x83) << 8);
        result = v / 256;
        break;
    }
    case 2: {
        int32_t v = ((int32_t)rx_frontend_rd(0x8A) << 24)
                  | ((int32_t)rx_frontend_rd(0x8B) << 16)
                  | ((int32_t)rx_frontend_rd(0x8C) << 8);
        result = v / 256;
        break;
    }
    default: {
        int16_t i_val = ((int16_t)rx_frontend_rd(0x87) << 8) | rx_frontend_rd(0x88);
        int16_t q_val = ((int16_t)rx_frontend_rd(0x8A) << 8) | rx_frontend_rd(0x8B);
        result = (int32_t)i_val * i_val + (int32_t)q_val * q_val;
        break;
    }
    }
    return result;
}

static void wf_rx_tune_test(void)
{
    /* WL_CON1 (0x11904) bits [11:8] = 0x08 */
    uint32_t v = FM1_WF_CON1;
    v = (v & ~(0xFu << 8)) | (8u << 8);
    FM1_WF_CON1 = v;

    /* CON20 (0x11950) set bit 27 */
    FM1_WF_CON20 |= (1u << 27);

    uint8_t dc_i = 0, dc_mid = 0, dc_q = 0;

    for (uint8_t round = 0; round < 3; round++) {
        /* CAL_START (0x11968) bits [9:0]: DAC code */
        v = FM1_WF_CAL_START;
        v &= ~0x3FFu;
        if (round == 0)      v |= 508;
        else if (round == 1) v |= 290;
        else                 v |= 160;
        FM1_WF_CAL_START = v;

        /* clear noise bits, set cal trigger */
        v = FM1_WF_CAL_START;
        v = (v & ~(0xFFu << 10)) | (30u << 10);
        FM1_WF_CAL_START = v;

        v = FM1_WF_CAL_START;
        v &= ~(0xFFu << 18);
        FM1_WF_CAL_START = v;

        v = FM1_WF_CAL_START;
        v &= ~(1u << 23);
        FM1_WF_CAL_START = v;

        /* start measurement */
        FM1_WF_CAL_START |= 1u;

        while (!(wlrf_get_(1) & 32))
            ;

        if (round == 0) {
            dc_i = (uint8_t)(wlrf_get_(1) >> 8);
        } else if (round == 1) {
            dc_mid = (uint8_t)(wlrf_get_(1) >> 8);
        } else {
            dc_q = (uint8_t)(wlrf_get_(1) >> 8);
        }

        FM1_WF_CAL_START &= ~1u;
    }

    /* store results: CON20 clear bit 27 */
    FM1_WF_CON20 &= ~(1u << 27);

    /* LOFC_CON (0x11920) bits [17:10] = dc_mid */
    v = FM1_WF_CON8;
    v = (v & ~(0xFFu << 10)) | ((uint32_t)dc_mid << 10);
    FM1_WF_CON8 = v;

    /* CON4 bits [21:14] = dc_mid */
    v = FM1_WF_CON4;
    v = (v & ~(0xFFu << 14)) | ((uint32_t)dc_mid << 14);
    FM1_WF_CON4 = v;

    /* CON23 (0x1195C): store calibration results */
    FM1_WF_CON23 |= 0xFF;
    v = FM1_WF_CON23;
    v = (v & ~(0xFFu << 8)) | ((uint32_t)dc_i << 8);
    FM1_WF_CON23 = v;
    v = FM1_WF_CON23;
    v = (v & ~(0xFFu << 16)) | ((uint32_t)dc_mid << 16);
    FM1_WF_CON23 = v;
    v = FM1_WF_CON23;
    v = (v & 0x00FFFFFFu) | ((uint32_t)dc_q << 24);
    FM1_WF_CON23 = v;

    /* CON24 (0x11960): same pattern */
    FM1_WF_CON24 |= 0xFF;
    v = FM1_WF_CON24;
    v = (v & ~(0xFFu << 8)) | ((uint32_t)dc_i << 8);
    FM1_WF_CON24 = v;
    v = FM1_WF_CON24;
    v = (v & ~(0xFFu << 16)) | ((uint32_t)dc_mid << 16);
    FM1_WF_CON24 = v;
    v = FM1_WF_CON24;
    v = (v & 0x00FFFFFFu) | ((uint32_t)dc_q << 24);
    FM1_WF_CON24 = v;
}

static int16_t rx_dc_tune[4][2];

static void wf_rx_dc_cal(void)
{
    wf_rf_en_set(2);
    wf_rx_tune_test();

    for (int bw = 0; bw < 4; bw++) {
        int16_t best_i = 0, best_q = 0;
        int32_t best_pwr = 0x7FFFFFFF;

        for (int16_t qi = -256; qi < 256; qi += 8) {
            set_anl_rxdc_q(qi);
            set_anl_rxdc_i(0);
            int32_t pwr = dem_filter_dc((int8_t)bw);
            if (pwr < best_pwr) {
                best_pwr = pwr;
                best_q = qi;
            }
        }
        set_anl_rxdc_q(best_q);

        best_pwr = 0x7FFFFFFF;
        for (int16_t ii = -256; ii < 256; ii += 8) {
            set_anl_rxdc_i(ii);
            int32_t pwr = dem_filter_dc((int8_t)bw);
            if (pwr < best_pwr) {
                best_pwr = pwr;
                best_i = ii;
            }
        }
        set_anl_rxdc_i(best_i);

        rx_dc_tune[bw][0] = best_i;
        rx_dc_tune[bw][1] = best_q;
    }
}

static void wf_tx_iq_cal(void)
{
    wf_rf_en_set(5);
    rx_tx_cal_attenuation(1);
    txlo_cali_set(64);
    txlo_calq_set(64);
}

static void wf_rx_iq_cal(void)
{
    wf_rf_en_set(5);
    rx_tx_cal_attenuation(0);
}

static void wf_dc_offset_cal(void)
{
    wf_rx_dc_cal();
}

static void wf_bw_cal(void)
{
    wfrf_hp_open();
    wfrf_flt_vga_gain(20);
    wfrf_hp_close();
}

/* ======== AGC configuration (wf_soft_adj.c.ll) ======== */

static const int32_t dagc_ram_set[128] = {
    -5242644, -4718099, -4193554, -3669009, -3144464, -2619919, -2095374, -1570829,
    -1046284,  -521739,     2806,   527351,  1051896,  1576441,  2100986,  2625531,
     3150076,  3674621,  4199166,  4723711,  5248000,  5772545,  6297090,  6821635,
     7346180,  7870725,  8395270,  8919815,  9444360,  9968905, 10493450, 11017995,
    11542540, 12067085, 12591630, 13116175, 13640720, 14165265, 14689810, 15214355,
    15738900, 16263445, 16787990, 17312535, 17837080, 18361625, 18886170, 19410715,
    19935260, 20459805, 20984350, 21508895, 22033440, 22557985, 23082530, 23607075,
    24131620, 24656165, 25180710, 25705255, 26229800, 26754345, 27278890, 27803435,
    28327980, 28852525, 29377070, 29901615, 30426160, 30950705, 31475250, 31999795,
    32524340, 33048885, 33573430, 34097975, 34622520, 35147065, 35671610, 36196155,
    36720700, 37245245, 37769790, 38294335, 38818880, 39343425, 39867970, 40392515,
    40917060, 41441605, 41966150, 42490695, 43015240, 43539785, 44064330, 44588875,
    45113420, 45637965, 46162510, 46687055, 47211600, 47736145, 48260690, 48785235,
    49309780, 49834325, 50358870, 50883415, 51407960, 51932505, 52457050, 52981595,
    53506140, 54030685, 54555230, 55079775, 55604320, 56128865, 56653410, 57177955,
    57702500, 58227045, 58751590, 59276135, 59800680, 60325225, 60849770, 61374315,
};

static void dig_agc_tale_set(void)
{
    for (int i = 0; i < 128; i++) {
        cca_reg_set(47, (uint8_t)i);
        int32_t entry = dagc_ram_set[i];

        int8_t val = (int8_t)entry;
        if ((entry << 24) > (int32_t)0x3F000000)
            val = 63;
        else if ((entry << 24) < (int32_t)0xC0000000)
            val = -64;
        cca_reg_set(43, (uint8_t)val);
        cca_reg_set(78, (uint8_t)(entry >> 8));
        cca_reg_set(84, (uint8_t)(entry >> 16));
        cca_reg_set(85, (uint8_t)(entry >> 24));
        cca_reg_set(47, (uint8_t)i | 0x80);
    }
    cca_reg_set(47, 0);
    cca_reg_set(84, 0xC0);
    cca_reg_set(85, 1);
    cca_reg_set(78, 66);
    cca_reg_set(43, 66);
    cca_reg_set(44, 66);
    cca_reg_set(121, 0);
    cca_reg_set(122, 32);
}

static int8_t last_rssi_gain = 66;

static void adj_agc_max_gain(int8_t rssi)
{
    uint8_t gain;
    if (rssi > -36)       gain = 34;
    else if (rssi > -46)  gain = 43;
    else if (rssi > -60)  gain = 51;
    else if (rssi > -76)  gain = 57;
    else                  gain = 66;

    if ((int8_t)gain != last_rssi_gain) {
        cca_reg_set(44, gain);
        last_rssi_gain = (int8_t)gain;
    }
}

/* ======== CFR/RAPP initialization (wf_cfr_init.c.ll) ======== */

static const int32_t rapp_ram_data[128] = {
    511,511,510,509,508,507,506,505,504,503,502,501,500,498,497,496,
    495,493,492,491,489,488,487,485,484,482,481,479,477,476,474,473,
    471,469,467,466,464,462,460,458,457,455,453,451,449,447,445,443,
    441,439,437,435,433,431,428,426,424,422,420,418,416,413,411,409,
    407,404,402,400,398,396,393,391,389,386,384,382,380,377,375,373,
    371,368,366,364,362,359,357,355,353,350,348,346,344,341,339,337,
    335,333,330,328,326,324,322,320,318,315,313,311,309,307,305,303,
    301,299,297,295,293,291,289,287,285,283,281,279,277,275,273,271,
};

static void rapp_ram_wr_dis_pulse(void)
{
    uint8_t v = tx_backend_rd(92);
    tx_backend_wr(92, v | 16);
}

static void rapp_ram_wr_en_pulse(void)
{
    uint8_t v = tx_backend_rd(92);
    tx_backend_wr(92, v & ~16);
}

static void rapp_ram_wr_addr(uint8_t addr)
{
    uint8_t v = tx_backend_rd(93) & 0x80;
    if (addr > 127) addr = 127;
    tx_backend_wr(93, v | addr);
}

static void rapp_ram_wr_data(uint32_t data)
{
    uint8_t ctrl = tx_backend_rd(92) & ~32;
    if (data > 511) data = 511;
    tx_backend_wr(94, (uint8_t)data);
    tx_backend_wr(92, (uint8_t)(ctrl | ((data >> 3) & 32)));
}

static void rapp_ctl(uint8_t mode)
{
    uint8_t v = tx_backend_rd(81) & ~3;
    if (mode > 3) mode = 3;
    tx_backend_wr(81, v | mode);
}

static void rapp_ram_init(void)
{
    rapp_ctl(0);
    for (int i = 0; i < 128; i++) {
        rapp_ram_wr_dis_pulse();
        rapp_ram_wr_addr((uint8_t)i);
        rapp_ram_wr_data((uint32_t)rapp_ram_data[i]);
        rapp_ram_wr_en_pulse();
    }
}

static void rapp_set_thr_pwr_11gn(uint32_t val)
{
    uint8_t v = tx_backend_rd(81) & ~12;
    tx_backend_wr(82, (uint8_t)val);
    tx_backend_wr(83, (uint8_t)(val >> 8));
    tx_backend_wr(81, (uint8_t)(v | ((val >> 14) & 12)));
}

static void rapp_set_thr_pwr_11b(uint32_t val)
{
    uint8_t v = tx_backend_rd(81) & ~48;
    tx_backend_wr(84, (uint8_t)val);
    tx_backend_wr(85, (uint8_t)(val >> 8));
    tx_backend_wr(81, (uint8_t)(v | ((val >> 12) & 48)));
}

static void rapp_set_thr_pwr_bpsk(uint32_t val)
{
    uint8_t v = tx_backend_rd(81) & 63;
    tx_backend_wr(86, (uint8_t)val);
    tx_backend_wr(87, (uint8_t)(val >> 8));
    tx_backend_wr(81, (uint8_t)(v | ((val >> 10) & 192)));
}

static void rapp_set_thr_11gn(uint32_t val)
{
    uint8_t v = tx_backend_rd(88) & ~7;
    tx_backend_wr(89, (uint8_t)val);
    tx_backend_wr(88, (uint8_t)(v | ((val >> 8) & 7)));
}

static void rapp_set_thr_11b(uint32_t val)
{
    uint8_t v = tx_backend_rd(88) & ~56;
    tx_backend_wr(90, (uint8_t)val);
    tx_backend_wr(88, (uint8_t)(v | ((val >> 5) & 56)));
}

static void rapp_set_thr_bpsk(uint32_t val)
{
    uint8_t v88 = tx_backend_rd(88) & 63;
    uint8_t v92 = tx_backend_rd(92) & ~1;
    tx_backend_wr(91, (uint8_t)val);
    tx_backend_wr(88, (uint8_t)(v88 | ((val >> 2) & 192)));
    tx_backend_wr(92, (uint8_t)(v92 | ((val >> 10) & 1)));
}

static void rapp_init(void)
{
    rapp_ram_init();
    rapp_set_thr_11gn(160);
    rapp_set_thr_pwr_11gn(208228);
    rapp_set_thr_11b(160);
    rapp_set_thr_pwr_11b(208228);
    rapp_set_thr_bpsk(400);
    rapp_set_thr_pwr_bpsk(147414);
    rapp_ctl(2);
}

static void cfr_int(int8_t enable)
{
    if (!enable) return;

    tx_backend_wr(77, 15);
    tx_backend_wr(78, 15);
    tx_backend_wr(79, 0);
    tx_backend_wr(38, 55);
    tx_backend_wr(39, 1);
    tx_backend_wr(40, 2);
    tx_backend_wr(41, 108);
    tx_backend_wr(42, 1);
    tx_backend_wr(43, 2);
    tx_backend_wr(44, 2);
    tx_backend_wr(49, 3);
    tx_backend_wr(45, 106);
    tx_backend_wr(46, 1);
    tx_backend_wr(47, 1);
    tx_backend_wr(48, 2);
    tx_backend_wr(50, 0);
    tx_backend_wr(51, 56);
    tx_backend_wr(52, 1);
    tx_backend_wr(53, 1);
    tx_backend_wr(54, (uint8_t)-20);
    tx_backend_wr(55, 1);
    tx_backend_wr(56, (uint8_t)-73);
    tx_backend_wr(57, 2);
    tx_backend_wr(62, 2);
    tx_backend_wr(58, (uint8_t)-21);
    tx_backend_wr(59, 1);
    tx_backend_wr(60, (uint8_t)-74);
    tx_backend_wr(61, 2);
    tx_backend_wr(63, 1);
    tx_backend_wr(64, 56);
    tx_backend_wr(65, 1);
    tx_backend_wr(66, 1);
    tx_backend_wr(67, 107);
    tx_backend_wr(68, 1);
    tx_backend_wr(69, 2);
    tx_backend_wr(70, 2);
    tx_backend_wr(75, 2);
    tx_backend_wr(71, 106);
    tx_backend_wr(72, 1);
    tx_backend_wr(73, 1);
    tx_backend_wr(74, 2);
    tx_backend_wr(76, 0);
    tx_backend_wr(11, 32);
    tx_backend_wr(13, 32);
    tx_backend_wr(15, 32);
    tx_backend_wr(17, 32);
    tx_backend_wr(19, 32);
    tx_backend_wr(21, 43);
    tx_backend_wr(23, 38);
}

static void wf_cfr_init(void)
{
    cfr_int(1);
    rapp_init();
}

static void wf_pa_dpd_init(void)
{
    pa_tune_gain(0);
}

/* ======== channel set (wf_soft_adj.c.ll) ======== */

static uint8_t wifi_current_channel;

static void wifi_set_channel(uint8_t channel)
{
    if (channel < 1 || channel > 14) return;
    wifi_current_channel = channel;

    /* 82012 = 0x1405C: bits [7:0] = channel + 101 (PLL feedback divider) */
    uint32_t v = FM1_WF_RST;
    v = (v & ~0xFFu) | ((channel + 101u) & 0xFF);
    FM1_WF_RST = v;
}

/* ======== TX power control (wf_soft_adj.c.ll) ======== */

static void wifi_set_pwr(uint8_t power)
{
    /* 82012 = 0x1405C: bits [23:16] = power (capped at 6) */
    uint32_t v = FM1_WF_RST;
    v &= ~(0xFFu << 16);
    uint32_t pwr = (power < 6) ? ((uint32_t)power << 16) : (6u << 16);
    v |= pwr;
    FM1_WF_RST = v;
}

static void wifi_set_tx_power(uint8_t power)
{
    wifi_set_pwr(power);
}

/* ======== PLL offset adjustment (wf_soft_adj.c.ll) ======== */

static void wf_rf_pll_offset_adj(int32_t offset_hz, int8_t mode)
{
    int32_t adj;
    if (mode == 0) {
        adj = (offset_hz << 20) / 24000;
    } else {
        adj = (int32_t)FM1_WF_WR_CTL + (offset_hz << 20) / 24000;
        if ((uint32_t)(offset_hz + 30) <= 60)
            return;
    }
    if (adj > 6553) adj = 6553;
    if (adj < -6553) adj = -6553;
    FM1_WF_WR_CTL = (uint32_t)adj;
}

static void rf_pll_retune(void)
{
    wf_rf_en_set(0);
    FM1_WF_CON20 = 0;

    uint32_t v = FM1_WF_CON16;
    v &= ~1u;
    FM1_WF_CON16 = v;

    v = FM1_WF_CON16;
    v = (v & ~(3u << 6)) | (2u << 6);
    FM1_WF_CON16 = v;

    v = FM1_WF_VCO_FINE;
    v = (v & ~(0xFu << 18)) | (0xEu << 18);
    FM1_WF_VCO_FINE = v;

    v = FM1_WF_VCO_FINE;
    v |= (7u << 28);
    FM1_WF_VCO_FINE = v;

    wfa_pll_bank_scan(119);
}

/* ======== deep sleep (wf_deep_sleep.c.ll) ======== */

static void wifi_rf_sleep(void)
{
    wf_rf_en_set(0);
}

static void wifi_rf_wake(void)
{
    wf_rf_en_set(1);
    delay_us(200);
    wf_rf_en_set(2);
}

/* ======== PHY/MAC init ======== */

static void wf_phy_mac_init(void)
{
    wf_phy_mac_rst();
    dig_agc_tale_set();
}

/* ======== clock and reset init ======== */

static void wf_clk_rst_init(void)
{
    FM1_WF_CLK0 = 0;
    FM1_WF_RST  = 0;
    for (volatile int d = 0; d < 100; d++) ;
    FM1_WF_RST  = 1;
}

/* ======== main RF init entry point (wf_rf_init.c.ll) ======== */

static int wifi_rf_init(void)
{
    wf_clk_rst_init();
    wifi_get_xosc();

    printf("\n xosc: %d %d %s",
           (int)wifi_xosc_val[0], (int)wifi_xosc_val[1],
           wifi_xosc_is_40m ? "40M" : "24M");

    /* WL_ANL_CON2 (0x11930): PLL analog config */
    uint32_t v = FM1_WF_CON12;
    v |= 1u;                    /* enable */
    FM1_WF_CON12 = v;
    v = FM1_WF_CON12; v |= 2u;  FM1_WF_CON12 = v;
    v = FM1_WF_CON12; v |= 4u;  FM1_WF_CON12 = v;
    v = FM1_WF_CON12; v |= 8u;  FM1_WF_CON12 = v;
    v = FM1_WF_CON12; v |= 16u; FM1_WF_CON12 = v;
    v = FM1_WF_CON12;
    v = (v & ~(3u << 5)) | (2u << 5);
    FM1_WF_CON12 = v;
    v = FM1_WF_CON12; v &= ~(1u << 7); FM1_WF_CON12 = v;
    v = FM1_WF_CON12;
    v = (v & ~(3u << 8)) | (2u << 8);
    FM1_WF_CON12 = v;
    v = FM1_WF_CON12; v &= ~(1u << 10); FM1_WF_CON12 = v;
    v = FM1_WF_CON12; v &= ~(1u << 11); FM1_WF_CON12 = v;
    v = FM1_WF_CON12; v &= ~(1u << 12); FM1_WF_CON12 = v;
    v = FM1_WF_CON12; v &= ~(1u << 17); FM1_WF_CON12 = v;
    v = FM1_WF_CON12; v &= ~(1u << 18); FM1_WF_CON12 = v;

    /* xosc trim -> bits [16:13] and [22:19] */
    v = FM1_WF_CON12;
    v = (v & ~(0xFu << 13)) | (((uint32_t)(wifi_xosc_val[0] & 0xF)) << 13);
    FM1_WF_CON12 = v;
    v = FM1_WF_CON12;
    v = (v & ~(0xFu << 19)) | (((uint32_t)(wifi_xosc_val[1] & 0xF)) << 19);
    FM1_WF_CON12 = v;

    /* clock source select: 40M or 24M */
    v = FM1_WF_CON12;
    v &= ~(0xFu << 24);
    v |= wifi_xosc_is_40m ? (0x0Du << 24) : (0x05u << 24);
    FM1_WF_CON12 = v;

    /* clear CON20 */
    FM1_WF_CON20 = 0;

    /* WL_CON0 (0x11900): PLL + analog base config */
    v = FM1_WF_CON0; v |= 1u;  FM1_WF_CON0 = v;
    v = FM1_WF_CON0;
    v = (v & ~(0x1Fu)) | 4u;
    FM1_WF_CON0 = v;
    v = FM1_WF_CON0; v &= ~(1u << 5);  FM1_WF_CON0 = v;
    v = FM1_WF_CON0; v &= ~(3u << 6);  FM1_WF_CON0 = v;
    v = FM1_WF_CON0; v &= ~(1u << 8);  FM1_WF_CON0 = v;
    v = FM1_WF_CON0; v &= ~(1u << 9);  FM1_WF_CON0 = v;
    v = FM1_WF_CON0; v &= ~(1u << 10); FM1_WF_CON0 = v;
    v = FM1_WF_CON0; v &= ~(3u << 11); FM1_WF_CON0 = v;
    v = FM1_WF_CON0; v &= ~(1u << 18); FM1_WF_CON0 = v;

    for (int i = 0; i < WF_WLA_CON_SIZE; i++)
        wf_wla_con[i] = FM1_RF_REG(0x11900u + i * 4);

    /* RF enable: PLL warmup mode */
    wf_rf_en_set(1);

    /* VCO scan */
    int band = wf_vco_scan();
    if (band < 0) return -1;

    /* PLL bank scan for all channels */
    wfa_pll_bank_scan(73);

    /* PHY/MAC init */
    wf_phy_mac_init();

    /* Calibration */
    wf_tx_iq_cal();
    wf_rx_iq_cal();
    wf_dc_offset_cal();
    wf_bw_cal();

    /* CFR and PA */
    wf_cfr_init();
    wf_pa_dpd_init();

    /* Switch to receive mode and set default channel */
    wf_rf_en_set(2);
    wifi_set_channel(1);

    return 0;
}
