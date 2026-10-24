/* SPDX-License-Identifier: GPL-3.0-only */
/* Felucca parameter pages: ENV, LFO, FX, SCL, EDIT, ARP, VOICE.
 * Each shows 4 cards with parameter values from the adapter. */

/* generic parameter page: shows 4 params by their P_* ids */
typedef struct {
    const char *name;
    const char *title;
    uint32_t params[4];
    const char *labels[4];
    const char *units[4];
    uint32_t icons[4];
} fparams_def_t;

static void fparams_draw_page(const fparams_def_t *def)
{
    uint32_t c;
    const track_t *t = &trk[song.sel];
    const engine_t *eng = (t->eng_req < NENGINES) ? ENGINES[t->eng_req] : 0;

    fui_draw_head();

    for (c = 0; c < 4; c++) {
        uint32_t pid = def->params[c];
        int16_t v = t->p[pid];
        char val[12];
        if (def->units[c] && (def->units[c][0] == 'm' && def->units[c][1] == 's'))
            fui_fmt_time(val, v);
        else
            fui_fmt_val(val, v);
        int32_t ratio = v >= 0 ? v * 1000 / 127 : -1;
        fui_draw_card(c, def->labels[c], val, def->units[c],
                      T_THEME, ratio, def->icons[c]);
    }

    fui_clear_graph();
    fui_draw_foot(eng ? eng->name : "---", "ACID", def->name);
}

static int fparams_knob(const fparams_def_t *def, uint32_t id, int32_t clicks)
{
    if (id >= 2 && id <= 5) {
        track_t *t = &trk[song.sel];
        uint32_t pid = def->params[id - 2];
        int16_t v = t->p[pid] + (int16_t)(clicks * 4);
        if (v < 0) v = 0;
        if (v > 127) v = 127;
        t->p[pid] = v;
        fsh_dirty = 1;
        return 1;
    }
    return 0;
}

/* ENV page: ADSR envelope */
static const fparams_def_t fenv_def = {
    "ENV", "ENVELOPE",
    {P_ATK, P_DEC, P_SUS, P_REL},
    {"ATK", "DEC", "SUS", "REL"},
    {"ms", "ms", "%", "ms"},
    {ICON_ATTACK, ICON_DECAY, ICON_SUSTAIN, ICON_RELEASE},
};
static void fenv_draw(void) { fparams_draw_page(&fenv_def); }
static int fenv_knob(uint32_t id, int32_t clicks) { return fparams_knob(&fenv_def, id, clicks); }
static const cfm1_page_t FPAGE_ENV = {
    .name = "ENV", .title = "ENVELOPE", .draw = fenv_draw, .knob = fenv_knob,
};

/* LFO page */
static const fparams_def_t flfo_def = {
    "LFO", "LFO",
    {P_LFO, P_LFRT, P_LFSH, P_LFDL},
    {"LFO", "RATE", "SHAPE", "DELAY"},
    {"%", "Hz", "%", "ms"},
    {ICON_LFO_WAVE, ICON_RATE, ICON_SHAPE, ICON_TIME},
};
static void flfo_draw(void) { fparams_draw_page(&flfo_def); }
static int flfo_knob(uint32_t id, int32_t clicks) { return fparams_knob(&flfo_def, id, clicks); }
static const cfm1_page_t FPAGE_LFO = {
    .name = "LFO", .title = "LFO", .draw = flfo_draw, .knob = flfo_knob,
};

/* FX page: send levels */
static const fparams_def_t ffx_def = {
    "FX", "EFFECTS",
    {P_DIST, P_CHOR, P_DLY, P_REV},
    {"DST", "CHO", "DLY", "REV"},
    {"%", "%", "%", "%"},
    {ICON_DIST, ICON_CHORUS, ICON_DELAY, ICON_REVERB},
};
static void ffx_draw(void) { fparams_draw_page(&ffx_def); }
static int ffx_knob(uint32_t id, int32_t clicks) { return fparams_knob(&ffx_def, id, clicks); }
static const cfm1_page_t FPAGE_FX = {
    .name = "FX", .title = "EFFECTS", .draw = ffx_draw, .knob = ffx_knob,
};

