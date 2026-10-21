/* SPDX-License-Identifier: MIT */
/* Sequencer adapter: transport, step data, pattern state.
 * Used by UI shells to show playback/record state and pattern grids. */
#ifndef CFM1_SEQ_H
#define CFM1_SEQ_H
#include <stdint.h>

/* step_t and song_t are in cfm1_audio.h (shared with audio core) */

/* sequencer API (implemented by lineage seq.c, stubbed if no seq) */
static const step_t *seq_steps(const track_t *t) { return t->step; }
static int seq_counting(void) { return song.playing; }
static int seq_is_empty(const track_t *t)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        if (t->step[i].note[0]) return 0;
    return 1;
}
static uint32_t seq_entry(const track_t *t) { (void)t; return 0; }

#endif /* CFM1_SEQ_H */
