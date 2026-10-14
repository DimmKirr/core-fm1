/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Melodee's Stage (HOME), in the large type (docs/design/README.md "Large type": C, rows).
 * Stage: the header (where you are: the engine, a kit's name); a full-width row per track in use, sharing rows
 * 32 .. 205 (four 42 px, three 56, two 86, one 174): the track's numeral filled in its colour, the sound (20 px; two
 * tracks 28 px; one track 48 px when it fits), under it its role (the sound's category) in its colour and what plays
 * (the pattern, the next one waiting for the bar; one track: ARP, HOLD, CHORD, the scale too), the bar playing as
 * 4 x 4 steps at the right (one, two tracks: 16 in a row under the words); each row tinted in its track's colour, the
 * selected one more and outlined in it; the pattern a pill in it. No level (MIXER has it). Under them the band (rows 210 .. 239): the notes the selected track played and
 * their chord over its waveform, faint (a DRUM track: its lanes, lit as they hit). The first turn of a knob drops the
 * track's four knobs (its engine's, stage_page) in as the pages' strip (ST_DROP frames) and the knob turning comes
 * up as the hero over what it shapes (a filter's response, an envelope, the waveform), as on a page; ST_KNOB_MS
 * after the last turn they go and the rows are whole again.
 * Each part remembers what it drew: a row redraws when its sound, pattern or steps change, the playhead its steps
 * alone; the band follows the scope every other frame, drawn last (the SPI, 12 MHz, sends it while the next frame is
 * worked out: a blit right after it would wait for it, lcd_window).
 */
#define ST_ROW_Y 32                                     /* the rows: 32 .. 205, 2 px apart */
#define ST_ROWS_H 174
#define ST_BAND_Y 210                                   /* the band: rows 210 .. 239 */
#define ST_BAND_H 30
#define ST_KNOB_MS 1500u                                /* the knobs stay this long after the last turn */
#define ST_DROP 4u                                      /* .. and drop in over this many frames */
#define ST_AREA_W 232                                   /* (MIXER's strips: x 4 .. 236) */
#define ST_GRID 30                                      /* a row's 4 x 4 steps: 30 x 30 at its right */

static int32_t st_row_h(void)
{
    int32_t n = (int32_t)trk_n();
    return (ST_ROWS_H - 2 * (n - 1)) / n;
}
static int32_t st_row_y(uint32_t k) { return ST_ROW_Y + (int32_t)k * (st_row_h() + 2); }
/* MIXER's strips (ui_pages.c pv_strip): four 54 px, fewer share the place, 4 px apart (three: 74, 5 px apart) */
static int32_t st_col_w(void)
{
    int32_t n = (int32_t)trk_n();
    return n >= NTRK ? CARD_W : (ST_AREA_W - 4 * (n - 1)) / n;
}
static int32_t st_col_x(uint32_t k)
{
    int32_t n = (int32_t)trk_n(), w = st_col_w();
    if (n >= NTRK)
        return CARD_X(k);
    return 4 + (int32_t)k * (w + 4) + (n > 1 ? (int32_t)k * (ST_AREA_W - n * w - 4 * (n - 1)) / (n - 1) : 0);
}
/* the x of a text centred on cx in at most maxw px */
static int32_t st_mid_x(int32_t cx, const aafont_t *f, const char *s, int32_t maxw)
{
    int32_t w = text_w(f, s);
    return cx - (w < maxw ? w : maxw) / 2;
}
static int pattern_empty(uint32_t k, uint32_t b);       /* ui_patterns.c */
/* track k plays no pattern: all eight empty (no note, no motion), none waiting, not armed (the synth's Stage) */
static int stage_seq_idle(uint32_t k)
{
    uint32_t b;
    if (trk[k].pattern_next < NPAT || ((song.rec >> k) & 1u))
        return 0;
    for (b = 0; b < NPAT; b++)
        if (!pattern_empty(k, b))
            return 0;
    return 1;
}

static struct {
    uint32_t col[NTRK], grid[NTRK];                     /* each row's drawn state, its steps' */
    uint8_t hit_t[NLANE];                               /* frames a DRUM lane's name stays lit */
    uint8_t hit_trk;                                    /* .. of this track */
    uint32_t knob_ms;                                   /* the last knob turn on Stage (| 1), 0 none */
    uint8_t out, drop;                                  /* the knobs out; frames still dropping */
} stage;

static void view_reset(void);                           /* ui_view.c */
static void hero_touch(void);                           /* ui_pages.c: the hero, as on a page */
static void pv_hero(int32_t x0, int32_t pw, int corner);
static void stage_knob_touch(void) { stage.knob_ms = fm1_ms | 1u; hero_touch(); }   /* (ui_input.c: a knob on Stage) */
static int stage_knobs_out(void) { return stage.knob_ms && fm1_ms - (stage.knob_ms & ~1u) < ST_KNOB_MS; }

/* ------------------------------------------------------- the knobs --- */
/* a knob's label: the panel's, plain words for CZ-1's (its panel says R1, L1 ..) */
static const char *stage_label(uint32_t k, const param_desc_t *d)
{
    static const char *const CZ_L[4] = {"WAVE", "DCW", "DETUNE", "VIBRATO"};
    return stage_pg.scope == SC_CZ1 ? CZ_L[k & 3u] : d->label;
}
/* a knob's value as Stage shows it: the Prophet's 0..127 panel values in % (the mockup's CUTOFF 62 %) */
static void stage_value(const page_t *sp, const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    if (sp->scope == SC_P5 && d->fmt == F_INT && d->min == 0 && d->max == 127) {
        fmt_int(val, (v * 100 + 63) / 127);
        *unit = "%";
        return;
    }
    param_format(d, v, val, unit);
}
/* the four cards (while the knobs are out): a value without effect on the sound as it is drawn dim */
static void stage_columns(void)
{
    const page_t *sp = stage_page();
    uint32_t c;
    for (c = 0; c < 4u; c++) {
        int16_t *vp;
        const char *unit;
        char val[12];
        const param_desc_t *d = page_desc(sp, c, &vp);
        int dim = (sp->scope == SC_CZ1 && !cz_ed_active(cz_patch[song.sel % NTRK].raw, sp->id[c])) ||
                  (sp->scope == SC_P5 && !p5_ed_active(TSEL, sp->id[c]));
        if (!d || !vp || !d->label || d->label[0] == '-') {
            draw_column(c, "", "", "", T_THEME, -1, ICON_NONE);
            continue;
        }
        stage_value(sp, d, *vp, val, &unit);
        draw_column(c, stage_label(c, d), val, unit, dim ? T_DIM : VAL(c), d->fmt == F_ENUM && d->max < 2 ? -1 : RATIO(d, *vp),
                    ICON_NONE);
    }
}

/* ------------------------------------------------------- the band --- */
/* the output's waveform over w px from x 8, smooth: triggered on a rising zero crossing, auto-scaled, each sample
 * the mean of three, 2 px anti-aliased (gfx.c cv_vspan_aa); rows top .. top + h */
static void stage_wave(uint16_t c, int32_t top, int32_t h)
{
    int16_t *snap = scope_snap;
    uint32_t w = scope_w, i, trig = 1;
    int32_t peak = 1500, x, py = 0, cy = (top + h / 2) * 16 - 16;
    for (i = 0; i < SCOPE_N; i++) {
        snap[i] = scope_buf[(w + i) & (SCOPE_N - 1u)];
        if (snap[i] > peak) peak = snap[i];
        else if (-snap[i] > peak) peak = -snap[i];
    }
    for (i = 2; i < SCOPE_N - 240u; i++)
        if (snap[i - 1] < 0 && snap[i] >= 0) { trig = i; break; }
    for (x = 0; x < 224; x++) {
        uint32_t k = trig + (uint32_t)x;
        int32_t v = ((int32_t)snap[k - 1u] + 2 * snap[k] + snap[k + 1u]) / 4;
        int32_t y = cy - v * (h / 2 - 4) * 16 / peak;
        cv_vspan_aa(8 + x, x ? py : y, y, c);
        py = y;
    }
}

/* the notes the selected track played (live_last: its last voicing, held or let go), the chord they make, in front of
 * what is on the canvas (cv_over), capitals centred on row cy: the chord (the text face), then the notes (the label
 * face, the track's colour). Held: in text; let go: faded into the background. 0 none, 1 let go, 2 held */
static int stage_notes(int32_t x1, int32_t cy)
{
    uint32_t bits[4], i, n = 0, pcs = 0, root = 0, bass, held = 0, k = song.sel % NTRK;
    uint8_t note[8];
    char b[48];
    const char *q;
    uint16_t c, cn;
    int32_t x = 10;
    const aafont_t *tf = TY_TX, *lf = TY_LB;
    fm1_irq_off();                                      /* one coherent ISR snapshot, nothing drawn meanwhile */
    for (i = 0; i < 4u; i++) {
        bits[i] = live_last[k][i];
        held |= live_held[k][i];
    }
    fm1_irq_on();
    for (i = 0; i < 128u; i++)
        if ((bits[i >> 5] >> (i & 31u)) & 1u) {
            pcs |= 1u << (i % 12u);
            if (n < sizeof note) note[n] = (uint8_t)i;
            n++;
        }
    if (!n) return 0;
    c = held ? T_TEXT : ux_mix(T_BG, T_THEME, 75);
    cn = held ? T_THEME : ux_mix(T_BG, T_THEME, 55);
    bass = note[0] % 12u;
    q = micro_active(TSEL) ? 0 : chord_of(pcs, bass, &root);
    if (q) {
        x = cv_text(x, ty_y(cy, tf), tf, N_NOTE[root], c);
        x = cv_text(x, ty_y(cy, tf), tf, q, c);
        if (root != bass) {
            x = cv_text(x + 2, ty_y(cy, tf), tf, "/", c);
            x = cv_text(x, ty_y(cy, tf), tf, N_NOTE[bass], c);
        }
        x += 10;
    }
    notes_fit(b, note, n, lf, x1 - x);
    cv_text(x, ty_y(cy, lf), lf, b, cn);
    return (int)(1u + (held != 0u));
}
/* HOME's notes alone (tests/ui_test.c): 1 = there were notes */
static int graph_notes(void) { return stage_notes(220, ST_BAND_H / 2) != 0; }

/* a DRUM track: its eight lanes across the band, a lane's name lit (the track's colour, a bar under it) while it hits
 * (drum_flash: the ISR's), else dim */
static void stage_hits(void)
{
    uint32_t l;
    const aafont_t *lf = TY_LB;
    cv_over = 0;
    for (l = 0; l < NLANE; l++) {
        int32_t xc = 4 + 29 * (int32_t)l + 14;
        int lit = stage.hit_t[l] != 0u;
        cv_text_c(xc, ty_y(ST_BAND_H / 2 - 2, lf), lf, drum_lane_abbr(TSEL, l), lit ? T_THEME : T_DIM, T_BG);
        if (lit)
            cv_rrect(xc - 9, ST_BAND_H - 5, 18, 3, 1, T_THEME, T_BG);
    }
}

/* While the knobs are out, the hero's picture: what the one turning shapes (a filter's response: cutoff, resonance,
 * the Prophet's envelope amount too; an envelope: its attack .. release); 0 = the waveform */
enum { SP_NONE, SP_FILTER, SP_ENV };
static uint32_t stage_picture(int32_t *v)
{
    const page_t *sp = stage_page();
    uint32_t hot = ui.hot_col & 3u, id = sp->id[hot], k;
    if (!stage_knobs_out())
        return SP_NONE;
    if (sp->scope == SC_P5) {
        const uint8_t *r = p5_patch_of(TSEL)->raw;
        if (id == P5_CUTOFF || id == P5_RESONANCE || id == P5_ENV_FILTER) {
            v[0] = r[P5_CUTOFF];
            v[1] = r[P5_RESONANCE];
            return SP_FILTER;
        }
        if (id == P5_RELEASE_AMP) {
            v[0] = r[P5_ATTACK_AMP]; v[1] = r[P5_DECAY_AMP]; v[2] = r[P5_SUSTAIN_AMP]; v[3] = r[P5_RELEASE_AMP];
            return SP_ENV;
        }
        return SP_NONE;
    }
    if (sp->scope != SC_ENGINE)
        return SP_NONE;
    if (id >= P_ATK && id <= P_REL) {
        for (k = 0; k < 4u; k++)
            v[k] = TSEL->p[P_ATK + k];
        return SP_ENV;
    }
    {   /* an engine's filter: its F_CUTOFF knob and the RES beside it */
        int32_t cut = -1, res = 0;
        for (k = 0; k < 4u; k++) {
            const param_desc_t *d = track_desc(TSEL, sp->id[k]);
            if (d->fmt == F_CUTOFF) cut = (int32_t)k;
            else if (str_eq(d->label, "RES")) res = TSEL->p[sp->id[k]];
        }
        if (cut < 0 || (hot != (uint32_t)cut && !str_eq(track_desc(TSEL, id)->label, "RES")))
            return SP_NONE;
        v[0] = TSEL->p[sp->id[cut]];
        v[1] = res;
        return SP_FILTER;
    }
}
/* a low-pass response: flat, the resonance's peak at the cutoff (a dotted line), then the slope; cut, res 0..127;
 * rows top .. bot */
static void stage_filter(int32_t cut, int32_t res, uint16_t c, int32_t top, int32_t bot)
{
    int32_t x, fc = 10 + clamp(cut, 0, 127) * 200 / 127, peak = clamp(res, 0, 127) * (bot - top) / 4 / 127;
    int32_t y0 = (top + (bot - top) * 2 / 5) * 16, py = 0;
    for (x = top; x < bot; x += 5)
        cv_rect(fc, x, 1, 2, ux_mix(T_BG, c, 60));
    for (x = 0; x < 220; x++) {
        int32_t d = 8 + x - fc, y = y0 - peak * 16 * 64 / (64 + d * d) + (d > 0 ? d * 14 : 0);
        y = clamp(y, top * 16, bot * 16);
        cv_vspan_aa(8 + x, x ? py : y, y, c);
        py = y;
    }
}

static void stage_panel_hits(void)                      /* every frame: the DRUM lanes hit since the last one */
{
    uint8_t f[NTRK];
    uint32_t k, sel = song.sel % NTRK, l;
    fm1_irq_off();
    for (k = 0; k < NTRK; k++) {
        f[k] = drum_flash[k];
        drum_flash[k] = 0;
    }
    fm1_irq_on();
    if (stage.hit_trk != sel + 1u) {
        stage.hit_trk = (uint8_t)(sel + 1u);
        memset(stage.hit_t, 0, sizeof stage.hit_t);
    }
    for (l = 0; l < NLANE; l++)
        stage.hit_t[l] = (f[sel] >> l) & 1u ? 10u : stage.hit_t[l] ? stage.hit_t[l] - 1u : 0u;
}
/* the band: the selected track's waveform, faint, the chord and notes in front (a DRUM track: its lanes) */
static void stage_band(void)
{
    uint32_t sel = song.sel % NTRK, drum = (uint32_t)drum_track(TSEL), i;
    int held = 0;
    cv_begin(240, ST_BAND_H, T_BG);
    if (!drum) {
        fm1_irq_off();
        for (i = 0; i < 4u; i++) held |= live_held[sel][i] != 0u;
        fm1_irq_on();
    }
    cv_over = 1;                                        /* (blended over what is there) */
    if (drum)
        stage_hits();
    else if (!stage_notes(228, ST_BAND_H / 2)) {        /* nothing played yet: the waveform alone */
        cv_over = 0;
        stage_wave(ux_mix(T_BG, T_THEME, 45), 1, ST_BAND_H - 2);
    }
    cv_over = 0;
    (void)held;
    cv_blit(0, ST_BAND_Y);
}

/* the knobs out: under their strip, what the knob turning shapes, its value as the hero (ui_pages.c pv_hero) */
static void stage_hero(void)
{
    int32_t pv[4];
    uint32_t pic = stage_picture(pv);
    cv_begin(240, H_GRAPH, T_BG);
    cv_bg = T_BG;
    if (pic == SP_FILTER)
        stage_filter(pv[0], pv[1], ux_mix(T_BG, T_THEME, 70), 8, H_GRAPH - 8);
    else if (pic == SP_ENV)
        graph_adsr_v(pv[0], pv[1], pv[2], pv[3], 8, H_GRAPH - 8, ux_mix(T_BG, T_THEME, 70));
    else
        stage_wave(ux_mix(T_BG, T_THEME, 70), 8, H_GRAPH - 16);
    {
        uint16_t panel = ux.panel;                      /* (the hero's band: the background's colour) */
        ux.panel = ux.bg;
        pv_hero(BOX_X, BOX_W, 0);
        ux.panel = panel;
    }
    cv_blit(0, Y_GRAPH);
}

/* ------------------------------------------------------- the rows --- */
/* track k's sound as a list entry (browse.c cur_entry, which reads the selected track) */
static void stage_entry(uint32_t k, uint32_t *src, uint32_t *kk)
{
    uint8_t s = song.sel;
    song.sel = (uint8_t)k;
    cur_entry(src, kk);
    song.sel = s;
}
/* a name in two lines of w px in face f: the first ends at the last space, '.' or '-' that fits (one long word: cut),
 * the rest below (MIXER's strips) */
static void stage_two_lines(const char *s, char *a, char *b, int32_t w, const aafont_t *f)
{
    uint32_t i, cut = 0;
    for (i = 0; s[i] && i < 15u; i++)                   /* (cut: the first line's length; a space goes) */
        if (s[i] == ' ' || s[i] == '.' || s[i] == '-') {
            uint32_t n = i + (s[i] != ' ');
            str_cpy(a, s, n + 1u);
            if (n && text_w(f, a) <= w) cut = n;
        }
    if (!cut || text_w(f, s) <= w) {
        str_cpy(a, s, 16);
        b[0] = 0;
        return;
    }
    str_cpy(a, s, cut + 1u);
    str_cpy(b, s + cut + (s[cut] == ' '), 16);
}
/* the bar playing (page) of track t: 4 x 4 dots in ST_GRID px at (x, y), or (row) 16 in a row from x, w px wide, centred
 * on row y: hits in the track's colour, a tie half of it, the rest in line, the playhead larger in text */
static void stage_steps(const track_t *t, uint32_t page, uint32_t ph, uint16_t tc, uint16_t bg, int32_t x, int32_t y,
                        int row, int32_t w)
{
    uint32_t i, len = (uint32_t)t->p[P_SLEN];
    for (i = 0; i < 16u && page * 16u + i < len; i++) {
        uint32_t si = page * 16u + i;
        const step_t *st = &seq_steps(t)[si % NSTEP];
        uint16_t c = si == ph ? T_TEXT : step_on(st) ? tc : st->time == ST_TIE ? ux_mix(bg, tc, 40) : T_LINE;
        int32_t px, py, d = row ? (w >= 200 ? 9 : 7) : 6;
        if (row) {
            int32_t gap = w / 16;
            px = x + (int32_t)i * (w - 3 * gap / 2) / 16 + (int32_t)(i / 4u) * gap / 8 + gap / 2;
            py = y;
        } else {
            px = x + 4 + (int32_t)(i % 4u) * 7;
            py = y + 4 + (int32_t)(i / 4u) * 7;
        }
        cv_disc(px, py, si == ph ? d + 2 : d, c, 0);
    }
}
static void stage_row(uint32_t k)
{
    static const char *const AM[7] = {"ARP", "ARP UP", "ARP DN", "ARP UPDN", "ARP RND", "ARP ORD", "REPEAT"};
    const track_t *t = &trk[k];
    uint32_t sel = k == song.sel, mute = t->p[P_MUTE] != 0u, arm = (song.rec >> k) & 1u, len = (uint32_t)t->p[P_SLEN];
    uint32_t page = song.playing && t->seq_idx < len ? t->seq_idx / 16u : 0u, ph = song.playing ? t->seq_idx : 0xFFFFu;
    uint32_t next = t->pattern_next < NPAT ? t->pattern_next + 1u : 0u, sig, src, kk, cat, g;
    uint32_t n = trk_n(), wide = n == 1u, idle = wide && stage_seq_idle(k);
    int pending = sel && browse_pending();
    int32_t h = st_row_h(), y = st_row_y(k), top, cs = h >= 80 ? 34 : 26, tx = 6 + cs + 10, gx = BOX_W - 6 - ST_GRID;
    int32_t steps_row = h >= 80;                        /* (one, two tracks: the steps in a row under the words) */
    uint16_t tc = mute ? T_DIM : T_TRK(k), bg = ux_mix(T_BG, tc, sel ? 24 : 11);   /* (the track's tint) */
    const aafont_t *lf = TY_LB, *nf = h >= 80 ? TY_VL : TY_TX;
    char nm[24], line[48], d[3] = {(char)('1' + k), 0, 0};
    if (pending) {                                      /* browsing: the sound the list shows, not yet loaded */
        char tag[6];
        src = browse_shown(&kk);
        entry_label(src, kk, tag, nm);
        if (!nm[0]) str_cpy(nm, tag, sizeof nm);
    } else {
        sound_name(t, nm);
        if (!nm[0]) str_cpy(nm, ENGINES[eng_idx(t->eng_req)]->name, sizeof nm);
        stage_entry(k, &src, &kk);
    }
    cat = entry_cat(src, kk) % CAT_N;
    {   /* the role (NEW SONG's words: DRUMS, CHORDS; none, OTHER: the track), then what plays */
        char tn[3] = {'T', (char)('1' + k), 0};
        str_cpy(line, cat == CAT_DRUM ? "DRUMS" : cat == CAT_KEYS ? "CHORDS" : cat && cat != CAT_OTHER ? CAT_NAME[cat] : tn,
                sizeof line);
    }
    sig = str_hash(str_hash(1u + sel * 2u + mute * 4u + arm * 8u + (uint32_t)pending * 16u + stage.out * 32u, nm), line) +
          next * 131u + t->pattern * 7919u + len * 13u + ux.gen * 977u + ux.pal * 31u + n * 7u + (uint32_t)h * 211u + idle * 3u;
    if (wide)
        sig += (uint32_t)t->p[P_AMODE] * 3001u + (uint32_t)t->p[P_AHOLD] * 37u + (uint32_t)t->p[P_CHRD] * 641u +
               (uint32_t)t->p[P_QUANT] * 1291u + (uint32_t)t->p[P_ROOT] * 17u + (uint32_t)t->p[P_SCALE] * 4099u;
    g = steps_hash(t) + page * 613u + ph * 104729u;
    if (stage.out)                                      /* (the knobs and the hero over the rows: nothing to draw) */
        return;
    if (!ui.force && sig == stage.col[k]) {
        if (g != stage.grid[k] && !idle) {              /* the steps or the playhead: the steps only */
            stage.grid[k] = g;
            if (steps_row) {
                cv_begin(BOX_W - tx - 6, 14, bg);
                stage_steps(t, page, ph, tc, bg, 0, 7, 1, BOX_W - tx - 6);
                cv_blit((uint32_t)(BOX_X + tx), (uint32_t)(y + h - 20));
            } else {
                cv_begin(ST_GRID, ST_GRID, bg);
                stage_steps(t, page, ph, tc, bg, 0, 0, 0, 0);
                cv_blit((uint32_t)(BOX_X + gx), (uint32_t)(y + (h - ST_GRID) / 2));
            }
        }
        return;
    }
    stage.col[k] = sig;
    stage.grid[k] = g;
    if (wide) {                                         /* one track: what plays besides */
        if (t->p[P_AMODE]) { str_cpy(line + str_len(line), "  ", 3); str_cpy(line + str_len(line), AM[clamp(t->p[P_AMODE], 0, 6)], 10); }
        if (t->p[P_AHOLD]) str_cpy(line + str_len(line), "  HOLD", 8);
        if (t->p[P_CHRD]) { str_cpy(line + str_len(line), "  ", 3); str_cpy(line + str_len(line), N_CHRD[clamp(t->p[P_CHRD], 0, (int32_t)NELEM(N_CHRD) - 1)], 8); }
    }
    char l1[24], l2[24];
    l1[0] = l2[0] = 0;
    if (wide) {                                         /* one track: the sound as large as it fits, on two lines if need be */
        int32_t room = BOX_W - tx - 8;
        if (text_w(TY_HR, nm) <= room && text_has(TY_HR, nm)) {
            nf = TY_HR;
        } else if (text_w(TY_VL, nm) > room) {
            stage_two_lines(nm, l1, l2, room, TY_VL);
            if (l2[0] && text_w(TY_VL, l1) <= room)
                str_cpy(nm, l1, sizeof nm);
            else
                l2[0] = 0;
        }
    }
    for (top = 0; top < h; top += (int32_t)(CV_MAX / BOX_W)) {   /* (a tall row: in slices) */
        int32_t sh = h - top < (int32_t)(CV_MAX / BOX_W) ? h - top : (int32_t)(CV_MAX / BOX_W);
        int32_t nh = nf->h + (l2[0] ? nf->h - 4 : 0);    /* (the name's lines) */
        int32_t by = (h - (nh + lf->h - 2) - (steps_row && !idle ? 18 : 0)) / 2;   /* (the words' block, centred) */
        cv_begin(BOX_W, (uint32_t)sh, T_BG);
        cv_oy = -top;
        if (sel) {                                      /* (the selected: outlined in its colour) */
            cv_rrect(0, 0, BOX_W, h, 8, tc, T_BG);
            cv_rrect(2, 2, BOX_W - 4, h - 4, 6, bg, tc);
        } else {
            cv_rrect(0, 0, BOX_W, h, 8, bg, T_BG);
        }
        cv_rrect(6, h / 2 - cs / 2 - (steps_row && !idle ? 9 : 0), cs, cs, 7, tc, bg);   /* the numeral */
        cv_text_c(6 + cs / 2, ty_y(h / 2 - (steps_row && !idle ? 9 : 0), cs > 30 ? TY_VL : TY_TX), cs > 30 ? TY_VL : TY_TX,
                  d, T_INK, tc);
        if (arm)                                        /* armed: a REC dot on its corner */
            cv_disc(6 + cs - 2, h / 2 - cs / 2 - (steps_row && !idle ? 9 : 0) + 2, 9, T_REC, 0);
        cv_free_text(tx, by, nf, nm, pending ? T_ACCENT : mute ? T_DIM : T_TEXT, bg, (steps_row ? BOX_W - 6 : gx - 8) - tx);
        if (l2[0])
            cv_free_text(tx, by + nf->h - 4, nf, l2, pending ? T_ACCENT : mute ? T_DIM : T_TEXT, bg, BOX_W - 6 - tx);
        {   /* the role, then the pattern (a pill in the track's colour), the next one waiting for the bar dashed */
            int32_t lx = tx, ly = by + nh - 2, re = (steps_row ? BOX_W - 6 : gx - 8);
            char role[16], pt[3] = {'P', (char)('1' + t->pattern % NPAT), 0}, pn[3] = {'P', (char)('0' + next), 0};
            uint32_t m;
            for (m = 0; line[m] && line[m] != ' ' && m + 1u < sizeof role; m++)
                role[m] = line[m];
            role[m] = 0;
            lx = cv_text_on(lx, ly, lf, role, tc, bg) + 8;
            if (!idle) {
                int32_t pw = text_w(lf, pt) + 10, cy = ly + lf->asc - (lf->cap + 1) / 2;
                cv_rrect(lx, cy - 10, pw, 20, 5, tc, bg);
                cv_text_on(lx + 5, ly, lf, pt, T_INK, tc);
                lx += pw + 4;
                if (next) {
                    cv_dashed(lx, cy - 10, pw, 20, 5, tc);
                    cv_text_on(lx + 5, ly, lf, pn, tc, bg);
                    lx += pw + 4;
                }
            }
            if (line[m] && lx < re)                     /* (one track: what plays besides) */
                cv_free_text(lx + 4, ly, lf, line + m + 2, tc, bg, re - lx - 4);
        }
        if (!idle) {
            if (steps_row)
                stage_steps(t, page, ph, tc, bg, tx, h - 13, 1, BOX_W - tx - 6);
            else
                stage_steps(t, page, ph, tc, bg, gx, (h - ST_GRID) / 2, 0, 0);
        }
        cv_oy = 0;
        cv_blit(BOX_X, (uint32_t)(y + top));
    }
}

/* the knobs: out while one turns (and ST_KNOB_MS after), dropping in over ST_DROP frames (each its cells' bottom rows,
 * more each frame); the hero under them; gone at once, the rows whole again */
static void stage_knobs(void)
{
    int out = stage_knobs_out();
    if (out && !stage.out) {
        stage.out = 1;
        stage.drop = ST_DROP;
        lcd_fill(0, Y_LABEL, 240, Y_GRAPH - Y_LABEL, T_BG);   /* (the rows under the strip go at once) */
    } else if (!out && stage.out) {
        stage.out = 0;
        memset(stage.col, 0, sizeof stage.col);         /* (the rows back, HOME's second page whole) */
        view_reset();
        memset(ui.col, 0, sizeof ui.col);
        lcd_fill(0, Y_LABEL, 240, ST_BAND_Y - Y_LABEL, T_BG);
        return;
    }
    if (!stage.out)
        return;
    if (stage.drop) {                                   /* each cell whole (its value snaps, no roll), its bottom rows */
        uint32_t c;
        for (c = 0; c < 4u; c++) {
            ui.col[c][0] = 0;
            ui.roll[c].sig = (uint8_t)~ui.roll[c].sig;
        }
        stage_drop_rows = (uint8_t)((ST_DROP + 1u - stage.drop) * CARD_H / ST_DROP);
        stage_columns();
        stage_drop_rows = 0;
        stage.drop--;
        return;
    }
    stage_columns();
}

/* the BG around the rows and the band (a full redraw: the canvases cover the rest) */
static void stage_frame(void)
{
    lcd_fill(0, H_HEAD, 240, 240u - H_HEAD, T_BG);
}

/* every frame the knobs; every other frame (the scope's) the rows (or, the knobs out, the hero), then the band last */
static void stage_draw(void)
{
    uint32_t k;
    stage_panel_hits();
    stage_knobs();
    if (!ui.force && (ui.frame & 1u))
        return;
    if (stage.out && !ui.home_pg)
        stage_hero();
    for (k = 0; k < trk_n(); k++)
        stage_row(k);
    stage_band();
}
