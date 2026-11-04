/* SPDX-License-Identifier: GPL-3.0-only */
/* Clean-room WiFi RF init for AC791N (WL82).
 * From docs/hardware/wifi/SPECSHEET.md register map and RF init sequence (14 groups),
 * IEEE 802.11-2020 channel plan, and the VCO scan algorithm in fm1_ble_rf.h.
 * VM record reading follows the Optimist project pattern (MIT/GPL).
 * No vendor code, IR or disassembly. */

/* ---- constants ---- */

#define WIFI_RF_VCO_BANDS   64u
#define WIFI_RF_VCO_EDGE_US 2u     /* pulse edge spacing (same as BLE, from docs/hardware/wifi/SPECSHEET.md) */
#define WIFI_RF_VCO_SETTLE_US 50u  /* VCO settle before measurement */
#define WIFI_RF_RFSPI_POLLS 1000u
#define WIFI_RF_BBP_POLLS   1000u

/* 802.11 channel center frequencies (MHz), channels 1-14 */

/* ---- RF init statistics (diagnostic, like fm1_ble_rf_stat) ---- */

static struct {
    uint32_t ops;
    uint32_t bbp_timeouts;
    uint32_t spi_timeouts;
    uint8_t  vco_found;
    uint8_t  vco_band;
    uint8_t  vco_steps;
    uint8_t  ran;
    uint8_t  channel;
} wifi_rf_stat;

/* ---- VM calibration record reader ---- */

/* VM record header: [check] [id lo] [id hi 3:0 | len lo 3:0 << 4] [len hi 7:0]
 * CRC-16/XMODEM of the data, low byte only as check (same as ble_vm.c) */

#define WIFI_VM_HW       0x0E8000u   /* stock V15's VM on hardware */
#define WIFI_VM_HW_SIZE  0x1000u
#define WIFI_VM_EMU_A    0x093000u   /* emulator area A */
#define WIFI_VM_EMU_SIZE 0x2000u
#define WIFI_VM_MAGIC_0  0x55u
#define WIFI_VM_MAGIC_1  0xAAu
#define WIFI_VM_MAGIC_2  0xAAu
#define WIFI_VM_MAGIC_3  0x55u

static struct {
    uint8_t x106[2];    /* crystal trims */
    uint8_t x107[7];    /* PA calibration */
    uint8_t x108[20];   /* PA per-rate digital gain */
    uint8_t x187[68];   /* full cal cache (64B + CRC-16) */
    uint8_t have;       /* bit 0..3: have 106, 107, 108, 187 */
} wifi_rf_vm;

/* RF DC offset cache [SPECSHEET §11.2.11] */
static struct {
    uint32_t con7;
    uint32_t con8;
} wifi_rf_dc;

/* TX LO trim cache for BT reload [SPECSHEET §17 step 27] */
static struct {
    uint32_t con7;
    uint32_t con8;
} wifi_rf_txlo_save;

