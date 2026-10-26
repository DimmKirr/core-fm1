/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-1 (AC791N / WL82) WiFi RF register definitions.
 * Header-only HAL: register macros and inline helpers for the WiFi radio front end.
 * Addresses from docs/hardware/WIFI.md, fm1_ble_rf.h (shared analog), and
 * decompiled IR (.scratch/examples/wifi/ir/wl_rf_common/). */
#pragma once
#include <stdint.h>

#define FM1_RF_REG(a)       (*(volatile uint32_t *)(uintptr_t)(a))

/* WiFi analog registers (0x11900-0x1197C): shared with BLE radio front end */
#define FM1_WF_CON0         FM1_RF_REG(0x11900u)  /* bit 14: periodic routine flag */
#define FM1_WF_CON1         FM1_RF_REG(0x11904u)
#define FM1_WF_CON2         FM1_RF_REG(0x11908u)
#define FM1_WF_CON3         FM1_RF_REG(0x1190Cu)
#define FM1_WF_CON4         FM1_RF_REG(0x11910u)
#define FM1_WF_CON5         FM1_RF_REG(0x11914u)
#define FM1_WF_CON6         FM1_RF_REG(0x11918u)
#define FM1_WF_CON7         FM1_RF_REG(0x1191Cu)
#define FM1_WF_CON8         FM1_RF_REG(0x11920u)
#define FM1_WF_CON9         FM1_RF_REG(0x11924u)
#define FM1_WF_CON10        FM1_RF_REG(0x11928u)
#define FM1_WF_CON11        FM1_RF_REG(0x1192Cu)
#define FM1_WF_CON12        FM1_RF_REG(0x11930u)
#define FM1_WF_VCO_EN       FM1_RF_REG(0x11934u)  /* bit 24: pulsed per VCO step */
#define FM1_WF_VCO_BAND     FM1_RF_REG(0x11938u)  /* [25:19] band; bits 16,18 pulsed */
#define FM1_WF_VCO_FINE     FM1_RF_REG(0x1193Cu)
#define FM1_WF_CON16        FM1_RF_REG(0x11940u)
#define FM1_WF_CON17        FM1_RF_REG(0x11944u)
#define FM1_WF_CON18        FM1_RF_REG(0x11948u)
#define FM1_WF_CON19        FM1_RF_REG(0x1194Cu)
#define FM1_WF_CON20        FM1_RF_REG(0x11950u)
#define FM1_WF_CON21        FM1_RF_REG(0x11954u)
#define FM1_WF_CON22        FM1_RF_REG(0x11958u)
#define FM1_WF_CON23        FM1_RF_REG(0x1195Cu)
#define FM1_WF_CON24        FM1_RF_REG(0x11960u)
#define FM1_WF_CON25        FM1_RF_REG(0x11964u)
#define FM1_WF_CAL_START    FM1_RF_REG(0x11968u)  /* bit 28: start comparator measurement */
#define FM1_WF_CON27        FM1_RF_REG(0x1196Cu)
#define FM1_WF_CON28        FM1_RF_REG(0x11970u)
#define FM1_WF_CON29        FM1_RF_REG(0x11974u)
#define FM1_WF_CON30        FM1_RF_REG(0x11978u)
#define FM1_WF_CON31        FM1_RF_REG(0x1197Cu)

/* Wireless control registers (0x14000-0x14064) */
#define FM1_WF_CTL0         FM1_RF_REG(0x14000u)
#define FM1_WF_CTL_08       FM1_RF_REG(0x14008u)  /* WiFi control (discovered in IR) */
#define FM1_WF_RFSPI_CTL    FM1_RF_REG(0x14028u)  /* [22:20] busy, 0x10000 | addr<<8 | cmd */
#define FM1_WF_RFSPI_DATA   FM1_RF_REG(0x1402Cu)
#define FM1_WF_RFSPI_WAIT   FM1_RF_REG(0x14034u)  /* written 0 while waiting */
#define FM1_WF_CLK0         FM1_RF_REG(0x14040u)
#define FM1_WF_CLK1         FM1_RF_REG(0x14044u)
#define FM1_WF_CLK2         FM1_RF_REG(0x14048u)
#define FM1_WF_CLK3         FM1_RF_REG(0x1404Cu)
#define FM1_WF_CLK4         FM1_RF_REG(0x14050u)
#define FM1_WF_RST          FM1_RF_REG(0x1405Cu)  /* reset control */
#define FM1_WF_RFPRIO_0     FM1_RF_REG(0x1405Eu)  /* RFPRIO hardware arbitration */
#define FM1_WF_RFPRIO_1     FM1_RF_REG(0x14060u)
#define FM1_WF_WR_CTL       FM1_RF_REG(0x14064u)

/* MAC registers (0x30000-0x31FFF) */
#define FM1_WF_MAC_BASE     0x30000u
#define FM1_WF_MAC_REG(off) FM1_RF_REG(FM1_WF_MAC_BASE + (off))
#define FM1_WF_MAC_CFG0     FM1_RF_REG(0x30F00u)  /* RF_CFG0 */
#define FM1_WF_MAC_CFG1     FM1_RF_REG(0x30F04u)  /* RF_CFG1 */
#define FM1_WF_MAC_10       FM1_RF_REG(0x30F10u)  /* discovered in IR */
#define FM1_WF_MAC_14       FM1_RF_REG(0x30F14u)
#define FM1_WF_MAC_18       FM1_RF_REG(0x30F18u)
#define FM1_WF_MAC_1C       FM1_RF_REG(0x30F1Cu)

/* BBP port (shared with BLE) */
#define FM1_WF_BBP_PORT     FM1_RF_REG(0x3101Cu)  /* bit17 start, bit16 read, [15:8] reg, [7:0] data */

/* WF_WLA_CON shadow array size */
#define WF_WLA_CON_SIZE     18

/* helper: read-modify-write a register field */
static inline void fm1_wf_rmw(volatile uint32_t *reg, uint32_t mask, uint32_t val)
{
    *reg = (*reg & ~mask) | (val & mask);
}

/* helper: busy-wait on RFSPI */
static inline void fm1_wf_rfspi_wait(void)
{
    while (FM1_WF_RFSPI_CTL & (7u << 20))
        FM1_WF_RFSPI_WAIT = 0;
}
