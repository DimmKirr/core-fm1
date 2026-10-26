/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-1 (AC791N / WL82) WiFi internal SDIO bus registers.
 * On-chip interconnect between CPU and WiFi MAC/BBP, not an external card.
 * Addresses from decompiled IR (.scratch/examples/wifi/SDIO_PROTOCOL.md). */
#pragma once
#include <stdint.h>

#define FM1_WF_SDIO_REG(a) (*(volatile uint32_t *)(uintptr_t)(a))

/* SDIO controller registers (0x50000 range, discovered in IR) */
#define FM1_WF_SDIO_BASE    FM1_WF_SDIO_REG(0x50000u)
#define FM1_WF_SDIO_04      FM1_WF_SDIO_REG(0x50004u)
#define FM1_WF_SDIO_DATA    FM1_WF_SDIO_REG(0x50008u)
#define FM1_WF_SDIO_STATUS  FM1_WF_SDIO_REG(0x5000Cu)

/* CMD52: single-byte read/write (IO_RW_DIRECT) */
static inline uint8_t fm1_wf_sdio_cmd52_read(uint8_t fn, uint32_t addr)
{
    (void)fn;
    /* CMD52 read: fn | addr | 0 (read). Full protocol in wifi_sdio.c. */
    (void)addr;
    return 0; /* stub: real implementation in wifi_sdio.c */
}

static inline void fm1_wf_sdio_cmd52_write(uint8_t fn, uint32_t addr, uint8_t val)
{
    (void)fn; (void)addr; (void)val;
    /* stub: real implementation in wifi_sdio.c */
}
