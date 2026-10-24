/* SPDX-License-Identifier: GPL-3.0-only */
/* FILES page: USB disk file browser for Felucca shell.
 * Shows directory listing with scroll, folder navigation, file sizes. */

#define FILES_VISIBLE   7
#define FILES_ROW_H     18
#define FILES_Y_START   (Y_CARDS + 2)
#define FILES_X_PAD     8

static void ffiles_draw_list(void)
{
    uint32_t i;
    int32_t y = 0;

    cv_begin(240, FILES_VISIBLE * FILES_ROW_H + 4, T_BG);

    if (udisk_nentry == 0) {
        cv_text_in(0, FILES_ROW_H, 240, &AF_S, "(empty)", T_DIM, T_BG);
    }

    for (i = 0; i < FILES_VISIBLE && (udisk_scroll + i) < udisk_nentry; i++) {
        uint32_t idx = udisk_scroll + i;
        const udisk_entry_t *e = &udisk_entries[idx];
        int32_t row_y = y + (int32_t)i * FILES_ROW_H;
        uint16_t fg = (idx == udisk_cursor) ? T_THEME : T_TEXT;
        uint16_t bg = (idx == udisk_cursor) ? T_SURF : T_BG;

        if (idx == udisk_cursor)
            cv_rrect(2, row_y, 236, FILES_ROW_H - 2, 3, T_SURF, T_BG);

        if (e->is_dir) {
            cv_text_fit(FILES_X_PAD, row_y + 3, &AF_S, "/", T_ACCENT, bg, 8);
            cv_text_fit(FILES_X_PAD + 8, row_y + 3, &AF_S, e->name, fg, bg, 170);
        } else {
            cv_text_fit(FILES_X_PAD, row_y + 3, &AF_S, e->name, fg, bg, 170);
            char sz[12];
            udisk_fmt_size(sz, e->size);
            int32_t sw = text_w(&AF_S, sz);
            cv_text_on(232 - sw, row_y + 3, &AF_S, sz, T_DIM, bg);
        }
    }

    cv_blit(0, FILES_Y_START);
}

static void ffiles_draw(void)
{
    fui_draw_head();

    /* path bar: 4 cards area used as a single banner */
    cv_begin(240, CARD_H, T_BG);
    if (!udisk_is_mounted()) {
        cv_text_in(0, 10, 240, &AF_M, "No USB Disk", T_DIM, T_BG);
        {
            const char *st;
            switch (uhost.state) {
            case UHOST_DISCONNECTED: st = "USB:DISCONNECTED"; break;
            case UHOST_ATTACHED:     st = "USB:ATTACHED";     break;
            case UHOST_ENUMERATED:   st = "USB:ENUMERATED";   break;
            case UHOST_MSC_READY:    st = "USB:MSC READY";    break;
            case UHOST_ERROR: {
                static char errbuf[16];
                int8_t e = uhost.last_err;
                char *p = errbuf;
                *p++ = 'U'; *p++ = 'S'; *p++ = 'B'; *p++ = ':';
                *p++ = 'E'; *p++ = 'R'; *p++ = 'R'; *p++ = ' ';
                if (e <= 0) { *p++ = '-'; e = -e; }
                if (e >= 10) { *p++ = (char)('0' + e / 10); }
                *p++ = (char)('0' + e % 10);
                *p = 0;
                st = errbuf;
            } break;
            default:                 st = "USB:???";           break;
            }
            cv_text_in(0, 28, 240, &AF_S, st, T_ACCENT, T_BG);
            cv_text_in(0, 42, 240, &AF_S,
                       udisk_active ? "OTG:HOST" : "OTG:DEVICE", T_MID, T_BG);
        }
    } else {
        cv_text_fit(FILES_X_PAD, 4, &AF_S, "USB DISK", T_ACCENT, T_BG, 80);
        cv_text_fit(FILES_X_PAD, 20, &AF_S, udisk_cwd, T_TEXT, T_BG, 224);
        char info[16];
        fui_fmt_val(info, (int16_t)udisk_nentry);
        uint32_t n = 0;
        while (info[n]) n++;
        info[n++] = ' '; info[n++] = 'i'; info[n++] = 't'; info[n++] = 'e';
        info[n++] = 'm'; info[n++] = 's'; info[n] = 0;
        int32_t iw = text_w(&AF_S, info);
        cv_text_on(232 - iw, 4, &AF_S, info, T_MID, T_BG);
    }
    cv_blit(0, Y_CARDS);

    if (udisk_is_mounted())
        ffiles_draw_list();
    else
        fui_clear_graph();

    {
        const char *mid = "---";
        if (udisk_is_mounted()) mid = "FAT32";
        else if (uhost.state == UHOST_ERROR) mid = "ERR";
        else if (uhost.state == UHOST_ATTACHED) mid = "WAIT";
        else if (udisk_active) mid = "SCAN";
        fui_draw_foot("USB", mid, "FILES");
    }
}

static int ffiles_knob(uint32_t id, int32_t clicks)
{
    if (!udisk_is_mounted()) return 0;
    if (id == 0) {
        /* master knob: scroll through entries */
        int32_t c = (int32_t)udisk_cursor + clicks;
        if (c < 0) c = 0;
        if (c >= (int32_t)udisk_nentry) c = (int32_t)udisk_nentry - 1;
        if (c < 0) c = 0;
        udisk_cursor = (uint32_t)c;
        if (udisk_cursor < udisk_scroll)
            udisk_scroll = udisk_cursor;
        if (udisk_cursor >= udisk_scroll + FILES_VISIBLE)
            udisk_scroll = udisk_cursor - FILES_VISIBLE + 1;
        fsh_dirty = 1;
        return 1;
    }
    return 0;
}

static int ffiles_button(uint32_t id, uint32_t down)
{
    if (!udisk_is_mounted()) return 0;
    if (down) return 0;

    if (id == 9) {
        /* ENTER: open directory */
        if (udisk_cursor < udisk_nentry) {
            const udisk_entry_t *e = &udisk_entries[udisk_cursor];
            if (e->is_dir) {
                udisk_chdir(e->name);
                fsh_dirty = 1;
                return 1;
            }
        }
    }
    if (id == 7) {
        /* BACK: go up one level */
        if (udisk_go_up() == 0)
            fsh_dirty = 1;
        return 1;
    }
    return 0;
}

static void ffiles_enter(void)
{
    udisk_activate();
    if (udisk_is_mounted())
        udisk_opendir(udisk_cwd);
}

static void ffiles_leave(void)
{
    udisk_deactivate();
}

static const cfm1_page_t FPAGE_FILES = {
    .name   = "FILES",
    .title  = "FILES",
    .flags  = PAGEF_SYSTEM,
    .enter  = ffiles_enter,
    .leave  = ffiles_leave,
    .draw   = ffiles_draw,
    .knob   = ffiles_knob,
    .button = ffiles_button,
};
