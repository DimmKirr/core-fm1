/* SPDX-License-Identifier: GPL-3.0-only */
/* DEMO page: kitchen-sink showcase of UI elements.
 * Long-FX opens this view. SELECT switches demo screens,
 * K1-K4 adjust each screen's card values (Felucca knob pattern). */

#define FDEMO_SCREENS  5
#define FDEMO_ROW_H   16
#define FDEMO_X_PAD    6

static char fdemo_text[32] = "Hello FM-1!";

static struct {
    uint8_t  screen;
    uint8_t  val[4];
    uint8_t  scroll;
    uint32_t frame;
    uint8_t  last_note;
    uint8_t  last_vel;
} fdemo;

static const char *FDEMO_NAMES[] = {
    "CARDS", "LIST", "GRAPH", "TEXT", "EDIT"
};

static void fdemo_kbd_done(const char *buf, uint8_t ok)
{
    if (ok) {
        uint8_t i = 0;
        while (buf[i] && i < 31u) { fdemo_text[i] = buf[i]; i++; }
        fdemo_text[i] = 0;
    }
    nav_dirty = 1;
}

/* --- Screen 0: Cards demo --- */

static void fdemo_draw_cards(void)
{
    char fr[8], sc[4], v0[4];
    fui_fmt_val(fr, (int16_t)(fdemo.frame & 0x7FFF));
    sc[0] = '0' + fdemo.screen;
    sc[1] = '/';
    sc[2] = '0' + FDEMO_SCREENS - 1;
    sc[3] = 0;
    fui_fmt_val(v0, (int16_t)fdemo.val[0]);

    ui_card(0, "FRA", fr, "",
            T_THEME, (int32_t)(fdemo.frame % 1000), ICON_GENERIC);
    ui_card(1, "SCR", sc, "",
            T_ACCENT, (int32_t)fdemo.screen * 250, ICON_GENERIC);
    ui_card(2, "K1", v0, "",
            T_MID, (int32_t)fdemo.val[0] * 100, ICON_GENERIC);
    {
        char note[4];
        note[0] = '0' + fdemo.last_note / 100;
        note[1] = '0' + (fdemo.last_note / 10) % 10;
        note[2] = '0' + fdemo.last_note % 10;
        note[3] = 0;
        if (fdemo.last_note < 100) { note[0] = note[1]; note[1] = note[2]; note[2] = 0; }
        if (fdemo.last_note < 10) { note[0] = note[1]; note[1] = 0; }
        ui_card(3, "NOTE", note, "",
                fdemo.last_vel ? T_THEME : T_DIM,
                fdemo.last_vel ? (int32_t)fdemo.last_vel * 8 : -1,
                ICON_GENERIC);
    }

    /* graph area: color swatches for all theme tokens */
    {
        uint32_t h = Y_FOOT - Y_GRAPH;
        cv_begin(240, h, T_BG);

        cv_rect(6, 4, 24, 12, T_BG);
        cv_text_on(32, 6, &AF_S, "BG", T_DIM, T_BG);

        cv_rect(6, 20, 24, 12, T_SURF);
        cv_text_on(32, 22, &AF_S, "SURF", T_DIM, T_BG);

        cv_rect(6, 36, 24, 12, T_THEME);
        cv_text_on(32, 38, &AF_S, "THEME", T_TEXT, T_BG);

        cv_rect(6, 52, 24, 12, T_ACCENT);
        cv_text_on(32, 54, &AF_S, "ACCENT", T_TEXT, T_BG);

        cv_rect(120, 4, 24, 12, T_TEXT);
        cv_text_on(148, 6, &AF_S, "TEXT", T_DIM, T_BG);

        cv_rect(120, 20, 24, 12, T_MID);
        cv_text_on(148, 22, &AF_S, "MID", T_DIM, T_BG);

        cv_rect(120, 36, 24, 12, T_DIM);
        cv_text_on(148, 38, &AF_S, "DIM", T_TEXT, T_BG);

        cv_rect(120, 52, 24, 12, T_LINE);
        cv_text_on(148, 54, &AF_S, "LINE", T_TEXT, T_BG);

        cv_rect(6, 70, 24, 12, T_RAISE);
        cv_text_on(32, 72, &AF_S, "RAISE", T_TEXT, T_BG);

        cv_rect(120, 70, 24, 12, T_INK);
        cv_text_on(148, 72, &AF_S, "INK", T_DIM, T_BG);

        /* spinner */
        fui_draw_spinner(210, 44, 12, fdemo.frame, T_THEME, T_BG);

        cv_blit(0, Y_GRAPH);
    }
}

