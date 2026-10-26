/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-1 (AC791N / WL82) WiFi BBP (Baseband Processor) access.
 * Header-only: inline read/write via the BBP window register at 0x3101C.
 * Derived from wf_phy_mac_lib.c.ll in decompiled IR. */
#pragma once
#include <stdint.h>
#include "fm1_wifi_rf.h"

/* BBP_PORT layout: [17] start, [16] read, [15:8] register address, [7:0] data */
#define FM1_BBP_START_BIT   (1u << 17)
#define FM1_BBP_READ_BIT    (1u << 16)

static inline void fm1_wf_bbp_write(uint8_t reg, uint8_t val)
{
    uint32_t cmd = FM1_BBP_START_BIT | ((uint32_t)reg << 8) | val;
    FM1_WF_BBP_PORT = cmd;
    while (FM1_WF_BBP_PORT & FM1_BBP_START_BIT)
        ;
}

static inline uint8_t fm1_wf_bbp_read(uint8_t reg)
{
    uint32_t cmd = FM1_BBP_START_BIT | FM1_BBP_READ_BIT | ((uint32_t)reg << 8);
    FM1_WF_BBP_PORT = cmd;
    while (FM1_WF_BBP_PORT & FM1_BBP_START_BIT)
        ;
    return (uint8_t)(FM1_WF_BBP_PORT & 0xFF);
}
