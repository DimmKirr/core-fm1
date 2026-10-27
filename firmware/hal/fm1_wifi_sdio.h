/* SPDX-License-Identifier: GPL-3.0-only */
/* Clean-room implementation from docs/hardware/wifi/SPECSHEET.md and SDIO Simplified Specification.
 * No vendor code, IR or disassembly. */
/* AC791N / WL82 internal SDIO bus and MAC/BBP register definitions.
 * The SDIO here is an on-chip interconnect between the CPU and the WiFi
 * MAC/BBP block, not an external card slot. */
#pragma once
#include <stdint.h>

/* ---- SDIO controller (0x50000 range) ---- */

#define FM1_SDIO_BASE     (*(volatile uint32_t *)0x50000u)
#define FM1_SDIO_08       (*(volatile uint32_t *)0x50008u)   /* data / command */
#define FM1_SDIO_STATUS   (*(volatile uint32_t *)0x5000Cu)   /* status / response */

/* SDIO CMD52 (IO_RW_DIRECT): single-byte transfer
 * CMD53 (IO_RW_EXTENDED): multi-byte / block DMA transfer */
#define FM1_SDIO_CMD52    52u
#define FM1_SDIO_CMD53    53u

/* ---- MAC registers (0x30000-0x31FFF) ---- */

#ifndef FM1_MAC_308
#define FM1_MAC_308       (*(volatile uint32_t *)0x30308u)
#endif
#ifndef FM1_MAC_30C
#define FM1_MAC_30C       (*(volatile uint32_t *)0x3030Cu)
#endif
#ifndef FM1_RF_CFG0
#define FM1_RF_CFG0       (*(volatile uint32_t *)0x30F00u)
#endif
#ifndef FM1_RF_CFG1
#define FM1_RF_CFG1       (*(volatile uint32_t *)0x30F04u)
#endif

/* BBP indirect port (may already be defined by fm1_wifi_rf.h or fm1_ble_rf.h) */
#ifndef FM1_BBP_PORT
#define FM1_BBP_PORT      (*(volatile uint32_t *)0x3101Cu)
#endif
