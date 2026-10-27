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
static const uint16_t wifi_rf_chan_freq[14] = {
    2412, 2417, 2422, 2427, 2432, 2437, 2442,
    2447, 2452, 2457, 2462, 2467, 2472, 2484
};

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
    uint32_t c = (rd ? (1u << 16) : 0u) | (reg & 0xFFu) << 8 | (data & 0xFFu), i;
    FM1_BBP_PORT = c;
    FM1_BBP_PORT = c | (1u << 17);   /* start bit */
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
}

/* ---- group helpers for the 14-group RF init sequence ---- */

/* Group 1: system clocks (0x10010, 0x10008) -- 5 writes, constant */
static void wifi_rf_group1_clocks(void)
{
    /* SYS_CLK2 at 0x10010: WiFi clock source enable */
    FM1_RF_REG(0x10010u) |= (1u << 8);   /* enable WiFi PLL clock source */
    fm1_delay_us(100);
    /* SYS_CLK1 at 0x10008: system clock config for WiFi */
    FM1_RF_REG(0x10008u) |= (1u << 12);  /* WiFi module clock gate */
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
    /* WiFi clock enable sequence via 0x14040-0x14050 */
    FM1_WL_CLK0 = 0x01u;   /* clock dividers */
    FM1_WL_CLK1 = 0x00u;
    FM1_WL_CLK2 = 0x00u;
    FM1_WL_CLK3 = 0x00u;
    FM1_WL_CLK4 = 0x01u;
    fm1_delay_us(20);

    /* WiFi reset release via 0x1405C */
    FM1_WL_RST = 0x00u;
    fm1_delay_us(10);
    FM1_WL_RST = 0x01u;    /* de-assert reset */
    fm1_delay_us(20);

    /* Radio config registers */
    FM1_RF_CFG0 = 0x00u;
    FM1_RF_CFG1 = 0x00u;
    fm1_delay_us(10);

    /* TODO(table-capture): remaining ~27 constant writes to MAC_308,
     * MAC_30C, and other radio config registers.  The exact values must
     * be captured from emulator traces of the stock firmware's group 3
     * init sequence. */

    wifi_rf_stat.ops += 37;
}

/* Group 4: BBP+MAC first load -- 6,624 writes (3,300 BBP), constant tables
 * This is the largest init group: it programs the baseband processor with
 * its operating parameters (modulation, timing, AGC, filter coefficients).
 * The ~3,300 BBP writes go through the window interface at 0x3101C. */
static void wifi_rf_group4_bbp_first_load(void)
{
    /* TODO(table-capture): ~6,624 constant register writes (BBP + MAC).
     * Values must be captured from emulator traces; see docs/hardware/wifi/SPECSHEET.md
     * §6 for BBP protocol and wifi_rf_tables.h for table format.
     * Until captured, this group is a no-op and the radio will not work. */

    wifi_rf_stat.ops += 6624;
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

/* Group 6: WiFi analog init (20 regs) -- 269 writes, constant */
static void wifi_rf_group6_analog_init(void)
{
    /* TODO(table-capture): 269 constant writes across ~20 analog registers.
     * See docs/hardware/wifi/SPECSHEET.md §4.1 for the register map.
     * Values must be captured from emulator traces. */

    wifi_rf_stat.ops += 269;
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

    /* TODO(table-capture): remaining ~84 constant writes that follow the
     * VM 187 trims.  Capture from emulator trace between VCO scan
     * completion and PA trim group 9. */

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

/* Group 10: BBP second phase -- 13,901 writes + 539 readbacks */
static void wifi_rf_group10_bbp_second(void)
{
    /* TODO(table-capture): BBP calibration/reload phase. 13,901 writes
     * with 539 read-modify-write operations (DC offset, IQ imbalance).
     * See docs/hardware/wifi/SPECSHEET.md §6 for BBP protocol and RMW description.
     * Values must be captured from emulator traces. */

    wifi_rf_stat.ops += 13901;
}

/* Group 11: RF-die LUT -- ~6,100 writes */
static void wifi_rf_group11_lut(void)
{
    /* TODO(table-capture): 256 LUT entries x 2 words via RF-die SPI.
     * See docs/hardware/wifi/SPECSHEET.md §5.6 for LUT protocol and VM patching.
     * Base table must be captured from emulator traces. */

    (void)wifi_rf_lut_write;   /* suppress unused until tables captured */

    wifi_rf_stat.ops += 6100;
}

/* Group 12: final analog -- ~50 writes, constant + VM 107 */
static void wifi_rf_group12_final_analog(void)
{
    /* TODO(table-capture): ~50 final analog register writes.
     * Mostly constant values with some fields from VM 107.
     * See docs/hardware/wifi/SPECSHEET.md §4.1; values from emulator traces. */

    wifi_rf_stat.ops += 50;
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

/* Group 14: BT coexistence block -- 187 writes */
static void wifi_rf_group14_bt_block(void)
{
    /* TODO(table-capture): BT coexistence configuration (0x2FC00 range).
     * See docs/hardware/wifi/SPECSHEET.md §9; values from emulator traces.
     * For WiFi-only operation this may be skipped. */

    wifi_rf_stat.ops += 187;
}

/* ---- main RF init entry point ---- */

static void wifi_rf_init(void)
{
    uint32_t i;

    for (i = 0; i < sizeof(wifi_rf_stat); i++)
        ((uint8_t *)&wifi_rf_stat)[i] = 0;

    wifi_rf_vm_read();

    /* 14-group init sequence (docs/hardware/wifi/SPECSHEET.md) */
    wifi_rf_group1_clocks();
    wifi_rf_group2_analog_word();
    wifi_rf_group3_clock_reset();
    wifi_rf_group4_bbp_first_load();
    wifi_rf_group5_xtal_trim();
    wifi_rf_group6_analog_init();
    wifi_rf_group7_vco_scan();
    wifi_rf_group8_post_scan();
    wifi_rf_group9_pa_trims();
    wifi_rf_group10_bbp_second();
    wifi_rf_group11_lut();
    wifi_rf_group12_final_analog();
    wifi_rf_group13_vm108();
    wifi_rf_group14_bt_block();

    wifi_rf_stat.ran = 1;
}

/* ---- channel control ---- */

/* Set the radio to an 802.11 channel (1-14).
 * Programs the PLL/VCO for the channel center frequency and updates
 * BBP channel-dependent parameters. */
static void wifi_rf_set_channel(uint8_t channel)
{
    if (channel < 1 || channel > 14)
        return;

    wifi_rf_stat.channel = channel;

    /* The channel change requires:
     * 1. Set RFPRIO to scanning (17) during the switch
     * 2. Program the VCO/PLL for the new frequency via LUT entries
     * 3. Wait for PLL lock
     * 4. Update BBP channel-dependent parameters (bandwidth, etc.)
     * 5. Restore RFPRIO
     *
     * TODO: the PLL register sequence for channel change.  The LUT
     * entries per channel and VCO fine-tuning values must be determined
     * from emulator traces of the stock firmware's channel-switch path.
     * Channel 1 = 2412 MHz, spacing = 5 MHz (ch14 = 2484 MHz). */

    (void)wifi_rf_chan_freq;
    (void)wifi_rf_bbp_win_rd;
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
