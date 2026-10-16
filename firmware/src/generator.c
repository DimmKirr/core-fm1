/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Session-only musical memory. Fixed-point Marbles memory is in generator_memory.h;
 * scale mapping, lifecycle and input-palette integration are original Melodee code.
 * Phrase length and event gates are literal clock slots, independent of memory.
 * A locked idea survives STOP/PLAY; only NEW or a new SEED replaces its memory. */
#include "generator_branch.h"
static uint32_t generator_word(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *state = x;
}
static uint32_t generator_random(generator_t *g) { return generator_word(&g->random); }

static uint32_t generator_count(const track_t *t)
{
    if (micro_active(t)) return micro_scale(t)->count;
    uint32_t mask = scale_mask(t), count = 0;
    while (mask) { count += mask & 1u; mask >>= 1; }
    return count ? count : 1u;
}

static uint32_t generator_note(const track_t *t, int32_t degree)
{
    if (micro_active(t)) return micro_degree_map(t, degree, 0, 1);
    int32_t count = (int32_t)generator_count(t), oct = degree / count;
    degree %= count;
    if (degree < 0) { degree += count; oct--; }
    uint32_t mask = scale_mask(t), i;
    for (i = 0; i < 12u; i++) if ((mask >> i) & 1u) { if (!degree) break; degree--; }
    int32_t note = 60 + t->p[P_ROOT] + t->p[P_TRANS] + oct * 12 + (int32_t)i;
    return note < 0 || note > 127 ? KB_SILENT : (uint32_t)note;
}

static int32_t generator_degree(const track_t *t, uint32_t note)
{
    if (micro_active(t)) {
        int32_t degree = (int32_t)note - 60;
        for (uint32_t i = 0; i < 128u && generator_note(t, degree) == KB_SILENT; i++)
            degree += micro_pitch(t, 60 + degree) < 0 ? 1 : -1;
        return degree;
    }
    int32_t best = 0, distance = 1000, count = (int32_t)generator_count(t);
    /* Input is already mapped/transposed. Find the nearest usable degree;
     * ascending search gives the lower scale note on an exact tie. */
    for (int32_t d = -11 * count; d <= 11 * count; d++) {
        uint32_t n = generator_note(t, d);
        if (n == KB_SILENT) continue;
        int32_t delta = (int32_t)n - (int32_t)note;
        if (delta < 0) delta = -delta;
        if (delta < distance) { best = d; distance = delta; }
    }
    return best;
}

static int32_t generator_mapped_degree(const track_t *t, uint32_t note)
{
    if (micro_active(t)) return (int32_t)note - 60;
    int32_t relative = (int32_t)note - 60 - t->p[P_ROOT] - t->p[P_TRANS];
    int32_t octave = relative / 12, pc = relative % 12;
    if (pc < 0) { pc += 12; octave--; }
    uint32_t mask = scale_mask(t) & ((1u << pc) - 1u), degree = 0;
    while (mask) { degree += mask & 1u; mask >>= 1; }
    return octave * (int32_t)generator_count(t) + (int32_t)degree;
}

static void generator_release(track_t *t)
{
    generator_t *g = &generator[trk_index(t)];
    for (uint32_t i = 0; i < g->n; i++) trk_note_off(t, g->note[i]);
    g->n = g->pending_n = 0;
    g->running = 0;
}

static int32_t generator_pitch(const track_t *t, int32_t d)
{
    uint32_t note = generator_note(t, d);
    return note == KB_SILENT ? -10000000 : micro_active(t) ? micro_pitch(t, note) : (int32_t)note * 10000;
}

static int generator_diatonic(const track_t *t);
/* Relative scale pitch without MIDI-register clipping. Used only for choosing
 * intervals; generator_note still validates every note that can sound. */
