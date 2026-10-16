/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Session-only experimental generator. No project/preset wire-format fields. */
enum { GN_MODE, GN_RATE, GN_RANGE, GN_FILL, GN_LENGTH, GN_EVOLVE, GN_GATE, GN_SEED,
       GN_HARMONY, GN_CHANGE, GN_COLOR, GN_WANDER, GN_PALETTE, GN_SOURCE, GN_ANSWER, GN_CONDITION, GN_DYNAMICS, GN_STRUM,
       GN_ASTYLE, GN_BSTYLE, GN_CSTYLE, GN_JAM, GN_ENERGY, GN_TENSION, GN_VARIETY, GN_COUNT,
       GN_STYLE = GN_COUNT };   /* (a page slot only: the selected role's own style) */
enum { GEN_OFF, GEN_LOOP, GEN_WALK, GEN_CHORD, GEN_ANSWER };   /* ROLE: ARP, BASS, CHORD, ANSWER */
enum { GA_KEEP = 1, GA_RETURN, GA_REMIX, GA_NEW, GA_RHYTHM, GA_LOCK, GA_MATCH };
/* HARMONY: still, a related graph, a planned journey, a Piston-table Markov chain, then
 * well-known progressions as scale-degree offsets from the played home (generator_harmony.c). */
enum { GH_HOLD, GH_NEAR, GH_PATH, GH_MARKOV, GH_POP, GH_AXIS, GH_DOOWOP, GH_CANON, GH_ROYAL,
       GH_CIRCLE, GH_JAZZ, GH_FALL, GH_RISE, GH_VAMP, GH_BLUES, GH_COUNT };
enum { AS_DRIFT, AS_UP, AS_DOWN, AS_BOUNCE, AS_INWARD, AS_PINKY, AS_THUMB, AS_ALBERTI, AS_STAIRS, AS_COUNT };
enum { BS_DRIFT, BS_ROOT, BS_OCTAVE, BS_FIFTH, BS_WALK, BS_ARP, BS_BOOGIE, BS_PEDAL, BS_COUNT };
enum { CS_BLOCK, CS_STRIDE, CS_COUNT };
enum { GV_TRIAD, GV_7TH, GV_OPEN, GV_911, GV_SUS2, GV_ADD9, GV_SUS4, GV_69, GV_QUARTAL, GV_SHELL, GV_COUNT };
enum { GJ_OFF, GJ_POP, GJ_ROCK, GJ_FUNK, GJ_JAZZ, GJ_BALLAD, GJ_AMBIENT, GJ_EPIC, GJ_BLUES, GJ_COUNT };
enum { GW_ALWAYS, GW_ODD, GW_EVEN, GW_4TH, GW_REST };
enum { GD_FLAT, GD_PHRASE, GD_SWELL, GD_HUMAN };
#include "generator_memory.h"
/* One JAM theme: what a section type (verse, chorus, bridge) plays each time it returns. */
typedef struct { uint8_t harmony, every, seed, astyle, bstyle, cstyle, answer, shift; } gen_theme_t;
typedef struct {
    int16_t p[GN_COUNT];
    uint8_t initialized, running, n, chord_n;
    uint8_t note[8], motif[16];
    gen_memory_t memory;
    uint16_t value[16];
    uint32_t held[4], rhythm_random;
    int16_t preferred[16], tuning[4];
    uint8_t preferred_n, memory_ready, seed_used, palette, action, rhythm_shift;
    uint8_t phrase[16];
    uint8_t cursor, length, rate, gate_value, range, fill, dirty, harmony_due, color, style;
    uint16_t rhythm;
    int16_t heard[16], history[16], answer_memory[16];
    uint8_t history_n, answer_n, answer_style, answer_wait, condition, dynamics, strum, level;
    uint8_t pending_n, pending_note[4], pending_vel[4];
    uint32_t delay[4], pending_gate[4];
    uint8_t path[16], path_pos, path_valid, path_len, harmony_used, source_used, route_used, link_bad, jam_lead;
    uint32_t cycles, cycle_origin, harmonic_random, frac;
    uint32_t gate[8], random, left, harmony_left, step, changes, signature;
    int16_t anchor, root, walk, chord[4];
    /* JAM director, on a LOCAL leader (generator_jam.c) */
    gen_theme_t theme[3];
    uint8_t form[8], form_pos, section, jam_count, jam_len, jam_feel, jam_cycle, jam_new, jam_first, jam_energy, jam_started;
    uint32_t jam_random;
} generator_t;
static generator_t generator[NTRK] __attribute__((section(".pool")));
static struct { generator_t state; uint8_t valid, group; } generator_keep[NTRK] __attribute__((section(".pool")));
static int generator_source(uint32_t tr, uint32_t *error);
static void generator_defaults(uint32_t tr)
{
    generator_t *g = &generator[tr % NTRK];
    if (g->initialized) return;
    static const int16_t defaults[GN_COUNT] = {GEN_OFF, 1, 1, 5, 8, 0, 80, 11, 0, 2, 0, 15, 35, 0, 0, 0, 1, 0,
        AS_DRIFT, BS_DRIFT, CS_BLOCK, GJ_OFF, 60, 35, 50};
    memcpy(g->p, defaults, sizeof defaults);
    g->initialized = 1;
}
static int generator_on(const track_t *t) { return generator[t - trk].p[GN_MODE] != GEN_OFF; }
/* The stored setting the STYLE slot shows: the role's own style list. */
static uint32_t generator_style_id(const generator_t *g)
{
    switch (g->p[GN_MODE]) {
    case GEN_WALK: return GN_BSTYLE;
    case GEN_CHORD: return GN_CSTYLE;
    case GEN_ANSWER: return GN_ANSWER;
    default: return GN_ASTYLE;
    }
}
/* JAM's macros belong to the ensemble: any linked part shows and edits its leader's. */
static uint32_t generator_jam_owner(uint32_t tr)
{
    uint32_t error;
    int lead = generator_source(tr, &error);
    return lead >= 0 ? (uint32_t)lead : tr;
}
static int generator_jam_id(uint32_t id) { return id >= GN_JAM && id <= GN_VARIETY; }
static int generator_pending(const track_t *t)
{
    const generator_t *g = &generator[t - trk];
    return generator_on(t) && g->running && (g->action || g->harmony_due || g->dirty ||
        g->length != g->p[GN_LENGTH] || g->range != g->p[GN_RANGE] || g->fill != g->p[GN_FILL] ||
        g->rate != g->p[GN_RATE] || g->gate_value != g->p[GN_GATE] || g->color != g->p[GN_COLOR] || g->palette != g->p[GN_PALETTE] || g->seed_used != g->p[GN_SEED] || (g->p[GN_MODE] == GEN_ANSWER && g->answer_style != g->p[GN_ANSWER]) || g->condition != g->p[GN_CONDITION] || g->dynamics != g->p[GN_DYNAMICS] || g->strum != g->p[GN_STRUM] ||
        (g->p[GN_MODE] != GEN_ANSWER && g->style != g->p[generator_style_id(g)]));
}
