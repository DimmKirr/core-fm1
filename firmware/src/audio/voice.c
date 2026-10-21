/* SPDX-License-Identifier: GPL-3.0-only */
/* Voice allocator stub. Dispatches note_on/off to engine vtable.
 * TODO: import from felucca voice.c with adaptation. */

static void trk_note_on(track_t *t, uint32_t note, uint32_t vel)
{
    (void)t; (void)note; (void)vel;
}

static void trk_note_off(track_t *t, uint32_t note)
{
    (void)t; (void)note;
}

static void trk_all_off(track_t *t)
{
    (void)t;
}

static void engine_block(track_t *t, int32_t *buf_l, int32_t *buf_r, uint32_t n)
{
    (void)t; (void)buf_l; (void)buf_r; (void)n;
}
