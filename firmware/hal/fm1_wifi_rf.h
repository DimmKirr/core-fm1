/* SPDX-License-Identifier: GPL-3.0-only */
/* Clean-room implementation from docs/hardware/wifi/SPECSHEET.md register map.
 * No vendor code, IR or disassembly. */
/* AC791N / WL82 RF analog block, wireless control, and system clock registers
 * for the WiFi driver. Guards against double definition with fm1_ble_rf.h
 * which defines the shared subset (FM1_RF_REG, FM1_BBP_PORT, FM1_RFSPI_*,
 * FM1_WL_VCO_*, FM1_WL_CAL_*, FM1_WL_A00). */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

/* ---- shared register accessor (may already be defined by fm1_ble_rf.h) ---- */

#ifndef FM1_RF_REG
#define FM1_RF_REG(a)     (*(volatile uint32_t *)(uintptr_t)(a))
#endif
#ifndef FM1_BBP_PORT
#define FM1_BBP_PORT      (*(volatile uint32_t *)0x3101Cu)
#endif
#ifndef FM1_RFSPI_CTL
#define FM1_RFSPI_CTL     (*(volatile uint32_t *)0x14028u)
#endif
#ifndef FM1_RFSPI_DATA
#define FM1_RFSPI_DATA    (*(volatile uint32_t *)0x1402Cu)
#endif
#ifndef FM1_RFSPI_WAIT
#define FM1_RFSPI_WAIT    (*(volatile uint32_t *)0x14034u)
#endif

/* ---- RF analog block (0x11900-0x1197C) ---- */

#ifndef FM1_WL_A00
#define FM1_WL_A00        0x11900u
#endif
#define FM1_WL_A04        (*(volatile uint32_t *)0x11904u)
#define FM1_WL_A08        (*(volatile uint32_t *)0x11908u)   /* trim: VM 187 bytes 32,33,36,37 */
#define FM1_WL_A10        (*(volatile uint32_t *)0x11910u)   /* [15:14] VM187 b42; [26:22] T6 */
#define FM1_WL_A14        (*(volatile uint32_t *)0x11914u)   /* [17:15],[14:12],[7:5] T6 */
#define FM1_WL_A1C        (*(volatile uint32_t *)0x1191Cu)   /* [9:8] trim from VM 187 byte 24 */
#define FM1_WL_A20        (*(volatile uint32_t *)0x11920u)   /* [1:0] VM187 b25; [11:10] VM187 b42 */
#define FM1_WL_PA0        (*(volatile uint32_t *)0x11924u)   /* [26:24] PA trim from VM 107 byte 0 */
#define FM1_WL_PA1        (*(volatile uint32_t *)0x11928u)   /* PA trims VM 107 b1-4 */
#define FM1_WL_PA2        (*(volatile uint32_t *)0x1192Cu)   /* [2:0] PA trim from VM 107 byte 5 */
#define FM1_WL_XOSC       (*(volatile uint32_t *)0x11930u)   /* [16:13] xosc_l, [22:19] xosc_r from VM 106 */
#ifndef FM1_WL_VCO_EN
#define FM1_WL_VCO_EN     (*(volatile uint32_t *)0x11934u)   /* [24] pulsed per VCO scan step */
#endif
#ifndef FM1_WL_VCO_BAND
#define FM1_WL_VCO_BAND   (*(volatile uint32_t *)0x11938u)   /* [25:19] band; [16],[18] pulsed */
#endif
#ifndef FM1_WL_VCO_FINE
#define FM1_WL_VCO_FINE   (*(volatile uint32_t *)0x1193Cu)
#endif
#define FM1_WL_A40        (*(volatile uint32_t *)0x11940u)   /* post-scan constant */
#define FM1_WL_A50        (*(volatile uint32_t *)0x11950u)   /* written 32x around LUT load */
#define FM1_WL_A54        (*(volatile uint32_t *)0x11954u)   /* post-scan */
#define FM1_WL_A5C        (*(volatile uint32_t *)0x1195Cu)   /* filter cal: VM187 b40-43 */
#define FM1_WL_A60        (*(volatile uint32_t *)0x11960u)   /* filter cal: VM187 b44-47 */
#define FM1_WL_A64        (*(volatile uint32_t *)0x11964u)   /* analog init (near end) */
#ifndef FM1_WL_CAL_START
#define FM1_WL_CAL_START  (*(volatile uint32_t *)0x11968u)   /* [28] start meas; [0] filter cal */
#endif
#ifndef FM1_WL_CAL_STROBE
#define FM1_WL_CAL_STROBE (*(volatile uint32_t *)0x11978u)   /* [17] comp low; [18] comp high; [15:8] filter */
#endif

/* ---- wireless control block (0x14000-0x1405C) ---- */

#define FM1_WL_CTL0       (*(volatile uint32_t *)0x14000u)
#define FM1_WL_CTL8       (*(volatile uint32_t *)0x14008u)
#define FM1_WL_CLK0       (*(volatile uint32_t *)0x14040u)
#define FM1_WL_CLK1       (*(volatile uint32_t *)0x14044u)
#define FM1_WL_CLK2       (*(volatile uint32_t *)0x14048u)
#define FM1_WL_CLK3       (*(volatile uint32_t *)0x1404Cu)
#define FM1_WL_CLK4       (*(volatile uint32_t *)0x14050u)
#define FM1_WL_RST        (*(volatile uint32_t *)0x1405Cu)

/* ---- system clocks ---- */

#define FM1_SYS_CLK1      (*(volatile uint32_t *)0x10008u)
#define FM1_SYS_CLK2      (*(volatile uint32_t *)0x10010u)   /* WiFi clock source */

/* ---- MAC / radio config (0x30000 block) ---- */

#define FM1_MAC_308       (*(volatile uint32_t *)0x30308u)
#define FM1_MAC_30C       (*(volatile uint32_t *)0x3030Cu)
#define FM1_RF_CFG0       (*(volatile uint32_t *)0x30F00u)
#define FM1_RF_CFG1       (*(volatile uint32_t *)0x30F04u)

/* ---- RFPRIO values (BBP registers 0x05E / 0x060) ---- */

#define FM1_RFPRIO_SCAN   17u
#define FM1_RFPRIO_IDLE   26u
#define FM1_RFPRIO_DATA   28u
#define FM1_RFPRIO_CTRL   30u

/* ---- polling bounds (ours, not from a datasheet) ---- */

#ifndef FM1_BBP_POLLS
#define FM1_BBP_POLLS     1000u
#endif
#ifndef FM1_RFSPI_POLLS
#define FM1_RFSPI_POLLS   1000u
#endif
#ifndef FM1_VCO_BANDS
#define FM1_VCO_BANDS     64u
#endif
#ifndef FM1_VCO_EDGE_US
#define FM1_VCO_EDGE_US   2u
#endif
#ifndef FM1_VCO_SETTLE_US
#define FM1_VCO_SETTLE_US 50u
#endif

/* 802.11b/g channels 1-13: center freq = 2412 + (ch - 1) * 5 MHz */
#define FM1_WIFI_CH_BASE_MHZ  2412u
#define FM1_WIFI_CH_STEP_MHZ  5u
#define FM1_WIFI_CH_MAX       13u
