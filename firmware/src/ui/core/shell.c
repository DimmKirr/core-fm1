/* SPDX-License-Identifier: GPL-3.0-only */
/* core-fm1 UI shell: minimal fallback (bitmap font, no theming).
 * Navigation uses the universal ui_nav.c module. */

/* -------------------------------------------------------- status bar --- */
/* 240 x 16 strip at the top: page name left, view info right. */

static void shell_draw_status(void)
{
    const cfm1_page_t *p = nav_active();
    if (!p)
        return;

    lcd_fill(0, 0, 240, 16, 0x0000);

    /* page name, left */
    boot_text_1x(2, p->title ? p->title : p->name, 0x8410, 0x0000);

    /* page position indicator: "2/5" right-aligned */
    if (nav_npage > 1) {
        char buf[8];
        uint32_t i = 0;
        buf[i++] = '0' + (char)((nav_cur + 1) / 10);
        if (buf[0] == '0') i = 0;
        buf[i++] = '0' + (char)((nav_cur + 1) % 10);
        buf[i++] = '/';
        buf[i++] = '0' + (char)(nav_npage / 10);
        if (buf[i - 1] == '0') i--;
        buf[i++] = '0' + (char)(nav_npage % 10);
        buf[i] = 0;
        uint32_t w = boot_text_width_1x(buf);
        boot_text_1x(2 + 240 - 4 - w, buf, 0x8410, 0x0000);
    }

    /* let the page add its own status (optional) */
    if (p->status)
        p->status();
}

/* ----------------------------------------------------------- drawing --- */

static void shell_draw(void)
{
    const cfm1_page_t *p = nav_active();
    if (!p)
        return;

    if (p->status)
        p->status();

    if (!nav_dirty)
        return;

    lcd_fill(0, 0, 240, 240, 0x0000);
    shell_draw_status();
    if (p->draw)
        p->draw();
    if (nav_modal && nav_modal->draw)
        nav_modal->draw();
    nav_dirty = 0;
}

/* ------------------------------------------------------------- input --- */

static void shell_knob(uint32_t id, int32_t clicks)
{
    if (nav_modal && nav_modal->knob && nav_modal->knob(id, clicks))
        return;
    const cfm1_page_t *p = nav_active();
    if (p && p->knob && p->knob(id, clicks))
        return;
    if (id == 0) {
        if (clicks > 0) nav_cycle(1);
        else if (clicks < 0) nav_cycle(-1);
    }
}

static void shell_button(uint32_t id, uint32_t down)
{
    if (nav_modal && nav_modal->button && nav_modal->button(id, down))
        return;
    const cfm1_page_t *p = nav_active();
    if (p && p->button && p->button(id, down))
        return;
    if (!down && id < BTN_COUNT && cfm1_controls.btn_tap[id] != VIEW_NONE) {
        nav_open_view(cfm1_controls.btn_tap[id]);
    }
}

static void shell_key(uint32_t note, uint32_t vel)
{
    if (nav_modal && nav_modal->key && nav_modal->key(note, vel))
        return;
    const cfm1_page_t *p = nav_active();
    if (p && p->key)
        p->key(note, vel);
}

/* -------------------------------------------------------------- init --- */

static void shell_init(void)
{
    nav_init();
}

static void shell_boot(void)
{
    nav_boot();
}
