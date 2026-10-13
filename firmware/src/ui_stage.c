/* SPDX-License-Identifier: GPL-3.0-only */
/* Melodee's Stage (HOME) and the PATTERNS grid (SEQ > PATTERNS), drawn to the redesign's mockups (docs/design:
 * stage_columns, concept_screens).
 * Stage: the header (its chip: the engine, a kit's name); a column per track in use, side by side over the four's
 * place (two: halves, three: thirds, the sound 15 px from two on): a bar and its role (the sound's category) in its
 * colour, the sound in two lines, the engine, its pattern (the next one dashed beside it while it waits for the bar),
 * the bar playing as 4 x 4 steps, its level as a ring, all of it centred; the selected one lifted and outlined. One track: its column is the synth's card across them all, centred (stage_wide: the sound 30 px, what plays
 * as pills, the steps in a row; a track with no pattern at all: no pattern pill, no steps, the ring alone). Under
 * them the panel: the notes the selected track played and their chord in front of its waveform, smooth, dimmed while
 * held (a DRUM track: its lanes, lit as they hit). The first turn of a knob drops the track's four knobs (its engine's,
 * stage_page) down over the columns' tops (ST_DROP frames); while they are out the panel shows what the knob turning
 * shapes and its value; ST_KNOB_MS after the last turn they go and the columns are whole again.
 * Each part remembers what it drew: a column redraws when its sound, pattern, steps or level change, the playhead its
 * steps alone; the panel follows the scope every other frame, drawn last (the SPI, 12 MHz, sends it while the next
 * frame is worked out: a blit right after it would wait for it, lcd_window).
 */
#define ST_COL_Y Y_LABEL                                /* the columns: x CARD_X(k), y 22 .. 174, the cards' width */
#define ST_COL_H 152
#define ST_GRID_Y 84                                    /* .. their steps: 4 x 4, rows 84 .. 121 (a canvas of their own) */
#define ST_GRID_H 38
#define ST_PANEL_Y (ST_COL_Y + ST_COL_H + 4)            /* the panel: y 178 .. 238 */
#define ST_PANEL_H 60
#define ST_KNOB_MS 1500u                                /* the knobs stay this long after the last turn */
#define ST_DROP 4u                                      /* .. and drop in over this many frames */
#define ST_AREA_W 232                                   /* the columns' place: x 4 .. 236, the panel's width */

/* the columns' width: four the cards' (54), fewer share the place, 4 px apart (three: 74, 5 px apart) */
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
    uint32_t col[NTRK], grid[NTRK];                     /* each column's drawn state, its steps' */
    uint8_t hit_t[NLANE];                               /* frames a DRUM lane's name stays lit */
    uint8_t hit_trk;                                    /* .. of this track */
    uint32_t knob_ms;                                   /* the last knob turn on Stage (| 1), 0 none */
    uint8_t out, drop;                                  /* the knobs out; frames still dropping */
} stage;

static void view_reset(void);                           /* ui_view.c */
static void stage_knob_touch(void) { stage.knob_ms = fm1_ms | 1u; }   /* (ui_input.c: a knob on Stage) */
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

/* ------------------------------------------------------- the panel --- */
/* the output's waveform over the panel's width, smooth: triggered on a rising zero crossing, auto-scaled, each sample
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
 * what is on the canvas (cv_over): the root 30 px, its quality 15 px, then the notes 11 px, on one baseline (38).
 * Held: the chord in text, the notes in the track's colour; let go: both faded into the panel. 0 none, 1 let go, 2 held */
