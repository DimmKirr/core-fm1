/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Session-only experimental generator. No project/preset wire-format fields. */
enum { GN_MODE, GN_RATE, GN_RANGE, GN_FILL, GN_LENGTH, GN_EVOLVE, GN_GATE, GN_SEED,
       GN_HARMONY, GN_CHANGE, GN_COLOR, GN_WANDER, GN_PALETTE, GN_SOURCE, GN_ANSWER, GN_CONDITION, GN_DYNAMICS, GN_STRUM, GN_COUNT };
enum { GEN_OFF, GEN_LOOP, GEN_WALK, GEN_CHORD, GEN_ANSWER };
enum { GA_KEEP = 1, GA_RETURN, GA_REMIX, GA_NEW, GA_RHYTHM, GA_LOCK, GA_MATCH };
#include "generator_memory.h"
typedef struct {
    int16_t p[GN_COUNT];
    uint8_t initialized, running, n, chord_n;
    uint8_t note[8], motif[16];
    gen_memory_t memory;
    uint16_t value[16];
    uint32_t held[4], rhythm_random;
    int16_t preferred[16], tuning[4];
    uint8_t preferred_n, memory_ready, seed_used, palette, action, rhythm_shift;
    uint8_t phrase[16], accent[16];
    uint8_t cursor, length, rate, gate_value, range, fill, dirty, harmony_due, color;
    uint16_t rhythm;
    int16_t heard[16], history[16], answer_memory[16];
    uint8_t history_n, answer_n, answer_style, answer_wait, condition, dynamics, strum;
    uint8_t pending_n, pending_note[4], pending_vel[4];
    uint32_t delay[4], pending_gate[4];
    uint8_t path[8], path_pos, path_valid, source_used, route_used, link_bad;
    uint32_t cycles, cycle_origin, harmonic_random;
    uint32_t gate[8], random, left, harmony_left, step, changes, signature;
    int16_t anchor, root, walk, chord[4];
} generator_t;
static generator_t generator[NTRK] __attribute__((section(".pool")));
static struct { generator_t state; uint8_t valid, group; } generator_keep[NTRK] __attribute__((section(".pool")));
static void generator_defaults(uint32_t tr)
{
    generator_t *g = &generator[tr % NTRK];
    if (g->initialized) return;
    static const int16_t defaults[GN_COUNT] = {GEN_OFF, 1, 1, 5, 8, 0, 80, 11, 0, 2, 0, 15, 35, 0, 0, 0, 1, 0};
    memcpy(g->p, defaults, sizeof defaults);
    g->initialized = 1;
}
static int generator_on(const track_t *t) { return generator[t - trk].p[GN_MODE] != GEN_OFF; }
static int generator_pending(const track_t *t)
{
    const generator_t *g = &generator[t - trk];
    return generator_on(t) && g->running && (g->action || g->harmony_due || g->dirty ||
        g->length != g->p[GN_LENGTH] || g->range != g->p[GN_RANGE] || g->fill != g->p[GN_FILL] ||
        g->rate != g->p[GN_RATE] || g->gate_value != g->p[GN_GATE] || g->color != g->p[GN_COLOR] || g->palette != g->p[GN_PALETTE] || g->seed_used != g->p[GN_SEED] || (g->p[GN_MODE] == GEN_ANSWER && g->answer_style != g->p[GN_ANSWER]) || g->condition != g->p[GN_CONDITION] || g->dynamics != g->p[GN_DYNAMICS] || g->strum != g->p[GN_STRUM]);
}