static int32_t generator_tone(const track_t *t, int32_t degree)
{
    int32_t count = (int32_t)generator_count(t), octave = degree / count;
    degree %= count; if (degree < 0) { degree += count; octave--; }
    if (micro_active(t)) {
        const micro_scale_t *s = micro_scale(t);
        return octave * s->pitch[count] + s->pitch[degree];
    }
    uint32_t mask = scale_mask(t), semi = 0;
    for (; semi < 12u; semi++) if ((mask >> semi) & 1u) { if (!degree) break; degree--; }
    return (octave * 12 + (int32_t)semi) * 10000;
}
static void generator_chord_shape(const track_t *t, int32_t root, uint32_t color, uint8_t shape[4])
{
    static const uint8_t offsets[5][4] = {{0,2,4,0}, {0,2,4,6}, {0,2,4,0}, {0,2,6,8}, {0,1,4,0}};
    memcpy(shape, offsets[color], 4);
    uint32_t count = generator_count(t);
    if (!micro_active(t) && count != 7u) {
        /* Sparse and chromatic Western scales do not use the same ordinal
         * thirds as a seven-note scale. Find the nearest legal intervals. */
        int32_t target[4] = {0, color == 4 ? 20000 : 40000, 70000, 110000};
        if (color == 3) { target[2] = 110000; target[3] = 140000; }
        for (uint32_t i = 1; i < 4u; i++) {
            int32_t best = 0x7FFFFFFF; shape[i] = 0;
            for (uint32_t d = 1; d <= 2u * count; d++) {
                int32_t interval = generator_tone(t, root + (int32_t)d) - generator_tone(t, root);
                int32_t distance = interval > target[i] ? interval - target[i] : target[i] - interval;
                if (distance < best) { best = distance; shape[i] = (uint8_t)d; }
            }
            if (i == 1 && color != 4 && generator_tone(t, root + shape[1]) - generator_tone(t, root) < 35000)
                target[color == 3 ? 2 : 3] = 100000;
        }
    }
    if (color == 3 && generator_diatonic(t)) {
        int32_t ninth = generator_tone(t, root + shape[3]) - generator_tone(t, root + (int32_t)count);
        if (ninth < 18000) shape[3] = 10; /* eleventh instead of a flat ninth */
    }
}
static void generator_voicing_for(track_t *t, uint32_t color)
{
    generator_t *g = &generator[trk_index(t)];
    int32_t count = (int32_t)generator_count(t), best = 0x7FFFFFFF;
    uint32_t n = color == 1 || color == 3 ? 4u : 3u;
    uint8_t shape[4]; generator_chord_shape(t, g->root, color, shape);
    int16_t chosen[4] = {0};
    for (uint32_t inv = 0; inv < n; inv++) for (int32_t oct = -2; oct <= 2; oct++) {
        int16_t candidate[4]; int32_t cost = 0;
        for (uint32_t i = 0; i < n; i++) {
            int32_t d = g->root + shape[i] + count * oct;
            if (i < inv) d += count;
            if (color == 2 && i == 1) d += count;
            candidate[i] = (int16_t)d;
        }
        for (uint32_t i = 1; i < n; i++) for (uint32_t j = i; j && candidate[j] < candidate[j-1]; j--) {
            int16_t d = candidate[j]; candidate[j] = candidate[j-1]; candidate[j-1] = d;
        }
        for (uint32_t i = 0; i < n; i++) {
            int32_t pitch = generator_pitch(t, candidate[i]);
            int32_t target = generator_pitch(t, g->chord_n == n ? g->chord[i] : g->anchor + shape[i]);
            if (target < 0) target = generator_pitch(t, g->anchor);
            int32_t delta = pitch - target;
            if (pitch < 0 || target < 0) { cost = 0x3FFFFFFF; break; }
            cost += delta < 0 ? -delta : delta;
        }
        if (cost < best) { best = cost; memcpy(chosen, candidate, n * sizeof chosen[0]); }
    }
    memcpy(g->chord, chosen, n * sizeof chosen[0]);
    g->chord_n = (uint8_t)n;
}

static void generator_voicing(track_t *t)
{
    generator_voicing_for(t, (uint32_t)generator[trk_index(t)].p[GN_COLOR]);
}

static uint32_t generator_mod(int32_t n, uint32_t count)
{
    int32_t r = n % (int32_t)count;
    return (uint32_t)(r < 0 ? r + (int32_t)count : r);
}

static uint32_t generator_signature(const track_t *t);
#include "generator_harmony.c"

/* Euclidean/maximally-even distribution, rotated onto an onset at slot zero.
 * The complete mask is fixed before the phrase starts: FILL never becomes a
 * per-note probability and always yields exactly min(FILL, LENGTH) onsets. */
static uint16_t generator_rhythm(uint32_t length, uint32_t fill)
{
    uint16_t mask = 0;
    if (fill > length) fill = length;
    for (uint32_t i = 0; i < length; i++)
        if ((i * fill) % length < fill) mask |= (uint16_t)(1u << i);
    return mask;
}

static void generator_identity(generator_t *g)
{
    g->random = 0x6D2B79F5u ^ (uint32_t)g->p[GN_SEED] * 0x9E3779B9u;
    if (!g->random) g->random = 1;
    g->harmonic_random = g->random ^ 0xA511E9B3u;
    g->rhythm_random = g->random ^ 0x85EBCA6Bu;
    memset(&g->memory, 0, sizeof g->memory);
    for (uint32_t i = 0; i < 16u; i++) g->memory.loop[i] = (uint16_t)(generator_random(g) >> 16);
    g->memory_ready = 1; g->seed_used = (uint8_t)g->p[GN_SEED];
    g->rhythm_shift = 0; g->path_valid = g->path_pos = 0;
    for (uint32_t i = 0; i < 16u; i++)
        g->value[i] = g->memory.loop[i];
}