/* --- Screen 1: List/scrollable rows --- */

#define FDEMO_LIST_N  16
static const char *FDEMO_LIST[] = {
    "Oscillator", "Filter", "Envelope", "LFO",
    "Reverb", "Delay", "Chorus", "Distortion",
    "Sequencer", "Arpeggiator", "Mixer", "Compressor",
    "Phaser", "Flanger", "EQ", "Limiter"
};

static void fdemo_draw_list(void)
{
    uint32_t i;
    uint32_t visible = (Y_FOOT - Y_GRAPH) / FDEMO_ROW_H;

    /* cards: item count, selected, scroll offset, visible */
    {
        char n[4], s[4], o[4], v[4];
        fui_fmt_val(n, FDEMO_LIST_N);
        fui_fmt_val(s, (int16_t)fdemo.val[0]);
        fui_fmt_val(o, (int16_t)fdemo.scroll);
        fui_fmt_val(v, (int16_t)visible);
        ui_card(0, "ITEMS", n, "", T_MID, -1, ICON_GENERIC);
        ui_card(1, "SEL", s, "", T_THEME, (int32_t)fdemo.val[0] * 1000 / FDEMO_LIST_N, ICON_GENERIC);
        ui_card(2, "SCROLL", o, "", T_DIM, -1, ICON_GENERIC);
        ui_card(3, "VISIBLE", v, "", T_DIM, -1, ICON_GENERIC);
    }

    cv_begin(240, visible * FDEMO_ROW_H, T_BG);
    for (i = 0; i < visible && (i + fdemo.scroll) < FDEMO_LIST_N; i++) {
        uint32_t idx = i + fdemo.scroll;
        int32_t y = (int32_t)i * FDEMO_ROW_H;
        uint8_t selected = (idx == fdemo.val[0]);
        uint16_t bg = selected ? T_SURF : T_BG;
        uint16_t fg = selected ? T_THEME : T_TEXT;

        if (selected)
            cv_rrect(2, y, 236, FDEMO_ROW_H, 3, T_SURF, T_BG);

        /* row number */
        {
            char num[4];
            num[0] = '0' + (char)(idx / 10);
            num[1] = '0' + (char)(idx % 10);
            num[2] = 0;
            if (idx < 10) { num[0] = ' '; }
            cv_text_on(FDEMO_X_PAD, y + 2, &AF_S, num, T_DIM, bg);
        }

        cv_text_fit(FDEMO_X_PAD + 20, y + 2, &AF_S,
                    FDEMO_LIST[idx], fg, bg, 180);

        /* right: index as bar */
        {
            int32_t bw = (int32_t)idx * 40 / FDEMO_LIST_N;
            cv_rrect(200, y + 5, bw < 2 ? 2 : bw, 6, 2, fg, bg);
        }
    }
    cv_blit(0, Y_GRAPH);
}

/* --- Screen 2: Graph/bars demo --- */

