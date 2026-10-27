/* SPDX-License-Identifier: GPL-3.0-only */
/* Clean-room implementation from docs/hardware/wifi/SPECSHEET.md RF init sequence description.
 * No vendor code, IR or disassembly. */
/* WiFi RF constant table definitions and placeholder structures.
 *
 * The RF init sequence (docs/hardware/wifi/SPECSHEET.md) has 14 groups totaling ~34,274
 * writes. Several groups use large constant tables:
 *   Group  4: BBP + MAC first load, ~6,624 writes (~3,300 BBP), constant
 *   Group 10: BBP second phase, ~13,901 writes + 539 readbacks, tables + live RMW
 *   Group 11: RF-die LUT, ~6,100 writes, constant base + VM patches
 *
 * These tables contain the actual register values that configure the baseband
 * processor and RF-die. The values are NOT available from the fact sheet (which
 * describes protocol and structure, not data). They must be captured from
 * emulator traces; see docs/hardware/wifi/SPECSHEET.md for the init sequence structure. */
#pragma once
#include <stdint.h>

#define WIFI_RF_TABLES_FORMAT 1

/* ---- BBP init table: address/value pairs for BBP window writes ---- */

typedef struct {
    uint8_t window;   /* 0=C, 1=C', 2=D, 3=D' */
    uint8_t entry;    /* register address within the window */
    uint8_t value;    /* data byte */
} wifi_bbp_entry_t;

/* Group 4: BBP + MAC first load.
 * ~3,300 BBP window writes (mostly window C and D) plus ~3,300 direct
 * MAC register writes. The BBP writes configure the baseband: AGC tables,
 * filter coefficients, modulation parameters.
 *
 * TODO: Capture from emulator trace. Run stock V15 with MMIO trace, collect
 * all writes to 0x3101C between the group-3 end (radio config done) and the
 * crystal trim (group 5, first write to 0x11930). Extract the BBP window
 * writes: for each triplet of port writes, decode window/entry/value from
 * the commit/entry/data pattern. Direct MAC writes (0x30xxx addresses) go
 * into wifi_mac_init_table[]. */
static const wifi_bbp_entry_t wifi_bbp_init_g4[] = {
    {0, 0, 0}  /* placeholder: real data from emulator capture */
};
#define WIFI_BBP_INIT_G4_COUNT 0u  /* TODO: ~3,300 entries from capture */

/* Group 10: BBP second phase.
 * ~13,901 writes + 539 readbacks. Contains DC offset calibration,
 * IQ imbalance correction, and AGC reconfiguration. Some entries
 * use read-modify-write (the readback values feed into subsequent
 * writes), so the table has both constant entries and RMW markers.
 *
 * TODO: Capture from emulator trace. The group begins after the PA trim
 * writes (group 9) and ends before the RF-die LUT load (group 11).
 * Entries that depend on readback values should be marked with a flag
 * so the init code can perform live RMW instead of blind writes. */

typedef struct {
    uint8_t window;
    uint8_t entry;
    uint8_t value;
    uint8_t flags;    /* bit 0: RMW (read before write, merge with readback) */
} wifi_bbp_rmw_entry_t;

#define WIFI_BBP_RMW_FLAG  0x01u

static const wifi_bbp_rmw_entry_t wifi_bbp_init_g10[] = {
    {0, 0, 0, 0}  /* placeholder: real data from emulator capture */
};
#define WIFI_BBP_INIT_G10_COUNT 0u  /* TODO: ~13,901 entries from capture */

/* ---- RF-die LUT: 256 entries x 2 words, loaded via SPI ---- */

/* Each LUT entry consists of two 32-bit words written through the RF-die
 * SPI port (0x14028 control, 0x1402C data). The write protocol per word:
 *   1. Write data to FM1_RFSPI_DATA (0x1402C)
 *   2. Write FM1_RFSPI_CTL = 0x10000 | (entry << 8) | cmd
 *      cmd 0xE = LUT word 0, cmd 0xD = LUT word 1, cmd 0x6 = read
 *   3. Toggle bit 4 (kick): 5x high then 5x low
 *
 * The base LUT is constant. VM 107 patches entries 0xE0-0xFF (PA cal),
 * and the VCO scan result patches word-1 of entries 0x00-0x7F. */

typedef struct {
    uint32_t word0;
    uint32_t word1;
} wifi_lut_entry_t;

/* TODO: Capture from emulator trace. During group 11 of the RF init,
 * record all SPI writes (commands 0xE and 0xD) to 0x14028/0x1402C.
 * Pair them by entry index to reconstruct the 256-entry table.
 * Then identify which entries are patched by VM 107 / 187 / VCO scan. */
static const wifi_lut_entry_t wifi_lut_base[256] = {{0, 0}};
#define WIFI_LUT_ENTRY_COUNT 256u

/* ---- MAC register init table ---- */

typedef struct {
    uint32_t addr;    /* absolute MMIO address (0x30xxx) */
    uint32_t value;
} wifi_mac_reg_t;

/* TODO: Capture MAC register writes from groups 3 and 4.
 * Group 3 writes ~37 MAC/radio config words (0x14xxx, 0x30xxx).
 * Group 4 interleaves MAC writes with BBP writes. */
static const wifi_mac_reg_t wifi_mac_init_table[] = {
    {0, 0}  /* placeholder */
};
#define WIFI_MAC_INIT_COUNT 0u

/* ---- VM calibration record sizes (docs/hardware/wifi/SPECSHEET.md) ---- */

#define WIFI_VM_106_SIZE  2u    /* crystal trims: xosc_l, xosc_r */
#define WIFI_VM_107_SIZE  7u    /* PA calibration (7 bytes) */
#define WIFI_VM_108_SIZE  20u   /* PA per-rate digital gain */
#define WIFI_VM_187_SIZE  68u   /* full cal cache: 64 data + CRC-16 + 2 pad */
#define WIFI_VM_187_DATA  64u   /* data portion before the inner CRC */