/* A weighted inverse CDF over every usable scale degree in the chosen register.
 * Memory is upstream of pitch shaping, so changing COLOR/RANGE and returning
 * recovers the same pitches without rerolling. No contour templates or DP. */
static uint32_t generator_bank(const track_t *t, const generator_t *g, uint8_t *note, uint16_t *cdf)
{
    uint32_t count = generator_count(t), span = count * g->range, sum = 0, n = 0;
    for (uint32_t i = 0; i < span && n < 128u; i++) {
        int32_t degree = g->anchor - (int32_t)(count * (g->range / 2u)) + (int32_t)i;
        uint32_t mapped = generator_note(t, degree), preferred = 0;
        if (mapped == KB_SILENT) continue;
        if (g->preferred_n > 1u && !g->p[GN_SOURCE]) {
            for (uint32_t j = 0; j < g->preferred_n; j++)
                preferred |= generator_mod(degree, count) == generator_mod(g->preferred[j], count);
        } else for (uint32_t j = 0; j < g->chord_n; j++)
            preferred |= generator_mod(degree, count) == generator_mod(g->chord[j], count);
        uint32_t weight = preferred ? 100u : g->palette;
        if (g->p[GN_MODE] == GEN_WALK) {
            uint32_t interval = generator_mod(degree - g->root, count);
            weight = interval == 0 ? 220u : interval == 4u % count ? 100u : g->palette / 5u;
        }
        weight = weight ? (weight * (span - i) + span - 1u) / span : 0u;
        if (!weight) continue;
        sum += weight; note[n] = (uint8_t)mapped; cdf[n++] = (uint16_t)sum;
    }
    return n;
}
static uint32_t generator_map(const uint8_t *note, const uint16_t *cdf, uint32_t n, uint16_t value)
{
    if (!n) return KB_SILENT;
    /* <=128 candidates * maximum weight 220; this product fits uint32_t. */
    uint32_t draw = (uint32_t)value * cdf[n - 1u] >> 16, lo = 0, hi = n - 1u;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2u;
        if (draw < cdf[mid]) hi = mid; else lo = mid + 1u;
    }
    return note[lo];
}

static uint32_t generator_signature(const track_t *t)
{
    return (uint32_t)t->p[P_SCALE] * 131u + (uint32_t)t->p[P_ROOT] * 1031u +
        (uint32_t)(t->p[P_TRANS] + 64) * 8191u + (uint32_t)micro_active(t) * 524287u;
}

