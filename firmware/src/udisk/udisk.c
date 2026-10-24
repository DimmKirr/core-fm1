/* SPDX-License-Identifier: GPL-3.0-only */
/* UDisk subsystem: mounts FAT32 from USB mass storage, provides directory
 * listing for the file browser page. */

static FATFS udisk_fs;
static uint8_t udisk_active;

static void udisk_init(void)
{
    udisk_mounted = 0;
    udisk_nentry = 0;
    udisk_cursor = 0;
    udisk_scroll = 0;
    udisk_active = 0;
    udisk_cwd[0] = '/';
    udisk_cwd[1] = 0;
}

static void udisk_activate(void)
{
    if (udisk_active) return;
    udisk_active = 1;
    usb_host_init();
}

static void udisk_deactivate(void)
{
    if (!udisk_active) return;
    if (udisk_mounted) {
        f_mount(0, "", 0);
        udisk_mounted = 0;
        udisk_nentry = 0;
    }
    usb_host_deinit();
    udisk_active = 0;
}

static int udisk_opendir(const char *path);

static void udisk_try_mount(void)
{
    if (uhost.state != UHOST_MSC_READY) return;
    FRESULT r = f_mount(&udisk_fs, "", 1);
    if (r == FR_OK) {
        udisk_mounted = 1;
        udisk_cwd[0] = '/';
        udisk_cwd[1] = 0;
        udisk_opendir("/");
    }
}

static void udisk_poll(void)
{
    if (!udisk_active) return;
    if (!udisk_mounted && uhost.state == UHOST_MSC_READY)
        udisk_try_mount();
    if (udisk_mounted && uhost.state == UHOST_DISCONNECTED) {
        udisk_mounted = 0;
        udisk_nentry = 0;
        f_mount(0, "", 0);
    }
}

static int udisk_opendir(const char *path)
{
    DIR dp;
    FILINFO fno;
    FRESULT r;
    udisk_nentry = 0;
    udisk_cursor = 0;
    udisk_scroll = 0;
    r = f_opendir(&dp, path);
    if (r != FR_OK) return -1;
    while (udisk_nentry < UDISK_DIR_MAX) {
        r = f_readdir(&dp, &fno);
        if (r != FR_OK || fno.fname[0] == 0) break;
        if (fno.fattrib & AM_HID) continue;
        udisk_entry_t *e = &udisk_entries[udisk_nentry];
        uint32_t i;
        for (i = 0; i < UDISK_NAME_MAX - 1 && fno.fname[i]; i++)
            e->name[i] = fno.fname[i];
        e->name[i] = 0;
        e->size = (uint32_t)fno.fsize;
        e->is_dir = (fno.fattrib & AM_DIR) ? 1 : 0;
        udisk_nentry++;
    }
    f_closedir(&dp);
    return 0;
}

static void udisk_path_append(char *cwd, const char *name)
{
    uint32_t len = 0;
    while (cwd[len]) len++;
    if (len > 1 && len < UDISK_PATH_MAX - 2) cwd[len++] = '/';
    uint32_t i;
    for (i = 0; name[i] && len < UDISK_PATH_MAX - 1; i++)
        cwd[len++] = name[i];
    cwd[len] = 0;
}

static int udisk_chdir(const char *name)
{
    char path[UDISK_PATH_MAX];
    uint32_t i;
    for (i = 0; i < UDISK_PATH_MAX - 1 && udisk_cwd[i]; i++)
        path[i] = udisk_cwd[i];
    path[i] = 0;
    udisk_path_append(path, name);
    if (udisk_opendir(path) == 0) {
        for (i = 0; i < UDISK_PATH_MAX && path[i]; i++)
            udisk_cwd[i] = path[i];
        udisk_cwd[i] = 0;
        return 0;
    }
    return -1;
}

static int udisk_go_up(void)
{
    if (udisk_cwd[0] == '/' && udisk_cwd[1] == 0) return -1;
    uint32_t len = 0;
    while (udisk_cwd[len]) len++;
    while (len > 1 && udisk_cwd[len - 1] != '/') len--;
    if (len > 1) len--;
    udisk_cwd[len] = 0;
    if (len == 0) { udisk_cwd[0] = '/'; udisk_cwd[1] = 0; }
    return udisk_opendir(udisk_cwd);
}

static void udisk_u32_to_str(char *buf, uint32_t v)
{
    char tmp[12];
    uint32_t j = 0, i = 0;
    do {
        tmp[j++] = '0' + (char)(v % 10u);
        v /= 10u;
    } while (v && j < 11);
    while (j) buf[i++] = tmp[--j];
    buf[i] = 0;
}

static void udisk_fmt_size(char *buf, uint32_t bytes)
{
    if (bytes < 1024u) {
        udisk_u32_to_str(buf, bytes);
        uint32_t i = 0;
        while (buf[i]) i++;
        buf[i++] = 'B'; buf[i] = 0;
    } else if (bytes < 1024u * 1024u) {
        udisk_u32_to_str(buf, bytes / 1024u);
        uint32_t i = 0;
        while (buf[i]) i++;
        buf[i++] = 'K'; buf[i] = 0;
    } else {
        udisk_u32_to_str(buf, bytes / (1024u * 1024u));
        uint32_t i = 0;
        while (buf[i]) i++;
        buf[i++] = 'M'; buf[i] = 0;
    }
}