static int stage_notes(int32_t x1)
{
    uint32_t bits[4], i, n = 0, pcs = 0, root = 0, bass, held = 0, k = song.sel % NTRK;
    uint8_t note[8];
    char b[48];
    const char *q;
    uint16_t c, cn;
    int32_t x = 10;
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
    c = held ? T_TEXT : ux_mix(T_PANEL, T_THEME, 75);
    cn = held ? T_THEME : ux_mix(T_PANEL, T_THEME, 55);
    bass = note[0] % 12u;
    q = micro_active(TSEL) ? 0 : chord_of(pcs, bass, &root);
    if (q) {
        x = cv_text(x, 9, &AF_L, N_NOTE[root], c);
        x = cv_text(x + 1, 23, &AF_M, q, c);
        if (root != bass) {
            x = cv_text(x + 3, 9, &AF_L, "/", c);
            x = cv_text(x, 9, &AF_L, N_NOTE[bass], c);
        }
        x += 10;
    }
    notes_fit(b, note, n, &AF_S, x1 - x);
    cv_text(x, 27, &AF_S, b, cn);
    return (int)(1u + (held != 0u));
}
/* HOME's notes alone (tests/ui_test.c): 1 = there were notes */
static int graph_notes(void) { return stage_notes(220) != 0; }

/* a DRUM track: its eight lanes in a row at the panel's foot, a lane's name lit while it hits (drum_flash: the ISR's) */
static void stage_hits(void)
{
    uint32_t l;
    cv_over = 0;
    for (l = 0; l < NLANE; l++) {
        int32_t x = 7 + (int32_t)l * 28;
        int lit = stage.hit_t[l] != 0u;
        if (lit) {
            cv_rrect(x, 38, 26, 16, 4, T_THEME, T_PANEL);
        } else {                                        /* (an unlit lane: outlined in line) */
            cv_rrect(x, 38, 26, 16, 4, T_LINE, T_PANEL);
            cv_rrect(x + 1, 39, 24, 14, 3, T_SURF, T_LINE);
        }
        cv_text_c(x + 13, 40, &AF_X, drum_lane_abbr(TSEL, l), lit ? T_INK : T_DIM, lit ? T_THEME : T_SURF);
    }
}

