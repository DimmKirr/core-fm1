/* SPDX-License-Identifier: MIT */
/* Effects adapter: FX chain state for UI display and control. */
#ifndef CFM1_FX_H
#define CFM1_FX_H
#include <stdint.h>

/* stub API: lineage effects override these */
static void fx_assign(uint32_t slot, uint32_t type) { (void)slot; (void)type; }
static void fx_default(track_t *t) { (void)t; }
static int fx_key_held(void) { return 0; }
static void fx_map_sync(void) { }
static const char *fx_say(uint32_t slot) { (void)slot; return "---"; }

#endif /* CFM1_FX_H */