static void generator_plan(track_t *t, int boundary)
{
    generator_t *g = &generator[trk_index(t)];
    uint32_t action = boundary ? g->action : 0;
    /* Only the ensemble coordinator may restore a playing group. A follower's
     * earlier phrase boundary must not consume its own queued RETURN. */
    if (action == GA_RETURN && song.playing && boundary != 2) action = 0;
    if (action == GA_RETURN && generator_keep[trk_index(t)].valid) {
        generator_release(t);
        *g = generator_keep[trk_index(t)].state;
        const int16_t *tuning = g->tuning;
        t->p[P_SCALE] = tuning[0]; t->p[P_ROOT] = tuning[1];
        t->p[P_TRANS] = tuning[2]; t->p[P_QUANT] = tuning[3];
        g->signature = generator_signature(t);
        g->cycle_origin = (g->cycles + g->cycle_origin) % 12u;
        g->n = g->pending_n = g->cursor = 0; g->left = g->step = g->cycles = 0;
        memset(g->held, 0, sizeof g->held);
        g->running = song.playing != 0;
    }
    if (action == GA_NEW) g->memory_ready = 0;
    int rebuild = g->dirty || action || g->range != g->p[GN_RANGE] ||
        g->palette != g->p[GN_PALETTE] || g->color != g->p[GN_COLOR] || g->length != g->p[GN_LENGTH] || g->p[GN_MODE] == GEN_ANSWER;
    int fresh = !g->memory_ready || (boundary && g->seed_used != g->p[GN_SEED]);
    if (fresh) { generator_identity(g); rebuild = 1; }
    if (boundary) {
        g->length = (uint8_t)g->p[GN_LENGTH]; g->range = (uint8_t)g->p[GN_RANGE];
        g->fill = (uint8_t)g->p[GN_FILL]; g->rate = (uint8_t)g->p[GN_RATE];
        g->gate_value = (uint8_t)g->p[GN_GATE];
        g->color = (uint8_t)g->p[GN_COLOR]; g->palette = (uint8_t)g->p[GN_PALETTE];
        g->answer_style = (uint8_t)g->p[GN_ANSWER]; g->condition = (uint8_t)g->p[GN_CONDITION]; g->dynamics = (uint8_t)g->p[GN_DYNAMICS]; g->strum = (uint8_t)g->p[GN_STRUM];
        if (action == GA_RHYTHM) g->rhythm_shift = (uint8_t)(generator_word(&g->rhythm_random) % g->length);
        uint16_t mask = generator_rhythm(g->length, g->fill);
        uint32_t shift = g->rhythm_shift % g->length;
        g->rhythm = (uint16_t)(((uint32_t)mask << shift | (uint32_t)mask >> (g->length - shift)) & ((1u << g->length) - 1u));
        if (action != GA_RETURN && (fresh || action == GA_REMIX || (!action && g->cycles && g->p[GN_EVOLVE]))) {
            rebuild = 1;
            uint32_t chance = fresh ? 0u : action == GA_REMIX ? 100u : (uint32_t)g->p[GN_EVOLVE];
            g->memory.step = (uint8_t)(g->length - 1u);
            for (uint32_t i = 0; i < g->length; i++) {
                uint16_t decision = (uint16_t)(generator_random(g) >> 16);
                uint16_t replacement = (uint16_t)(generator_random(g) >> 16);
                g->value[i] = gen_memory_next(&g->memory, g->length, chance, action == GA_REMIX, decision, replacement);
            }
        }
        if (fresh) for (uint32_t i = g->length; i < 16u; i++)
            g->value[i] = g->memory.loop[(g->memory.head + 16u - g->length + i) & 15u];
        if (g->action != GA_RETURN || action == GA_RETURN) g->action = 0;
    }
    g->dirty = 0;
    if (!rebuild) return;
    uint8_t bank[128]; uint16_t cdf[128];
    uint32_t candidates = generator_bank(t, g, bank, cdf), anchor_slot = 0;
    if (g->rhythm) while (!((g->rhythm >> anchor_slot) & 1u)) anchor_slot++;
    const generator_t *source = g->p[GN_SOURCE] ? &generator[g->p[GN_SOURCE]-1] : g;
    if (g->p[GN_MODE] == GEN_ANSWER && ((boundary && action != GA_RETURN) || !g->answer_n) && source->history_n) {
        g->answer_n = source->history_n;
        memcpy(g->answer_memory, source->history, g->answer_n * sizeof g->answer_memory[0]);
    }
    g->answer_wait = g->p[GN_MODE] == GEN_ANSWER && !g->answer_n;
    uint8_t branch[16]; uint32_t branch_n = generator_branch(branch, g->cycles + g->cycle_origin);
    int32_t low = g->anchor - (int32_t)(generator_count(t) * (g->range / 2u));
    int32_t span = (int32_t)(generator_count(t) * g->range);
    for (uint32_t i = 0; i < 16u; i++) {
        g->motif[i] = (uint8_t)(g->value[i] >> 12);
        /* The first onset belongs to the active harmony, with the smallest
         * movement from the previous anchor. The rest can explore the scale. */
        uint32_t mapped = generator_map(bank, cdf, candidates, g->value[i]);
        if (g->p[GN_MODE] == GEN_ANSWER && g->answer_n) {
            uint32_t at = i % g->answer_n;
            if (g->answer_style == 1 || (g->answer_style == 4 && branch[i % branch_n])) at = g->answer_n - 1u - at;
            else if (g->answer_style == 2) at = (at + g->answer_n / 2u) % g->answer_n;
            int32_t relative = g->answer_memory[at];
            if (g->answer_style == 3) relative = -relative;
            if (g->answer_style == 4 && branch[i % branch_n]) relative += (g->value[i] & 1u) ? 1 : -1;
            int32_t degree = low + (int32_t)generator_mod(g->root + relative - low, (uint32_t)span);
            mapped = generator_note(t, degree);
            if (g->answer_style == 5) {
                int32_t best = 0x7FFFFFFF, target = generator_pitch(t, degree);
                for (uint32_t j = 0; j < candidates; j++) {
                    int32_t d = generator_mapped_degree(t, bank[j]); uint32_t eligible = 0;
                    for (uint32_t c = 0; c < g->chord_n; c++) eligible |= generator_mod(d - g->chord[c], generator_count(t)) == 0;
                    if (!eligible) continue;
                    int32_t pitch = generator_pitch(t, d), distance = pitch > target ? pitch - target : target - pitch;
                    if (distance < best) { best = distance; mapped = bank[j]; }
                }
            }
            /* At register limits, the ordinary bank supplies a playable fallback. */
            if (mapped == KB_SILENT) mapped = generator_map(bank, cdf, candidates, g->value[i]);
        }
        if (i == anchor_slot && candidates) {
            int32_t previous = g->phrase[anchor_slot] == KB_SILENT ? (int32_t)bank[0] : g->phrase[anchor_slot];
            uint32_t best = 0xFFFFFFFFu;
            for (uint32_t j = 0; j < candidates; j++) {
                int32_t d = generator_mapped_degree(t, bank[j]);
                uint32_t eligible = g->p[GN_MODE] == GEN_WALK ?
                    generator_mod(d - g->root, generator_count(t)) == 0 : 0;
                if (g->p[GN_MODE] != GEN_WALK) for (uint32_t c = 0; c < g->chord_n; c++)
                    eligible |= generator_mod(d - g->chord[c], generator_count(t)) == 0;
                uint32_t distance = (uint32_t)((int32_t)bank[j] > previous ? (int32_t)bank[j] - previous : previous - (int32_t)bank[j]);
                if (eligible && distance < best) { best = distance; mapped = bank[j]; }
            }
        }
        g->phrase[i] = (uint8_t)mapped;
        g->accent[i] = (uint8_t)(i == anchor_slot ? 100 : !(i & 3u) ? 88 : 72 + (g->value[i] & 15u));
    }
}

