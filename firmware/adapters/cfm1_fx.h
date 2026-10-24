/* SPDX-License-Identifier: MIT */
/* Effects adapter: FX chain state for UI display and control.
 * Stubs are active only when no effects lineage is configured. */
#ifndef CFM1_FX_H
#define CFM1_FX_H
#include <stdint.h>

#if !defined(CONFIG_EFFECTS_FELUCCA) && !defined(CONFIG_EFFECTS_MELODEE) && !defined(CONFIG_EFFECTS_SLOOP)
static void fx_assign(uint32_t slot, uint32_t type) { (void)slot; (void)type; }
static void fx_default(track_t *t) { (void)t; }
static int fx_key_held(void) { return 0; }
static void fx_map_sync(void) { }
static const char *fx_say(uint32_t slot) { (void)slot; return "---"; }
#endif

#endif /* CFM1_FX_H */
