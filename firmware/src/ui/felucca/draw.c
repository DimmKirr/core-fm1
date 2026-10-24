/* SPDX-License-Identifier: GPL-3.0-only */
/* Felucca-styled drawing primitives: header, cards, footer.
 * Matches the real Felucca layout using the upstream gfx.c canvas API. */

/* format an int16 into a decimal string */
static void fui_fmt_val(char *buf, int16_t v)
{
    int neg = v < 0;
    uint32_t i = 0;
    char tmp[8];
    uint32_t j = 0;
    if (neg) v = -v;
    do {
        tmp[j++] = '0' + (char)(v % 10);
        v /= 10;
    } while (v && j < 7);
    if (neg) buf[i++] = '-';
    while (j) buf[i++] = tmp[--j];
    buf[i] = 0;
}

/* format a time value: 0-99 as "X.X", 100+ as integer */
static void fui_fmt_time(char *buf, int16_t v)
{
    if (v < 100 && v >= 0) {
        buf[0] = '0' + (char)(v / 10);
        buf[1] = '.';
        buf[2] = '0' + (char)(v % 10);
        buf[3] = 0;
    } else {
        fui_fmt_val(buf, v);
    }
}

/* header: track cushion | stop/play icon | BPM | battery area (right) */
static void fui_draw_head(void)
{
    char bpm[8];
    cv_begin(240, H_HEAD, T_BG);

    /* track cushion (left, x=4) */
    cv_trk(4, 4, 16, song.sel, T_ACCENT, T_BG);

    /* transport icon: stop or play (after cushion) */
    cv_icon_mid(28, H_HEAD / 2, 16,
                song.playing ? ICON_X_PLAY : ICON_X_STOP,
                song.playing ? T_THEME : T_MID, T_BG);

    /* BPM (centred) */
    fui_fmt_val(bpm, (int16_t)song.bpm);
    cv_text_in(0, CAP_IN(M, H_HEAD), 240, &AF_M, bpm, T_THEME, T_BG);

    /* battery placeholder (right) */
    cv_icon_mid(220, H_HEAD / 2, 24, ICON_X_BAT4, T_THEME, T_BG);

    cv_blit(0, 0);
}

/* draw a single parameter card at column c (0..3)
 * matches real Felucca: icon + label (S, top), value (M) + unit (S, middle), gauge (bottom) */
static void fui_draw_card(uint32_t c, const char *label, const char *val,
                          const char *unit, uint16_t val_color, int32_t ratio,
                          uint32_t icon)
{
    int32_t x;
    cv_begin(CARD_W, CARD_H, T_BG);
    cv_rrect(0, 0, CARD_W, CARD_H, 4, T_SURF, T_BG);

    /* label row (top, S font): [icon] LABEL */
    if (label[0]) {
        int32_t lx = 5;
        if (FELUCCA_ICONS && icon != ICON_NONE && icon < ICON_COUNT) {
            int32_t cy = 3 + AF_S_CAP_Y, chh = AF_S_CAP_H;
            lx += cv_icon_in(5, cy, 0, chh, ICON_CELL, icon, T_MID, T_SURF) + ICON_GAP;
        }
        cv_text_fit(lx, 3, &AF_S, label, T_MID, T_SURF, CARD_W - 2 - lx);
    }

    /* value row (M font) + unit (S font, after value on same baseline) */
    x = 5;
    if (val[0]) {
        const aafont_t *vf = &AF_M;
        int32_t uw = (unit && unit[0]) ? text_w(&AF_S, unit) + 3 : 0;
        if (text_w(vf, val) + uw > CARD_W - 8)
            vf = &AF_S;
        x = cv_text_fit(5, 17, vf, val, val_color, T_SURF, CARD_W - 8 - uw);
        if (unit && unit[0])
            cv_text_on(x + 3, 20, &AF_S, unit, T_MID, T_SURF);
    }

    /* gauge bar (bottom) */
    if (ratio >= 0) {
        int32_t gw = CARD_W - 10;
        int32_t fx = ratio * gw / 1000;
        cv_rrect(5, 38, gw, 3, 1, ux.style ? T_LINE : T_BG, T_SURF);
        cv_rrect(5, 38, fx < 3 ? 3 : fx, 3, 1, T_THEME, T_BG);
    }

    cv_blit((uint32_t)CARD_X(c), Y_CARDS);
}

/* draw the graph area: clear it and let the page draw into it */
static void fui_clear_graph(void)
{
    cv_begin(240, Y_FOOT - Y_GRAPH, T_BG);
    cv_blit(0, Y_GRAPH);
}

/* footer row 2: engine icon + engine name (left), preset name (mid), page title (right)
 * matches real Felucca's bottom row */
static void fui_draw_foot(const char *eng_name, const char *preset,
                          const char *page_title)
{
    int32_t x, tx;
    cv_begin(240, H_FOOT, T_BG);

    /* row 1: step bar placeholder (top of footer) */
    {
        const track_t *t = &trk[song.sel];
        uint32_t i;
        for (i = 0; i < 16u; i++) {
            int32_t sx = 12 + (int32_t)i * 14;
            if (i > 0 && (i % 4) == 0) sx += 2;
            if (t->step[i].note[0])
                cv_rrect(sx, 1, 9, 11, 2, T_THEME, T_BG);
            else
                cv_rrect(sx, 9, 9, 3, 1, T_RAISE, T_BG);
        }
    }

    /* row 2: engine icon + name (left), preset (mid), page title (right) */
    x = 8;
    if (FELUCCA_ICONS) {
        uint32_t ei = engine_icon(eng_name);
        if (ei != ICON_NONE)
            x += cv_icon_in(x, 19 + AF_S_CAP_Y, 0, AF_S_CAP_H, 12, ei, T_MID, T_BG) + 5;
    }
    x = cv_text_fit(x, 19, &AF_S, eng_name, T_THEME, T_BG, 80);

    if (preset && preset[0]) {
        tx = 232 - text_w(&AF_S, page_title);
        cv_free_text(x + 10, 19, &AF_S, preset, T_TEXT, T_BG, tx - 12 - (x + 10));
    }

    /* page title (right-aligned) */
    {
        int32_t tw = text_w(&AF_S, page_title);
        cv_text_on(232 - tw, 19, &AF_S, page_title, T_MID, T_BG);
    }

    cv_blit(0, Y_FOOT);
}
