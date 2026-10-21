/* SPDX-License-Identifier: GPL-3.0-only */
/* Felucca-styled shell: page navigation and rendering pipeline.
 * Same structure as core-fm1 shell but draws with Felucca's visual language. */

static const cfm1_page_t *fsh_pages[CFM1_MAX_PAGES];
static uint32_t fsh_npage;
static uint32_t fsh_cur;
static uint32_t fsh_dirty;
static uint32_t fsh_splash_frames;
static void fsh_splash_render(void);

static void fsh_register(const cfm1_page_t *page)
{
    if (fsh_npage < CFM1_MAX_PAGES)
        fsh_pages[fsh_npage++] = page;
}

static void fsh_set_page(uint32_t idx)
{
    if (idx >= fsh_npage)
        return;
    if (fsh_cur < fsh_npage && fsh_pages[fsh_cur]->leave)
        fsh_pages[fsh_cur]->leave();
    fsh_cur = idx;
    fsh_dirty = 1;
    if (fsh_pages[fsh_cur]->enter)
        fsh_pages[fsh_cur]->enter();
}

static void fsh_next(void)
{
    if (fsh_npage > 1)
        fsh_set_page((fsh_cur + 1) % fsh_npage);
}

static void fsh_prev(void)
{
    if (fsh_npage > 1)
        fsh_set_page((fsh_cur + fsh_npage - 1) % fsh_npage);
}

static void fsh_draw(void)
{
    if (fsh_splash_frames) {
        fsh_splash_render();
        fsh_splash_frames--;
        if (!fsh_splash_frames)
            fsh_dirty = 1;
        return;
    }

    const cfm1_page_t *p = fsh_npage ? fsh_pages[fsh_cur] : 0;
    if (!p)
        return;

    if (fsh_dirty) {
        lcd_fill(0, 0, 240, 240, T_BG);
        fsh_dirty = 0;
    }

    if (p->draw)
        p->draw();
}

static void fsh_knob(uint32_t id, int32_t clicks)
{
    const cfm1_page_t *p = fsh_npage ? fsh_pages[fsh_cur] : 0;
    if (p && p->knob && p->knob(id, clicks))
        return;
    if (id == 0) {
        if (clicks > 0) fsh_next();
        else if (clicks < 0) fsh_prev();
    }
}

static uint32_t fsh_btn_held;
static uint32_t fsh_btn_us;

static uint32_t fsh_find_page(const char *name)
{
    uint32_t i;
    for (i = 0; i < fsh_npage; i++) {
        const char *a = fsh_pages[i]->name, *b = name;
        while (*a && *a == *b) { a++; b++; }
        if (!*a && !*b) return i;
    }
    return fsh_cur;
}

static void fsh_button(uint32_t id, uint32_t down)
{
    if (down) {
        fsh_btn_held = id + 1;
        fsh_btn_us = fm1_micros();
    }
    if (!down && fsh_btn_held == id + 1) {
        uint32_t held_us = fm1_micros() - fsh_btn_us;
        fsh_btn_held = 0;
        if (id == 8 && held_us > 500000u) {
            fsh_set_page(fsh_find_page("ABOUT"));
            return;
        }
    }
    const cfm1_page_t *p = fsh_npage ? fsh_pages[fsh_cur] : 0;
    if (p && p->button && p->button(id, down))
        return;
    if (!down) {
        switch (id) {
        case 8:  fsh_set_page(fsh_find_page("HOME"));  break;
        case 2:  fsh_set_page(fsh_find_page("FX"));    break;
        case 3:  fsh_set_page(fsh_find_page("SCL"));   break;
        case 4:  fsh_set_page(fsh_find_page("ENV"));   break;
        case 5:  fsh_set_page(fsh_find_page("LFO"));   break;
        case 6:  fsh_set_page(fsh_find_page("EDIT"));  break;
        case 10: fsh_set_page(fsh_find_page("ARP"));   break;
        case 11: fsh_set_page(fsh_find_page("SEQ"));   break;
        }
    }
}

static void fsh_key(uint32_t note, uint32_t vel)
{
    const cfm1_page_t *p = fsh_npage ? fsh_pages[fsh_cur] : 0;
    if (p && p->key)
        p->key(note, vel);
}

static void fsh_init(void)
{
    fsh_npage = 0;
    fsh_cur = 0;
    fsh_dirty = 1;
    palette_set(0);
#ifndef CONFIG_AUDIO
    cfm1_mock_init();
#endif
}

#ifdef CONFIG_SPLASH_LOGO

static void fsh_splash_render(void)
{
    uint32_t logo_y = (240u - (uint32_t)BOOT_LOGO_IMG_H) / 2u;
    uint32_t ver_y = logo_y + (uint32_t)BOOT_LOGO_IMG_H + 12u;
    uint32_t band_h = (uint32_t)(-AF_S_CAP_Y + AF_S.h);

    lcd_fill(0, 0, 240, 240, 0x0000);
    logo_draw_content(0, logo_y);

    cv_begin(240, band_h, 0x0000);
    cv_text_on(16, -AF_S_CAP_Y, &AF_S, CONFIG_FIRMWARE_VERSION, 0x8410, 0x0000);
    cv_blit(0, ver_y);

    lcd_sync();
}

#else

static void fsh_splash_render(void)
{
    lcd_fill(0, 0, 240, 240, T_BG);
    lcd_sync();
}

#endif

static void fsh_splash(void)
{
#ifdef CONFIG_SPLASH_NONE
    (void)0;
#else
    fsh_splash_render();
    fsh_splash_frames = 10;
#endif
}

static void fsh_boot(void)
{
    uint32_t i;
    for (i = 0; i < fsh_npage; i++) {
        if (fsh_pages[i]->flags & PAGEF_HOME) {
            fsh_set_page(i);
            return;
        }
    }
    if (fsh_npage)
        fsh_set_page(0);
}
