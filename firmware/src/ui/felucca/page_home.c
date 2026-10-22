/* SPDX-License-Identifier: GPL-3.0-only */
/* Felucca HOME page: 4 engine knob cards with units, track overview graph. */

static void fhome_draw(void)
{
    uint32_t c;
    const track_t *t = &trk[song.sel];
    const engine_t *eng = (t->eng_req < NENGINES) ? ENGINES[t->eng_req] : 0;

    fui_draw_head();

    for (c = 0; c < 4; c++) {
        if (eng) {
            uint32_t pi = eng->knob[c];
            uint32_t ei = (pi >= P_E0 && pi < P_E0 + 8) ? pi - P_E0 : pi;
            const param_desc_t *d = &eng->edit[ei < 8 ? ei : 0];
            char val[12];
            int16_t v = t->p[pi];
            const char *unit = d->unit ? d->unit : "";
            if (d->fmt == F_TIME)
                fui_fmt_time(val, v);
            else
                fui_fmt_val(val, v);
            int32_t ratio = d->max > d->min
                ? (int32_t)(v - d->min) * 1000 / (int32_t)(d->max - d->min)
                : -1;
            fui_draw_card(c, d->label, val, unit, T_THEME, ratio, ICON_AUTO);
        } else {
            fui_draw_card(c, "---", "--", "", T_DIM, -1, ICON_NONE);
        }
    }

    /* graph area: envelope / waveform placeholder + track overview */
    cv_begin(240, Y_FOOT - Y_GRAPH, T_BG);
    {
        /* simple waveform line (placeholder for real oscilloscope) */
        int32_t cx, last_y = 50;
        for (cx = 8; cx < 232; cx++) {
            int32_t y = 50 - (cx % 40 < 20 ? (cx % 20) * 3 : (20 - cx % 20) * 3);
            cv_rect(cx, y < last_y ? y : last_y, 1, (y > last_y ? y - last_y : last_y - y) + 1, T_THEME);
            last_y = y;
        }

        /* step lane (dashes below waveform, like real Felucca) */
        {
            uint32_t i;
            for (i = 0; i < 16u; i++) {
                int32_t sx = 12 + (int32_t)i * 14;
                if (i > 0 && (i % 4) == 0) sx += 2;
                uint16_t sc = t->step[i].note[0] ? T_THEME : T_RAISE;
                cv_rrect(sx, 90, 9, 3, 1, sc, T_BG);
            }
        }

        /* track cushions (bottom of graph) */
        {
            uint32_t k;
            for (k = 0; k < NTRK; k++) {
                int32_t tx = 8 + (int32_t)k * 58;
                uint16_t fg = (song.sel == k) ? T_ACCENT : T_MID;
                cv_trk(tx, 104, 16, k, fg, T_BG);
                const engine_t *te = (trk[k].eng_req < NENGINES) ? ENGINES[trk[k].eng_req] : 0;
                cv_text(tx + 20, 104 + CAP_IN(S, 16), &AF_S, te ? te->name : "---", fg);
            }
        }
    }
    cv_blit(0, Y_GRAPH);

    fui_draw_foot(eng ? eng->name : "---", "ACID", "HOME");
}

static int fhome_knob(uint32_t id, int32_t clicks)
{
    if (id == 1) {
        int32_t s = (int32_t)song.sel + clicks;
        if (s < 0) s = 0;
        if (s >= (int32_t)NTRK) s = NTRK - 1;
        song.sel = (uint8_t)s;
        fsh_dirty = 1;
        return 1;
    }
    if (id >= 2 && id <= 4) {
        track_t *t = &trk[song.sel];
        const engine_t *eng = (t->eng_req < NENGINES) ? ENGINES[t->eng_req] : 0;
        if (!eng) return 0;
        uint32_t pi = eng->knob[id - 1];
        uint32_t ei = (pi >= P_E0 && pi < P_E0 + 8) ? pi - P_E0 : pi;
        const param_desc_t *d = &eng->edit[ei < 8 ? ei : 0];
        int16_t v = t->p[pi] + (int16_t)(clicks * 4);
        if (v < d->min) v = d->min;
        if (v > d->max) v = d->max;
        t->p[pi] = v;
        fsh_dirty = 1;
        return 1;
    }
    return 0;
}

static const cfm1_page_t FPAGE_HOME = {
    .name  = "HOME",
    .title = "HOME",
    .flags = PAGEF_HOME,
    .draw  = fhome_draw,
    .knob  = fhome_knob,
};
