/* SPDX-License-Identifier: MIT */
/* Sequencer adapter: transport, step data, pattern state.
 * Used by UI shells to show playback/record state and pattern grids.
 * Stubs are active only when no sequencer lineage is configured. */
#ifndef CFM1_SEQ_H
#define CFM1_SEQ_H
#include <stdint.h>

/* step_t and song_t are in cfm1_audio.h (shared with audio core) */

/* shared helpers (always available) */
static const step_t *seq_steps(const track_t *t) { return t->step; }
static int seq_counting(void) { return song.playing; }
static int seq_is_empty(const track_t *t)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        if (t->step[i].note[0]) return 0;
    return 1;
}

#if !defined(CONFIG_SEQ_MELODEE) && !defined(CONFIG_SEQ_FELUCCA) && !defined(CONFIG_SEQ_SLOOP)
static uint32_t seq_entry(const track_t *t) { (void)t; return 0; }
static void seq_init(void) {}
static void seq_tick(void) {}
static void seq_start(void) { song.playing = 1; song.tick = 0; }
static void seq_stop(void) { song.playing = 0; song.tick = 0; }
static void seq_toggle(void) { if (song.playing) seq_stop(); else seq_start(); }
static void seq_events_block(void) {}
static void seq_pattern_clear(track_t *t)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        __builtin_memset(&t->step[i], 0, sizeof(step_t));
}
#endif

#endif /* CFM1_SEQ_H */