static void generator_begin(track_t *t)
{
    generator_t *g = &generator[trk_index(t)];
    generator_release(t);
    g->tuning[0] = t->p[P_SCALE]; g->tuning[1] = t->p[P_ROOT];
    g->tuning[2] = t->p[P_TRANS]; g->tuning[3] = t->p[P_QUANT];
    memset(g->note, 0, sizeof g->note); memset(g->gate, 0, sizeof g->gate);
    if (!g->memory_ready) memset(g->phrase, KB_SILENT, sizeof g->phrase);
    if (!g->memory_ready) g->root = g->walk = g->anchor;
    if (generator_note(t, g->anchor) == KB_SILENT)
        g->root = g->walk = g->anchor = (int16_t)generator_degree(t, 60);
    g->step = g->changes = g->cycles = g->left = g->cursor = g->harmony_due = 0;
    g->harmony_left = seq_beat_samples() * 4u * (1u << g->p[GN_CHANGE]);
    if (!g->memory_ready) g->chord_n = 0;
    g->dirty = 1;
    for (uint32_t i = 0; i < 16u; i++) g->heard[i] = -32768;
    generator_voicing(t);
    generator_plan(t, 1);
    g->running = 1;
}

/* Called with audio interrupts paused by the UI. REC is sealed before enabling;
 * all recording entry points also reject an enabled generator independently. */
static void generator_mode(track_t *t, uint32_t mode)
{
    generator_t *g = &generator[trk_index(t)];
    generator_defaults(trk_index(t));
    if (mode && chord_kit(t)) return;
    if (g->action == GA_RETURN) {
        uint32_t group = generator_keep[trk_index(t)].group;
        for (uint32_t i = 0; i < NTRK; i++) if ((group & (1u << i)) && generator[i].action == GA_RETURN) generator[i].action = 0;
    }
    g->action = 0;
    rec_set(t, 0);
    recording_stop(t);
    seq_release(t);
    if (t->arp_note) arp_off(t);
    trk_all_off(t);
    memset(live_held[trk_index(t)], 0, sizeof live_held[0]);
    t->arp_phys = t->nheld = 0;
    generator_release(t);
    memset(g->held, 0, sizeof g->held);
    /* Clear input owners at the mode boundary: a delayed key/pedal release
     * must not cut a note subsequently started by a stored pattern. */
    for (uint32_t k = 0; k < 27u; k++) if (kb_trk[k] == trk_index(t)) {
        if (!generator_on(t)) for (uint32_t i = 0; i < kb_chn[k]; i++)
            midi_out_event(0x08u | (0x80u | trk_midi_ch(trk_index(t))) << 8 | (uint32_t)kb_chord[k][i] << 16);
        kb_chn[k] = 0;
    }
    midi_forget_track(trk_index(t));
    g->p[GN_MODE] = (int16_t)mode;
    t->seq_idx = (uint16_t)(t->p[P_SLEN] - 1);
    t->seq_pos = 0x7FFFFFFFu;
}

/* Pressed notes form one gesture. The first press sets the anchor; additional
 * held keys add preferred scale degrees. Releasing the gesture latches the palette.
 * The next first press replaces it. Input and generated ownership never overlap. */
static void generator_input(track_t *t, uint32_t note)
{
    generator_t *g = &generator[trk_index(t)];
    if (note >= 128u || (g->held[note >> 5] & (1u << (note & 31u)))) return;
    uint32_t held = g->held[0] | g->held[1] | g->held[2] | g->held[3];
    int16_t degree = (int16_t)generator_degree(t, note);
    if (!held) {
        g->anchor = g->root = g->walk = degree;
        g->preferred_n = g->answer_n = 0; g->path_valid = g->path_pos = 0;
    }
    g->held[note >> 5] |= 1u << (note & 31u);
    uint32_t count = generator_count(t), duplicate = 0;
    for (uint32_t i = 0; i < g->preferred_n; i++)
        duplicate |= generator_mod(g->preferred[i], count) == generator_mod(degree, count);
    if (!duplicate && g->preferred_n < 16u) g->preferred[g->preferred_n++] = degree;
    /* A single root uses its scale-derived chord; a multi-note gesture is the
     * explicitly preferred palette. Keep the root in preferred[0] while counting. */
    g->changes = g->harmony_due = 0; g->dirty = 1;
    generator_voicing_for(t, g->running ? g->color : (uint32_t)g->p[GN_COLOR]);
    g->harmony_left = seq_beat_samples() * 4u * (1u << g->p[GN_CHANGE]);
}
static void generator_input_off(track_t *t, uint32_t note)
{
    if (note < 128u) generator[trk_index(t)].held[note >> 5] &= ~(1u << (note & 31u));
}

