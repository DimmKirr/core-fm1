/* SPDX-License-Identifier: MIT */
/* Scales adapter: stubs when no scale system is included. */
#ifndef CFM1_SCALES_H
#define CFM1_SCALES_H
#include <stdint.h>

#if !defined(CONFIG_SCALES_MELODEE) && !defined(CONFIG_SCALES_FELUCCA)
static uint32_t scale_root;
static uint32_t scale_index;
static const char *scale_name(uint32_t idx) { (void)idx; return "Chromatic"; }
static uint32_t scale_mask(uint32_t idx) { (void)idx; return 0x0FFF; }
static void scales_init(void) { scale_root = 0; scale_index = 0; }
#endif

#endif /* CFM1_SCALES_H */
