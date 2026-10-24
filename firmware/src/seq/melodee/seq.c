/* SPDX-License-Identifier: GPL-3.0-only */
/* Melodee sequencer stub: 64-step, 8 banks, 1024 timed notes,
 * recording, undo, motion, song chain.
 * TODO: port from upstream melodee (seq.c, seq_edit.c, seq_notes.c,
 * seq_erase.c, seq_undo.c, song_chain.c, pattern.c, pattern_store.c,
 * recording.c, recording_undo.c, motion.c, click.c, chord.c, tracks.c). */

#define KB_SILENT 255u
static volatile uint8_t kb_asleep;

static void seq_init(void) {}

static void seq_tick(void)
{
    if (song.playing) {
        song.tick++;
        if (song.tick >= song.len * CTL)
            song.tick = 0;
    }
}

static void seq_start(void)
{
    song.playing = 1;
    song.tick = 0;
}

static void seq_stop(void)
{
    song.playing = 0;
    song.tick = 0;
}

static void seq_toggle(void)
{
    if (song.playing)
        seq_stop();
    else
        seq_start();
}

static uint32_t seq_step_pos(void)
{
    return song.tick / CTL;
}

static int seq_armed(void) { return song.rec; }

static void seq_arm(int on) { song.rec = (uint8_t)on; }

static void seq_events_block(void) {}

static void seq_pattern_clear(track_t *t)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        __builtin_memset(&t->step[i], 0, sizeof(step_t));
}