/* UI calls with interrupts guarded. A kept linked ensemble returns together
 * at its source's phrase boundary, including each part's independent memory. */
static void generator_keep_track(uint32_t tr, uint32_t group)
{
    generator_t *g = &generator[tr];
    if (!g->running) { generator_begin(&trk[tr]); g->running = 0; }
    generator_t *saved = &generator_keep[tr].state;
    *saved = *g; saved->action = 0;
    saved->p[GN_LENGTH] = g->length; saved->p[GN_RATE] = g->rate;
    saved->p[GN_FILL] = g->fill; saved->p[GN_RANGE] = g->range;
    saved->p[GN_GATE] = g->gate_value; saved->p[GN_COLOR] = g->color;
    saved->p[GN_PALETTE] = g->palette; saved->p[GN_SEED] = g->seed_used;
    saved->p[GN_ANSWER] = g->answer_style; saved->p[GN_CONDITION] = g->condition; saved->p[GN_DYNAMICS] = g->dynamics; saved->p[GN_STRUM] = g->strum;
    if (g->running && g->source_used) saved->p[GN_SOURCE] = g->route_used;
    generator_keep[tr].valid = 1; generator_keep[tr].group = (uint8_t)group;
}
static int generator_action(track_t *t, uint32_t action)
{
    uint32_t tr = trk_index(t), error;
    generator_t *g = &generator[tr];
    if (!generator_on(t)) return 0;
    if (action == GA_MATCH) {
        uint32_t source = (uint32_t)g->p[GN_SOURCE];
        if (!source || source > trk_n() || source == tr + 1u) return 0;
        track_t *leader = &trk[source-1u];
        t->p[P_SCALE] = leader->p[P_SCALE]; t->p[P_ROOT] = leader->p[P_ROOT];
        t->p[P_TRANS] = leader->p[P_TRANS]; t->p[P_QUANT] = leader->p[P_QUANT];
        return 1;
    }
    if (action == GA_LOCK) { g->p[GN_EVOLVE] = 0; return 1; }
    if (action == GA_KEEP) {
        int source = g->running && g->source_used ? (int)g->source_used - 1 : generator_source(tr, &error);
        if (source < 0 || !generator_on(&trk[source])) return 0;
        uint32_t group = 0;
        for (uint32_t i = 0; i < trk_n(); i++) {
            int heard = generator[i].running && generator[i].source_used ? (int)generator[i].source_used - 1 : generator_source(i, &error);
            if (generator_on(&trk[i]) && heard == source) group |= 1u << i;
        }
        for (uint32_t i = 0; i < trk_n(); i++) if (group & (1u << i)) generator_keep_track(i, group);
        return 1;
    }
    if (action == GA_RETURN) {
        uint32_t group = generator_keep[tr].group;
        if (!generator_keep[tr].valid || !group) return 0;
        for (uint32_t i = 0; i < NTRK; i++) if ((group & (1u << i)) &&
            (!generator_keep[i].valid || generator_keep[i].group != group || i >= trk_n())) return 0;
        int running = 0;
        for (uint32_t i = 0; i < NTRK; i++) if (group & (1u << i)) running |= generator[i].running;
        for (uint32_t i = 0; i < NTRK; i++) if (group & (1u << i)) generator[i].action = GA_RETURN;
        if (!running) for (uint32_t i = 0; i < NTRK; i++) if (group & (1u << i)) generator_plan(&trk[i], 1);
        return 1;
    }
    if (action == GA_NEW) g->p[GN_SEED] = (int16_t)(g->p[GN_SEED] % 127 + 1);
    g->action = (uint8_t)action;
    if (!g->running) generator_plan(t, 1);
    return 1;
}

/* Every onset is a new finite event. Repeated pitches retrigger instead of
 * renewing old ownership. Even CHORD never accumulates an earlier voicing. */
static void generator_sound(track_t *t, uint32_t note, uint32_t gate, uint32_t velocity)
{
    generator_t *g = &generator[trk_index(t)];
    if (note == KB_SILENT || g->n >= 4u) return;
    for (uint32_t i = 0; i < g->n; i++) if (g->note[i] == note) return;
    g->note[g->n] = (uint8_t)note; g->gate[g->n++] = gate;
    trk_note_on(t, note, velocity);
}

