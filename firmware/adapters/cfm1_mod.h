/* SPDX-License-Identifier: MIT */
/* Modulation adapter: source/destination names and icons for UI. */
#ifndef CFM1_MOD_H
#define CFM1_MOD_H
#include <stdint.h>

static const char *mod_dst_name(uint32_t dst) { (void)dst; return "---"; }
static const char *mod_dst_icon(uint32_t dst) { (void)dst; return ""; }
static const char *mod_src_icon(uint32_t src) { (void)src; return ""; }
static const char *mod_src_name(uint32_t src) { (void)src; return "---"; }

#endif /* CFM1_MOD_H */