static void fdemo_draw_graph(void)
{
    uint32_t i;
    uint32_t h = Y_FOOT - Y_GRAPH;
    uint32_t bar_w = 12;
    uint32_t gap = 3;
    uint32_t n_bars = 16;
    int32_t base_x = (int32_t)(240 - n_bars * (bar_w + gap)) / 2;

    ui_card(0, "BARS", "16", "", T_MID, -1, ICON_GENERIC);
    ui_card(1, "FRAME", "", "", T_DIM, (int32_t)(fdemo.frame % 1000), ICON_GENERIC);
    ui_card(2, "ANIM", "ON", "", T_THEME, -1, ICON_GENERIC);
    ui_card(3, "STYLE", "BAR", "", T_DIM, -1, ICON_GENERIC);

    cv_begin(240, h, T_BG);

    /* animated bar chart */
    for (i = 0; i < n_bars; i++) {
        /* pseudo-random animated heights using frame + index */
        uint32_t seed = (fdemo.frame / 3 + i * 37) & 0xFF;
        int32_t bar_h = (int32_t)((seed * (h - 8)) >> 8);
        if (bar_h < 4) bar_h = 4;
        int32_t x = base_x + (int32_t)i * (int32_t)(bar_w + gap);
        int32_t y = (int32_t)h - bar_h - 2;
        uint16_t c = (i == (fdemo.frame / 4) % n_bars) ? T_ACCENT : T_THEME;
        cv_rrect(x, y, (int32_t)bar_w, bar_h, 2, c, T_BG);
    }

    /* baseline */
    cv_rect(base_x - 2, (int32_t)h - 2, (int32_t)(n_bars * (bar_w + gap)) + 4, 1, T_LINE);

    cv_blit(0, Y_GRAPH);
}

/* --- Screen 3: Text/font demo --- */

static void fdemo_draw_text(void)
{
    uint32_t h = Y_FOOT - Y_GRAPH;

    ui_card(0, "FONT", "S", "", T_MID, -1, ICON_GENERIC);
    ui_card(1, "FONT", "M", "", T_MID, -1, ICON_GENERIC);
    ui_card(2, "FONT", "L", "", T_MID, -1, ICON_GENERIC);
    ui_card(3, "DEMO", "TXT", "", T_DIM, -1, ICON_GENERIC);

    cv_begin(240, h, T_BG);

    cv_text_on(6, 4, &AF_S, "AF_S: The quick brown fox", T_TEXT, T_BG);
    cv_text_on(6, 18, &AF_M, "AF_M: ABCDEFGH", T_TEXT, T_BG);
    cv_text_on(6, 38, &AF_L, "AF_L: 12345", T_TEXT, T_BG);

    /* text alignment demos */
    cv_text_in(0, 64, 240, &AF_S, "CENTERED TEXT", T_THEME, T_BG);
    {
        int32_t tw = text_w(&AF_S, "RIGHT ALIGNED");
        cv_text_on(234 - tw, 80, &AF_S, "RIGHT ALIGNED", T_ACCENT, T_BG);
    }
    cv_text_on(6, 80, &AF_S, "LEFT", T_MID, T_BG);

    /* rounded rect showcase */
    cv_rrect(6, 96, 60, 16, 4, T_SURF, T_BG);
    cv_text_in(6, 100, 60, &AF_S, "RRECT4", T_TEXT, T_SURF);

    cv_rrect(72, 96, 60, 16, 8, T_THEME, T_BG);
    cv_text_in(72, 100, 60, &AF_S, "RRECT8", T_INK, T_THEME);

    cv_rrect(138, 96, 60, 16, 2, T_ACCENT, T_BG);
    cv_text_in(138, 100, 60, &AF_S, "RRECT2", T_INK, T_ACCENT);

    cv_blit(0, Y_GRAPH);
}

/* --- Screen 4: Editable text --- */