static void generator_event(track_t *t)
{
    generator_t *g = &generator[trk_index(t)];
    for (uint32_t i = 0; i < g->n; i++) trk_note_off(t, g->note[i]);
    g->n = g->pending_n = 0;
    uint32_t at = g->cursor;
    g->heard[at] = -32768;
    uint32_t period = seq_div_samples(g->rate);
    uint32_t gate = period * g->gate_value / 127u;
    /* Events run before a control block is rendered. A sub-block on/off pair
     * would otherwise vanish before any samples can sound. Round tiny gates
     * up to one audible control block, in the active clock's units. */
    uint32_t min_gate = midi_sync_active ?
        (uint32_t)(((uint64_t)2u * CTL * MIDI_BEAT_UNITS + beat_samples() - 1u) / beat_samples()) : 2u * CTL;
    if (gate < min_gate) gate = min_gate;
    if (gate > period) gate = period;
    uint32_t phase = g->cycles + g->cycle_origin;
    uint32_t allowed = !g->condition || (g->condition == 1 && !(phase & 1u)) ||
        (g->condition == 2 && (phase & 1u)) || (g->condition == 3 && (phase & 3u) == 3u);
    if (allowed && !g->answer_wait && ((g->rhythm >> at) & 1u)) {
        if (g->p[GN_MODE] == GEN_CHORD) {
            uint32_t voices = trk_vmode(t) == V_POLY ? g->chord_n : 1u;
            uint32_t strum = midi_sync_active ? (uint32_t)((uint64_t)g->strum * MIDI_BEAT_UNITS * FS / (1000u * beat_samples())) : (uint32_t)g->strum * FS / 1000u;
            if (strum > gate / (voices * 2u)) strum = gate / (voices * 2u);
            uint32_t max_strum = voices > 1u && gate > min_gate ? (gate - min_gate) / (voices - 1u) : 0u;
            if (strum > max_strum) strum = max_strum;
            for (uint32_t i = 0; i < voices; i++) {
                uint32_t note = generator_note(t, g->chord[i]), velocity = !g->dynamics ? 88u : !(at & 3u) ? 96u : 78u;
                uint32_t delay = i * strum;
                if (!delay) generator_sound(t, note, gate, velocity);
                else if (note != KB_SILENT) {
                    uint32_t j = g->pending_n++;
                    g->pending_note[j] = (uint8_t)note; g->pending_vel[j] = (uint8_t)velocity;
                    g->pending_gate[j] = gate - delay; g->delay[j] = delay;
                }
            }
        } else generator_sound(t, g->phrase[at], gate, g->dynamics ? g->accent[at] : 88u);
        uint32_t heard = g->p[GN_MODE] == GEN_CHORD ? generator_note(t, g->root) : g->phrase[at];
        if (heard != KB_SILENT) g->heard[at] = (int16_t)(generator_mapped_degree(t, heard) - g->root);
    }
    g->step++;
    g->cursor = (uint8_t)((at + 1u) % g->length);
}

/* One chronological scheduler for all parts. Harmony and links resolve before
 * any note at that timestamp, independent of track order or block size. */
