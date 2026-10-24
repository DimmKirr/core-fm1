/* SPDX-License-Identifier: MIT */
/* Modulation adapter: source/destination names and icons for UI.
 * Stubs are active only when no modulation lineage is configured. */
#ifndef CFM1_MOD_H
#define CFM1_MOD_H
#include <stdint.h>

#ifndef CONFIG_MODULATION
static const char *mod_dst_name(uint32_t dst) { (void)dst; return "---"; }
static const char *mod_dst_icon(uint32_t dst) { (void)dst; return ""; }
static const char *mod_src_icon(uint32_t src) { (void)src; return ""; }
static const char *mod_src_name(uint32_t src) { (void)src; return "---"; }
static void mod_begin(track_t *t) { (void)t; }
static void mod_end(track_t *t) { (void)t; }
static void mod_voice(voice_t *v) { (void)v; }
static void mod_init(void) {}
#endif

#endif /* CFM1_MOD_H */
