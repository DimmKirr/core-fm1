/* SPDX-License-Identifier: GPL-3.0-only */
/* Parameter defaults and ranges stub.
 * TODO: import from felucca params.c with P_COUNT superset. */

static void params_init(track_t *t)
{
    uint32_t i;
    const engine_t *e;
    for (i = 0; i < P_COUNT; i++)
        t->p[i] = 0;
    t->p[P_LEVEL] = 100;
    t->p[P_SUS] = 80;
    t->p[P_REL] = 30;
    e = (t->engine < NENGINES) ? ENGINES[t->engine] : 0;
    if (e) {
        for (i = 0; i < 8u; i++)
            t->p[P_E0 + i] = e->edit[i].def;
    }
}
