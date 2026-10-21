/* SPDX-License-Identifier: GPL-3.0-only */
/* Felucca SEQ page: step grid with SURF-style rendering. */

static void fseq_draw(void)
{
    const track_t *t = &trk[song.sel];
    const step_t *steps = seq_steps(t);
    const engine_t *eng = (t->eng_req < NENGINES) ? ENGINES[t->eng_req] : 0;
    uint32_t i;

    fui_draw_head();

    /* 4 cards: LEN, SWING, GATE, QUANTIZE */
    fui_draw_card(0, "LEN",  "16",  "", T_THEME, 500, ICON_LENGTH);
    fui_draw_card(1, "SWG",  "50",  "%", T_THEME, 500, ICON_SWING);
    fui_draw_card(2, "GATE", "75",  "%", T_THEME, 750, ICON_GATE);
    fui_draw_card(3, "QNT",  "OFF", "", T_DIM, -1, ICON_QUANTIZE);

    /* graph: 16-step grid */
    cv_begin(240, Y_FOOT - Y_GRAPH, T_BG);
    {
        uint32_t cols = 8;
        int32_t cw = 26, ch = 26, gap = 2;
        int32_t ox = (240 - (int32_t)cols * (cw + gap) + gap) / 2;
        int32_t oy = 8;

        for (i = 0; i < 16; i++) {
            uint32_t col = i % cols, row = i / cols;
            int32_t x = ox + (int32_t)col * (cw + gap);
            int32_t y = oy + (int32_t)row * (ch + gap);
            int active = steps[i].note[0] != 0;
            int playing = song.playing && t->seq_idx == i;

            uint16_t fill = active ? T_THEME : T_SURF;
            if (playing)
                fill = T_ACCENT;

            cv_rrect(x, y, cw, ch, 3, fill, T_BG);

            if (active) {
                char n[4];
                n[0] = '0' + (char)((i + 1) / 10);
                n[1] = '0' + (char)((i + 1) % 10);
                n[2] = 0;
                if (n[0] == '0') { n[0] = n[1]; n[1] = 0; }
                cv_text_in(x, y + CAP_IN(S, ch), cw, &AF_S, n, T_INK, fill);
            }
        }
    }
    cv_blit(0, Y_GRAPH);

    fui_draw_foot(eng ? eng->name : "---", "ACID", "SEQ");
}

static const cfm1_page_t FPAGE_SEQ = {
    .name = "SEQ", .title = "STEP", .draw = fseq_draw,
};
