/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The pages the proposal did not draw, in the redesign's language (docs/design: mock/r5_pages, ref/env, lfo, ..), set in
 * the large type (docs/design/README.md "Large type": B, focus): no footer; the header names the page and shows where
 * it is in its family (dots); the four knobs as the strip under the header (a cell a knob: its label over its value,
 * the knob just turned lifted in the track's colour); the page's own picture in the panel under them; the sound being
 * edited at the foot. The knob turning is the hero: its label and its value 48 px over the picture (pv_hero), until
 * PV_HERO_MS after the last turn.
 * CURVE (ENV, LFO, LFO 2): the curve is the picture (the panel, 124 rows); the hero in its top right corner.
 * RINGS (the pages of plain values: EDIT, the engines' own ..): what the page shapes: a filter's
 * response (a page with a cutoff), FM6's algorithm (its chart), else the sound itself (the scope, smooth, live).
 * NOTES (SEQ > STEP; a melodic track): the roll the whole height (PR_TOP, PR_H: ui_graph.c, in two slices),
 * the knobs as chips under it (the one turning filled).
 * MIXER: Stage's columns as channel strips: the sound, its level (a ring, dB in it) and meter, PAN, MUTE,
 * armed;
 * the selected track's lifted, the knob just turned (its control) in text.
 * Each part remembers what it drew. draw_column draws the knobs in the style col_style asks. Included by
 * ui_draw.c */
enum { PV_NONE, PV_CURVE, PV_RINGS, PV_FADERS, PV_MIXER, PV_NOTES, PV_MOD, PV_GRID };
#define PV_GRID_Y Y_GRAPH                               /* the drum grid: its panel, rows 84 .. 237 (under the strip) */
#define PV_GRID_H 154
static void list_words(char *d, const char *label, uint32_t n);   /* ui_list.c */
static void menu_words(char *d, const char *s, uint32_t n);         /* ui_menu.c */
#define PV_PANEL_X BOX_X                                /* a panel: x 2 .. 237 */
#define PV_PANEL_W BOX_W
#define PV_SLICE 124                                    /* rows of a panel slice (236 x 124 fits CV_MAX) */
#define PV_CURVE_Y Y_GRAPH                              /* CURVE: the panel, rows 84 .. 207 */
#define PV_CURVE_H H_GRAPH
#define PV_SOUND_Y Y_FOOT                               /* the sound: rows 212 .. 239 */
#define PV_PIC_Y Y_GRAPH                                /* RINGS: the picture, rows 84 .. 207 */
#define PV_PIC_H H_GRAPH
#define PV_HERO_MS 1500u                                /* the hero stays this long after the last turn */

static struct { uint32_t panel, sound, hero; int32_t slice0, gplay, pplay; } pv;   /* gplay, pplay: the step playing drawn
                                                                               * (drum grid, PATTERN; -1 none) */
static int32_t pv_tab_h;                                /* an engine in sections: its tabs under the header (rows 30 ..
                                                         * 55): the strip and the panel that much lower, the panel
                                                         * shorter */
static uint32_t hero_ms;                                /* the last knob turn on a page (| 1), 0 none (pv_hero) */
static void hero_touch(void) { hero_ms = fm1_ms | 1u; }   /* (ui_input.c page_knob) */
static int pv_hero_on(void) { return hero_ms && fm1_ms - (hero_ms & ~1u) < PV_HERO_MS && ui.hot_col < 4u; }
static struct { int16_t x0, y0, x1, y1; } pv_box[6];   /* the labels on a panel (pv_text) */
static uint32_t pv_nbox;

static uint32_t pv_kind(void)
{
    const page_t *pg = cur_page();
    if (!ui.home && grid_on() && (pg->graph == GR_ROLL || pg->graph == GR_DRUMHIT) && !ui.layer)
        return PV_GRID;
    if (ui.home || act_cols() || grid_on())
        return PV_NONE;
    if ((pg->graph == GR_ADSR && pg->scope == SC_TRACK) || pg->graph == GR_LFO || pg->graph == GR_FMEG ||
        pg->graph == GR_FMPEG)
        return PV_CURVE;
    if (pg->graph == GR_NONE || pg->graph == GR_ARP || pg->graph == GR_STEPS ||
        pg->graph == GR_CHORD)
        return PV_RINGS;
    if (pg->graph == GR_TRK)
        return PV_MIXER;
    if (pg->graph == GR_ROLL && !drum_track(TSEL))
        return PV_NOTES;
    if (pg->graph == GR_MOD)
        return PV_MOD;
    return PV_NONE;
}

/* MOD (docs/design: mock/r6_pages MOD; large type): the matrix's four routes as rows: its number, "LFO -> Cutoff" in
 * words (the text face; the label face when they do not fit: never cut), under them the amount's bar (from the
 * middle) and the amount ("+62 %") at the right; the route KNOB 2 is on outlined in the track's colour and tinted;
 * unused routes dim. KNOB 1 its source, 3 its destination, 4 its amount: the word turning in the track's colour */
#define PV_MOD_Y Y_LABEL                                /* the rows: 50 px each from y 32 */
#define PV_MOD_H 50
static void pv_mod_rows(void)
{
    static uint32_t sig;
    uint32_t k, hot = ui.hot_t || pv_hero_on() ? ui.hot_col + 1u : 0u;
    uint32_t sg = mod_ui_slot * 7u + ux.gen * 977u + ux.pal * 31u + song.sel * 13u + TSEL->eng_req * 613u + hot * 101u;
    for (k = 0; k < NMSLOT * 3u; k++)
        sg = (sg ^ (uint16_t)TSEL->p[P_M1SRC + k]) * 16777619u;
    if (!ui.force && sg == sig)
        return;
    sig = sg;
    for (k = 0; k < NMSLOT; k++) {
        const int16_t *p = &TSEL->p[P_M1SRC + 3u * k];
        int sel = k == mod_ui_slot, on = p[0] && p[1] && p[2];
        uint16_t kc = T_KNOB(k), bg = sel ? ux_mix(T_BG, kc, 18) : T_BG, wc = on ? (sel ? T_TEXT : T_SEC) : T_DIM;   /* (each
                                                                                                         * slot a colour) */
        int32_t a = clamp(p[2], TP[P_M1AMT].min, TP[P_M1AMT].max), m = 120, x, room;
        char src[16], dst[16], b[16];
        const char *unit;
        const aafont_t *f = TY_TX;
        cv_begin(240, PV_MOD_H, T_BG);
        if (sel) {                                      /* (the route picked: outlined, tinted) */
            cv_rrect(BOX_X, 1, BOX_W, PV_MOD_H - 4, 8, kc, T_BG);
            cv_rrect(BOX_X + 2, 3, BOX_W - 4, PV_MOD_H - 8, 6, bg, kc);
        }
        b[0] = (char)('1' + k);
        b[1] = 0;
        cv_text_on(10, ty_y(17, TY_TX), TY_TX, b, on || sel ? kc : T_MID, bg);
        list_words(src, p[0] ? N_MSRC[clamp(p[0], 0, MS_N - 1)] : "--", sizeof src);
        list_words(dst, p[1] ? mod_dst_name(TSEL, p[1]) : "--", sizeof dst);
        room = 228 - 30 - 22;                           /* (the words, the arrow between them) */
        if (text_w(f, src) + text_w(f, dst) > room)
            f = TY_LB;
        x = cv_text_on(30, ty_y(17, f), f, src, sel && hot == 1u ? kc : wc, bg);
        cv_rect(x + 6, 17, 9, 2, on ? T_MID : T_DIM);   /* an arrow */
        cv_line(x + 12, 14, x + 15, 17, on ? T_MID : T_DIM);
        cv_line(x + 12, 20, x + 15, 17, on ? T_MID : T_DIM);
        cv_free_text(x + 22, ty_y(17, f), f, dst, sel && hot == 3u ? kc : wc, bg, 228 - x - 22);
        param_format(&TP[P_M1AMT], a, b, &unit);        /* "+62 %" */
        if (unit[0] && str_len(b) + str_len(unit) + 2u < sizeof b) {
            str_cpy(b + str_len(b), " ", 2);
            str_cpy(b + str_len(b), unit, 8);
        }
        x = cv_text_r(228, ty_y(36, TY_LB), TY_LB, b, sel && hot == 4u ? kc : on ? (sel ? T_TEXT : T_SEC) : T_DIM, bg);
        cv_rect(30, 35, x - 38, 3, T_LINE);             /* the amount, from the middle */
        m = 30 + (x - 38) / 2;
        {
            int32_t ax = 30 + (a - TP[P_M1AMT].min) * (x - 38) / (TP[P_M1AMT].max - TP[P_M1AMT].min);
            if (a)
                cv_rect(ax < m ? ax : m, 35, ax < m ? m - ax : ax - m, 3, on ? (sel ? kc : ux_mix(bg, kc, 60)) : T_DIM);
        }
        cv_rect(m, 32, 1, 9, T_MID);
        cv_blit(0, (uint32_t)(PV_MOD_Y + (int32_t)k * PV_MOD_H));
    }
}

/* ---------------------------------------------------------- knobs --- */
/* a knob's words as the mockups write them: a name value in sentence case ("SAW" -> "Saw"; numbers and notes as they
 * are), a unit in lower case ("STEP" -> "steps"); its label as the page has it (the hero writes it in full: pv_long) */
static const struct { const char *k, *w; } PV_LABEL[] = {
    {"OCT", "OCTAVES"}, {"LEN", "LENGTH"}, {"DIV", "DIVISION"}, {"SWG", "SWING"}, {"PAT", "PATTERN"},
    {"DTN", "DETUNE"}, {"NOIS", "NOISE"}, {"CUT", "CUTOFF"}, {"DRV", "DRIVE"}, {"ATK", "ATTACK"}, {"DEC", "DECAY"},
    {"SUS", "SUSTAIN"}, {"REL", "RELEASE"}, {"FDBK", "FEEDBACK"}, {"DPTH", "DEPTH"}, {"VOIC", "VOICING"}, {"QNT", "QUANTIZE"},
    {"SEND 1", "SEND TO FX 1"}, {"SEND 2", "SEND TO FX 2"}, {"SEND 3", "SEND TO FX 3"}, {"RET", "RETURN"}};
static void pv_words(const char *label, const char *val, const char *unit, char *lb, char *v, char *u)
{
    uint32_t i, word = val[0] >= 'A' && val[0] <= 'Z' && val[1];
    str_cpy(lb, label, 12);
    for (i = 0; val[i] && word; i++)                    /* (a name: letters, spaces and signs only) */
        word = !(val[i] >= '0' && val[i] <= '9');
    if (word) menu_words(v, val, 16);
    else str_cpy(v, val, 16);
    str_cpy(u, unit, 8);
    if (str_eq(unit, "STEP")) {
        str_cpy(u, str_eq(val, "1") ? "step" : "steps", 8);
    } else if (unit[0] && unit[1] && !str_eq(unit, "dB")) {
        for (i = 0; u[i]; i++)
            if (u[i] >= 'A' && u[i] <= 'Z') u[i] = (char)(u[i] + 32);
    }
}
static const char *pv_long(const char *label)          /* a label in full ("ATK" -> "ATTACK") */
{
    uint32_t i;
    for (i = 0; i < NELEM(PV_LABEL); i++)
        if (str_eq(label, PV_LABEL[i].k))
            return PV_LABEL[i].w;
    return label;
}
/* ---------------------------------------------------------- parts --- */
/* the sound bar: the track's numeral (filled in its colour) and the sound's name (the text face, secondary), capitals
 * centred on the bar's middle row (en: a page of no track's, its words in the name's place when there is no name) */
static void pv_sound_bar(int32_t y, uint32_t k, const char *en, const char *nm, uint16_t tc, uint16_t bg)
{
    const aafont_t *lf = TY_LB, *tf = TY_TX;
    char d[2] = {(char)('1' + k % NTRK), 0};
    int32_t cy = y + H_FOOT / 2;
    cv_rrect(4, cy - 11, 22, 22, 6, tc, bg);
    cv_text_c(15, ty_y(cy, lf), lf, d, T_INK, tc);
    cv_free_text(34, ty_y(cy, tf), tf, nm[0] ? nm : en, T_SEC, bg, 236 - 34);
}
/* the sound being edited: its track, engine and name (pv_sound_bar) */
static void pv_sound(void)
{
    char nm[20];
    const char *en = ENGINES[eng_idx(TSEL->eng_req)]->name;
    uint32_t sig;
    if (designer_on() && designer_foot())                /* the sound designer: its keys (ui_designer.c) */
        return;
    if(cur_page()->fam==FAM_FX && cur_page()->scope==SC_GLOBAL && cur_page()->id[0]!=G_TRK_SEND){en="FX";memcpy(nm,"SEND / RETURN",14);}
    else sound_name(TSEL, nm);
    sig = str_hash(str_hash(song.sel * 7u + ux.gen * 977u + ux.pal * 31u, en), nm);
    if (!ui.force && sig == pv.sound)
        return;
    pv.sound = sig;
    cv_begin(240, 240 - PV_SOUND_Y, T_BG);
    pv_sound_bar(0, song.sel, en, nm, T_THEME, T_BG);
    cv_blit(0, PV_SOUND_Y);
}
/* a panel h rows tall at y, drawn by fn in slices (fn draws in panel coordinates; pv.slice0: the slice's first row) */
static void pv_panel(int32_t y, int32_t h, void (*fn)(int32_t h))
{
    int32_t top;
    for (top = 0; top < h; top += PV_SLICE) {
        int32_t sh = h - top < PV_SLICE ? h - top : PV_SLICE;
        cv_begin(PV_PANEL_W, (uint32_t)sh, T_BG);
        cv_oy = -top;
        pv.slice0 = top;
        cv_rrect(0, 0, PV_PANEL_W, h, 6, T_PANEL, T_BG);
        cv_bg = T_PANEL;
        pv_nbox = 0;
        fn(h);
        cv_oy = 0;
        cv_blit(PV_PANEL_X, (uint32_t)(y + top));
    }
}
/* a canvas drawn (at x 0, row y) for its playhead alone: sent only where it was and is, columns from .. from+w-1 and
 * to .. to+w-1 (-1: none; close together: one span) */
static void pv_cols(uint32_t y, int32_t from, int32_t to, int32_t w)
{
    if (from >= 0 && to >= 0 && from < to + w + 8 && to < from + w + 8) {
        int32_t a = from < to ? from : to, b = from < to ? to : from;
        cv_blit_spans(0, y, (uint32_t)a, (uint32_t)(b - a + w), 0, 0);
    } else {
        cv_blit_spans(0, y, from >= 0 ? (uint32_t)from : 0u, from >= 0 ? (uint32_t)w : 0u, to >= 0 ? (uint32_t)to : 0u,
                      to >= 0 ? (uint32_t)w : 0u);
    }
}
static int32_t pv_hot(void) { return ui.hot_t || pv_hero_on() ? (int32_t)(ui.hot_col & 3u) : -1; }

/* the hero (large type, B: focus): the knob turning over the page's picture, its words as its cell shows them
 * (ui_draw.c hero_w): the label in full (the label face, the track's colour), the value 48 px (the value face when the
 * hero's face lacks a sign or the value is too wide, then the text face), its unit after it (the value face, mid); over
 * a picture on a band fading out below it, over a curve (corner: its top right) outlined in the background's colour.
 * x0: the picture's left on the canvas. Over the canvas as it is (cv_over) */
static uint32_t pv_hero_sig(void)
{
    return pv_hero_on() ? str_hash(str_hash(str_hash(1u + ui.hot_col * 7u + hero_w.dim * 31u + (uint32_t)(hero_w.ratio + 1) * 131u,
                                                     hero_w.label), hero_w.val), hero_w.unit) : 0u;
}
static void pv_hero(int32_t x0, int32_t pw, int corner)
{
    const aafont_t *lf = TY_LB, *vf = TY_HR, *uf = TY_VL;
    int32_t room = pw - 24, uw, vw, lw, bw, bh, x, y = 6;
    uint16_t lc = hero_w.dim ? T_DIM : T_KNOB(ui.hot_col), vc = hero_w.dim ? T_DIM : T_TEXT;   /* (its knob's colour) */
    if (!pv_hero_on() || hero_w.kc || !hero_w.val[0])
        return;
    uw = hero_w.unit[0] ? text_w(uf, hero_w.unit) + 4 : 0;
    if (!text_has(vf, hero_w.val) || text_w(vf, hero_w.val) + uw > room)
        vf = TY_VL;
    if (text_w(vf, hero_w.val) + uw > room) {
        vf = TY_TX;
        uf = lf;
        uw = hero_w.unit[0] ? text_w(uf, hero_w.unit) + 3 : 0;
    }
    vw = text_w(vf, hero_w.val);
    if (vw > room - uw)
        vw = room - uw;
    lw = text_w(lf, pv_long(hero_w.label));
    bw = (vw + uw > lw ? vw + uw : lw) + 24;
    bh = y + lf->h + vf->h + 4;
    x = corner ? x0 + pw - bw : x0;
    if (!corner)                                        /* (a picture: a band over its top, fading out below) */
        cv_scrim(x0, 0, pw, bh + 10, T_PANEL, 30, 12);
    cv_over = cv_hero = 1;
    {   /* the words; over a curve each outlined in the panel's colour (no box: the curve stays whole around them) */
        int32_t pass, dx, dy, xv = x + 12;
        for (pass = corner ? 0 : 1; pass < 2; pass++)
            for (dy = pass ? 0 : -2; dy <= (pass ? 0 : 2); dy += 2)
                for (dx = pass ? 0 : -2; dx <= (pass ? 0 : 2); dx += 2) {
                    uint16_t ink = pass ? 0 : T_PANEL;
                    if (!pass && !dx && !dy)
                        continue;
                    cv_text_fit(x + 12 + dx, y + dy, lf, pv_long(hero_w.label), pass ? lc : ink, T_PANEL, bw - 16);
                    xv = cv_free_text(x + 12 + dx, y + lf->h - 2 + dy, vf, hero_w.val, pass ? vc : ink, T_PANEL, vw) - dx;
                    if (uw)
                        cv_text_on(xv + 4 + dx, y + lf->h - 2 + dy + vf->asc - uf->asc, uf, hero_w.unit, pass ? T_MID : ink,
                                   T_PANEL);
                }
    }
    cv_over = cv_hero = 0;
}

/* ---------------------------------------------------------- CURVE --- */
/* ENV: attack (linear), decay (exponential to the sustain), the sustain held, the release (exponential); the area
 * under it tinted; a handle at each segment's end, the segment turning in text (its value: the strip, the hero) */
static void pv_env(int32_t h)
{
    const page_t *pg = cur_page();
    int32_t va = TSEL->p[pg->id[0]], vd = TSEL->p[pg->id[1]], vs = TSEL->p[pg->id[2]], vr = TSEL->p[pg->id[3]];
    int32_t top = 14, bot = h - 12, x0 = 12, x1 = x0 + 10 + va * 40 / 127, x2 = x1 + 16 + vd * 52 / 127;
    int32_t x4 = PV_PANEL_W - 14, x3 = x4 - 24 - vr * 48 / 127, sus = vs * 1000 / 127, hot = pv_hot(), x, py = 0;
    int32_t e = 32768, kd = 150733 / (x2 - x1), kr = 150733 / (x4 - x3);
    uint16_t sc[4], sf[4];                              /* each segment in its knob's colour (ATK DEC SUS REL) */
    for (x = 0; x < 4; x++) sc[x] = T_KNOB(x), sf[x] = ux_mix(T_PANEL, sc[x], 14);
#define PVY(l) (bot * 16 - (l) * (bot - top) * 16 / 1000)
    for (x = x0; x <= x4; x++) {                        /* the curve in 1/16 px */
        int32_t l, y, seg = x < x1 ? 0 : x < x2 ? 1 : x < x3 ? 2 : 3;
        if (seg == 0) l = (x - x0) * 1000 / (x1 - x0 ? x1 - x0 : 1);
        else if (seg == 1) { e = x == x1 ? 32768 : (e * (32768 - kd)) >> 15; l = sus + ((1000 - sus) * e >> 15); }
        else if (seg == 2) l = sus;
        else { e = x == x3 ? 32768 : (e * (32768 - kr)) >> 15; l = sus * e >> 15; }
        y = PVY(l);
        cv_rect(x, y / 16 + 1, 1, bot - y / 16, sf[seg]);
        cv_vspan_aa(x, x > x0 ? py : y, y, seg == hot ? T_TEXT : sc[seg]);
        py = y;
    }
    {   /* the handles: the attack's peak, the decay's end, the sustain's end, the release's end */
        const int32_t hx[4] = {x1, x2, x3, x4}, hy[4] = {top, PVY(sus) / 16, PVY(sus) / 16, bot};
        uint32_t i;
        for (i = 0; i < 4u; i++) {
            int32_t d = (int32_t)i == hot ? 11 : 9;
            cv_disc(hx[i], hy[i], d + 4, T_PANEL, 0);
            cv_disc(hx[i], hy[i], d, (int32_t)i == hot ? T_TEXT : sc[i], 0);
        }
    }
#undef PVY
}
/* LFO: two cycles from its phase, its reach dashed (the fade in, then the depth), the zero line; where it goes (the
 * strongest of LFO DEST's) as a chip at the top left (its rate: the strip, the hero) */
static void pv_lfo(int32_t h)
{
    static const uint8_t DST[4] = {P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP};
    static const char *const DST_NAME[4] = {"Pitch", "Filter", "Shape", "Level"};
    const track_t *t = TSEL;
    int32_t uni = t->p[P_LPOL] != 0, cy = uni ? h - 14 : h / 2 + 8, amp = uni ? h - 50 : 40, x, py = 0;
    int32_t fw = t->p[P_LFADE] * 120 / 127, x0 = 10, w = PV_PANEL_W - 20, best = 0, bi = -1, hot = pv_hot();
    uint32_t ph = (uint32_t)t->p[P_LPHASE] << 25, i;
    cv_rect(x0, cy, w, 1, T_LINE);
    {                                                   /* its reach, dashed: the fade in, then the depth */
        uint16_t gc = hot == 3 ? T_TEXT : ux_mix(T_PANEL, T_KNOB(3), 45);   /* (FADE's colour) */
        for (x = 0; x < w; x += 5) {
            int32_t lv = x < fw ? x * amp / fw : amp;
            cv_rect(x0 + x, cy - lv, 2, 1, gc);
        }
    }
    for (x = 0; x < w; x++) {                           /* two cycles (lfo_wave only reads the track) */
        int32_t wv = lfo_wave((track_t *)t, ph + (uint32_t)x * (0xFFFFFFFFu / ((uint32_t)w / 2u))), y;
        int32_t f = fw && x < fw ? x * 1024 / fw : 1024;
        if (t->p[P_LWAVE] == 4)
            wv = (int32_t)((x / 20 * 2654435761u) >> 16) - 32768;
        y = uni ? cy * 16 - (wv + 32768) * amp / 4096 * f / 1024 : cy * 16 - wv * amp / 2048 * f / 1024;
        cv_vspan_aa(x0 + x, x ? py : y, y, T_KNOB(1));   /* (WAVE's colour) */
        py = y;
    }
    for (i = 0; i < 4u; i++) {                          /* where it goes: the strongest destination */
        int32_t a = t->p[DST[i]] < 0 ? -t->p[DST[i]] : t->p[DST[i]];
        if (a > best) best = a, bi = (int32_t)i;
    }
    if (bi >= 0 && pv.slice0 == 0 && !pv_hero_on()) {   /* (the hero over the panel: it says what turns) */
        int32_t cw = text_w(TY_LB, DST_NAME[bi]) + 30;
        uint16_t dc = T_KNOB(2), cb = ux_mix(T_PANEL, dc, 20);
        cv_rrect(8, 6, cw, 24, 6, cb, T_PANEL);
        cv_icon_mid(12, 18, 12, ICON_X_RIGHT, dc, cb);
        cv_text_on(26, ty_y(18, TY_LB), TY_LB, DST_NAME[bi], dc, cb);
    }
}
/* FM6's EG RATE / EG LVL (operator fm6_opsel), PITCH EG / LV (mock/r6_pages OP3 EG): the DX7 envelope from L4 by R1..R3
 * to L1..L3, held there, then R4 back to L4 (a slower rate a wider segment); the area under it tinted, a dot at each
 * point; the knob turning: its segment (a rate) or its point (a level) in text. PITCH: the note's own pitch dashed */
static void pv_dxeg(int32_t h)
{
    const uint8_t *v = fm6_patch[song.sel % NTRK];
    int pitch = cur_page()->graph == GR_FMPEG, lv = cur_page()->id[0] == FP_L1 || cur_page()->id[0] == FP_PL1;
    const uint8_t *r = pitch ? v + FP_PR1 : v + (5u - fm6_opsel % 6u) * FP_OP + FP_R1;
    const uint8_t *l = pitch ? v + FP_PL1 : v + (5u - fm6_opsel % 6u) * FP_OP + FP_L1;
    int32_t x0 = 10, x1 = 222, top = 14, bot = h - 12, px[6], py[6], i, x, hot = pv_hot();
    uint16_t fill = ux_mix(T_PANEL, T_THEME, 22);
#define DXY(a) (bot - (int32_t)(a) * (bot - top) / 99)
    int32_t w[5], sum = 0;
    for (i = 0; i < 4; i++)                             /* (a segment 8..46 wide by its rate, the hold 30: the whole */
        sum += w[i < 3 ? i : 4] = 8 + (99 - r[i]) * 38 / 99;   /* stretched over the panel) */
    w[3] = 30;
    sum += 30;
    px[0] = x0;
    py[0] = DXY(l[3]);
    for (i = 0; i < 5; i++) {                           /* P1..P3 (after R1..R3), P4 the hold's end, P5 after R4 */
        px[i + 1] = px[i] + w[i] * (x1 - x0) / sum;
        py[i + 1] = i < 3 ? DXY(l[i]) : i == 3 ? py[3] : DXY(l[3]);
    }
    px[5] = x1;
    for (x = x0, i = 0; x <= x1; x++) {                 /* the area under it */
        int32_t y;
        while (i < 5 && x > px[i + 1]) i++;
        y = i < 5 && px[i + 1] > px[i] ? py[i] + (py[i + 1] - py[i]) * (x - px[i]) / (px[i + 1] - px[i]) : py[5];
        cv_rect(x, y, 1, bot - y, fill);
    }
    if (pitch)
        for (x = x0; x < x1; x += 8)
            cv_rect(x, DXY(50), 4, 1, T_LINE);
    for (i = 0; i < 5; i++) {                           /* the segments; R1..R3 the first three, R4 the last */
        int seg = i < 3 ? i : i == 4 ? 3 : -1;
        cv_line_t(px[i], py[i], px[i + 1], py[i + 1], !lv && seg >= 0 && seg == hot ? T_TEXT : T_KNOB(seg < 0 ? 2 : seg),
                  !lv && seg >= 0 && seg == hot ? 3 : 2);
    }
    for (i = 1; i < 5; i++) {                           /* the points: L1 L2 L3, the hold's end (the turning one's: text) */
        int on = hot >= 0 && (lv ? i == hot + 1 || (hot == 2 && i == 4) : hot < 3 && i == hot + 1);
        cv_disc(px[i], py[i], on ? 9 : 7, on ? T_TEXT : T_KNOB(i < 4 ? i - 1 : 2), 0);
    }
#undef DXY
}
static void pv_curve(int32_t h)
{
    if (cur_page()->graph == GR_FMEG || cur_page()->graph == GR_FMPEG)
        pv_dxeg(h);
    else if (cur_page()->graph == GR_ADSR)
        pv_env(h);
    else
        pv_lfo(h);
    if (pv.slice0 == 0)                                 /* the knob turning: the hero in the top right */
        pv_hero(0, PV_PANEL_W, 1);
}
/* the drum grid (mock/r6_pages STEP): the eight lanes down the panel, 16 steps of the page across; a hit a cell in the
 * track's colour (an accent in text), an empty step a dot (every fourth brighter); the lane picked a band, its name in
 * the track's colour, the cursor a frame on it; the step playing marked over the lanes */
static int32_t pv_grid_play(void)                       /* the step playing on the page shown, -1 */
{
    uint32_t idx = TSEL->seq_idx;
    return song.playing && idx < (uint32_t)TSEL->p[P_SLEN] && idx / 16u == ui.bank ? (int32_t)(idx % 16u) : -1;
}
#define PV_GX(i) (40 + (int32_t)(i) * 11 + (int32_t)(i) / 4 * 4)   /* step i's cell (9 px) */
static void pv_grid_mark(void)                          /* (rows 1 .. 3, over the lanes) */
{
    if (pv.gplay >= 0)
        cv_rect(PV_GX(pv.gplay), 1, 9, 3, T_TEXT);
}
static void pv_grid(int32_t h)
{
    const track_t *t = TSEL;
    uint32_t l, i, len = (uint32_t)t->p[P_SLEN], base = ui.bank * 16u;
    (void)h;
    pv_grid_mark();
    for (l = 0; l < NLANE; l++) {                       /* (lanes 18 apart from row 6: the name in the label face) */
        int32_t y = 6 + (int32_t)l * 18;
        int sel = l == ui.lane;
        uint16_t row = sel ? T_LIFT : T_PANEL;
        if (sel)
            cv_rrect(2, y, PV_PANEL_W - 4, 17, 4, T_LIFT, T_PANEL);
        cv_text_on(6, ty_y(y + 8, TY_LB), TY_LB, drum_lane_abbr(t, l), sel ? T_KNOB(l) : ux_mix(T_PANEL, T_KNOB(l), 70), row);
        for (i = 0; i < 16u && base + i < len; i++) {
            const step_t *st = &seq_steps(t)[base + i];
            uint32_t b = 1u << l;
            int32_t x = PV_GX(i);
            if (sel && base + i == ui.cursor) {         /* the cursor: a rounded ring around the cell */
                cv_rrect(x - 2, y, 13, 17, 4, T_SEC, row);
                cv_rrect(x - 1, y + 1, 11, 15, 3, row, T_SEC);
            }
            if (step_lanes(st) & b)
                cv_rrect(x, y + 2, 9, 13, 3, (step_accents(st) & b) ? T_TEXT : T_KNOB(l), row);   /* (each lane a colour) */
            else
                cv_disc(x + 4, y + 8, i % 4u == 0u || sel ? 4 : 3, i % 4u == 0u || sel ? T_MID : T_DIM, 0);
        }
    }
}
/* EG RATE / EG LVL: the operators as tabs over the panel, the one shown filled */
#define PV_OPTAB_H 24
static void pv_optabs(int32_t y)
{
    uint32_t i;
    char b[4] = {'O', 'P', '1', 0};
    cv_begin(240, PV_OPTAB_H + 2, T_BG);                /* (and the 2 rows down to the panel) */
    for (i = 0; i < 6u; i++) {
        int on = i == fm6_opsel % 6u;
        int32_t x = 2 + (int32_t)i * 40;
        b[2] = (char)('1' + i);
        cv_rrect(x, 0, 36, PV_OPTAB_H, 6, on ? T_THEME : T_SURF, T_BG);
        cv_text_c(x + 18, ty_y(PV_OPTAB_H / 2, TY_LB), TY_LB, b, on ? T_INK : T_MID, on ? T_THEME : T_SURF);
    }
    cv_blit(0, (uint32_t)y);
}
static uint32_t pv_curve_sig(void)
{
    const page_t *pg = cur_page();
    uint32_t h = 2166136261u, c;
    for (c = 0; c < 4u; c++)
        h = (h ^ (uint32_t)(pg->id[c] < P_COUNT ? TSEL->p[pg->id[c]] : 0)) * 16777619u;
    h = (h ^ (uint32_t)(TSEL->p[P_LD_PIT] + 131 * TSEL->p[P_LD_FLT] + 17161 * TSEL->p[P_LD_SHP])) * 16777619u;
    h = (h ^ (uint32_t)(TSEL->p[P_LD_AMP] + 256 * TSEL->p[P_LPOL] + 65536 * TSEL->p[P_LWAVE])) * 16777619u;
    h ^= fm6_pgen[song.sel % NTRK] * 2654435761u + fm6_opsel * 131u;   /* (FM6's EG pages: its patch, the operator) */
    return h ^ (uint32_t)(pv_hot() + 1) * 40503u ^ ux.gen * 977u ^ ux.pal * 31u ^ ui.page * 7919u ^ song.sel * 13u ^
           pv_hero_sig() * 2246822519u;
}

/* ---------------------------------------------------------- RINGS --- */

/* a page with a cutoff: the filter's response (flat, the resonance's peak at the cutoff, then the slope); h: the
 * panel's rows */
static void pv_filter(int32_t cut, int32_t res, int32_t h)
{
    int32_t x, fc = 16 + clamp(cut, 0, 127) * 196 / 127, peak = clamp(res, 0, 127) * 30 / 127, y0 = h * 2 / 5 * 16, py = 0;
    for (x = 10; x < h - 10; x += 5)
        cv_rect(fc, x, 1, 2, ux_mix(T_PANEL, T_THEME, 60));
    for (x = 0; x < 216; x++) {
        int32_t d = 12 + x - fc, y = y0 - peak * 16 * 64 / (64 + d * d) + (d > 0 ? d * 14 : 0);
        y = clamp(y, 8 * 16, (h - 10) * 16);
        cv_vspan_aa(12 + x, x ? py : y, y, T_THEME);
        py = y;
    }
}
/* which picture: 3 a filter (cut: its knob), 4 FM6's chart, 0 the scope */
/* ARP (mock/r6_pages ARP): the notes it plays, a block a step (16 at most): the keys held (else C3 E3 G3) over OCT
 * octaves in MODE's order, low to high bottom to top; the notes and the octaves at the top. OFF: dim */
static void pv_arp(int32_t h)
{
    static const uint8_t DEF[3] = {48, 52, 55};
    const track_t *t = TSEL;
    uint8_t base[16], seq[16];
    uint32_t nb = t->nheld ? t->nheld : 3u, n = 0, o, i, j, oct = (uint32_t)clamp(t->p[P_AOCT], 1, 4), mode = (uint32_t)t->p[P_AMODE];
    int32_t lo = 127, hi = 0, w;
    char b[32];
    if (nb > 16u) nb = 16u;
    for (i = 0; i < nb; i++) base[i] = t->nheld ? t->held[i] : DEF[i];
    if (mode != 5u)                                     /* (ORD: as played; the others from the lowest) */
        for (i = 1; i < nb; i++)
            for (j = i; j > 0 && base[j - 1] > base[j]; j--) { uint8_t x = base[j]; base[j] = base[j - 1]; base[j - 1] = x; }
    for (o = 0; o < oct; o++)
        for (i = 0; i < nb && n < 16u; i++) seq[n++] = (uint8_t)clamp(base[i] + 12 * (int32_t)o, 0, 127);
    if (mode == 2u)                                     /* DN */
        for (i = 0; i < n / 2u; i++) { uint8_t x = seq[i]; seq[i] = seq[n - 1u - i]; seq[n - 1u - i] = x; }
    else if (mode == 3u)                                /* UPDN: up, then down without the ends again */
        for (i = n >= 2u ? n - 2u : 0u; i > 0u && n < 16u; i--) seq[n++] = seq[i];
    else if (mode == 4u)                                /* RND: a shuffle (as a picture: one of its orders) */
        for (i = n; i > 1u; i--) { j = (i * 7u + 3u) % i; uint8_t x = seq[i - 1u]; seq[i - 1u] = seq[j]; seq[j] = x; }
    for (i = 0; i < n; i++) { lo = seq[i] < lo ? seq[i] : lo; hi = seq[i] > hi ? seq[i] : hi; }
    w = n ? 208 / (int32_t)n : 208;
    for (i = 0; i < n; i++) {                           /* (under the notes' line: rows 34 .. h - 12) */
        int32_t y = h - 19 - (hi > lo ? (seq[i] - lo) * (h - 53) / (hi - lo) : (h - 53) / 2);
        cv_rrect(16 + (int32_t)i * w, y, w - 4 < 18 ? w - 4 : 18, 7, 2, mode ? T_KNOB((uint32_t)(seq[i] - lo) / 12u) : T_DIM,
                 T_PANEL);                              /* (each octave a colour) */
    }
    b[0] = 0;
    for (i = 0; i < nb && i < 4u; i++) {
        note_name(b + str_len(b), base[i]);
        str_cpy(b + str_len(b), " ", 2);
    }
    str_cpy(b + str_len(b), "\xB7 ", 4);
    fmt_int(b + str_len(b), (int32_t)oct);
    str_cpy(b + str_len(b), " oct", 5);
    if (!pv_hero_on())                                  /* (the hero over it: the picture's words go) */
        cv_free_text(12, 6, TY_LB, b, T_MID, T_PANEL, PV_PANEL_W - 24);
}
/* PATTERN (mock/r6_pages PATTERN): the 64 steps as dots, four rows of 16: a note or a hit the track's colour, a rest
 * a line, past LEN dimmer still; the step playing white; LEN of 64 as the bar under them */
#define PV_STEP_X(s) (22 + (int32_t)((s) % 16u) * 12 + (int32_t)((s) % 16u / 4u) * 4)   /* a step's dot (7 px) */
static void pv_steps(int32_t h)
{
    const track_t *t = TSEL;
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), s, ph = pv.pplay >= 0 ? (uint32_t)pv.pplay : 0xFFu;
    for (s = 0; s < NSTEP; s++) {
        int32_t x = PV_STEP_X(s), y = 16 + (int32_t)(s / 16u) * (h - 40) / 3;
        uint16_t c = s == ph ? T_TEXT : s >= len ? ux_mix(T_PANEL, T_LINE, 50) :
                     step_on(&seq_steps(t)[s]) ? T_KNOB(s % 16u / 4u) : T_LINE;   /* (each beat a colour) */
        cv_disc(x, y, s == ph || (s < len && step_on(&seq_steps(t)[s])) ? 7 : 5, c, 0);
    }
    cv_rrect(16, h - 12, 208, 4, 2, T_LINE, T_PANEL);
    cv_rrect(16, h - 12, (int32_t)len * 208 / NSTEP, 4, 2, T_THEME, T_LINE);
}


static uint32_t pv_pic(int32_t *cut, int32_t *res)
{
    const page_t *pg = cur_page();
    uint32_t e = TSEL->eng_req % NENGINES, c;
    *cut = -1;
    *res = 0;
    if(pg->fam==FAM_FX)return 9u;
    if (pg->graph == GR_ARP || pg->graph == GR_STEPS)
        return pg->graph == GR_ARP ? 5u : 6u;
    if (pg->fam == FAM_ARP)                             /* ARP 2: the notes it plays too */
        return 5u;
    if (pg->graph == GR_CHORD)                          /* CHORD: its name, its notes (ui_graph.c) */
        return 8u;
    if ((pg->scope == SC_ENGINE || pg->scope == SC_FM6 || pg->scope == SC_FMOP) && e == ENGI_FM6)
        return 4;
    for (c = 0; c < 4u; c++) {
        int16_t *vp;
        const param_desc_t *d = page_desc(pg, c, &vp);
        if (!d || !vp)
            continue;
        if (d->fmt == F_CUTOFF || str_eq(d->label, "CUTOFF")) *cut = *vp;   /* (the Prophet's: by its label) */
        else if (str_eq(d->label, "RES") || str_eq(d->label, "RESO")) *res = *vp;
    }
    return *cut >= 0 ? 3u : 0u;
}
/* FX follows R5: four rings, a rounded track-colour picture, compact labels.
 * WIDTH explicitly names its two sides and the mono tradeoff. */
static void pv_fx(int32_t h)
{
    const aafont_t *lf = TY_LB;
    int words = !pv_hero_on();                          /* (the hero over it: the picture's words go) */
    if(cur_page()->id[0]==G_TRK_SEND || cur_page()->id[0]==P_FXSEND){
        if(!words)return;
        uint32_t sends=cur_page()->id[0]==P_FXSEND;
        cv_free_text(12,6,lf,song.g[G_FXMODE]?"Independent FX sends":"Track to shared FX",T_MID,T_PANEL,PV_PANEL_W-24);
        cv_free_text(12,h/2-lf->h,lf,sends?"One level per effect":song.g[G_FXMODE]?"SEND 1: this track":"SEND: this track",T_THEME,T_PANEL,PV_PANEL_W-24);
        cv_free_text(12,h/2+4,lf,sends?"Returns join the dry mix":"RETURN: all tracks",T_THEME,T_PANEL,PV_PANEL_W-24);return;
    }
    uint32_t base=cur_page()->id[0],master=cur_page()->scope==SC_GLOBAL,slot=(base-(master?G_MFXTYPE:P_FXTYPE))/FX_PARAMS;
    const int16_t *values=master?song.g:TSEL->p;uint32_t type=(uint32_t)clamp(values[base],0,FX_COUNT-1);
    static const char *const hint[FX_COUNT]={"Dry signal","Moving notches","All-pass L / dry R","Random vowels","Phase / crossover jitter","Jumping resonance","Short lo-fi echoes","Small lo-fi space","Diffused stereo reverb","Stereo pitch shimmer"};
    int32_t mix=values[base+1u],cha=values[base+3u],cy=h/2+2,py=cy*16;
    if(words)cv_free_text(12,6,lf,hint[type],T_MID,T_PANEL,PV_PANEL_W-24);
    for(int32_t x=0;x<208;x++){
        int32_t ph=(int32_t)(sine_i((uint32_t)x*0x06000000u)>>10);
        int32_t y=cy*16;
        if(type==FX_PHASER || type==FX_WIDTH || type==FX_CHORUS)y+=ph*mix/9;
        else if(type==FX_FORMANT)y-=((ph+32)*mix)/15;
        else if(type==FX_TAPE || type==FX_SPIT)y+=(ph*((x*(cha+1)/32)%32))/4;
        else if(type>=FX_ECHO)y+=(ph*mix*(208-x))/1900;
        y=clamp(y,(lf->h+10)*16,(h-lf->h-10)*16);cv_vspan_aa(16+x,py,y,type&&mix?T_THEME:T_DIM);py=y;
    }
    const char *foot=fx_state[(master?NPART:song.sel%NPART)*FX_SLOTS+slot].failed?"MEMORY FULL":type==FX_WIDTH?"Mono changes the tone":type==FX_OFF || !mix?"Turn MIX to hear FX":master?"Shared by all tracks":"OCT+ sound actions";
    if(words)cv_free_text(12,h-lf->h-6,lf,foot,T_DIM,T_PANEL,PV_PANEL_W-24);
}
static void pv_hero_graph(void) { pv_hero(3, 234, 0); }   /* (over draw_graph's panel: x 3, 234 wide) */
static void pv_picture(void)
{
    int32_t cut, res, y = PV_PIC_Y + pv_tab_h, h = PV_PIC_H - pv_tab_h;
    uint32_t pic = pv_pic(&cut, &res), sig = pv_curve_sig() ^ pic * 131u ^ (uint32_t)(cut + 1) * 7919u ^ (uint32_t)h * 613u, i;
    int32_t pp = pic == 6u && song.playing ? (int32_t)(TSEL->seq_idx % (uint32_t)clamp(TSEL->p[P_SLEN], 1, NSTEP)) : -1;
    sig ^= pic == 0u ? (ui.frame >> 1) * 2654435761u : 0u;   /* (the scope: every other frame) */
    if(pic==9u && cur_page()->id[0]!=G_TRK_SEND && cur_page()->id[0]!=P_FXSEND)sig^=(uint32_t)fx_state[(cur_page()->scope==SC_GLOBAL?NPART:song.sel%NPART)*FX_SLOTS+(cur_page()->id[0]-(cur_page()->scope==SC_GLOBAL?G_MFXTYPE:P_FXTYPE))/FX_PARAMS].failed*347u;
    if (pic >= 5u) {                                    /* ARP, PATTERN: the keys and steps */
        sig ^= steps_hash(TSEL) + (song.playing ? 1u + TSEL->seq_idx * 257u : 0u) * 2246822519u;
        for (i = 0; i < TSEL->nheld && i < 16u; i++)
            sig = (sig ^ TSEL->held[i]) * 16777619u;
        sig ^= TSEL->nheld * 131u;
    }
    if (pic == 4u || pic == 8u) {                       /* FM6: its chart, CHORD: its chord, as the panel draws them; the
                                                         * hero over them (draw_graph's graph_over) */
        uint32_t hs = pv_hero_sig() ^ (uint32_t)h;
        if (hs != pv.hero)
            ui.graph_sig = 0;
        pv.hero = hs;
        graph_y = y;
        graph_h = h;
        graph_oy = h < H_GRAPH ? GOY - 8 : GOY;         /* (under the designer's tabs: the chart a little higher) */
        graph_over = pv_hero_graph;
        draw_graph();
        graph_over = 0;
        graph_y = Y_GRAPH;
        graph_h = H_GRAPH;
        graph_oy = GOY;
        return;
    }
    if (!ui.force && sig == pv.panel && pp == pv.pplay)
        return;
    pv.panel = sig;                                     /* (PATTERN, the step playing alone moved: cv_blit sends its */
    pv.pplay = pp;                                      /*  two dots) */
    cv_begin(240, (uint32_t)h, T_BG);
    cv_rrect(PV_PANEL_X, 0, PV_PANEL_W, h, 6, T_PANEL, T_BG);
    cv_bg = T_PANEL;
    if (pic == 5u) pv_arp(h);
    else if (pic == 6u) pv_steps(h);
    else if (pic == 3u) pv_filter(cut, res, h);
    else if (pic == 9u) pv_fx(h);
    else stage_wave(T_THEME, 8, h - 16);
    pv_hero(PV_PANEL_X, PV_PANEL_W, 0);                 /* the knob turning over it */
    cv_blit(0, (uint32_t)y);
}

/* ---------------------------------------------------------- MIXER --- */
/* MIXER (large type: C, rows as Stage's, ui_stage.c st_row_*): a track a row tinted in its colour (the selected one
 * more, outlined): its numeral, its sound, its level in dB at the right; under them the level as a bar in the track's
 * colour, the meter under it, its pan at the right ("L20", "C"); muted: dim, "Mute" in red; armed: a REC dot on the
 * numeral. The knob turning (LEVEL, PAN, MUTE: the selected track's) in text */
#define PV_MIX_BAR_X 46                                 /* the level's bar: x 46 .. 46 + PV_MIX_BAR_W, in the row */
#define PV_MIX_BAR_W 120
static struct { uint32_t strip[NTRK]; uint8_t meter[NTRK]; } pvm;
static int32_t pv_mix_y(int32_t h) { return (h - (TY_TX->cap + 8 + 10)) / 2; }   /* the row's block: its first capital */
/* track k's meter: a 3 px bar under its level's, as long as the output now */
static void pv_meter_draw(uint32_t k, int32_t x, int32_t y, uint16_t c, uint16_t bg)
{
    int32_t w = pvm.meter[k] * PV_MIX_BAR_W / (TS_MH - 2);
    cv_rrect(x, y, PV_MIX_BAR_W, 3, 1, ux_mix(bg, T_LINE, 60), bg);
    if (w > 0)
        cv_rrect(x, y, w, 3, 1, c, T_LINE);
}
static void pv_meter(uint32_t k, uint16_t c)
{
    int32_t h = st_row_h();
    uint16_t bg = ux_mix(T_BG, c, k == song.sel ? 24 : 11);
    cv_begin(PV_MIX_BAR_W, 3, bg);
    pv_meter_draw(k, 0, 0, c, bg);
    cv_blit((uint32_t)(BOX_X + PV_MIX_BAR_X), (uint32_t)(st_row_y(k) + pv_mix_y(h) + TY_TX->cap + 8 + 7));
}
static void pv_strip(uint32_t k)
{
    track_t *t = &trk[k];
    uint32_t sel = k == song.sel, mute = t->p[P_MUTE] != 0, arm = (song.rec >> k) & 1u, lvl = trk_level(k), sig;
    uint32_t hot = sel && (ui.hot_t || pv_hero_on()) ? ui.hot_col + 1u : 0u;   /* 1 LEVEL, 2 PAN, 3 unused, 4 MUTE */
    int32_t pk = t->peak, m, pan = clamp(t->p[P_PAN], -64, 63), h = st_row_h(), y = st_row_y(k), top;
    uint16_t tc = mute ? T_DIM : T_TRK(k), bg = ux_mix(T_BG, mute ? T_TRK(k) : tc, sel ? 24 : 11);
    const aafont_t *tf = TY_TX, *lf = TY_LB;
    int32_t T = pv_mix_y(h), ly = T + tf->cap + 8, cs = h >= 80 ? 34 : 26;
    char nm[20], b[12], d[2] = {(char)('1' + k), 0};
    t->peak = 0;
    m = mute ? 0 : meter_px(pk);
    if (m < pvm.meter[k] - 1)
        m = pvm.meter[k] - 1;                           /* (falls ~2 dB a frame) */
    sound_name(t, nm);
    sig = str_hash(1u + sel * 2u + mute * 4u + arm * 8u + hot * 16u, nm) + lvl * 7919u +
          (uint32_t)(pan + 128) * 104729u + (uint32_t)h * 31u + ux.gen * 977u + ux.pal * 31u;
    if (!ui.force && sig == pvm.strip[k]) {
        if (m != pvm.meter[k]) {                        /* the meter alone */
            pvm.meter[k] = (uint8_t)(m < 0 ? 0 : m);
            pv_meter(k, tc);
        }
        return;
    }
    pvm.strip[k] = sig;
    pvm.meter[k] = (uint8_t)(m < 0 ? 0 : m);
    for (top = 0; top < h; top += (int32_t)(CV_MAX / BOX_W)) {   /* (a tall row: in slices) */
        int32_t sh = h - top < (int32_t)(CV_MAX / BOX_W) ? h - top : (int32_t)(CV_MAX / BOX_W), x;
        cv_begin(BOX_W, (uint32_t)sh, T_BG);
        cv_oy = -top;
        if (sel) {
            cv_rrect(0, 0, BOX_W, h, 8, mute ? T_DIM : T_TRK(k), T_BG);
            cv_rrect(2, 2, BOX_W - 4, h - 4, 6, bg, mute ? T_DIM : T_TRK(k));
        } else {
            cv_rrect(0, 0, BOX_W, h, 8, bg, T_BG);
        }
        cv_rrect(6, h / 2 - cs / 2, cs, cs, 7, tc, bg);  /* the numeral */
        cv_text_c(6 + cs / 2, ty_y(h / 2, tf), tf, d, T_INK, tc);
        if (arm)
            cv_disc(6 + cs - 2, h / 2 - cs / 2 + 2, 9, T_REC, 0);
        /* the level in dB at the right, the sound before it */
        if (lvl) {
            int32_t db = LEVEL_DB_X10[lvl];
            fmt_int(b, (db + (db < 0 ? -5 : 5)) / 10);
        } else {
            str_cpy(b, "Off", sizeof b);
        }
        x = 228;
        if (lvl && text_w(lf, nm) <= 228 - text_w(lf, "dB") - 3 - text_w(tf, b) - 8 - PV_MIX_BAR_X)   /* (the unit goes
                                                         * before the name is cut) */
            x = cv_text_r(x, ty_y(T + tf->cap / 2, lf), lf, "dB", T_MID, bg) - 3;
        x = cv_text_r(x, ty_y(T + tf->cap / 2, tf), tf, b, hot == 1u ? tc : mute ? T_DIM : T_TEXT, bg);
        cv_free_text(PV_MIX_BAR_X, ty_y(T + tf->cap / 2, ty_fit(tf, nm, x - 8 - PV_MIX_BAR_X)), ty_fit(tf, nm, x - 8 - PV_MIX_BAR_X),
                     nm, mute ? T_DIM : T_TEXT, bg, x - 8 - PV_MIX_BAR_X);
        /* the level's bar, the meter under it; the pan (or Mute) at the right */
        cv_rrect(PV_MIX_BAR_X, ly, PV_MIX_BAR_W, 5, 2, ux_mix(bg, T_LINE, 60), bg);
        if (lvl)
            cv_rrect(PV_MIX_BAR_X, ly, (int32_t)lvl * PV_MIX_BAR_W / 127 < 4 ? 4 : (int32_t)lvl * PV_MIX_BAR_W / 127, 5, 2,
                     hot == 1u ? T_TEXT : tc, T_LINE);
        pv_meter_draw(k, PV_MIX_BAR_X, ly + 7, tc, bg);
        if (mute) {
            cv_text_r(228, ty_y(ly + 5, lf), lf, "Mute", hot == 4u ? T_TEXT : T_REC, bg);
        } else {
            if (pan) {
                b[0] = pan < 0 ? 'L' : 'R';
                fmt_int(b + 1, pan < 0 ? -pan : pan);
            } else {
                str_cpy(b, "C", sizeof b);
            }
            cv_text_r(228, ty_y(ly + 5, lf), lf, b, hot == 2u ? T_TEXT : T_MID, bg);
        }
        cv_oy = 0;
        cv_blit(BOX_X, (uint32_t)(y + top));
    }
}

/* ----------------------------------------------------------- draw --- */
static void pv_draw(void)
{
    uint32_t kind = pv_kind(), sig;
    int32_t px;
    if (kind == PV_NOTES) {
        if (ui.force) {
            lcd_fill(0, H_HEAD, 240, 240 - H_HEAD, T_BG);
            ui.graph_sig = 0;
        }
        draw_head();
        col_style = CS_CARD;                            /* (the strip, as a page's) */
        draw_columns();
        sig = graph_sig(0);
        px = notes_play_x(TSEL);
        if (ui.force || sig != ui.graph_sig || px != proll.play) {   /* the roll, in two slices (its labels may cross
                                                                      * the seam); the playhead alone: its columns */
            int32_t top, from = proll.play, cols = !ui.force && sig == ui.graph_sig;
            ui.graph_sig = sig;
            proll.play = px;
            for (top = 0; top < PR_H; top += H_GRAPH) {
                int32_t sh = PR_H - top < H_GRAPH ? PR_H - top : H_GRAPH;
                cv_begin(240, (uint32_t)sh, T_BG);
                cv_oy = -top;
                cv_rrect(PV_PANEL_X, 0, PV_PANEL_W, PR_H, 6, T_PANEL, T_BG);
                cv_bg = T_PANEL;
                cv_scroll = 1;
                graph_recorded_notes(TSEL, T_THEME);
                cv_scroll = 0;
                cv_oy = 0;
                if (cols)
                    pv_cols((uint32_t)(PR_TOP + top), from, px, 1);
                else
                    cv_blit(0, (uint32_t)(PR_TOP + top));
            }
        }
        return;
    }
    if (kind == PV_GRID) {
        if (ui.force)
            lcd_fill(0, H_HEAD, 240, 240 - H_HEAD, T_BG);
        draw_head();
        col_style = CS_CARD;                            /* (the strip, as a page's) */
        draw_columns();
        sig = graph_sig(0);
        px = pv_grid_play();
        if (ui.force || sig != ui.graph_sig || px != pv.gplay) {   /* (the step playing alone: cv_blit sends its mark) */
            ui.graph_sig = sig;
            pv.gplay = px;
            pv_panel(PV_GRID_Y, PV_GRID_H, pv_grid);
        }
        return;
    }
    if (kind == PV_MOD) {
        if (ui.force)
            lcd_fill(0, H_HEAD, 240, 240 - H_HEAD, T_BG);
        draw_head();
        pv_mod_rows();                                  /* (the routes say what the knobs turn: no strip) */
        col_style = CS_CAPTURE;                         /* (the hero's words: draw_column records them) */
        draw_columns();
        col_style = CS_CARD;
        return;
    }
    if (kind == PV_MIXER) {
        uint32_t k;
        if (ui.force)
            lcd_fill(0, H_HEAD, 240, 240 - H_HEAD, T_BG);
        draw_head();
        for (k = 0; k < trk_n(); k++)
            pv_strip(k);
        return;
    }
    pv_tab_h = (kind == PV_RINGS || kind == PV_CURVE) && designer_on() && designer_tabs_on() ? SEC_TAB_H : 0;
    if (kind == PV_CURVE && cur_page()->graph == GR_FMEG)   /* (EG RATE / LVL: the operators' tabs) */
        pv_tab_h = SEC_TAB_H;
    /* RINGS, CURVE: the header, the sections' tabs, the strip, the sound, then the picture (the busiest last) */
    if (ui.force) {                                     /* (the parts then cover what they draw) */
        lcd_fill(0, H_HEAD, 240, PV_SOUND_Y - H_HEAD, T_BG);
        ui.graph_sig = 0;
    }
    draw_head();
    if (pv_tab_h && designer_on()) {                    /* the sound designer: its stages */
        designer_tabs();
    } else if (kind == PV_CURVE && cur_page()->graph == GR_FMEG) {
        pv_optabs(H_HEAD + 1);                          /* EG RATE / LVL: the operators */
    }
    if (sec_on())                                       /* an engine in sections: the page noted (the header says where:
                                                         * the section's page, its dots) */
        sec_note();
    card_y = Y_LABEL + pv_tab_h;
    col_style = CS_CARD;
    draw_columns();
    card_y = Y_LABEL;
    pv_sound();
    {   /* the picture straight on the background (no panel: the screen calm) */
        uint16_t panel = ux.panel;
        ux.panel = ux.bg;
        if (kind == PV_RINGS) {
            pv_picture();
        } else {
            sig = pv_curve_sig();
            if (ui.force || sig != pv.panel) {
                pv.panel = sig;
                pv_panel(PV_CURVE_Y + pv_tab_h, PV_CURVE_H - pv_tab_h, pv_curve);
            }
        }
        ux.panel = panel;
    }
}
