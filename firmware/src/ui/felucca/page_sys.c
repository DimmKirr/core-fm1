/* SPDX-License-Identifier: GPL-3.0-only */
/* Felucca settings page: tabbed menu driven by cfm1_sys_menu adapter.
 * Long-HOME opens this page. ALGO switches tabs, PRESET scrolls rows,
 * SELECT cycles the selected row's value. */

#define FSYS_ROW_H    18
#define FSYS_X_PAD    6
#define FSYS_TAB_H    16
#define FSYS_GRAPH_H  (Y_FOOT - Y_GRAPH)
#define FSYS_LIST_Y   (Y_GRAPH + FSYS_TAB_H + 2)
#define FSYS_LIST_H   (FSYS_GRAPH_H - FSYS_TAB_H - 2)
#define FSYS_VISIBLE  ((uint32_t)(FSYS_LIST_H / FSYS_ROW_H))

static struct {
    uint8_t tab;
    uint8_t row;
    uint8_t scroll;
    uint8_t prev_page;
} fsys;

static uint32_t fsys_tab_count(uint8_t tab)
{
    uint32_t n = 0, i;
    for (i = 0; i < SYS_MENU_COUNT; i++)
        if (SYS_MENU[i].tab == tab) n++;
    return n;
}

static const sys_menu_item_t *fsys_tab_item(uint8_t tab, uint32_t idx)
{
    uint32_t n = 0, i;
    for (i = 0; i < SYS_MENU_COUNT; i++) {
        if (SYS_MENU[i].tab == tab) {
            if (n == idx) return &SYS_MENU[i];
            n++;
        }
    }
    return 0;
}

static void fsys_draw(void)
{
    uint32_t t, n, r;

    fui_draw_head();

    /* tab bar in top of graph area */
    cv_begin(240, FSYS_TAB_H, T_BG);
    {
        int32_t x = FSYS_X_PAD;
        for (t = 0; t < (uint32_t)SMTAB_COUNT; t++) {
            if (fsys_tab_count((uint8_t)t) == 0)
                continue;
            uint32_t tw = text_w(&AF_S, SMTAB_NAME[t]) + 8;
            uint16_t fg = (t == fsys.tab) ? T_THEME : T_DIM;
            if (t == fsys.tab)
                cv_rrect(x - 2, 1, (int32_t)tw + 4, FSYS_TAB_H - 2, 3, T_SURF, T_BG);
            cv_text_on(x + 2, 2 + CAP_IN(S, FSYS_TAB_H - 4), &AF_S,
                       SMTAB_NAME[t], fg, t == fsys.tab ? T_SURF : T_BG);
            x += (int32_t)tw + 6;
        }
    }
    cv_blit(0, Y_GRAPH);

    /* no cards: fill card area with BG */
    lcd_fill(0, Y_CARDS, 240, Y_GRAPH - Y_CARDS, T_BG);

    /* rows for current tab */
    n = fsys_tab_count(fsys.tab);
    cv_begin(240, FSYS_LIST_H, T_BG);
    for (r = 0; r < n && r < fsys.scroll + FSYS_VISIBLE; r++) {
        if (r < fsys.scroll) continue;
        const sys_menu_item_t *item = fsys_tab_item(fsys.tab, r);
        if (!item) break;
        int32_t y = (int32_t)(r - fsys.scroll) * FSYS_ROW_H;
        uint8_t selected = (r == fsys.row);
        if (selected)
            cv_rrect(2, y, 236, FSYS_ROW_H, 3, T_SURF, T_BG);
        cv_text_fit(FSYS_X_PAD, y + CAP_IN(S, FSYS_ROW_H),
                    &AF_S, item->name, T_DIM,
                    selected ? T_SURF : T_BG, 130);
        cv_text_r(240 - FSYS_X_PAD, y + CAP_IN(S, FSYS_ROW_H),
                  &AF_S, item->values[item->get()],
                  selected ? T_THEME : T_TEXT,
                  selected ? T_SURF : T_BG);
    }
    cv_blit(0, FSYS_LIST_Y);

    fui_draw_foot(CONFIG_FIRMWARE_NAME, CONFIG_FIRMWARE_VERSION,
                  SMTAB_NAME[fsys.tab]);
}

static int fsys_knob(uint32_t id, int32_t clicks)
{
    if (id == ENC_ALGO) {
        /* switch tabs (skip empty ones) */
        int32_t t = (int32_t)fsys.tab;
        uint32_t tries = SMTAB_COUNT;
        do {
            t += clicks > 0 ? 1 : -1;
            if (t < 0) t = SMTAB_COUNT - 1;
            if (t >= (int32_t)SMTAB_COUNT) t = 0;
        } while (fsys_tab_count((uint8_t)t) == 0 && --tries);
        fsys.tab = (uint8_t)t;
        fsys.row = 0;
        fsys.scroll = 0;
        fsh_dirty = 1;
        return 1;
    }
    if (id == ENC_PRESET) {
        /* scroll rows */
        uint32_t n = fsys_tab_count(fsys.tab);
        int32_t r = (int32_t)fsys.row + clicks;
        if (r < 0) r = 0;
        if (n > 0 && (uint32_t)r >= n) r = (int32_t)(n - 1);
        fsys.row = (uint8_t)r;
        if (fsys.row < fsys.scroll)
            fsys.scroll = fsys.row;
        if (fsys.row >= fsys.scroll + FSYS_VISIBLE)
            fsys.scroll = (uint8_t)(fsys.row - FSYS_VISIBLE + 1u);
        fsh_dirty = 1;
        return 1;
    }
    if (id == ENC_SELECT) {
        /* cycle value of selected row */
        const sys_menu_item_t *item = fsys_tab_item(fsys.tab, fsys.row);
        if (item && item->n_values > 0) {
            uint8_t v = item->get();
            if (clicks > 0)
                v = (uint8_t)((v + 1u) % item->n_values);
            else if (v > 0)
                v--;
            else
                v = (uint8_t)(item->n_values - 1u);
            item->set(v);
            fsh_dirty = 1;
        }
        return 1;
    }
    return 0;
}

static int fsys_button(uint32_t id, uint32_t down)
{
    if (!down && id == BTN_HOME) {
        fsh_set_page(fsys.prev_page);
        return 1;
    }
    return 0;
}

static void fsys_enter(void)
{
    fsys.prev_page = (uint8_t)fsh_cur;
    fsys.row = 0;
    fsys.scroll = 0;
    /* start on first tab that has items */
    {
        uint32_t t;
        for (t = 0; t < (uint32_t)SMTAB_COUNT; t++) {
            if (fsys_tab_count((uint8_t)t) > 0) {
                fsys.tab = (uint8_t)t;
                break;
            }
        }
    }
}

static const cfm1_page_t FPAGE_SYS = {
    .name   = "SYS",
    .title  = "SETTINGS",
    .flags  = PAGEF_SYSTEM,
    .enter  = fsys_enter,
    .draw   = fsys_draw,
    .knob   = fsys_knob,
    .button = fsys_button,
};
