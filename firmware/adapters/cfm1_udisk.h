/* SPDX-License-Identifier: MIT */
/* UDisk adapter: USB mass storage file browsing. */
#ifndef CFM1_UDISK_H
#define CFM1_UDISK_H
#include <stdint.h>

#define UDISK_PATH_MAX   256
#define UDISK_NAME_MAX    64
#define UDISK_DIR_MAX     64

typedef struct udisk_entry {
    char     name[UDISK_NAME_MAX];
    uint32_t size;
    uint8_t  is_dir;
} udisk_entry_t;

/* state */
static uint8_t  udisk_mounted;
static char     udisk_cwd[UDISK_PATH_MAX];
static udisk_entry_t udisk_entries[UDISK_DIR_MAX];
static uint32_t udisk_nentry;
static uint32_t udisk_cursor;
static uint32_t udisk_scroll;

#ifndef CONFIG_UDISK
/* stubs when CONFIG_UDISK is not set */
static void     udisk_init(void)  { udisk_mounted = 0; udisk_nentry = 0; udisk_cursor = 0; udisk_scroll = 0; udisk_cwd[0] = '/'; udisk_cwd[1] = 0; }
static void     udisk_poll(void)  { (void)0; }
static void     udisk_activate(void)   { (void)0; }
static void     udisk_deactivate(void) { (void)0; }
static int      udisk_opendir(const char *path) { (void)path; return -1; }
static int      udisk_chdir(const char *name) { (void)name; return -1; }
static int      udisk_go_up(void) { return -1; }
#endif

static int      udisk_is_mounted(void) { return udisk_mounted; }

#endif /* CFM1_UDISK_H */