/* SCL page: scale and tuning */
static const fparams_def_t fscl_def = {
    "SCL", "SCALE",
    {P_ROOT, P_SCALE, P_OCT, P_TRANS},
    {"ROOT", "SCL", "OCT", "TRN"},
    {"", "", "", ""},
    {ICON_PITCH, ICON_SCALE, ICON_OCTAVE, ICON_TRANSPOSE},
};
static void fscl_draw(void) { fparams_draw_page(&fscl_def); }
static int fscl_knob(uint32_t id, int32_t clicks) { return fparams_knob(&fscl_def, id, clicks); }
static const cfm1_page_t FPAGE_SCL = {
    .name = "SCL", .title = "SCALE", .draw = fscl_draw, .knob = fscl_knob,
};

/* EDIT page: engine params (reads from engine_t.edit[]) */
static void fedit_draw(void)
{
    uint32_t c;
    const track_t *t = &trk[song.sel];
    const engine_t *eng = (t->eng_req < NENGINES) ? ENGINES[t->eng_req] : 0;

    fui_draw_head();

    for (c = 0; c < 4; c++) {
        if (eng) {
            const param_desc_t *d = &eng->edit[c];
            char val[12];
            int16_t v = t->p[P_E0 + c];
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

    fui_clear_graph();
    fui_draw_foot(eng ? eng->name : "---", "ACID", eng ? eng->page_title[0] : "EDIT");
}

static int fedit_knob(uint32_t id, int32_t clicks)
{
    if (id >= 2 && id <= 5) {
        track_t *t = &trk[song.sel];
        const engine_t *eng = (t->eng_req < NENGINES) ? ENGINES[t->eng_req] : 0;
        if (!eng) return 0;
        uint32_t pi = P_E0 + (id - 2);
        int16_t v = t->p[pi] + (int16_t)clicks;
        const param_desc_t *d = &eng->edit[id - 2];
        if (v < d->min) v = d->min;
        if (v > d->max) v = d->max;
        t->p[pi] = v;
        fsh_dirty = 1;
        return 1;
    }
    return 0;
}

static const cfm1_page_t FPAGE_EDIT = {
    .name = "EDIT", .title = "EDIT", .draw = fedit_draw, .knob = fedit_knob,
};

/* ARP page */
static const fparams_def_t farp_def = {
    "ARP", "ARPEGGIO",
    {P_ARP, P_ARTM, P_ARNG, P_AGATE},
    {"MODE", "TIME", "RANGE", "GATE"},
    {"", "", "oct", "%"},
    {ICON_ARP, ICON_RATE, ICON_OCTAVE, ICON_GATE},
};
static void farp_draw(void) { fparams_draw_page(&farp_def); }
static int farp_knob(uint32_t id, int32_t clicks) { return fparams_knob(&farp_def, id, clicks); }
static const cfm1_page_t FPAGE_ARP = {
    .name = "ARP", .title = "ARPEGGIO", .draw = farp_draw, .knob = farp_knob,
};

/* VOICE page: mono, hold, glide, unison */
static const fparams_def_t fvoice_def = {
    "VOICE", "VOICE",
    {P_MONO, P_HOLD, P_GLI, P_UNI},
    {"VCE", "HOLD", "GLD", "UNI"},
    {"", "", "ms", ""},
    {ICON_VOICE, ICON_HOLD, ICON_GLIDE, ICON_OCTAVE},
};
static void fvoice_draw(void) { fparams_draw_page(&fvoice_def); }
static int fvoice_knob(uint32_t id, int32_t clicks) { return fparams_knob(&fvoice_def, id, clicks); }
static const cfm1_page_t FPAGE_VOICE = {
    .name = "VOICE", .title = "VOICE", .draw = fvoice_draw, .knob = fvoice_knob,
};
