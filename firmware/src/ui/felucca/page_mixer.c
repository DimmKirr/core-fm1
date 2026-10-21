/* SPDX-License-Identifier: GPL-3.0-only */
/* Felucca MIXER page: 4-track level/pan overview. */

static void fmixer_draw(void)
{
    uint32_t k;

    fui_draw_head();

    for (k = 0; k < NTRK && k < 4; k++) {
        const track_t *t = &trk[k];
        char val[8];
        fui_fmt_val(val, t->p[P_LEVEL]);
        uint16_t vc = (song.sel == k) ? T_ACCENT : T_THEME;
        int32_t ratio = t->p[P_LEVEL] * 1000 / 127;
        fui_draw_card(k, "LEVEL", val, "", vc, ratio, trk_icon(k, 1));
    }

    /* graph: vertical bars */
    cv_begin(240, Y_FOOT - Y_GRAPH, T_BG);
    {
        int32_t bw = 40, gap = 12;
        int32_t ox = (240 - (int32_t)NTRK * (bw + gap) + gap) / 2;
        int32_t max_h = Y_FOOT - Y_GRAPH - 20;

        for (k = 0; k < NTRK; k++) {
            int32_t x = ox + (int32_t)k * (bw + gap);
            int32_t h = trk[k].p[P_LEVEL] * max_h / 127;
            int32_t y = max_h - h + 4;
            uint16_t c = (song.sel == k) ? T_ACCENT : T_SEL;

            cv_rrect(x, y, bw, h, 3, c, T_BG);
            cv_trk(x + (bw - 16) / 2, max_h + 8, 16, k,
                   (song.sel == k) ? T_ACCENT : T_MID, T_BG);
        }
    }
    cv_blit(0, Y_GRAPH);

    {
        const track_t *t = &trk[song.sel];
        const engine_t *eng = (t->eng_req < NENGINES) ? ENGINES[t->eng_req] : 0;
        fui_draw_foot(eng ? eng->name : "---", "ACID", "MIXER");
    }
}

static int fmixer_knob(uint32_t id, int32_t clicks)
{
    if (id >= 1 && id <= 4) {
        uint32_t k = id - 1;
        if (k < NTRK) {
            int16_t v = trk[k].p[P_LEVEL] + (int16_t)(clicks * 4);
            if (v < 0) v = 0;
            if (v > 127) v = 127;
            trk[k].p[P_LEVEL] = v;
            fsh_dirty = 1;
            return 1;
        }
    }
    return 0;
}

static const cfm1_page_t FPAGE_MIXER = {
    .name = "MIXER", .title = "MIXER", .draw = fmixer_draw, .knob = fmixer_knob,
};
