/* SPDX-License-Identifier: GPL-3.0-only */
/* Felucca-styled shell: rendering pipeline and input routing.
 * Navigation state lives in ui_nav.c (view-based). */

static void fsh_splash_render(void);

/* Fill only the BG gaps between layout regions, matching upstream Felucca's
 * draw_frame(): header, cards, graph, and footer cover the rest. */
static void fsh_draw_frame(void)
{
    /* gap between header and cards row */
    lcd_fill(0, H_HEAD, 240, Y_CARDS - H_HEAD, T_BG);
    /* gaps beside the four card columns */
    {
        uint32_t i;
        lcd_fill(0, Y_CARDS, (uint32_t)CARD_X(0), CARD_H, T_BG);
        for (i = 0; i < 4u; i++) {
            uint32_t x = (uint32_t)(CARD_X(i) + CARD_W);
            uint32_t w = i < 3u ? (uint32_t)CARD_X(i + 1u) - x : 240u - x;
            lcd_fill(x, Y_CARDS, w, CARD_H, T_BG);
        }
    }
    /* gap between cards and graph */
    lcd_fill(0, Y_CARDS + CARD_H, 240, Y_GRAPH - (Y_CARDS + CARD_H), T_BG);
    /* gap between graph and footer */
    lcd_fill(0, Y_FOOT, 240, 0, T_BG);
}

static void fsh_draw(void)
{
    const cfm1_page_t *p = nav_active();
    if (!p)
        return;

    if (p->status)
        p->status();

    if (!nav_dirty)
        return;

    fsh_draw_frame();
    if (p->draw)
        p->draw();
    if (nav_modal && nav_modal->draw)
        nav_modal->draw();
    nav_dirty = 0;
}

static void fsh_knob(uint32_t id, int32_t clicks)
{
    if (nav_modal && nav_modal->knob && nav_modal->knob(id, clicks))
        return;
    const cfm1_page_t *p = nav_active();
    if (p && p->knob && p->knob(id, clicks))
        return;
    if (id == ENC_SELECT && cfm1_controls.select_default == CTL_SELECT_PAGE) {
        if (clicks > 0) nav_cycle(1);
        else if (clicks < 0) nav_cycle(-1);
    }
}

static uint32_t fsh_btn_held;
static uint32_t fsh_btn_us;

static void fsh_button(uint32_t id, uint32_t down)
{
    if (id >= BTN_COUNT) return;
    if (nav_modal && nav_modal->button && nav_modal->button(id, down))
        return;
    if (down) {
        fsh_btn_held = id + 1;
        fsh_btn_us = fm1_micros();
    }
    if (!down && fsh_btn_held == id + 1) {
        uint32_t held_us = fm1_micros() - fsh_btn_us;
        fsh_btn_held = 0;
        if (held_us > cfm1_controls.hold_us &&
            cfm1_controls.btn_hold[id] != VIEW_NONE) {
            nav_open_view(cfm1_controls.btn_hold[id]);
            return;
        }
    }
    const cfm1_page_t *p = nav_active();
    if (p && p->button && p->button(id, down))
        return;
    if (!down && cfm1_controls.btn_tap[id] != VIEW_NONE) {
        nav_open_view(cfm1_controls.btn_tap[id]);
    }
}

static void fsh_key(uint32_t note, uint32_t vel)
{
    if (nav_modal && nav_modal->key && nav_modal->key(note, vel))
        return;
    const cfm1_page_t *p = nav_active();
    if (p && p->key)
        p->key(note, vel);
}

static void fsh_init(void)
{
    nav_init();
    ui_set_theme(&felucca_theme);
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
    uint32_t ms;
    fsh_splash_render();
    for (ms = 0; ms < 400u; ms += 10u)
        fm1_delay_ms(10);
#endif
}

static void fsh_boot(void)
{
    nav_boot();
}
