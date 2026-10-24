/* SPDX-License-Identifier: GPL-3.0-only */
/* FM6 adapter for FM Lab's dx7_core.c (Dexed/msfa integer port).
 * The upstream core renders DX7 voices in Q24; this shim wraps it as
 * an engine_t with voice management and amplitude scaling. */

#include "upstream/fmlab/firmware/src/dx7_voice.h"
#include "upstream/fmlab/firmware/src/dx7_core.c"
#include "upstream/fmlab/firmware/src/dx7_voices.h"

#define FM6_POLY 6

static dxv_t fm6_dxv_state[NTRK][FM6_POLY];
static uint8_t fm6_live[NTRK][FM6_POLY];

static dxv_t *fm6_voice(track_t *t, voice_t *v)
{
    uint32_t ti = (uint32_t)(t - trk);
    uint32_t vi = (uint32_t)(v - t->v);
    if (ti >= NTRK || vi >= FM6_POLY)
        return 0;
    return &fm6_dxv_state[ti][vi];
}

static void fm6_note_on(track_t *t, voice_t *v)
{
    uint32_t ti = (uint32_t)(t - trk);
    uint32_t vi = (uint32_t)(v - t->v);
    dxv_t *d = fm6_voice(t, v);
    int32_t idx;
    if (!d || ti >= NTRK || vi >= FM6_POLY)
        return;
    idx = t->p[P_E7];
    if (idx < 0 || idx >= DX_NVOICES)
        idx = DX_NVOICES - 1;
    dx_init(d, DX_VOICES[idx], (int)v->note, (int)v->vel);
    fm6_live[ti][vi] = 1;
}

static void fm6_render(track_t *t, voice_t *v, int32_t *out,
                       uint32_t n, const vmod_t *m)
{
    uint32_t ti = (uint32_t)(t - trk);
    uint32_t vi = (uint32_t)(v - t->v);
    dxv_t *d = fm6_voice(t, v);
    int32_t buf[DX_N];
    int32_t a, da, k;
    uint32_t i, blk;

    if (!d || ti >= NTRK || vi >= FM6_POLY || !fm6_live[ti][vi])
        return;

    if (!v->gate)
        dx_keyup(d);

    k = VOICE_FS;
    a = mulq15(m->amp0, k);
    da = (mulq15(m->amp1, k) - a) / (int32_t)n;

    for (blk = 0; blk < n; blk += DX_N) {
        __builtin_memset(buf, 0, sizeof(buf));
        dx_compute(d, buf, 0);
        for (i = 0; i < (uint32_t)DX_N && blk + i < n; i++) {
            a += da;
            out[blk + i] += (int32_t)(((int64_t)buf[i] * a) >> 26);
        }
    }
}

static int fm6_done(track_t *t, voice_t *v)
{
    uint32_t ti = (uint32_t)(t - trk);
    uint32_t vi = (uint32_t)(v - t->v);
    dxv_t *d = fm6_voice(t, v);
    if (!d || ti >= NTRK || vi >= FM6_POLY || !fm6_live[ti][vi])
        return 1;
    if (!dx_playing(d)) {
        fm6_live[ti][vi] = 0;
        return 1;
    }
    return 0;
}

static const char *const N_FM6_VOICE[] = {
    "EPIANO1", "EPIANO2", "FMBASS", "SLPBAS", "SUBBAS",
    "BRASS", "STRING", "GLSPAD", "BELLS", "MARIMB",
    "ORGAN", "CLAV", "PLUCK", "FLUTE", "SAWLED",
    "KOTO", "DEPSUB", "808SUB", "REESE", "INIT",
    0
};

static const preset_t FM6_PRESETS[] = {
    {"EPIANO 1",  {0, 0, 0, 0, 0, 0, 0,  0}},
    {"EPIANO 2",  {0, 0, 0, 0, 0, 0, 0,  1}},
    {"FM BASS",   {0, 0, 0, 0, 0, 0, 0,  2}},
    {"SLAP BASS", {0, 0, 0, 0, 0, 0, 0,  3}},
    {"SUB BASS",  {0, 0, 0, 0, 0, 0, 0,  4}},
    {"BRASS",     {0, 0, 0, 0, 0, 0, 0,  5}},
    {"STRINGS",   {0, 0, 0, 0, 0, 0, 0,  6}},
    {"GLASS PAD", {0, 0, 0, 0, 0, 0, 0,  7}},
    {"BELLS",     {0, 0, 0, 0, 0, 0, 0,  8}},
    {"MARIMBA",   {0, 0, 0, 0, 0, 0, 0,  9}},
    {"ORGAN",     {0, 0, 0, 0, 0, 0, 0, 10}},
    {"CLAV",      {0, 0, 0, 0, 0, 0, 0, 11}},
    {"PLUCK",     {0, 0, 0, 0, 0, 0, 0, 12}},
    {"FLUTE",     {0, 0, 0, 0, 0, 0, 0, 13}},
    {"SAW LEAD",  {0, 0, 0, 0, 0, 0, 0, 14}},
    {"KOTO",      {0, 0, 0, 0, 0, 0, 0, 15}},
    {"DEEP SUB",  {0, 0, 0, 0, 0, 0, 0, 16}},
    {"808 SUB",   {0, 0, 0, 0, 0, 0, 0, 17}},
    {"REESE",     {0, 0, 0, 0, 0, 0, 0, 18}},
    {"INIT VOICE",{0, 0, 0, 0, 0, 0, 0, 19}},
};

static const engine_t ENG_FM6 = {
    .name       = "FM6",
    .page_title = {"OPS", "VOICE"},
    .edit       = {
        {"VOICE", F_INT, 0, DX_NVOICES - 1, 0, N_FM6_VOICE, 0},
        {"BRGHT", F_BIPCT, -64, 63, 0, 0, 0},
        {"Spare", F_PCT, 0, 127, 0, 0, 0},
        {"Spare", F_PCT, 0, 127, 0, 0, 0},
        {"Spare", F_PCT, 0, 127, 0, 0, 0},
        {"Spare", F_PCT, 0, 127, 0, 0, 0},
        {"Spare", F_PCT, 0, 127, 0, 0, 0},
        {"SLOT",  F_INT, 0, DX_NVOICES - 1, 0, N_FM6_VOICE, 0},
    },
    .presets    = FM6_PRESETS,
    .npresets   = NELEM(FM6_PRESETS),
    .note_on    = fm6_note_on,
    .render     = fm6_render,
    .knob       = {P_E0, P_E1, P_E2, P_E7},
    .poly       = FM6_POLY,
    .ownenv     = 1,
    .done       = fm6_done,
};
