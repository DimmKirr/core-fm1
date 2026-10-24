/* SPDX-License-Identifier: GPL-3.0-only */
/* core-fm1 ENGINE page: shows the active engine's 4 knob params.
 * Reads through cfm1_engine.h (engine_t vtable) and cfm1_audio.h (track_t). */

static void engine_enter(void) { }
static void engine_leave(void) { }

static void engine_draw(void)
{
    const track_t *t = &trk[song.sel];
    const engine_t *eng = (t->eng_req < NENGINES)
        ? ENGINES[t->eng_req] : 0;

    if (!eng) {
        boot_text_1x(120, "NO ENGINE", 0x8410, 0x0000);
        return;
    }

    /* engine name header */
    boot_text_1x(20, eng->name, 0xFFFF, 0x0000);

    /* 4 knob parameters */
    uint32_t k;
    for (k = 0; k < 4; k++) {
        uint32_t pi = eng->knob[k];
        const param_desc_t *d = eng->desc
            ? eng->desc(t, pi) : &eng->edit[pi < 8 ? pi : 0];

        char buf[32];
        uint32_t i = 0;
        const char *label = d ? d->label : "?";
        while (label[i] && i < 12) { buf[i] = label[i]; i++; }
        buf[i++] = ':';
        buf[i++] = ' ';

        /* value */
        int16_t val = t->p[P_E0 + pi];
        if (val < 0) { buf[i++] = '-'; val = -val; }
        if (val >= 100) buf[i++] = '0' + (char)(val / 100);
        if (val >= 10)  buf[i++] = '0' + (char)((val / 10) % 10);
        buf[i++] = '0' + (char)(val % 10);
        buf[i] = 0;

        uint32_t y = 44 + k * 20;
        boot_text_1x(y, buf, 0x07E0, 0x0000);
    }
}

static int engine_knob(uint32_t id, int32_t clicks)
{
    if (id >= 1 && id <= 4) {
        track_t *t = &trk[song.sel];
        const engine_t *eng = (t->eng_req < NENGINES)
            ? ENGINES[t->eng_req] : 0;
        if (!eng) return 0;
        uint32_t pi = P_E0 + eng->knob[id - 1];
        int16_t v = t->p[pi] + (int16_t)clicks;
        const param_desc_t *d = &eng->edit[eng->knob[id - 1]];
        if (d) {
            if (v < d->min) v = d->min;
            if (v > d->max) v = d->max;
        }
        t->p[pi] = v;
        shell_dirty = 1;
        return 1;
    }
    return 0;
}

static int engine_button(uint32_t id, uint32_t down)
{
    (void)id; (void)down;
    return 0;
}

static const cfm1_page_t PAGE_ENGINE = {
    .name  = "ENGINE",
    .title = "ENGINE",
    .flags = 0,
    .enter  = engine_enter,
    .leave  = engine_leave,
    .draw   = engine_draw,
    .status = 0,
    .knob   = engine_knob,
    .button = engine_button,
    .key    = 0,
};