static void generator_run(uint32_t mask, uint32_t n)
{
    for (uint32_t i = 0; i < trk_n(); i++) if (mask & (1u << i)) {
        track_t *t = &trk[i]; generator_t *g = &generator[i];
        if (!generator_on(t)) continue;
        if (chord_kit(t)) { generator_release(t); g->p[GN_MODE] = 0; continue; }
        uint32_t error; generator_source(i, &error); g->link_bad = (uint8_t)error;
        if (!song.playing) { if (g->running) generator_release(t); continue; }
        if (error) { generator_release(t); continue; }
        uint32_t sig = generator_signature(t);
        if (!g->running || g->signature != sig) {
            if (g->signature && g->signature != sig) {
                g->preferred_n = g->path_valid = g->path_pos = 0;
                memset(g->held, 0, sizeof g->held);
            }
            g->signature = sig; generator_begin(t);
        }
    }
    if (!song.playing) return;
    for (;;) {
        uint32_t restored = 0, boundary = 0, active = 0;
        /* RETURN is a group transaction, even with different phrase lengths. */
        for (uint32_t i = 0; i < trk_n(); i++) if ((mask & (1u << i)) && generator[i].action == GA_RETURN) {
            uint32_t group = generator_keep[i].group, leader = i;
            for (uint32_t j = 0; j < trk_n(); j++) if ((group & (1u << j)) && !generator_keep[j].state.p[GN_SOURCE]) leader = j;
            generator_t *source = &generator[leader];
            if (source->running && (source->left || source->cursor)) continue;
            for (uint32_t j = 0; j < trk_n(); j++) if (group & (1u << j)) {
                generator[j].action = GA_RETURN; generator_plan(&trk[j], 2); restored |= 1u << j;
            }
        }
        for (uint32_t i = 0; i < trk_n(); i++) if ((mask & (1u << i)) && generator_on(&trk[i]) && generator[i].running && !generator[i].link_bad) {
            generator_t *g = &generator[i]; active |= 1u << i;
            if (restored & (1u << i)) continue;
            if (!g->p[GN_SOURCE] && !g->harmony_left) {
                g->harmony_due = g->p[GN_HARMONY] != 0;
                g->harmony_left = seq_beat_samples() * 4u * (1u << g->p[GN_CHANGE]);
            }
            if (!g->left && !g->cursor && g->step) {
                boundary |= 1u << i; g->cycles++;
                uint32_t history_n = 0;
                for (uint32_t j = 0; j < g->length; j++) if (g->heard[j] != -32768) g->history[history_n++] = g->heard[j];
                if (history_n) g->history_n = (uint8_t)history_n;
                if (!g->p[GN_SOURCE] && g->harmony_due) {
                    if (g->p[GN_HARMONY]) generator_next_chord(&trk[i]);
                    g->harmony_due = 0;
                    g->harmony_left = seq_beat_samples() * 4u * (1u << g->p[GN_CHANGE]);
                }
            }
        }
        for (uint32_t i = 0; i < trk_n(); i++) if (active & (1u << i)) {
            generator_t *g = &generator[i]; uint32_t error;
            int source = generator_source(i, &error);
            if (source >= 0 && (uint32_t)source != i) {
                uint32_t count = generator_count(&trk[i]);
                int16_t root = (int16_t)(g->anchor - (int32_t)generator_mod(g->anchor, count) +
                    (int32_t)generator_mod(generator[source].root, count));
                if (g->root != root || g->source_used != (uint32_t)source + 1u) {
                    g->root = root; g->dirty = 1;
                    generator_voicing_for(&trk[i], g->color);
                }
            }
            g->source_used = (uint8_t)(source + 1); g->route_used = (uint8_t)g->p[GN_SOURCE]; g->harmony_due = g->p[GN_SOURCE] ? 0 : g->harmony_due;
            if ((boundary & (1u << i)) && g->color != g->p[GN_COLOR]) generator_voicing(&trk[i]);
            if (boundary & (1u << i)) generator_plan(&trk[i], 1);
            else if (g->dirty || (g->answer_wait && g->p[GN_SOURCE] && generator[g->p[GN_SOURCE]-1].history_n)) generator_plan(&trk[i], 0);
        }
        for (uint32_t i = 0; i < trk_n(); i++) if ((active & (1u << i)) && !generator[i].left) {
            generator_event(&trk[i]); generator[i].left = seq_div_samples(generator[i].rate);
        }
        for (uint32_t i = 0; i < trk_n(); i++) if (active & (1u << i)) {
            generator_t *g = &generator[i];
            for (uint32_t j = 0; j < g->pending_n;) {
                if (g->delay[j]) { j++; continue; }
                generator_sound(&trk[i], g->pending_note[j], g->pending_gate[j], g->pending_vel[j]);
                g->pending_n--; uint32_t last = g->pending_n;
                g->pending_note[j] = g->pending_note[last]; g->pending_vel[j] = g->pending_vel[last];
                g->pending_gate[j] = g->pending_gate[last]; g->delay[j] = g->delay[last];
            }
        }
        if (!n || !active) break;
        uint32_t chunk = n;
        for (uint32_t i = 0; i < trk_n(); i++) if (active & (1u << i)) {
            generator_t *g = &generator[i];
            if (chunk > g->left) chunk = g->left;
            if (!g->p[GN_SOURCE] && chunk > g->harmony_left) chunk = g->harmony_left;
            for (uint32_t j = 0; j < g->n; j++) if (chunk > g->gate[j]) chunk = g->gate[j];
            for (uint32_t j = 0; j < g->pending_n; j++) if (chunk > g->delay[j]) chunk = g->delay[j];
        }
        for (uint32_t i = 0; i < trk_n(); i++) if (active & (1u << i)) {
            generator_t *g = &generator[i];
            for (uint32_t j = 0; j < g->n;) {
                if (g->gate[j] <= chunk) {
                    trk_note_off(&trk[i], g->note[j]); g->n--;
                    g->note[j] = g->note[g->n]; g->gate[j] = g->gate[g->n];
                } else { g->gate[j] -= chunk; j++; }
            }
            g->left -= chunk;
            for (uint32_t j = 0; j < g->pending_n; j++) g->delay[j] -= chunk;
            if (!g->p[GN_SOURCE]) g->harmony_left -= chunk;
        }
        n -= chunk;
    }
}
static void generator_tick(track_t *t, uint32_t n) { generator_run(1u << trk_index(t), n); }
static void generator_advance(uint32_t n) { generator_run((1u << trk_n()) - 1u, n); }
