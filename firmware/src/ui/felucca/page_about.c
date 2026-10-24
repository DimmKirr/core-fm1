/* SPDX-License-Identifier: GPL-3.0-only */
/* Felucca ABOUT page: firmware info in Felucca visual style. */

static void fabout_draw(void)
{
    fui_draw_head();

    /* 4 info cards */
    {
        char eng_val[8], page_val[8];
        fui_fmt_val(eng_val, (int16_t)NENGINES);
        fui_fmt_val(page_val, (int16_t)fsh_npage);
        fui_draw_card(0, "FW",   "cfm1",                   "", T_THEME, -1, ICON_GENERIC);
        fui_draw_card(1, "VER",  CONFIG_FIRMWARE_VERSION,   "", T_MID,   -1, ICON_GENERIC);
        fui_draw_card(2, "ENG",  eng_val,                   "", T_THEME, -1, ICON_WAVE);
        fui_draw_card(3, "PAGE", page_val,                  "", T_THEME, -1, ICON_GENERIC);
    }

    /* centre text */
    cv_begin(240, Y_FOOT - Y_GRAPH, T_BG);
    cv_text_in(0, 20, 240, &AF_M, "core-fm1", T_TEXT, T_BG);
    cv_text_in(0, 44, 240, &AF_S, "unified firmware framework", T_MID, T_BG);
    cv_text_in(0, 64, 240, &AF_S, "felucca + melodee + sloop", T_DIM, T_BG);
    cv_blit(0, Y_GRAPH);

    fui_draw_foot("core-fm1", CONFIG_FIRMWARE_VERSION, "ABOUT");
}

static const cfm1_page_t FPAGE_ABOUT = {
    .name = "ABOUT", .title = "ABOUT", .flags = PAGEF_SYSTEM, .draw = fabout_draw,
};
