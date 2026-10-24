/* SPDX-License-Identifier: GPL-3.0-only */
/* Melodee storage stub: FBKG format, 384 presets, migration, backup/restore.
 * TODO: port from upstream melodee (storage.c, storage_hw.c, project.c,
 * upreset.c, cz_store.c, fm6_store.c, favorites.c, native_presets.c,
 * shared_presets.c, settings_persist.c, browse.c). */

#define ST_MAGIC 0x554C4546u

static int st_read(uint32_t off, void *dst, uint32_t n)
{
    (void)off; (void)dst; (void)n;
    return -1;
}

static int st_erase(uint32_t off)
{
    (void)off;
    return -1;
}

static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    (void)off; (void)src; (void)n;
    return -1;
}

static void storage_init(void) {}

static int project_save(uint32_t slot) { (void)slot; return 0; }
static void project_load(uint32_t slot) { (void)slot; }
static int project_used(uint32_t slot) { (void)slot; return 0; }
static int project_name(uint32_t slot, char *b) { (void)slot; b[0] = 0; return 0; }
static const char *project_cur_name(void) { return ""; }
static void project_erase(uint32_t slot) { (void)slot; }
static void project_rename(uint32_t slot, const char *name) { (void)slot; (void)name; }
static int project_save_as(uint32_t slot, const char *name) { (void)slot; (void)name; return 0; }
static int project_info(uint32_t slot, char *b) { (void)slot; b[0] = 0; return 0; }

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

static void settings_load(void) {}
static void settings_save(void) {}