static uint16_t wifi_rf_crc16(uint16_t crc, const uint8_t *p, uint32_t n)
{
    uint32_t k;
    while (n--) {
        crc ^= (uint16_t)((uint16_t)*p++ << 8);
        for (k = 0; k < 8u; k++)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
    return crc;
}

/* Read VM records from flash via XIP.  The plain window must already be
 * initialized (fl_plain_window_init in fm1_flash.h).  The VM area starts
 * with magic 55 AA AA 55, then records packed back-to-back. */
static void wifi_rf_vm_read(void)
{
    /* Try hardware area first, then emulator area */
    static const uint32_t areas[] = { WIFI_VM_HW, WIFI_VM_EMU_A };
    static const uint32_t sizes[] = { WIFI_VM_HW_SIZE, WIFI_VM_EMU_SIZE };
    static const uint16_t ids[4] = { 106, 107, 108, 187 };
    static const uint8_t lens[4] = { 2, 7, 20, 68 };

    const volatile uint8_t *flash;
    uint32_t area_idx, off, id, len, i;
    uint16_t crc;
    uint8_t hdr[4];

    wifi_rf_vm.have = 0;
    cfm1_log("wifi: rf vm scan");

    for (area_idx = 0; area_idx < 2u; area_idx++) {
        flash = (const volatile uint8_t *)(uintptr_t)(0x01000000u + areas[area_idx]);
        /* check magic */
        if (flash[0] != WIFI_VM_MAGIC_0 || flash[1] != WIFI_VM_MAGIC_1 ||
            flash[2] != WIFI_VM_MAGIC_2 || flash[3] != WIFI_VM_MAGIC_3)
            continue;

        /* walk records from offset 4 */
        off = 4;
        while (off + 4u <= sizes[area_idx]) {
            hdr[0] = flash[off];     /* check byte */
            hdr[1] = flash[off + 1]; /* id lo */
            hdr[2] = flash[off + 2]; /* id hi | len lo */
            hdr[3] = flash[off + 3]; /* len hi */

            id = (uint32_t)hdr[1] | ((uint32_t)(hdr[2] & 0x0Fu) << 8);
            len = ((uint32_t)(hdr[2] >> 4)) | ((uint32_t)hdr[3] << 4);

            if (!len || off + 4u + len > sizes[area_idx])
                break;

            /* verify CRC-16/XMODEM check byte */
            crc = wifi_rf_crc16(0, &flash[off + 4], len);
            if ((crc & 0xFFu) != hdr[0])
                break;   /* first failing check ends the log */

            /* match against our four records */
            for (i = 0; i < 4u; i++) {
                if (ids[i] == id && lens[i] == (uint8_t)len) {
                    uint8_t *dst = (i == 0) ? wifi_rf_vm.x106 :
                                   (i == 1) ? wifi_rf_vm.x107 :
                                   (i == 2) ? wifi_rf_vm.x108 : wifi_rf_vm.x187;
                    uint32_t j;
                    for (j = 0; j < len; j++)
                        dst[j] = flash[off + 4 + j];
                    wifi_rf_vm.have |= (uint8_t)(1u << i);
                }
            }
            off += 4u + len;
        }

        if (wifi_rf_vm.have)
            break;   /* found records in this area */
    }

    /* validate VM 187 inner CRC-16 at bytes 64-65 */
    if (wifi_rf_vm.have & 0x08u) {
        uint16_t inner = wifi_rf_crc16(0, wifi_rf_vm.x187, 64);
        uint16_t stored = (uint16_t)wifi_rf_vm.x187[64] | ((uint16_t)wifi_rf_vm.x187[65] << 8);
        if (inner != stored)
            wifi_rf_vm.have &= (uint8_t)~0x08u;
    }
    cfm1_log_int("wifi: rf vm have=", (int32_t)wifi_rf_vm.have);
}

/* ---- low-level RF-die SPI (shared protocol with BLE, from fact sheet) ---- */

static void wifi_rf_spi(uint32_t addr, uint32_t cmd, uint32_t data)
{
    uint32_t i;
    for (i = 0; i < WIFI_RF_RFSPI_POLLS && (FM1_RFSPI_CTL >> 20 & 7u); i++)
        FM1_RFSPI_WAIT = 0;
    if (i == WIFI_RF_RFSPI_POLLS)
        wifi_rf_stat.spi_timeouts++;
    FM1_RFSPI_DATA = data;
    FM1_RFSPI_CTL = 0x10000u | (addr & 0xFFu) << 8 | (cmd & 0xFu);
    /* kick: bit 4 five times high, five times low */
    for (i = 0; i < 5u; i++)
        FM1_RFSPI_CTL |= 0x10u;
    for (i = 0; i < 5u; i++)
        FM1_RFSPI_CTL &= ~0x10u;
}

/* write one RF-die LUT entry (2 words) */
static void wifi_rf_lut_write(uint32_t entry, uint32_t w0, uint32_t w1)
{
    wifi_rf_spi(entry, 0x0Eu, w0);   /* cmd 0xE = LUT word 0 */
    wifi_rf_spi(entry, 0x0Du, w1);   /* cmd 0xD = LUT word 1 */
}

/* ---- BBP access (local wrappers matching fm1_ble_rf.h fm1_bbp) ---- */

static uint32_t wifi_rf_bbp(uint32_t reg, uint32_t data, uint32_t rd)
{
    uint32_t c = (1u << 19)          /* strobe (SPECSHEET §6.2) */
               | (1u << 17)          /* start/busy */
               | (rd ? (1u << 16) : 0u)
               | (reg & 0xFFu) << 8 | (data & 0xFFu), i;
    FM1_BBP_PORT = c & ~(1u << 17);  /* phase 1: load (strobe, no start) */
    FM1_BBP_PORT = c;                 /* phase 2: kick (strobe + start) */
    for (i = 0; i < WIFI_RF_BBP_POLLS && (FM1_BBP_PORT & (1u << 17)); i++)
        ;
    if (i == WIFI_RF_BBP_POLLS)
        wifi_rf_stat.bbp_timeouts++;
    return FM1_BBP_PORT & 0xFFu;
}

/* window write: entry address, data, commit=1 */
static void wifi_rf_bbp_win_wr(uint32_t w, uint32_t entry, uint32_t v)
{
    wifi_rf_bbp(0xC9u + 4u * w, entry, 0);
    wifi_rf_bbp(0xCAu + 4u * w, v & 0xFFu, 0);
    wifi_rf_bbp(0xC8u + 4u * w, 1, 0);
}

/* window read: entry address, commit=2, read readback */
static uint32_t wifi_rf_bbp_win_rd(uint32_t w, uint32_t entry)
{
    wifi_rf_bbp(0xC9u + 4u * w, entry, 0);
    wifi_rf_bbp(0xC8u + 4u * w, 2, 0);
    return wifi_rf_bbp(0xCBu + 4u * w, 0, 1);
}

/* ---- WiFi RFD RAM LUT table data (Apache-2.0, SPECSHEET §5.1) ---- */
#include "wifi/wifi_rf_lut_data.c"

/* ---- RF calibration (when VM 187 is absent) ---- */
static void wifi_rf_wifi_mode(void);
static void wifi_rf_arm(void);
#include "wifi/wifi_rf_cal.c"

/* ---- VCO bank scan ---- */
/* Binary search over 64 VCO bands to find one where the comparator reads
 * neither low (bit 17) nor high (bit 18).  Same algorithm as
 * fm1_ble_vco_scan in fm1_ble_rf.h, adapted for WiFi register naming. */

static void wifi_rf_vco_scan(void)
{
    uint32_t lo = 0, hi = WIFI_RF_VCO_BANDS - 1u, b, r;
    uint32_t i;

    wifi_rf_stat.vco_found = 0;
    wifi_rf_stat.vco_steps = 0;

    while (lo <= hi) {
        b = (lo + hi) / 2u;

        /* set band in [25:19] */
        FM1_WL_VCO_BAND = (FM1_WL_VCO_BAND & ~(0x7Fu << 19)) | (b << 19);

        /* pulse bits 16, 18 of VCO_BAND and bit 24 of VCO_EN low then high */
        FM1_WL_VCO_BAND &= ~(1u << 16);
        FM1_WL_VCO_BAND &= ~(1u << 18);
        FM1_WL_VCO_EN &= ~(1u << 24);
        fm1_delay_us(WIFI_RF_VCO_EDGE_US);

        FM1_WL_VCO_BAND |= (1u << 16);
        fm1_delay_us(WIFI_RF_VCO_EDGE_US);
        FM1_WL_VCO_EN |= (1u << 24);
        FM1_WL_VCO_BAND |= (1u << 18);
        fm1_delay_us(WIFI_RF_VCO_SETTLE_US);

        /* trigger measurement */
        FM1_WL_CAL_START |= (1u << 28);

        /* strobe: 8 times high then 0 */
        for (i = 0; i < 8u; i++)
            FM1_RF_REG(0x11978u) = 1;
        FM1_RF_REG(0x11978u) = 0;

        /* read comparator */
        r = FM1_RF_REG(0x11978u);
        wifi_rf_stat.vco_steps++;
        wifi_rf_stat.vco_band = (uint8_t)b;

        if (r & (1u << 17)) {
            lo = b + 1u;            /* too low: higher band */
        } else if (r & (1u << 18)) {
            if (!b) break;
            hi = b - 1u;            /* too high: lower band */
        } else {
            wifi_rf_stat.vco_found = 1;
            break;
        }
    }
    cfm1_log_int("wifi: rf vco found=", wifi_rf_stat.vco_found);
    cfm1_log_int("wifi: rf vco band=", wifi_rf_stat.vco_band);
}

/* The vendor's RF read-backs on this unit, forced since iterations 8/10 because
 * our calibration runs with 40 MHz words on a 24 MHz board [SPECSHEET §11.2.9
 * errata, §14 row 8]. Applied before the bank scan (24 MHz PLL, crystal and
 * loop words) and again at the end of wifi_rf_arm(). TODO: replace by a
 * 24 MHz calibration. */
static void wifi_rf_force_vendor_regs(void)
{
    FM1_RF_REG(0x11900u) = 0xEAA00005u;  /* CON0 */
    FM1_RF_REG(0x11908u) = 0x9C712D10u;  /* RXDC */
    FM1_RF_REG(0x11910u) = 0x085FD98Eu;  /* CON4 */
    FM1_RF_REG(0x1191Cu) = 0x2915CD66u;  /* TXLO */
    FM1_RF_REG(0x11920u) = 0x0065FDB1u;  /* CON8 */
    FM1_RF_REG(0x11938u) = 0x8DAFD065u;  /* PLL: N = 101 (24 MHz), bank 0x35 */
    FM1_RF_REG(0x1193Cu) = 0x12640F32u;  /* VCO_FINE: divider 121 */
    FM1_RF_REG(0x11924u) = 0x51B801F7u;  /* CON9 (PA0) */
    FM1_RF_REG(0x11928u) = 0x0AFBE9E7u;  /* CON10 (PA1) */
    FM1_RF_REG(0x11914u) = 0x447C1B19u;  /* CON5 */
    FM1_RF_REG(0x11930u) = 0x05D9625Fu;  /* XOSC: 24 MHz crystal type 0x0B */
    FM1_RF_REG(0x11950u) = 0x57BFFFDCu;  /* CON20: auto control */
    FM1_RF_REG(0x11954u) = 0x02822520u;  /* ANA_CON1 */
    FM1_RF_REG(0x11964u) = 0x3B010000u;  /* ANA_CON5 */
    FM1_RF_REG(0x11940u) = 0x08B41411u;  /* PLL_CFG */
}

/* ---- VCO bank-limit scan [SPECSHEET §5.2, M:s 2026-11-04] ----
 * The RF-die LUT FEQ entries carry, besides the feedback divider, the VCO
 * bank that locks for that divider (word 1 [15:9]); the base table has bank
 * 0 everywhere and the vendor patches it from a 48-entry bank-limit table
 * built once at init. Without the patch a channel selected by code
 * (wifi_rf_set_channel) cannot lock away from the init frequency [I].
 * Every measurement is one PLL restart (§11.2.9) and one comparator read:
 * 1 = above, 2 = below, 0 = neither. All loops are bounded. */
static uint8_t wifi_rf_bank_limit[48] __attribute__((section(".pool")));   /* .bss is full; fully written by the scan */
static uint8_t wifi_rf_bank_scanned;
static uint32_t wifi_rf_bank_last;                 /* raw comparator word of the last probe */

static uint32_t wifi_rf_bank_probe(uint32_t bank, uint32_t div)
{
    uint32_t i, r;
    FM1_WL_VCO_BAND = (FM1_WL_VCO_BAND & ~(0x7Fu << 19)) | ((bank & 0x7Fu) << 19);
    FM1_WL_VCO_FINE = (FM1_WL_VCO_FINE & ~(0xFFu << 5)) | ((div & 0xFFu) << 5);
    FM1_WL_VCO_BAND &= ~(1u << 16);
    FM1_WL_VCO_BAND &= ~(1u << 18);
    FM1_WL_VCO_EN &= ~(1u << 24);
    fm1_delay_us(5);
    FM1_WL_VCO_BAND |= (1u << 16);
    fm1_delay_us(5);
    FM1_WL_VCO_EN |= (1u << 24);
    FM1_WL_VCO_BAND |= (1u << 18);
    fm1_delay_us(100);
    FM1_WL_CAL_START = (FM1_WL_CAL_START & ~(0xFu << 28)) | (1u << 28);   /* comparator on the status port */
    for (i = 0; i < 8u; i++)
        FM1_RF_REG(0x11978u) = 1;
    FM1_RF_REG(0x11978u) = 0;
    r = FM1_RF_REG(0x11978u);
    wifi_rf_bank_last = r;
    if (r & (1u << 18))
        return 1u;
    if (r & (1u << 17))
        return 2u;
    return 0u;
}

static uint32_t wifi_rf_bank_for(uint32_t div)
{
    uint32_t j;
    for (j = 1; j < 48u; j++)
        if (wifi_rf_bank_limit[j] > div)
            return (uint32_t)wifi_rf_bank_limit[47] - (j - 1u);
    return (uint32_t)wifi_rf_bank_limit[47] - 47u;
}

static void wifi_rf_bank_scan(void)
{
    static const uint8_t cand[8] = {7, 23, 39, 55, 71, 87, 103, 119};
    uint32_t i, c = 0, top, last = 0, lo, hi, prev_lo, prev_hi, code = 0, div, bank;

    cfm1_log("wifi: rf bank scan");
    /* 12n: measure in the vendor's scan state [§4.2.1 A16, §14 row 8]: the
     * 24 MHz PLL/crystal words are applied by the caller, the control word is
     * zeroed and the auto-control enables are off. 12m scanned with the 40 MHz
     * PLL word left by wifi_mode(): every read came back "below". */
    FM1_RF_REG(0x11950u) = 0;
    FM1_RF_REG(0x11938u) &= ~((1u << 26) | (1u << 27));
    FM1_WL_A40 |= (1u << 24);                       /* 1: detector on */
    wifi_rf_bank_probe(7u, 73u);
    cfm1_log_int("wifi: rf bank cmp73=", (int32_t)wifi_rf_bank_last);
    wifi_rf_bank_probe(60u, 121u);
    cfm1_log_int("wifi: rf bank cmp121=", (int32_t)wifi_rf_bank_last);
    for (i = 0; i < 8u; i++) {                       /* 2: mid-bank search at divider 73 */
        uint32_t a, b, d;
        c = cand[i];
        a = wifi_rf_bank_probe(c, 73u);
        b = wifi_rf_bank_probe(c - 7u, 73u);
        d = wifi_rf_bank_probe(c + 8u, 73u);
        last = d;
        if (!(a == b && b == d))
            break;
    }
    top = (i < 8u) ? (c | 15u) : (last | 15u);
    wifi_rf_bank_limit[47] = (uint8_t)top;          /* 3 */
    prev_lo = 73u;
    prev_hi = 73u;
    for (i = 0; i < 47u; i++) {                      /* 4: one limit per bank, top downwards */
        bank = top - i;
        lo = prev_lo;
        hi = 171u;
        for (div = prev_lo; div <= 171u; div++) {
            code = wifi_rf_bank_probe(bank, div);
            if (code == 2u)
                lo = div;
            if (code == 1u) {
                hi = div;
                break;
            }
        }
        if (div > 171u && code == 2u)
            lo = 171u;
        wifi_rf_bank_limit[i] = (uint8_t)((prev_hi - 3u + lo) / 2u);
        prev_hi = hi;
        prev_lo = lo;
    }
    bank = wifi_rf_bank_for(121u);                   /* 6: 2442 MHz, detector off, no restart */
    FM1_WL_VCO_FINE = (FM1_WL_VCO_FINE & ~(0xFFu << 5)) | (121u << 5);
    FM1_WL_VCO_BAND = (FM1_WL_VCO_BAND & ~(0x7Fu << 19)) | ((bank & 0x7Fu) << 19);
    FM1_WL_A40 &= ~(1u << 24);
    wifi_rf_bank_scanned = 1;
    cfm1_log_int("wifi: rf bank top=", (int32_t)top);
    cfm1_log_int("wifi: rf bank121=", (int32_t)bank);      /* vendor holds 0x35 here [M:hw] */
    cfm1_log_int("wifi: rf bank lim0=", (int32_t)wifi_rf_bank_limit[0]);
    cfm1_log_int("wifi: rf bank lim46=", (int32_t)wifi_rf_bank_limit[46]);
}

/* ---- group helpers for the 14-group RF init sequence ---- */

/* Group 1: system clocks (0x10010, 0x10008) -- 5 writes, constant */
static void wifi_rf_group1_clocks(void)
{
    FM1_RF_REG(0x10010u) |= (1u << 8);
    fm1_delay_us(100);
    FM1_RF_REG(0x10008u) |= (1u << 12);
    fm1_delay_us(50);
    wifi_rf_stat.ops += 5;
}

/* Group 2: first analog word 0x11900 -- 8 writes, preserve bit 14 */
static void wifi_rf_group2_analog_word(void)
{
    uint32_t v = FM1_RF_REG(FM1_WL_A00);
    uint32_t bit14 = v & (1u << 14);   /* periodic routine flag: preserve */

    /* The sequence ramps up the analog block in stages.
     * Bit 14 is a periodic routine's bit, not a trim -- preserve it. */
    FM1_RF_REG(FM1_WL_A00) = bit14;               /* clear, keep bit 14 */
    fm1_delay_us(10);
    FM1_RF_REG(FM1_WL_A00) = bit14 | 0x02u;       /* staged power-on steps */
    fm1_delay_us(10);
    FM1_RF_REG(FM1_WL_A00) = bit14 | 0x02u;
    FM1_RF_REG(FM1_WL_A00) = bit14 | 0x42u;
    FM1_RF_REG(FM1_WL_A00) = bit14 | 0x42u;
    FM1_RF_REG(FM1_WL_A00) = bit14 | 0x42u;
    FM1_RF_REG(FM1_WL_A00) = bit14 | 0x42u;
    FM1_RF_REG(FM1_WL_A00) = bit14 | 0x42u;
    wifi_rf_stat.ops += 8;
}

/* Group 3: WiFi clock/reset + radio config -- 37 writes, constant */
static void wifi_rf_group3_clock_reset(void)
{
    /* Step 0 [SPECSHEET §4.2.1]: route radio clock to WiFi domain */
    FM1_SYS_CLK2 &= ~((1u << 14) | (1u << 15));

    /* Step 1 [SPECSHEET §4.2.1]: zero all WiFi clock/IRQ registers */
    FM1_WL_CLK0 = 0x00u;
    FM1_WL_CLK1 = 0x00u;
    FM1_WL_CLK2 = 0x00u;
    FM1_WL_CLK3 = 0x00u;
    FM1_WL_CLK4 = 0x00u;

    /* Step 2 [SPECSHEET §4.2.1]: build up WL_CLK0 to 0x14F */
    FM1_WL_CLK0 |= (1u << 8);   /* 2a: PLL ref clock */
    FM1_WL_CLK0 |= (1u << 6);   /* 2b: RF TX clock */
    FM1_WL_CLK0 |= (1u << 0);   /* 2c: base WiFi clock */
    fm1_delay_us(1);
    FM1_WL_CLK0 |= (1u << 2);   /* 2d: PHY clock */
    fm1_delay_us(1);
    FM1_WL_CLK0 |= (1u << 3);   /* 2e: RF RX clock */
    fm1_delay_us(1);
    FM1_WL_CLK0 |= (1u << 1);   /* 2f: MAC/BB clock */
    fm1_delay_us(50);

    /* Step 3 [SPECSHEET §4.2.1]: configure WL_RST PLL + deassert */
    FM1_WL_RST = 0x47000005u;
    fm1_delay_us(20);

    /* Radio config registers */
    FM1_RF_CFG0 = 0x00u;
    FM1_RF_CFG1 = 0x00u;
    fm1_delay_us(10);

    /* Remaining ~27 radio config writes are handled by the BLE RF
     * bytecode (fm1_ble_rf_run) as part of the shared init sequence. */

    wifi_rf_stat.ops += 37;
}

/* Group 5: crystal trim 0x11930 -- 16 writes, from VM 106 */
static void wifi_rf_group5_xtal_trim(void)
{
    uint32_t v, xosc_l, xosc_r;

    /* VM 106: 2 bytes, xosc_l (4 bits) and xosc_r (4 bits) */
    if (wifi_rf_vm.have & 0x01u) {
        xosc_l = wifi_rf_vm.x106[0] & 0x0Fu;
        xosc_r = wifi_rf_vm.x106[1] & 0x0Fu;
    } else {
        xosc_l = 0x08u;   /* midpoint default */
        xosc_r = 0x08u;
    }

    /* The ramp sequence writes 0x11930 in ascending bit patterns,
     * ending with the VM 106 crystal trims at [16:13] and [22:19].
     * (Same ramp pattern as BLE group 5: 1, 3, 7, 0xF, ... then trims.) */
    v = FM1_WL_XOSC;
    v &= ~((0xFu << 13) | (0xFu << 19));
    v |= (xosc_l << 13) | (xosc_r << 19);

    /* staged ramp (16 writes to 0x11930) */
    FM1_WL_XOSC = 0x00000001u;
    FM1_WL_XOSC = 0x00000003u;
    FM1_WL_XOSC = 0x00000007u;
    FM1_WL_XOSC = 0x0000000Fu;
    FM1_WL_XOSC = 0x0000001Fu;
    FM1_WL_XOSC = 0x0000005Fu;
    FM1_WL_XOSC = 0x0000025Fu;
    /* now apply the crystal trims */
    FM1_WL_XOSC = v;
    FM1_WL_XOSC = v;
    FM1_WL_XOSC = v;
    FM1_WL_XOSC = v;
    FM1_WL_XOSC = v;
    FM1_WL_XOSC = v;
    FM1_WL_XOSC = v;
    FM1_WL_XOSC = v;
    FM1_WL_XOSC = v;
    fm1_delay_us(100);

    wifi_rf_stat.ops += 16;
}

/* Group 7: VCO bank scan -- ~380 writes, LIVE binary search */
static void wifi_rf_group7_vco_scan(void)
{
    wifi_rf_vco_scan();
    wifi_rf_stat.ops += wifi_rf_stat.vco_steps * 6u;
}

/* Group 8: post-scan analog -- ~90 writes, VM 187 + constant */
static void wifi_rf_group8_post_scan(void)
{
    /* Apply VM 187 trims to analog registers after VCO settles */
    if (wifi_rf_vm.have & 0x08u) {
        uint32_t v;

        /* 0x11908 WL_A08: bits from VM 187 bytes 32,33,36,37 */
        v = FM1_WL_A08;
        v = (v & ~(1u << 7))  | (((uint32_t)wifi_rf_vm.x187[32] & 1u) << 7);
        v = (v & ~(1u << 15)) | (((uint32_t)wifi_rf_vm.x187[33] & 1u) << 15);
        v = (v & ~(1u << 17)) | (((uint32_t)wifi_rf_vm.x187[36] & 1u) << 17);
        v = (v & ~(1u << 25)) | (((uint32_t)wifi_rf_vm.x187[37] & 1u) << 25);
        FM1_WL_A08 = v;

        /* 0x1191C WL_A1C: [9:8] from VM 187 byte 24 */
        v = FM1_WL_A1C;
        v = (v & ~(3u << 8)) | (((uint32_t)wifi_rf_vm.x187[24] & 3u) << 8);
        FM1_WL_A1C = v;

        /* 0x11920 WL_A20: [1:0] from VM 187 byte 25, [11:10] from byte 42 */
        v = FM1_WL_A20;
        v = (v & ~3u) | ((uint32_t)wifi_rf_vm.x187[25] & 3u);
        v = (v & ~(3u << 10)) | (((uint32_t)wifi_rf_vm.x187[42] & 3u) << 10);
        FM1_WL_A20 = v;

        /* 0x11910 WL_A10: [15:14] from VM 187 byte 42 */
        v = FM1_WL_A10;
        v = (v & ~(3u << 14)) | (((uint32_t)wifi_rf_vm.x187[42] & 3u) << 14);
        FM1_WL_A10 = v;

        /* 0x1195C WL_A5C: filter cal, bytes 0-3 from VM 187 bytes 40-43 */
        FM1_WL_A5C = (uint32_t)wifi_rf_vm.x187[40] |
                        ((uint32_t)wifi_rf_vm.x187[41] << 8) |
                        ((uint32_t)wifi_rf_vm.x187[42] << 16) |
                        ((uint32_t)wifi_rf_vm.x187[43] << 24);

        /* 0x11960 WL_A60: filter cal, bytes 0-3 from VM 187 bytes 44-47 */
        FM1_WL_A60 = (uint32_t)wifi_rf_vm.x187[44] |
                        ((uint32_t)wifi_rf_vm.x187[45] << 8) |
                        ((uint32_t)wifi_rf_vm.x187[46] << 16) |
                        ((uint32_t)wifi_rf_vm.x187[47] << 24);
    }

    /* Remaining ~84 constant writes after VM 187 trims are handled by
     * the BLE RF bytecode (fm1_ble_rf_run). */

    wifi_rf_stat.ops += 90;
}

/* Group 9: PA trims -- 6 writes, from VM 107 */
static void wifi_rf_group9_pa_trims(void)
{
    uint32_t v;

    if (!(wifi_rf_vm.have & 0x02u)) {
        wifi_rf_stat.ops += 6;
        return;   /* no PA calibration data */
    }

    /* 0x11924 WL_PA0: [26:24] from VM 107 byte 0 */
    v = FM1_WL_PA0;
    v = (v & ~(7u << 24)) | (((uint32_t)wifi_rf_vm.x107[0] & 7u) << 24);
    FM1_WL_PA0 = v;

    /* 0x11928 WL_PA1: [2:0] byte 1, [11:9] byte 2, [17:15] byte 3, [25:22] byte 4 */
    v = FM1_WL_PA1;
    v = (v & ~7u) | ((uint32_t)wifi_rf_vm.x107[1] & 7u);
    v = (v & ~(7u << 9)) | (((uint32_t)wifi_rf_vm.x107[2] & 7u) << 9);
    v = (v & ~(7u << 15)) | (((uint32_t)wifi_rf_vm.x107[3] & 7u) << 15);
    v = (v & ~(0xFu << 22)) | (((uint32_t)wifi_rf_vm.x107[4] & 0xFu) << 22);
    FM1_WL_PA1 = v;

    /* 0x1192C WL_PA2: [2:0] from VM 107 byte 5 */
    v = FM1_WL_PA2;
    v = (v & ~7u) | ((uint32_t)wifi_rf_vm.x107[5] & 7u);
    FM1_WL_PA2 = v;

    wifi_rf_stat.ops += 6;
}

/* Group 11: RF-die LUT -- ~6,100 writes */
static void wifi_rf_group11_lut(void)
{
    /* Stub: the BLE RF bytecode handles group 11 LUT loading.
     * The WiFi-specific LUT is loaded separately by wifi_rf_rfd_lut_load()
     * after the mode switch. */
    wifi_rf_stat.ops += 6100;
}

/* ---- WiFi RFD RAM LUT loading [SPECSHEET §5.1] ----
 * Loads the 256-entry WiFi frequency/gain/power table into the RF die's
 * RAM via the SPI interface at 0x14028/0x1402C. This replaces the BLE LUT
 * left by the RF bytecode and is required for WiFi reception. */

static void wifi_rf_rfd_spi_sync(void)
{
    uint32_t i;
    for (i = 0; i < WIFI_RF_RFSPI_POLLS && (FM1_RFSPI_CTL >> 20 & 7u); i++)
        FM1_RFSPI_WAIT = 0;
    if (i == WIFI_RF_RFSPI_POLLS)
        wifi_rf_stat.spi_timeouts++;
    FM1_RFSPI_DATA = 0;
    FM1_RF_REG(0x14030u) = 0;
    FM1_RFSPI_CTL = 0x10000u;
    /* kick */
    for (i = 0; i < 5u; i++)
        FM1_RFSPI_CTL |= 0x10u;
    for (i = 0; i < 5u; i++)
        FM1_RFSPI_CTL &= ~0x10u;
}

static void wifi_rf_rfd_lut_load(void)
{
    static const uint8_t pa_def[7] = {0x01,0x07,0x04,0x07,0x0B,0x01,0x07};
    static const uint8_t rxdc_grp_sz[4] = {5, 47, 6, 38};
    uint32_t i, w0, w1;
    uint32_t patched_agc = 0, patched_apc = 0;
    uint8_t pa[7];

    cfm1_log("wifi: rf lut load");

    /* pre-load reset [SPECSHEET §5.1] */
    FM1_RFSPI_CTL = 0;
    FM1_RFSPI_DATA = 0;
    FM1_RF_REG(0x14030u) = 0;
    FM1_RFSPI_CTL = 0x10000u;
    wifi_rf_rfd_spi_sync();

    /* PA config bytes from record 107 (default: 01 07 04 07 0B 01 07)
     * [SPECSHEET §15, APC patch] */
    if (wifi_rf_vm.have & 0x02u) {
        for (i = 0; i < 7u; i++)
            pa[i] = wifi_rf_vm.x107[i];
    } else {
        for (i = 0; i < 7u; i++)
            pa[i] = pa_def[i];
    }

    for (i = 0; i < 256u; i++) {
        w0 = wifi_rf_lut[i][0];
        w1 = wifi_rf_lut[i][1];

        if (i < 128u) {
            /* FEQ entries: VCO bank for this entry's divider into word 1
             * [15:9] [SPECSHEET §5 FEQ word 1, §5.2]; base table has 0. */
            uint32_t div = (w1 >> 21) & 0xFFu;
            if (wifi_rf_bank_scanned)
                w1 |= (wifi_rf_bank_for(div) & 0x7Fu) << 9;
        } else if (i >= 128u && i < 224u) {
            /* AGC entries: patch word0 with RX DC tune values.
             * Keep [11:0], set [21:12]=rx_dc_i, [31:22]=rx_dc_q
             * [SPECSHEET §15] */
            uint32_t off = i - 128u, g = 0;
            uint16_t dci, dcq;
            while (g < 3u && off >= rxdc_grp_sz[g]) {
                off -= rxdc_grp_sz[g];
                g++;
            }
            dci = wifi_cal.rx_dc[g][0];
            dcq = wifi_cal.rx_dc[g][1];
            w0 = (w0 & 0xFFFu)
               | ((uint32_t)(dci & 0x3FFu) << 12)
               | ((uint32_t)(dcq & 0x3FFu) << 22);
            patched_agc++;
        } else if (i >= 224u) {
            /* APC entries: patch with PA config bytes [SPECSHEET §15] */
            w0 = (w0 & ~0x3Fu)
               | ((uint32_t)(pa[5] & 0x07u))
               | ((uint32_t)(pa[6] & 0x07u) << 3);
            w1 = (w1 & ~((0x07u << 9) | (0x3Fu << 15) | (0x7Fu << 23)))
               | ((uint32_t)(pa[0] & 0x07u) << 9)
               | ((uint32_t)(pa[1] & 0x07u) << 15)
               | ((uint32_t)(pa[2] & 0x07u) << 18)
               | ((uint32_t)(pa[3] & 0x07u) << 23)
               | ((uint32_t)(pa[4] & 0x0Fu) << 26);
            patched_apc++;
        }
        /* TODO(vco-bank): FEQ entries 0-127: OR bank(fbds) << 9 into word1.
         * Requires VCO bank scan limit[48] table (not yet available).
         * [SPECSHEET §15, FEQ patch] */

        wifi_rf_lut_write(i, w0, w1);
    }

    /* post-load reset [SPECSHEET §5.1] */
    FM1_RFSPI_CTL = 0;
    FM1_RFSPI_CTL = 0;
    FM1_RFSPI_CTL = 0;
    FM1_RFSPI_CTL = 0;
    FM1_RFSPI_CTL = 0;
    FM1_RFSPI_CTL = 0;
    FM1_RFSPI_CTL = 0x10000u;

    cfm1_log_int("wifi: rf lut entries=", 256);
    cfm1_log_int("wifi: rf lut agc_p=", (int32_t)patched_agc);
    cfm1_log_int("wifi: rf lut apc_p=", (int32_t)patched_apc);
    cfm1_log_int("wifi: rf lut spi_to=", (int32_t)wifi_rf_stat.spi_timeouts);
}

/* Group 13: VM 108 -> BBP window D -- 36 writes */
static void wifi_rf_group13_vm108(void)
{
    uint32_t i;

    if (!(wifi_rf_vm.have & 0x04u)) {
        wifi_rf_stat.ops += 36;
        return;
    }

    /* VM 108: 20 bytes of PA per-rate digital gain -> 12 BBP window-D
     * entries (36 writes = 12 entries x 3 MMIO writes per BBP window).
     * Window D: commit=0xD0, entry=0xD1, data=0xD2 (window index 2). */
    for (i = 0; i < 12u && i < 20u; i++) {
        uint8_t val = wifi_rf_vm.x108[i];
        if (!val)
            continue;   /* non-zero replaces default (same convention as BLE) */
        wifi_rf_bbp_win_wr(2u, i, val);
    }

    wifi_rf_stat.ops += 36;
}

/* ---- WiFi RF mode switch [SPECSHEET §11.2.9] ----
 * After the BLE RF bytecode configures the shared radio for BLE,
 * this switches the RF analog block to WiFi mode:
 *   - CON0/CON1/ANL_CON2: RMW deltas (known starting values)
 *   - CON3-CON11, PLL, ANA: full writes (BLE values overwritten)
 *   - 0x11950 zeroed */

static void wifi_rf_wifi_mode(void)
{
    uint32_t v;
    cfm1_log("wifi: rf wifi_mode");

    /* --- RMW deltas for registers with known BLE residue --- */

    /* CON0 (0x11900): switch LNA/mixer from BLE to WiFi [SPECSHEET §11.2.9]
     * Clear bits 23,20,19,18 (BLE path); set bit 21 (WiFi path) */
    v = FM1_RF_REG(0x11900u);
    v &= ~((1u << 23) | (1u << 20) | (1u << 19) | (1u << 18));
    v |= (1u << 21);
    FM1_RF_REG(0x11900u) = v;

    /* CON1 (0x11904): filter/bias for WiFi 20MHz bandwidth [SPECSHEET §11.2.9]
     * Clear bits 18,10; set bits 17,16,14,11 */
    v = FM1_RF_REG(0x11904u);
    v &= ~((1u << 18) | (1u << 10));
    v |= (1u << 17) | (1u << 16) | (1u << 14) | (1u << 11);
    FM1_RF_REG(0x11904u) = v;

    /* ANL_CON2 (0x11930): 40MHz clock source [SPECSHEET §11.2.9]
     * Set bit 27 (40MHz mode); clear bits 23,10 */
    v = FM1_RF_REG(0x11930u);
    v &= ~((1u << 23) | (1u << 10));
    v |= (1u << 27);
    FM1_RF_REG(0x11930u) = v;

    /* --- Full writes for RF analog chain [SPECSHEET §11.2.9] --- */

    FM1_RF_REG(0x1190Cu) = 0x672B9349u;   /* CON3: filter/VGA/mixer */
    FM1_RF_REG(0x11910u) = 0x0508098Eu;   /* CON4 */
    FM1_RF_REG(0x11914u) = 0x4078C319u;   /* CON5 */
    FM1_RF_REG(0x11918u) = 0x00076A24u;   /* CON6 */
    FM1_RF_REG(0x1191Cu) = 0x2915C066u;   /* CON7 */
    FM1_RF_REG(0x11920u) = 0x00A641C0u;   /* CON8 */
    FM1_RF_REG(0x11924u) = 0x65B80057u;   /* CON9: PA trims */
    FM1_RF_REG(0x11928u) = 0x1AFAE7E7u;   /* CON10 */
    FM1_RF_REG(0x1192Cu) = 0x0052A070u;   /* CON11 */

    /* --- PLL for WiFi 2.4GHz [SPECSHEET §11.2.9] --- */

    FM1_RF_REG(0x11934u) = 0x01B33333u;   /* PLL frac */
    FM1_RF_REG(0x11938u) = 0x819FD028u;   /* PLL int=40 + config */
    FM1_RF_REG(0x1193Cu) = 0x12600EF2u;   /* PLL config */
    FM1_RF_REG(0x11940u) = 0x08B42451u;   /* PLL config (40MHz path) */
    FM1_RF_REG(0x11944u) = 0x00000A80u;   /* PLL config */

    /* --- Analog frontend --- */

    FM1_RF_REG(0x11950u) = 0x00000000u;   /* zeroed [SPECSHEET §11.2.9] */
    FM1_RF_REG(0x11954u) = 0x000A2500u;   /* ANA_CON1 (40MHz) */
    FM1_RF_REG(0x11964u) = 0x00010000u;   /* ANA_CON5 */

    fm1_delay_us(100);

    /* VCO re-lock after PLL change */
    FM1_WL_VCO_EN &= ~(1u << 24);
    fm1_delay_us(WIFI_RF_VCO_EDGE_US);
    FM1_WL_VCO_EN |= (1u << 24);
    fm1_delay_us(WIFI_RF_VCO_SETTLE_US);

    /* Diagnostic: verify mode switch */
    cfm1_log_int("wifi: wm con0=", (int32_t)FM1_RF_REG(0x11900u));
    cfm1_log_int("wifi: wm con1=", (int32_t)FM1_RF_REG(0x11904u));
    cfm1_log_int("wifi: wm anl2=", (int32_t)FM1_RF_REG(0x11930u));
    cfm1_log_int("wifi: wm pll0=", (int32_t)FM1_RF_REG(0x11934u));
    cfm1_log_int("wifi: wm pll1=", (int32_t)FM1_RF_REG(0x11938u));

    cfm1_log("wifi: rf wifi_mode done");
}

/* ---- RF baseband auto-control [SPECSHEET §11.2.11] ----
 * Enables automatic AGC and frequency tracking loops in the RF analog
 * section. Without these, the receiver cannot adapt to varying signal
 * levels from different APs. */

static void wifi_rf_bsb_auto_ctl(void)
{
    uint32_t v;

    cfm1_log("wifi: rf bsb_auto_ctl");

    /* CON20 (0x11950): configure auto AGC/frequency control loops */
    v = FM1_RF_REG(0x11950u);
    v &= ~(1u << 0);
    v &= ~(1u << 1);
    v |= (1u << 2) | (1u << 3);
    v |= (1u << 23);
    v &= ~(1u << 29);
    v |= (1u << 28);
    v |= (1u << 10) | (1u << 11) | (1u << 12) | (1u << 13);
    v |= (1u << 18) | (1u << 19) | (1u << 20) | (1u << 21);
    v &= ~(1u << 22);
    FM1_RF_REG(0x11950u) = v;

    /* CON14 (0x11938): PLL auto-control enables */
    FM1_RF_REG(0x11938u) |= (1u << 26) | (1u << 27);

    /* WL_RST (0x1405C): PLL control bits */
    FM1_RF_REG(0x1405Cu) |= 0x07000000u;
    FM1_RF_REG(0x1405Cu) |= (1u << 30);

    cfm1_log_int("wifi: bsb con20=", (int32_t)FM1_RF_REG(0x11950u));
    cfm1_log_int("wifi: bsb con14=", (int32_t)FM1_RF_REG(0x11938u));
}

/* ---- RF DC offset snapshot/restore [SPECSHEET §11.2.11] ----
 * CON7 (0x1191C) and CON8 (0x11920) hold RX DC offset tuning.
 * Snapshot after init; restore on channel switch or wake. */

static void wifi_rf_dc_get(void)
{
    wifi_rf_dc.con7 = FM1_RF_REG(0x1191Cu);
    wifi_rf_dc.con8 = FM1_RF_REG(0x11920u);
    cfm1_log_int("wifi: rf dc con7=", (int32_t)wifi_rf_dc.con7);
    cfm1_log_int("wifi: rf dc con8=", (int32_t)wifi_rf_dc.con8);
}

static void wifi_rf_dc_set(void)
{
    FM1_RF_REG(0x1191Cu) = wifi_rf_dc.con7;
    FM1_RF_REG(0x11920u) = wifi_rf_dc.con8;
}

/* ---- main RF init entry point ---- */

static void wifi_rf_init(void)
{
    uint32_t i;
    cfm1_log("wifi: rf init");

    for (i = 0; i < sizeof(wifi_rf_stat); i++)
        ((uint8_t *)&wifi_rf_stat)[i] = 0;

    wifi_rf_vm_read();

    /* Group 1: WiFi-specific system clocks */
    wifi_rf_group1_clocks();

    /* Groups 2-13: shared analog block init via the BLE RF bytecode
     * interpreter.  The ble_rf_prog[] bytecode (captured from stock V15)
     * contains the complete 34K-write sequence for the shared radio:
     * analog ramp, BBP loads, crystal trim, VCO scan, LUT, VM trims.
     *
     * When VM records aren't in flash, pass zero-filled dummy arrays:
     * fm1_rf_field() dereferences rec[x][y] unconditionally, so NULL
     * would read garbage from address 0 on bare metal. */
    {
        static const uint8_t z106[2]  = {0};
        static const uint8_t z107[7]  = {0};
        static const uint8_t z108[20] = {0};
        static const uint8_t z187[68] = {0};
        fm1_ble_rf_run(
            (wifi_rf_vm.have & 0x01u) ? wifi_rf_vm.x106 : z106,
            (wifi_rf_vm.have & 0x02u) ? wifi_rf_vm.x107 : z107,
            (wifi_rf_vm.have & 0x04u) ? wifi_rf_vm.x108 : z108,
            (wifi_rf_vm.have & 0x08u) ? wifi_rf_vm.x187 : z187);
    }

    cfm1_log_int("wifi: rf ops=", (int32_t)fm1_ble_rf_stat.ops);
    cfm1_log_int("wifi: rf sections=", (int32_t)fm1_ble_rf_stat.sections);
    cfm1_log_int("wifi: rf vco_f=", (int32_t)fm1_ble_rf_stat.scan_found);
    cfm1_log_int("wifi: rf vco_b=", (int32_t)fm1_ble_rf_stat.scan_band);
    cfm1_log_int("wifi: rf bad_op=", (int32_t)fm1_ble_rf_stat.bad_op);
    cfm1_log_int("wifi: rf bbp_to=", (int32_t)fm1_ble_rf_stat.bbp_timeouts);
    cfm1_log_int("wifi: rf spi_to=", (int32_t)fm1_ble_rf_stat.spi_timeouts);
    cfm1_log_int("wifi: rf trims=", (int32_t)fm1_ble_rf_stat.trims);
    cfm1_log_int("wifi: rf lut=", (int32_t)fm1_ble_rf_stat.lut_words);

    /* Group 14 (BT coexistence) skipped: WiFi-only operation */

    /* Switch RF analog from BLE to WiFi mode [SPECSHEET §11.2.9] */
    wifi_rf_wifi_mode();

    /* Always calibrate: VM 187 from BLE boot contains BLE-mode values
     * (wrong filter BW, TX LO, RX DC) that make WiFi deaf.
     * [SPECSHEET §17 step 12-16, §18] */
    wifi_rf_calibrate();

    /* VCO bank-limit table for the LUT FEQ patch [SPECSHEET §5.2, §14 row 6],
     * measured with the 24 MHz PLL words in place (12n) */
    wifi_rf_force_vendor_regs();
    wifi_rf_bank_scan();

    /* LUT, auto control, enables, forced read-backs: the re-armable tail,
     * run again after the MAC/PHY resets by wifi_init() [§14 row 7] */
    wifi_rf_arm();

    wifi_rf_stat.ran = 1;
    cfm1_log("wifi: rf init done");
}

/* The tail of the RF bring-up that must be the LAST thing done to the RF
 * die [SPECSHEET §4.2.1 A20-A23, §14 row 7, M:s]: the vendor loads the LUT
 * and hands control to the RF-die sequencer after its final PHY init, and
 * performs no MAC/BBP reset afterwards. core-fm1's MAC/PHY resets live in
 * wifi_sdio_init(), so wifi_init() calls this once more after them. */
static void wifi_rf_arm(void)
{
    /* Load WiFi-specific RFD RAM LUT [SPECSHEET §5.1].
     * Replaces the BLE LUT left by the RF bytecode with WiFi
     * frequency equalization, AGC, and power control tables. */
    wifi_rf_rfd_lut_load();

    /* Enable RF auto-control loops (AGC, frequency tracking)
     * [SPECSHEET §11.2.11]. Must run after wifi_mode and calibration. */
    wifi_rf_bsb_auto_ctl();

    /* Snapshot post-calibration RX DC offset [SPECSHEET §11.2.11, step 16].
     * Restored by wifi_rf_dc_set() on channel switch or wake. */
    wifi_rf_dc_get();

    /* Enable analog blocks for Normal RX [SPECSHEET §14.10, mode 0].
     * wifi_rf_wifi_mode() wrote base register values but not the 29
     * enable bits that power on LNA, mixer, VGA, ADC. Without this
     * the analog RX chain is off and the radio is deaf. */
    wcal_en_set(0);

    wifi_rf_force_vendor_regs();

    cfm1_log_int("wifi: rf en0 con0=", (int32_t)FM1_RF_REG(0x11900u));
    cfm1_log_int("wifi: rf en0 con4=", (int32_t)FM1_RF_REG(0x11910u));

    /* Re-snapshot DC offset after override */
    wifi_rf_dc_get();

    /* Save TX LO trims for BT reload [SPECSHEET §17 step 27] */
    wifi_rf_txlo_save.con7 = FM1_RF_REG(0x1191Cu);
    wifi_rf_txlo_save.con8 = FM1_RF_REG(0x11920u);
    cfm1_log_int("wifi: rf txlo con7=", (int32_t)wifi_rf_txlo_save.con7);
    cfm1_log_int("wifi: rf txlo con8=", (int32_t)wifi_rf_txlo_save.con8);
}

/* ---- channel control ---- */

/* Set the radio to an 802.11 channel (1-14).
 * Programs the PLL/VCO for the channel center frequency and updates
 * BBP channel-dependent parameters. */
static void wifi_rf_set_channel(uint8_t channel)
{
    uint32_t v;

    if (channel < 1 || channel > 14)
        return;

    cfm1_log_int("wifi: rf ch=", channel);
    wifi_rf_stat.channel = channel;

    /* The vendor's entire per-channel RF step [SPECSHEET §4.2.1 C1, §4.2
     * 0x1405C, M:s 2026-11-04]: the channel code (channel + 101) into
     * WF_CON7 [7:0]; the RF die applies the LUT FEQ entry for that code
     * (divider and VCO bank, §5). Until 12l this function programmed the
     * divider directly with the VCO bank left at its init value, which
     * cannot lock away from 2442 MHz [I]. No BBP write on a channel change
     * (§6.8, erratum 5). The MAC front end is written by the caller. */
    v = FM1_RF_REG(0x1405Cu);
    FM1_RF_REG(0x1405Cu) = (v & ~0xFFu) | ((uint32_t)channel + 101u);
}

/* ---- TX power control ---- */

/* Set transmit power level.  The PA gain uses analog trim registers
 * (0x11924-0x1192C) and digital gain via BBP window-D entries. */
static void wifi_rf_set_txpower(uint8_t level)
{
    /* TODO: scale VM 108 digital gain values by level, clamp to safe
     * limits.  The dBm mapping must be calibrated on hardware. */
    (void)level;
}
