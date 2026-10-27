/* SPDX-License-Identifier: GPL-3.0-only */
/* Clean-room implementation from docs/hardware/wifi/SPECSHEET.md BBP window protocol.
 * No vendor code, IR or disassembly. */
/* AC791N / WL82 BBP (Baseband Processor) access helpers.
 * All access goes through the indirect port at 0x3101C:
 *   [17] start bit, [16] read flag, [15:8] register address, [7:0] data.
 *
 * Four BBP windows provide access to extended register banks:
 *   Window C  (w=0): commit 0xC8, entry 0xC9, data 0xCA, readback 0xCB, range 0x00-0x7A
 *   Window C' (w=1): commit 0xCC, entry 0xCD, data 0xCE, no readback,   range 0x00-0x47
 *   Window D  (w=2): commit 0xD0, entry 0xD1, data 0xD2, readback 0xD3, range 0x00-0x5F
 *   Window D' (w=3): commit 0xD4, entry 0xD5, data 0xD6, readback 0xD7, range 0x00-0x1C
 *
 * Write protocol: 3 MMIO writes to port (entry addr, data, commit=1).
 * Read protocol:  entry addr, commit=2, read readback register. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"
#include "fm1_wifi_rf.h"

#define WIFI_BBP_START    (1u << 17)
#define WIFI_BBP_READ     (1u << 16)
#define WIFI_BBP_POLLS    1000u

/* direct BBP registers: 0x15, 0x16, 0x19, 0x54, 0x6A (and the window regs) */

static uint32_t wifi_bbp_timeouts;

/* one BBP transaction: write reg with data, or read (rd=1) -> port [7:0] */
FM1_INLINE uint32_t wifi_bbp_xfer(uint32_t reg, uint32_t data, uint32_t rd)
{
    uint32_t cmd = WIFI_BBP_START | (rd ? WIFI_BBP_READ : 0u)
                 | (reg & 0xFFu) << 8 | (data & 0xFFu);
    uint32_t i;
    FM1_BBP_PORT = cmd & ~WIFI_BBP_START;
    FM1_BBP_PORT = cmd;
    for (i = 0; i < WIFI_BBP_POLLS && (FM1_BBP_PORT & WIFI_BBP_START); i++)
        ;
    if (i == WIFI_BBP_POLLS)
        wifi_bbp_timeouts++;
    return FM1_BBP_PORT & 0xFFu;
}

static void wifi_bbp_write(uint32_t reg, uint32_t data)
{
    wifi_bbp_xfer(reg, data, 0);
}

static uint32_t wifi_bbp_read(uint32_t reg)
{
    return wifi_bbp_xfer(reg, 0, 1);
}

/* window write: 3 BBP transactions (entry address, data byte, commit=1) */
static void wifi_bbp_win_write(uint32_t w, uint32_t entry, uint32_t val)
{
    uint32_t base = 0xC8u + 4u * w;  /* commit register for window w */
    wifi_bbp_xfer(base + 1u, entry & 0xFFu, 0);   /* entry address */
    wifi_bbp_xfer(base + 2u, val & 0xFFu, 0);      /* data */
    wifi_bbp_xfer(base, 1, 0);                      /* commit write */
}

/* window read: entry address, commit=2, read readback register.
 * Windows C' (w=1) has no readback register; returns 0 for w=1. */
static uint32_t wifi_bbp_win_read(uint32_t w, uint32_t entry)
{
    uint32_t base = 0xC8u + 4u * w;
    if (w == 1u)
        return 0;  /* C' has no readback */
    wifi_bbp_xfer(base + 1u, entry & 0xFFu, 0);   /* entry address */
    wifi_bbp_xfer(base, 2, 0);                      /* commit read */
    return wifi_bbp_xfer(base + 3u, 0, 1);          /* readback */
}
