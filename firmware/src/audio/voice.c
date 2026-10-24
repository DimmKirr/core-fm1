/* SPDX-License-Identifier: GPL-3.0-only */
/* Minimal voice allocator. Allocates voices round-robin, calls engine
 * note_on/render through the vtable. No envelope, no glide, no unison. */

static uint32_t voice_age;

static void trk_note_on(track_t *t, uint32_t note, uint32_t vel)
{
    const engine_t *eng;
    voice_t *v = 0;
    voice_t *oldest = 0;
    uint32_t i, min_age = 0xFFFFFFFFu;

    if (t->engine >= NENGINES)
        return;
    eng = ENGINES[t->engine];
    if (!eng->note_on)
        return;

    for (i = 0; i < NVOICE; i++) {
        if (!t->v[i].active) {
            v = &t->v[i];
            break;
        }
        if (t->v[i].age < min_age) {
            min_age = t->v[i].age;
            oldest = &t->v[i];
        }
    }
    if (!v)
        v = oldest;
    if (!v)
        return;

    __builtin_memset(v, 0, sizeof(*v));
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->gate = 1;
    v->active = 1;
    v->age = ++voice_age;
    v->pitch16 = (int32_t)note * 16;

    eng->note_on(t, v);
}

static void trk_note_off(track_t *t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++) {
        if (t->v[i].active && t->v[i].note == (uint8_t)note && t->v[i].gate) {
            t->v[i].gate = 0;
            t->v[i].active = 0;
            break;
        }
    }
}

static void trk_all_off(track_t *t)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++) {
        t->v[i].gate = 0;
        t->v[i].active = 0;
    }
}

/* Switch a track's engine: silence voices, set eng_req + engine, load defaults.
 * Ported from Felucca set_engine_of (ui.c). IRQ-safe: the audio ISR sees either
 * the old engine with its values or the new one with its own. */
static void cfm1_engine_select(track_t *t, uint32_t ei)
{
    const engine_t *e;
    uint32_t i;
    if (ei >= NENGINES)
        ei = 0;
    e = ENGINES[ei];
    fm1_irq_off();
    trk_all_off(t);
    t->eng_req = (uint8_t)ei;
    t->engine  = (uint8_t)ei;
    t->preset  = 0;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = e->edit[i].def;
    fm1_irq_on();
}

static int32_t voice_buf[BLOCK];

static void engine_block(track_t *t, int32_t *buf_l, int32_t *buf_r, uint32_t n)
{
    const engine_t *eng;
    uint32_t i, j;

    if (t->engine >= NENGINES)
        return;
    eng = ENGINES[t->engine];
    if (!eng->render)
        return;

    for (i = 0; i < n; i++)
        voice_buf[i] = 0;

    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &t->v[i];
        vmod_t m;
        if (!v->active)
            continue;
        __builtin_memset(&m, 0, sizeof(m));
        m.pitch16 = v->pitch16;
        m.inc = pitch_inc((uint32_t)clamp(m.pitch16, 0, 2047));
        m.amp0 = AUDIO_AMP_ONE;
        m.amp1 = AUDIO_AMP_ONE;
        eng->render(t, v, voice_buf, n, &m);
    }

    for (j = 0; j < n; j++) {
        buf_l[j] += voice_buf[j];
        buf_r[j] += voice_buf[j];
    }
}
