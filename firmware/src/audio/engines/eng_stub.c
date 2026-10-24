/* SPDX-License-Identifier: GPL-3.0-only */
/* Stub engine: silence with 4 dummy knobs.
 * Lets the UI and engine page work before any real engine is integrated. */

static void stub_note_on(struct track *t, voice_t *v)
{
    (void)t; (void)v;
}

static void stub_render(struct track *t, voice_t *v, int32_t *out,
                        uint32_t n, const vmod_t *m)
{
    (void)t; (void)v; (void)m;
    uint32_t i;
    for (i = 0; i < n * 2; i++)
        out[i] = 0;
}

static const preset_t stub_presets[] = {
    { .name = "Init", .e = {0} },
};

static const engine_t ENG_STUB = {
    .name       = "Stub",
    .page_title = {"STUB", "STUB"},
    .edit       = {
        { "Level",  F_PCT,   0, 100, 80, 0, 0 },
        { "Tone",   F_PCT,   0, 100, 50, 0, 0 },
        { "Shape",  F_PCT,   0, 100, 50, 0, 0 },
        { "Color",  F_PCT,   0, 100, 50, 0, 0 },
        { "Param5", F_PCT,   0, 100,  0, 0, 0 },
        { "Param6", F_PCT,   0, 100,  0, 0, 0 },
        { "Param7", F_PCT,   0, 100,  0, 0, 0 },
        { "Param8", F_PCT,   0, 100,  0, 0, 0 },
    },
    .presets    = stub_presets,
    .npresets   = NELEM(stub_presets),
    .knob       = {0, 1, 2, 3},
    .poly       = 8,
    .note_on    = stub_note_on,
    .render     = stub_render,
};