static void fdemo_draw_edit(void)
{
    uint32_t h = Y_FOOT - Y_GRAPH;
    uint8_t tl = 0;
    while (fdemo_text[tl]) tl++;

    {
        char ln[4];
        fui_fmt_val(ln, (int16_t)tl);
        ui_card(0, "EDIT", "TXT", "", T_THEME, -1, ICON_GENERIC);
        ui_card(1, "LEN", ln, "", T_MID, (int32_t)tl * 1000 / 31, ICON_GENERIC);
        ui_card(2, "MAX", "31", "", T_DIM, -1, ICON_GENERIC);
        ui_card(3, "OCT+", "GO", "", T_ACCENT, -1, ICON_GENERIC);
    }

    cv_begin(240, h, T_BG);

    cv_text_on(6, 4, &AF_S, "EDITABLE TEXT", T_DIM, T_BG);

    cv_rrect(4, 20, 232, 28, 4, T_SURF, T_BG);
    cv_text_fit(10, 28, &AF_M, fdemo_text, T_TEXT, T_SURF, 220);

    cv_rrect(60, 70, 120, 20, 6, T_THEME, T_BG);
    cv_text_in(60, 74, 120, &AF_S, "OCT+ TO EDIT", T_INK, T_THEME);

    cv_blit(0, Y_GRAPH);
}

/* --- Main draw dispatch --- */

static void fdemo_draw(void)
{
    ui_header();

    switch (fdemo.screen) {
    case 0: fdemo_draw_cards(); break;
    case 1: fdemo_draw_list();  break;
    case 2: fdemo_draw_graph(); break;
    case 3: fdemo_draw_text();  break;
    case 4: fdemo_draw_edit();  break;
    default: fdemo_draw_cards(); break;
    }

    fdemo.frame++;

    ui_footer("DEMO", FDEMO_NAMES[fdemo.screen], "DEMO");
}

static void fdemo_enter(void)
{
    fdemo.screen = 0;
    fdemo.val[0] = fdemo.val[1] = fdemo.val[2] = fdemo.val[3] = 0;
    fdemo.scroll = 0;
    fdemo.frame = 0;
    fdemo.last_note = 0;
    fdemo.last_vel = 0;
}

static int fdemo_knob(uint32_t id, int32_t clicks)
{
    if (id == ENC_SELECT) {
        int32_t s = (int32_t)fdemo.screen + clicks;
        if (s < 0) s = FDEMO_SCREENS - 1;
        if (s >= FDEMO_SCREENS) s = 0;
        fdemo.screen = (uint8_t)s;
        nav_dirty = 1;
        return 1;
    }
    if (id >= ENC_K1 && id <= ENC_K4) {
        uint32_t ki = id - ENC_K1;
        if (fdemo.screen == 1 && ki == 0) {
            uint32_t visible = (Y_FOOT - Y_GRAPH) / FDEMO_ROW_H;
            int32_t r = (int32_t)fdemo.val[0] + clicks;
            if (r < 0) r = 0;
            if (r >= FDEMO_LIST_N) r = FDEMO_LIST_N - 1;
            fdemo.val[0] = (uint8_t)r;
            if (fdemo.val[0] < fdemo.scroll)
                fdemo.scroll = fdemo.val[0];
            if (fdemo.val[0] >= fdemo.scroll + visible)
                fdemo.scroll = (uint8_t)(fdemo.val[0] - visible + 1u);
        } else {
            int32_t v = (int32_t)fdemo.val[ki] + clicks;
            if (v < 0) v = 0;
            if (v > 99) v = 99;
            fdemo.val[ki] = (uint8_t)v;
        }
        nav_dirty = 1;
        return 1;
    }
    return 0;
}

static int fdemo_button(uint32_t id, uint32_t down)
{
    if (!down && id == BTN_OCTUP && fdemo.screen == 4) {
        kbd_piano_start("EDIT TEXT", fdemo_text, 31, 0, fdemo_kbd_done);
        return 1;
    }
    return 0;
}

static int fdemo_key(uint32_t note, uint32_t vel)
{
    fdemo.last_note = (uint8_t)note;
    fdemo.last_vel = (uint8_t)vel;
    nav_dirty = 1;
    return 1;
}

static const cfm1_page_t FPAGE_DEMO = {
    .name   = "DEMO",
    .title  = "DEMO",
    .view   = VIEW_DEMO,
    .flags  = PAGEF_SYSTEM,
    .enter  = fdemo_enter,
    .draw   = fdemo_draw,
    .knob   = fdemo_knob,
    .button = fdemo_button,
    .key    = fdemo_key,
};