/* While the knobs are out, the panel shows what the one turning shapes behind the notes: a filter's response (cutoff,
 * resonance; the Prophet's envelope amount too), an envelope (its attack .. release); 0 = nothing to show */
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
/* a low-pass response: flat, the resonance's peak at the cutoff (a dotted line), then the slope; cut, res 0..127 */
static void stage_filter(int32_t cut, int32_t res, uint16_t c)
{
    int32_t x, fc = 10 + clamp(cut, 0, 127) * 200 / 127, peak = clamp(res, 0, 127) * 14 / 127;
    int32_t top = 3, bot = ST_PANEL_H - 4, y0 = 22 * 16, py = 0;
    for (x = top; x < bot; x += 5)
        cv_rect(fc, x, 1, 2, ux_mix(T_PANEL, c, 60));
    for (x = 0; x < 220; x++) {
        int32_t d = 8 + x - fc, y = y0 - peak * 16 * 64 / (64 + d * d) + (d > 0 ? d * 10 : 0);
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
static void stage_panel(void)
{
    uint32_t sel = song.sel % NTRK, drum = (uint32_t)drum_track(TSEL), pic;
    int32_t pv[4];
    int held = 0;
    cv_begin(240, ST_PANEL_H, T_BG);
    cv_rrect(4, 0, 232, ST_PANEL_H, 6, T_PANEL, T_BG);
    cv_bg = T_PANEL;
    if (!drum) {
        uint32_t i;
        fm1_irq_off();
        for (i = 0; i < 4u; i++) held |= live_held[sel][i] != 0u;
        fm1_irq_on();
    }
    pic = drum ? SP_NONE : stage_picture(pv);
    /* behind: the waveform (dimmed while notes are held, or over a drum's lanes), or what the knob turning shapes */
    if (pic == SP_FILTER)
        stage_filter(pv[0], pv[1], ux_mix(T_PANEL, T_THEME, 55));
    else if (pic == SP_ENV)
        graph_adsr_v(pv[0], pv[1], pv[2], pv[3], 4, ST_PANEL_H - 8, ux_mix(T_PANEL, T_THEME, 55));
    else
        stage_wave(held || drum ? ux_mix(T_PANEL, T_THEME, 40) : T_THEME, drum ? 2 : 4, drum ? 34 : ST_PANEL_H - 8);
    cv_over = 1;                                        /* in front: blended over it */
    if (drum) {
        stage_hits();
    } else {
        stage_notes(pic ? 176 : 228);
        if (pic) {                                      /* the value turning, at the top right */
            int16_t *vp;
            const char *unit;
            char val[12];
            const page_t *sp = stage_page();
            const param_desc_t *d = page_desc(sp, ui.hot_col & 3u, &vp);
            if (d && vp) {
                stage_value(sp, d, *vp, val, &unit);
                str_cpy(val + str_len(val), unit, 4);
                cv_text_r(228, 4, &AF_M, val, T_THEME, T_PANEL);
            }
        }
    }
    cv_over = 0;
    cv_blit(0, ST_PANEL_Y);
}

/* ------------------------------------------------------ the columns --- */
/* track k's sound as a list entry (browse.c cur_entry, which reads the selected track) */
static void stage_entry(uint32_t k, uint32_t *src, uint32_t *kk)
{
    uint8_t s = song.sel;
    song.sel = (uint8_t)k;
    cur_entry(src, kk);
    song.sel = s;
}
/* a name in two lines of w px in face f: the first ends at the last space, '.' or '-' that fits (one long word: cut),
 * the rest below */
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
/* column k's 4 x 4 steps (the bar playing): hits in the track's colour, the playhead larger in text, the rest in line;
 * a canvas of 50 x ST_GRID_H at the column's (2, ST_GRID_Y), clear of the level's ring under it */
static void stage_grid_at(const track_t *t, uint32_t page, uint32_t ph, uint16_t tc, uint16_t bg, int32_t ox)
{
    uint32_t i, len = (uint32_t)t->p[P_SLEN];
    for (i = 0; i < 16u; i++) {
        uint32_t si = page * 16u + i;
        const step_t *st = &seq_steps(t)[si % NSTEP];
        int32_t x = ox + 8 + (int32_t)(i % 4u) * 113 / 10, y = 6 + (int32_t)(i / 4u) * 95 / 10;
        if (si >= len)
            break;
        cv_circle(x, y, si == ph ? 6 : 5, si == ph ? T_TEXT : step_on(st) ? tc : st->time == ST_TIE ? ux_mix(bg, tc, 40) : T_LINE, bg);
    }
}
/* one track: its column as the synth's card (synth-only's Stage), across the four's place, as large as the little screen
 * reads it, centred: the four colours' bars, the role (12 px) in the track's colour, the sound 30 px (two lines if need
 * be), what plays as pills on the patterns' row: the pattern (filled in the track's colour, the next dashed beside it),
 * ARP, HOLD, CHORD, the scale, each in a colour of its own; under them the bar playing as 16 steps in a row (the beats
 * apart) and the level as the big ring at their right. A track with no pattern at all (stage_seq_idle): no pattern
 * pill, no steps, the ring in the middle. Rows: the role 5 .. 19, the sound 15 .. 83, the pills 84 .. 100, the steps
 * and the ring 110 .. 150. Drawn in two canvases (ST_SLICE rows, then the rest: the card is wider than one holds) */
#define ST_WIDE_NAME_W (ST_AREA_W - 12)                 /* the sound and the pills: x 6 .. 226 */
#define ST_PILL_Y 84
#define ST_RING_Y 130                                   /* the ring's centre (KNOB_BIG), the steps' row */
#define ST_RING_X (ST_AREA_W - 8 - KNOB_BIG_R)          /* (no steps: ST_AREA_W / 2) */
#define ST_STEPS_X 10                                   /* the steps: 16 from x 10, 10 px apart, the beats 6 more */
#define ST_STEPS_W 186
#define ST_SLICE 84
static int32_t stage_pill(int32_t x, int32_t y, const aafont_t *f, const char *s, int dashed, uint16_t c, uint16_t bg)
{
    int32_t w = text_w(f, s) + 2 * (f->h - 7), h = f->h + 2;
    if (dashed) {
        cv_dashed(x, y, w, h, 3, c);
        cv_text_c(x + w / 2, y + 1, f, s, c, bg);
    } else {
        cv_rrect(x, y, w, h, h / 3, c, bg);
        cv_text_c(x + w / 2, y + 1, f, s, T_INK, c);
    }
    return x + w + 3;
}
/* the sound as large as it fits: 30 px on one line, else on two (cut at the last space, '.' or '-' that fits), else
 * (a sign L lacks) 15 px */
static void stage_wide_name(const char *s, uint16_t c, uint16_t bg)
{
    char a[24];
    uint32_t i, cut = 0;
    for (i = 0; s[i]; i++)
        if (glyph_at(&AF_L, (uint8_t)s[i]) < 0) {
            cv_free_text(st_mid_x(ST_AREA_W / 2, &AF_M, s, ST_WIDE_NAME_W), 32, &AF_M, s, c, bg, ST_WIDE_NAME_W);
            return;
        }
    if (text_w(&AF_L, s) <= ST_WIDE_NAME_W) {
        cv_text_c(ST_AREA_W / 2, 24, &AF_L, s, c, bg);
        return;
    }
    for (i = 0; s[i] && i < 22u; i++)
        if (s[i] == ' ' || s[i] == '.' || s[i] == '-') {
            uint32_t n = i + (s[i] != ' ');
            str_cpy(a, s, n + 1u);
            if (n && text_w(&AF_L, a) <= ST_WIDE_NAME_W) cut = n;
        }
    if (!cut) {
        cv_free_text(6, 24, &AF_L, s, c, bg, ST_WIDE_NAME_W);
        return;
    }
    str_cpy(a, s, cut + 1u);
    cv_text_c(ST_AREA_W / 2, 15, &AF_L, a, c, bg);
    s += cut + (s[cut] == ' ');
    cv_free_text(st_mid_x(ST_AREA_W / 2, &AF_L, s, ST_WIDE_NAME_W), 47, &AF_L, s, c, bg, ST_WIDE_NAME_W);
}
/* the 16 steps of the bar playing in a row, on a canvas whose row ST_RING_Y is y0 (the card's: 0; their own: 8 down) */
static void stage_steps_row(const track_t *t, uint32_t page, uint32_t ph, uint16_t tc, uint16_t bg, int32_t y0)
{
    uint32_t i, len = (uint32_t)t->p[P_SLEN];
    for (i = 0; i < 16u && page * 16u + i < len; i++) {
        uint32_t si = page * 16u + i;
        const step_t *st = &seq_steps(t)[si % NSTEP];
        int32_t x = ST_STEPS_X + 4 + (int32_t)i * 10 + (int32_t)(i / 4u) * 6;
        cv_circle(x, y0, si == ph ? 9 : 7, si == ph ? T_TEXT : step_on(st) ? tc : st->time == ST_TIE ? ux_mix(bg, tc, 40) : T_LINE, bg);
    }
}
static void stage_wide_draw(const track_t *t, const char *nm, uint32_t page, uint32_t ph, uint32_t next, uint32_t lvl,
                            uint32_t cat, int pending, int idle, uint16_t tc, uint16_t bg)
{
    static const char *const AM[7] = {"ARP", "ARP UP", "ARP DN", "ARP UPDN", "ARP RND", "ARP ORD", "REPEAT"};
    uint32_t k = trk_index(t), mute = t->p[P_MUTE] != 0;
    char v[16];
    cv_rrect(0, 0, ST_AREA_W, ST_COL_H, 6, bg, T_BG);
    if (!stage.out) {                                   /* (under the knobs, while they are out: not drawn) */
        char tn[3] = {'T', (char)('1' + k), 0};
        uint32_t i;
        for (i = 0; i < 4u; i++)                        /* the bar: the four columns' (each its colour), as the synth's */
            cv_rect(3 + (int32_t)i * ST_AREA_W / 4, 0, ST_AREA_W / 4 - 6, 3, mute ? T_DIM : T_TRK(k + i));
        cv_text_c(ST_AREA_W / 2, 5, &AF_S, cat == CAT_DRUM ? "DRUMS" : cat == CAT_KEYS ? "CHORDS" : cat && cat != CAT_OTHER ?
                  CAT_NAME[cat] : tn, tc, bg);
        if ((song.rec >> k) & 1u)                       /* armed: a REC dot */
            cv_circle(ST_AREA_W - 14, 12, 8, T_REC, bg);
        stage_wide_name(nm, pending ? T_ACCENT : mute ? T_DIM : T_TEXT, bg);
    }
    {   /* what plays: the pattern (the next one dashed), then ARP, HOLD, CHORD, the scale, each in a colour of the four;
         * 12 px, 10 px when the row does not hold them */
        const char *ps[6];
        uint16_t pc[6];
        int dash[6];
        char pt[3] = {'P', (char)('1' + t->pattern % NPAT), 0}, pn[3] = {'P', (char)('0' + next), 0}, ch[12] = "CHORD ";
        const aafont_t *f = &AF_S;
        uint32_t n = 0, i, fit;
        int32_t x, w = -3;
        if (!idle) { ps[n] = pt; pc[n] = tc; dash[n++] = 0; }
        if (next) { ps[n] = pn; pc[n] = tc; dash[n++] = 1; }
        if (t->p[P_AMODE]) { ps[n] = AM[clamp(t->p[P_AMODE], 0, 6)]; pc[n] = mute ? T_DIM : T_TRK(k + 1u); dash[n++] = 0; }
        if (t->p[P_AHOLD]) { ps[n] = "HOLD"; pc[n] = mute ? T_DIM : T_TRK(k + 2u); dash[n++] = !t->p[P_AMODE]; }
        if (t->p[P_CHRD]) {                             /* (which: "CHORD MAJ7") */
            str_cpy(ch + 6, N_CHRD[clamp(t->p[P_CHRD], 0, (int32_t)NELEM(N_CHRD) - 1)], sizeof ch - 6u);
            ps[n] = ch; pc[n] = mute ? T_DIM : T_TRK(k + 3u); dash[n++] = 0;
        }
        if (t->p[P_QUANT] != Q_OFF) {
            str_cpy(v, N_NOTE[(uint32_t)t->p[P_ROOT] % 12u], 4);
            str_cpy(v + str_len(v), " ", 2);
            str_cpy(v + str_len(v), N_SCALE[clamp(t->p[P_SCALE], 0, (int32_t)NELEM(N_SCALE) - 1)], 10);
            ps[n] = v; pc[n] = mute ? T_DIM : T_TRK(k + 1u); dash[n++] = 0;
        }
        for (i = 0; i < n; i++)
            w += text_w(f, ps[i]) + 2 * (f->h - 7) + 3;
        if (w > ST_WIDE_NAME_W)
            f = &AF_X;
        for (i = 0, w = -3; i < n; i++)
            w += text_w(f, ps[i]) + 2 * (f->h - 7) + 3;
        if (w > ST_WIDE_NAME_W && t->p[P_CHRD])         /* (a full row: the chord's name alone, "MAJ7") */
            for (i = 0; i < n; i++)
                if (ps[i] == ch)
                    ps[i] = ch + 6;
        for (w = -3, fit = 0; fit < n && w + 3 + text_w(f, ps[fit]) + 2 * (f->h - 7) <= ST_WIDE_NAME_W; fit++)
            w += 3 + text_w(f, ps[fit]) + 2 * (f->h - 7);   /* (the ones that fit, centred) */
        for (i = 0, x = (ST_AREA_W - w) / 2; i < fit; i++)
            x = stage_pill(x, ST_PILL_Y + (f == &AF_X), f, ps[i], dash[i], pc[i], bg);
    }
    if (!idle)
        stage_steps_row(t, page, ph, tc, bg, ST_RING_Y);
    {   /* the level: the big ring, its dB inside (no steps: in the middle) */
        int32_t a1 = lvl ? -KA_END + (int32_t)lvl * 2 * KA_END / 127 : -KA_END - 1, rx = idle ? ST_AREA_W / 2 : ST_RING_X;
        knob_arc(rx - KNOB_BIG_R, ST_RING_Y - KNOB_BIG_R, KNOB_BIG_R, KNOB_BIG_COV, KNOB_BIG_ANG, -KA_END, a1, T_LINE, tc, bg);
        if (lvl) {
            int32_t d = LEVEL_DB_X10[lvl];
            fmt_int(v, (d + (d < 0 ? -5 : 5)) / 10);
        } else {
            str_cpy(v, "OFF", sizeof v);
        }
        cv_text_c(rx, ST_RING_Y - 7, &AF_S, v, lvl ? T_TEXT : T_DIM, bg);
    }
}

static void stage_col(uint32_t k)
{
    const track_t *t = &trk[k];
    uint32_t sel = k == song.sel, mute = t->p[P_MUTE] != 0u, arm = (song.rec >> k) & 1u, len = (uint32_t)t->p[P_SLEN];
    uint32_t page = song.playing && t->seq_idx < len ? t->seq_idx / 16u : 0u, ph = song.playing ? t->seq_idx : 0xFFFFu;
    uint32_t next = t->pattern_next < NPAT ? t->pattern_next + 1u : 0u, lvl = (uint32_t)t->p[P_LEVEL] & 127u, sig, src, kk;
    uint32_t n = trk_n(), wide = n == 1u, idle = wide && stage_seq_idle(k);
    int pending = sel && browse_pending();
    uint16_t tc = mute ? T_DIM : T_TRK(k), bg = sel && !wide ? T_LIFT : T_PANEL;
    int32_t x = st_col_x(k), w = st_col_w(), gx = (w - CARD_W) / 2;   /* gx: the 4 x 4 steps and the ring centred */
    const aafont_t *nf = w >= 100 ? &AF_M : &AF_S;       /* (two columns: the sound 15 px) */
    char nm[24], l1[24], l2[24], chip[4] = {'P', (char)('1' + t->pattern % NPAT), 0, 0};
    const char *en;
    if (pending) {                                      /* browsing: the sound the list shows, not yet loaded */
        char tag[6];
        src = browse_shown(&kk);
        entry_label(src, kk, tag, nm);
        if (!nm[0]) str_cpy(nm, tag, sizeof nm);
    } else if (wide) {
        sound_name(t, nm);
        if (!nm[0]) str_cpy(nm, ENGINES[eng_idx(t->eng_req)]->name, sizeof nm);
        stage_entry(k, &src, &kk);
    } else {
        trk_short_name(k, nm);
        if (t->eng_req == ENGI_PROPHET)
            p5_short_name(t, nm, 16);
        stage_entry(k, &src, &kk);
    }
    en = ENGINES[eng_idx(pending ? src_engine(src, kk) : t->eng_req)]->name;
    sig = str_hash(str_hash(1u + sel * 2u + mute * 4u + arm * 8u + (uint32_t)pending * 16u + stage.out * 32u, nm), en) +
          next * 131u + lvl * 40503u + t->pattern * 7919u + len * 13u + ux.gen * 977u + ux.pal * 31u + entry_cat(src, kk) * 61u +
          n * 7u + (uint32_t)x * 211u + idle * 3u;
    if (wide)
        sig += (uint32_t)t->p[P_AMODE] * 3001u + (uint32_t)t->p[P_AHOLD] * 37u + (uint32_t)t->p[P_CHRD] * 641u +
               (uint32_t)t->p[P_QUANT] * 1291u + (uint32_t)t->p[P_ROOT] * 17u + (uint32_t)t->p[P_SCALE] * 4099u;
    if (!ui.force && sig == stage.col[k]) {
        uint32_t g = steps_hash(t) + page * 613u + ph * 104729u;
        if (g != stage.grid[k]) {                       /* the steps or the playhead: the grid only */
            stage.grid[k] = g;
            if (wide && idle) {
            } else if (wide) {
                cv_begin(ST_STEPS_W, 18, bg);
                stage_steps_row(t, page, ph, tc, bg, 9);
                cv_blit((uint32_t)x, (uint32_t)(ST_COL_Y + ST_RING_Y - 9));
            } else {
                cv_begin(50, ST_GRID_H, bg);
                stage_grid_at(t, page, ph, tc, bg, 0);
                cv_blit((uint32_t)(x + gx + 2), (uint32_t)(ST_COL_Y + ST_GRID_Y));
            }
        }
        return;
    }
    stage.col[k] = sig;
    stage.grid[k] = steps_hash(t) + page * 613u + ph * 104729u;
    if (wide) {                                         /* the synth's card: two canvases */
        uint32_t top;
        for (top = 0; top < ST_COL_H; top += ST_SLICE) {
            uint32_t h = ST_COL_H - top < ST_SLICE ? ST_COL_H - top : ST_SLICE;
            cv_begin(ST_AREA_W, h, T_BG);
            cv_oy = -(int32_t)top;
            stage_wide_draw(t, nm, page, ph, next, lvl, entry_cat(src, kk) % CAT_N, pending, (int)idle, tc, bg);
            cv_oy = 0;
            if (stage.out && !top)                      /* the knobs over the top: the rows under them only */
                cv_blit_from((uint32_t)x, ST_COL_Y, CARD_H);
            else
                cv_blit((uint32_t)x, ST_COL_Y + top);
        }
        return;
    }
    cv_begin((uint32_t)w, ST_COL_H, T_BG);
    if (sel) {                                          /* the selected track: lifted, outlined in its colour */
        cv_rrect(0, 0, w, ST_COL_H, 6, T_TRK(k), T_BG);
        cv_rrect(1, 1, w - 2, ST_COL_H - 2, 5, bg, T_TRK(k));
    } else {
        cv_rrect(0, 0, w, ST_COL_H, 6, bg, T_BG);
    }
    if (!stage.out) {                                   /* (under the knobs, while they are out: not drawn) */
        cv_rect(3, 0, w - 6, 3, tc);                    /* the bar */
        {   /* the role: the sound's category, in NEW SONG's words (DRUMS, CHORDS); none, OTHER: the track */
            uint32_t cat = entry_cat(src, kk) % CAT_N;
            char tn[3] = {'T', (char)('1' + k), 0};
            const char *role = cat == CAT_DRUM ? "DRUMS" : cat == CAT_KEYS ? "CHORDS" : cat && cat != CAT_OTHER ? CAT_NAME[cat] : tn;
            int32_t rx = st_mid_x(w / 2, &AF_X, role, w - 8), rw = text_w(&AF_X, role);
            if (arm && rx + rw > w - 12)                /* (clear of the REC dot) */
                rx = w - 12 - rw > 4 ? w - 12 - rw : 4;
            cv_text_on(rx, 6, &AF_X, role, tc, bg);
        }
        if (arm)                                        /* armed: a REC dot */
            cv_circle(w - 7, 10, 6, T_REC, bg);
        stage_two_lines(nm, l1, l2, w - 10, nf);
        cv_free_text(st_mid_x(w / 2, nf, l1, w - 8), nf == &AF_M ? 16 : 20, nf, l1,
                     pending ? T_ACCENT : mute ? T_DIM : T_TEXT, bg, w - 8);
        if (l2[0])
            cv_free_text(st_mid_x(w / 2, nf, l2, w - 8), nf == &AF_M ? 32 : 34, nf, l2,
                         pending ? T_ACCENT : mute ? T_DIM : T_TEXT, bg, w - 8);
    }
    cv_text_fit(st_mid_x(w / 2, &AF_X, en, w - 8), nf == &AF_M ? 51 : 49, &AF_X, en, T_MID, bg, w - 8);
    {   /* the pattern playing, the next one (waiting for the bar) dashed beside it */
        int32_t px = (w - (next ? 45 : 22)) / 2;
        cv_rrect(px, 64, 22, 14, 4, tc, bg);
        cv_text_c(px + 11, 65, &AF_X, chip, T_INK, tc);
        if (next) {
            char n2[3] = {'P', (char)('0' + next), 0};
            cv_dashed(px + 25, 64, 20, 14, 3, tc);
            cv_text_c(px + 35, 65, &AF_X, n2, tc, bg);
        }
    }
    {   /* the steps, as stage_grid draws them on their own canvas (2 px in, from row ST_GRID_Y) */
        cv_oy = ST_GRID_Y;
        stage_grid_at(t, page, ph, tc, bg, gx + 2);
        cv_oy = 0;
    }
    {   /* the level: a ring (KNOB_RING, 3 px, from 7:30), its dB inside */
        char v[8];
        int32_t a1 = lvl ? -KA_END + (int32_t)lvl * 2 * KA_END / 127 : -KA_END - 1;
        knob_arc(w / 2 - KNOB_RING_R, 136 - KNOB_RING_R, KNOB_RING_R, KNOB_RING_COV, KNOB_RING_ANG, -KA_END, a1, T_LINE, tc, bg);
        if (lvl) {
            int32_t d = LEVEL_DB_X10[lvl];
            fmt_int(v, (d + (d < 0 ? -5 : 5)) / 10);
        } else {
            str_cpy(v, "OFF", sizeof v);
        }
        cv_text_c(w / 2, 131, &AF_X, v, T_TEXT, bg);
    }
    if (stage.out)                                      /* the knobs over the tops: the rows under them only */
        cv_blit_from((uint32_t)x, ST_COL_Y, CARD_H);
    else
        cv_blit((uint32_t)x, ST_COL_Y);
}

/* the knobs: out while one turns (and ST_KNOB_MS after), dropping in over ST_DROP frames (each its cards' bottom rows,
 * more each frame: no column under them redraws); gone at once, the columns whole again */
static void stage_knobs(void)
{
    int out = stage_knobs_out();
    if (out && !stage.out) {
        uint32_t c;
        stage.out = 1;
        stage.drop = ST_DROP;
        if (trk_n() < NTRK || ui.home_pg)               /* (fewer, wider columns, HOME's second page: between the knobs'
                                                         * cards and right of the last, what is under goes at once) */
            for (c = 0; c < 4u; c++)
                lcd_fill((uint32_t)(CARD_X(c) + CARD_W), ST_COL_Y, c < 3u ? 4u : 240u - (uint32_t)(CARD_X(c) + CARD_W),
                         CARD_H, T_BG);
    } else if (!out && stage.out) {
        stage.out = 0;
        memset(stage.col, 0, sizeof stage.col);         /* (the columns' tops back, HOME's second page whole) */
        view_reset();
        memset(ui.col, 0, sizeof ui.col);
        return;
    }
    if (!stage.out)
        return;
    if (stage.drop) {                                   /* each card whole (its value snaps, no roll), its bottom rows */
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

/* the BG around the columns and the panel (a full redraw: the canvases cover the rest) */
static void stage_frame(void)
{
    uint32_t k, n = trk_n();
    for (k = 0; k <= n; k++) {                          /* left of each column, right of the last */
        uint32_t x0 = k ? (uint32_t)(st_col_x(k - 1u) + st_col_w()) : 0u, x1 = k < n ? (uint32_t)st_col_x(k) : 240u;
        lcd_fill(x0, ST_COL_Y, x1 - x0, ST_COL_H, T_BG);
    }
    lcd_fill(0, ST_COL_Y + ST_COL_H, 240, ST_PANEL_Y - ST_COL_Y - ST_COL_H, T_BG);
    lcd_fill(0, ST_PANEL_Y + ST_PANEL_H, 240, 240u - ST_PANEL_Y - ST_PANEL_H, T_BG);
}

/* every frame the knobs; every other frame (the scope's) the columns, then the panel last */
static void stage_draw(void)
{
    uint32_t k;
    stage_panel_hits();
    stage_knobs();
    if (!ui.force && (ui.frame & 1u))
        return;
    for (k = 0; k < trk_n(); k++)
        stage_col(k);
    stage_panel();
}
