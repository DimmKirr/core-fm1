/* SPDX-License-Identifier: MIT */
/* Storage adapter: project save/load, user presets. */
#ifndef CFM1_STORAGE_H
#define CFM1_STORAGE_H
#include <stdint.h>

/* --- projects --- */

static int project_save(uint32_t slot) { (void)slot; return 0; }
static void project_load(uint32_t slot) { (void)slot; }
static int project_used(uint32_t slot) { (void)slot; return 0; }
static int project_name(uint32_t slot, char *b) { (void)slot; b[0] = 0; return 0; }
static const char *project_cur_name(void) { return ""; }
static void project_erase(uint32_t slot) { (void)slot; }
static void project_rename(uint32_t slot, const char *name) { (void)slot; (void)name; }
static int project_save_as(uint32_t slot, const char *name) { (void)slot; (void)name; return 0; }
static int project_info(uint32_t slot, char *b) { (void)slot; b[0] = 0; return 0; }

/* --- user presets --- */

static int up_used(uint32_t k) { (void)k; return 0; }
static int up_load(uint32_t k) { (void)k; return 0; }
static uint32_t up_count(void) { return 0; }
static uint32_t up_nth(uint32_t n) { (void)n; return 0; }
static uint32_t up_rank(uint32_t slot) { (void)slot; return 0; }
static uint32_t up_engine(uint32_t k) { (void)k; return 0; }
static void up_name(uint32_t k, char *b) { (void)k; b[0] = 0; }
static void up_slot_label(char *b, uint32_t k) { (void)k; b[0] = 0; }
static void up_ui(uint32_t op, uint32_t k) { (void)op; (void)k; }
static int up_has_pat(uint32_t k) { (void)k; return 0; }
static uint32_t up_pat_count(void) { return 0; }
static uint32_t up_pat_nth(uint32_t n) { (void)n; return 0; }
static uint32_t up_pat_rank(uint32_t slot) { (void)slot; return 0; }
static void up_pat_load(track_t *t, uint32_t k) { (void)t; (void)k; }
static void up_auto_name(char *b, uint32_t e, uint32_t k) { (void)e; (void)k; b[0] = 0; }
static void up_ui_named(uint32_t op, uint32_t k, const char *name, uint32_t cat)
    { (void)op; (void)k; (void)name; (void)cat; }
static uint32_t up_cat(uint32_t k) { (void)k; return 0; }

#endif /* CFM1_STORAGE_H */
