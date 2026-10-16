/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Session-only musical memory. Fixed-point Marbles memory is in generator_memory.h;
 * scale mapping, styles, lifecycle and input-palette integration are original Melodee
 * code. Phrase length and event gates are literal clock slots, independent of memory.
 * A locked idea survives STOP/PLAY; only NEW or a new SEED replaces its memory.
 * Generated notes enter the retrospective capture ring (seq.c cap_ev) like played
 * notes, so holding REC keeps a generated phrase as a pattern; REC itself stays off. */
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

/* The sounding chord's scale: a borrowed or secondary chord's (EVOLVE), else the key's.
 * A linked part copies its leader's with the root. */
static uint32_t generator_mask(const track_t *t)
{
    uint32_t mask = generator[trk_index(t)].mask;
    return mask && !micro_active(t) ? mask : scale_mask(t);
}
static uint32_t generator_note(const track_t *t, int32_t degree)
{
    if (micro_active(t)) return micro_degree_map(t, degree, 0, 1);
    int32_t count = (int32_t)generator_count(t), oct = degree / count;
    degree %= count;
    if (degree < 0) { degree += count; oct--; }
    uint32_t mask = generator_mask(t), i;
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
    uint32_t mask = generator_mask(t) & ((1u << pc) - 1u), degree = 0;
    while (mask) { degree += mask & 1u; mask >>= 1; }
    return octave * (int32_t)generator_count(t) + (int32_t)degree;
}

static void generator_off(track_t *t, uint32_t note) { trk_note_off(t, note); cap_off(t, note); }
static void generator_release(track_t *t)
{
    generator_t *g = &generator[trk_index(t)];
    for (uint32_t i = 0; i < g->n; i++) generator_off(t, g->note[i]);
    g->n = g->pending_n = 0;
    g->running = 0;
}

static int32_t generator_pitch(const track_t *t, int32_t d)
{
    uint32_t note = generator_note(t, d);
    return note == KB_SILENT ? -10000000 : micro_active(t) ? micro_pitch(t, note) : (int32_t)note * 10000;
}

static int generator_diatonic(const track_t *t);
/* Relative scale pitch without MIDI-register clipping, in the sounding chord's scale (or
 * the key's). Used only for choosing intervals; generator_note validates every note. */
static int32_t generator_scale_tone(const track_t *t, int32_t degree, uint32_t mask)
{
    int32_t count = (int32_t)generator_count(t), octave = degree / count;
    degree %= count; if (degree < 0) { degree += count; octave--; }
    if (micro_active(t)) {
        const micro_scale_t *s = micro_scale(t);
        return octave * s->pitch[count] + s->pitch[degree];
    }
    uint32_t semi = 0;
    for (; semi < 12u; semi++) if ((mask >> semi) & 1u) { if (!degree) break; degree--; }
    return (octave * 12 + (int32_t)semi) * 10000;
}
static int32_t generator_tone(const track_t *t, int32_t degree) { return generator_scale_tone(t, degree, generator_mask(t)); }
static int32_t generator_key_tone(const track_t *t, int32_t degree) { return generator_scale_tone(t, degree, scale_mask(t)); }
/* VOICING shapes: scale steps above the root in a seven-note scale; a zero fourth
 * entry means three voices. Other scales aim for the semitone targets instead. */
static const uint8_t GEN_SHAPE[GV_COUNT][4] = {
    {0,2,4,0}, {0,2,4,6}, {0,2,4,0}, {0,2,6,8}, {0,1,4,0},
    {0,2,4,8}, {0,3,4,0}, {0,2,5,8}, {0,3,6,9}, {0,2,6,0}};
static const uint8_t GEN_TARGET[GV_COUNT][4] = {
    {0,4,7,0}, {0,4,7,11}, {0,4,7,0}, {0,4,11,14}, {0,2,7,0},
    {0,4,7,14}, {0,5,7,0}, {0,4,9,14}, {0,5,10,15}, {0,4,11,0}};
static uint32_t generator_voices(uint32_t color) { return GEN_SHAPE[color % GV_COUNT][3] ? 4u : 3u; }
static void generator_chord_shape(const track_t *t, int32_t root, uint32_t color, uint8_t shape[4])
{
    color %= GV_COUNT;
    memcpy(shape, GEN_SHAPE[color], 4);
    uint32_t count = generator_count(t);
    if (!micro_active(t) && count != 7u) {
        /* Sparse and chromatic Western scales do not use the same ordinal
         * thirds as a seven-note scale. Find the nearest legal intervals. */
        int32_t target[4];
        for (uint32_t i = 0; i < 4u; i++) target[i] = GEN_TARGET[color][i] * 10000;
        if (!target[3]) target[3] = 110000;
        for (uint32_t i = 1; i < 4u; i++) {
            int32_t best = 0x7FFFFFFF; shape[i] = 0;
            for (uint32_t d = 1; d <= 2u * count; d++) {
                int32_t interval = generator_tone(t, root + (int32_t)d) - generator_tone(t, root);
                int32_t distance = interval > target[i] ? interval - target[i] : target[i] - interval;
                if (distance < best) { best = distance; shape[i] = (uint8_t)d; }
            }
            /* A minor third takes a minor seventh. */
            if (i == 1 && target[1] == 40000 && generator_tone(t, root + shape[1]) - generator_tone(t, root) < 35000)
                for (uint32_t j = 2; j < 4u; j++) if (target[j] == 110000) target[j] = 100000;
        }
    }
    if ((color == GV_911 || color == GV_ADD9 || color == GV_69) && generator_diatonic(t)) {
        int32_t ninth = generator_tone(t, root + shape[3]) - generator_tone(t, root + (int32_t)count);
        if (ninth < 18000) shape[3] = color == GV_911 ? 10 : 7;   /* an eleventh (or the octave) instead of a flat ninth */
    }
}
static void generator_voicing_for(track_t *t, uint32_t color)
{
    generator_t *g = &generator[trk_index(t)];
    int32_t count = (int32_t)generator_count(t), best = 0x7FFFFFFF;
    uint32_t n = generator_voices(color);
    uint8_t shape[4]; generator_chord_shape(t, g->root, color, shape);
    uint8_t home[4]; generator_chord_shape(t, g->anchor, color, home);
    int16_t chosen[4] = {0};
    for (uint32_t inv = 0; inv < n; inv++) for (int32_t oct = -2; oct <= 2; oct++) {
        int16_t candidate[4]; int32_t cost = 0;
        for (uint32_t i = 0; i < n; i++) {
            int32_t d = g->root + shape[i] + count * oct;
            if (i < inv) d += count;
            if (color == GV_OPEN && i == 1) d += count;
            candidate[i] = (int16_t)d;
        }
        for (uint32_t i = 1; i < n; i++) for (uint32_t j = i; j && candidate[j] < candidate[j-1]; j--) {
            int16_t d = candidate[j]; candidate[j] = candidate[j-1]; candidate[j-1] = d;
        }
        for (uint32_t i = 0; i < n; i++) {
            int32_t pitch = generator_pitch(t, candidate[i]);
            int32_t target = generator_pitch(t, g->chord_n == n ? g->chord[i] : g->anchor + shape[i]);
            int32_t centre = generator_pitch(t, g->anchor + home[i]);
            if (target < 0) target = generator_pitch(t, g->anchor);
            if (centre < 0) centre = target;
            int32_t delta = pitch - target, drift = pitch - centre;
            if (pitch < 0 || target < 0) { cost = 0x3FFFFFFF; break; }
            /* Smallest voice movement, with a gentle pull back to the played
             * register so a long journey never creeps up or down the keyboard. */
            cost += (delta < 0 ? -delta : delta) + (drift < 0 ? -drift : drift) / 3;
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
static uint32_t generator_bits(uint32_t mask)
{
    uint32_t n = 0;
    while (mask) { n += mask & 1u; mask >>= 1; }
    return n;
}
/* Exact slot lengths: a division's remainder is carried (as Bresenham's line), so parts
 * at different rates stay on one bar grid however long a jam runs. */
static uint32_t generator_slot(generator_t *g)
{
    static const uint8_t num[10] = {1, 1, 1, 1, 1, 1, 2, 4, 8, 16}, den[10] = {1, 2, 4, 8, 3, 6, 1, 1, 1, 1};
    uint32_t r = g->rate < 10u ? g->rate : 1u, total = seq_beat_samples() * num[r] + g->frac;
    g->frac = total % den[r];
    return total / den[r];
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
    /* A JAM owns the rotation and the leader's planned cycle: a section's new seed
     * must not undo them at the same boundary. */
    if (!g->jam_lead) g->rhythm_shift = 0;
    if (!g->p[GN_JAM]) g->path_valid = g->path_pos = 0;
    for (uint32_t i = 0; i < 16u; i++)
        g->value[i] = g->memory.loop[i];
}

/* Every usable scale degree of the phrase register, ascending. A chord (or latched
 * palette) tone is 1, its root 2; others carry COLOR's weight. */
typedef struct { uint8_t note[128], weight[128], tone[128], pos[128]; uint32_t n; } gen_bank_t;
static void generator_bank(const track_t *t, const generator_t *g, gen_bank_t *b)
{
    uint32_t count = generator_count(t), span = count * g->range;
    int32_t low = g->anchor - (int32_t)(count * (g->range / 2u));
    b->n = 0;
    for (uint32_t i = 0; i < span && b->n < 128u; i++) {
        int32_t degree = low + (int32_t)i;
        uint32_t mapped = generator_note(t, degree), member = 0, pc = generator_mod(degree, count);
        if (mapped == KB_SILENT) continue;
        if (g->preferred_n > 1u && !g->p[GN_SOURCE]) {
            for (uint32_t j = 0; j < g->preferred_n; j++) member |= pc == generator_mod(g->preferred[j], count);
        } else for (uint32_t j = 0; j < g->chord_n; j++)
            member |= pc == generator_mod(g->chord[j], count);
        uint32_t weight = member ? 100u : g->palette;
        if (g->p[GN_MODE] == GEN_WALK) {
            uint32_t interval = generator_mod(degree - g->root, count);
            weight = interval == 0 ? 220u : interval == 4u % count ? 100u : g->palette / 5u;
            weight = weight ? (weight * (span - i) + span - 1u) / span : 0u;   /* a bass leans low */
        }
        if (!weight) continue;
        b->note[b->n] = (uint8_t)mapped; b->weight[b->n] = (uint8_t)weight; b->pos[b->n] = (uint8_t)i;
        b->tone[b->n++] = (uint8_t)(member ? (pc == generator_mod(g->root, count) ? 2u : 1u) : 0u);
    }
}
/* DRIFT: memory picks from a weighted inverse CDF, shaped like a sung line. Steps
 * and thirds are likelier than leaps, a leap is followed by a step back (gap fill),
 * strong slots favour chord tones and the phrase's last onset resolves onto one.
 * The weights are a pure function of the decisions, COLOR, RANGE and harmony, so
 * returning COLOR/RANGE recovers a locked melody exactly. */
static const uint8_t GEN_STEP[7] = {4, 10, 8, 5, 3, 2, 1};
static uint32_t generator_drift_weight(const gen_bank_t *b, uint32_t j, int32_t prev, int32_t leap, uint32_t strength)
{
    uint32_t w = b->weight[j];
    if (prev >= 0) {
        int32_t d = (int32_t)b->pos[j] - prev;
        uint32_t a = (uint32_t)(d < 0 ? -d : d);
        w *= GEN_STEP[a < 6u ? a : 6u];
        if ((leap >= 3 && d < 0 && d >= -2) || (leap <= -3 && d > 0 && d <= 2)) w *= 3u;
    } else w *= 4u;
    if (b->tone[j]) w *= strength == 3u ? (b->tone[j] == 2u ? 16u : 8u) : strength == 2u ? 3u : strength ? 2u : 1u;
    else if (strength == 3u) w = 0;
    return w;
}
static uint32_t generator_drift(const gen_bank_t *b, uint16_t value, int32_t prev, int32_t leap, uint32_t strength)
{
    if (!b->n) return KB_SILENT;
    uint32_t total = 0;
    for (uint32_t j = 0; j < b->n; j++) total += generator_drift_weight(b, j, prev, leap, strength);
    if (!total) {
        strength = 2u;
        for (uint32_t j = 0; j < b->n; j++) total += generator_drift_weight(b, j, prev, leap, strength);
    }
    /* <=128 candidates * 220 * 10 * 3 * 16 < 2^24; the 64-bit product cannot overflow. */
    uint32_t draw = (uint32_t)(((uint64_t)value * total) >> 16);
    for (uint32_t j = 0; j < b->n; j++) {
        uint32_t w = generator_drift_weight(b, j, prev, leap, strength);
        if (draw < w) return b->note[j];
        draw -= w;
    }
    return b->note[b->n - 1u];
}
/* Chord-tone arpeggios: the k-th onset's place in the ascending tone pool (m tones,
 * per tones in each scale period). UP/DOWN/BOUNCE/INWARD (outside-in), PINKY (a top
 * pedal), THUMB (a bottom pedal), ALBERTI (low-high-mid-high) and STAIRS (overlapping
 * rising threes, a melodic sequence). */
static uint32_t generator_arp_index(uint32_t style, uint32_t k, uint32_t m, uint32_t per)
{
    if (m < 2u) return 0;
    switch (style) {
    case AS_UP: return k % m;
    case AS_DOWN: return m - 1u - k % m;
    case AS_BOUNCE: { uint32_t p = k % (2u * m - 2u); return p < m ? p : 2u * m - 2u - p; }
    case AS_INWARD: { uint32_t j = k % m; return j & 1u ? m - 1u - j / 2u : j / 2u; }
    case AS_PINKY: return k & 1u ? m - 1u : (k / 2u) % (m - 1u);
    case AS_THUMB: return k & 1u ? 1u + (k / 2u) % (m - 1u) : 0u;
    case AS_ALBERTI: {
        static const uint8_t a[4] = {0, 2, 1, 2};
        uint32_t octaves = (m + per - 1u) / per, i = (k / 4u % octaves) * per + a[k & 3u];
        return i < m ? i : m - 1u;
    }
    default: return (k / 3u + k % 3u) % m;
    }
}
/* Bass figures in scale degrees from r, the chord root in the bass register. WALK
 * steps from the root toward the next chord and approaches it from a scale step
 * (the caller places both roots so the walk has room); when the chord stays, it
 * plays one of four classic figures chosen by memory. */
static int32_t generator_bass_degree(const track_t *t, const generator_t *g, uint32_t style, int32_t r, uint32_t k, uint32_t onsets,
                                     uint16_t value, int32_t home, int32_t goal, int32_t prev)
{
    int32_t count = (int32_t)generator_count(t);
    uint8_t s[4]; generator_chord_shape(t, r, GV_TRIAD, s);
    switch (style) {
    case BS_ROOT: return r;
    case BS_OCTAVE: return k & 1u ? r + count : r;
    case BS_FIFTH:
        if (!(k & 1u)) return r;
        return r + s[2] - count >= g->anchor - count / 2 ? r + s[2] - count : r + s[2];
    case BS_ARP: { const int32_t a[6] = {0, s[1], s[2], count, s[2], s[1]}; return r + a[k % 6u]; }
    case BS_BOOGIE: { const int32_t a[8] = {0, s[1], s[2], s[2] + 1, s[2] + 2, s[2] + 1, s[2], s[1]}; return r + a[k % 8u]; }
    case BS_PEDAL: return g->anchor + (int32_t)generator_mod(home - g->anchor, (uint32_t)count);
    default: break;
    }
    if (!k || onsets < 2u) return r;
    uint32_t last = k + 1u == onsets;
    if (goal == r) {
        const int32_t figure[4][4] = {{0, s[1], s[2], s[2] + 1}, {0, 1, s[1], s[2]},
            {0, -1, -2, -3}, {0, s[2] - count, s[2] - count + 1, s[2] - count + 2}};
        uint32_t at = last ? 3u : 1u + (k - 1u) * 2u / (onsets - 2u);
        return r + figure[value >> 14][at];
    }
    int32_t side = goal > r ? -1 : 1, approach = goal + side;
    if (last) return approach == prev ? goal - side : approach;
    int32_t d = r + (approach - r) * (int32_t)k / (int32_t)(onsets - 1u);
    if (!(k & 1u)) {   /* strong beats on chord tones */
        int32_t best = d, distance = 1000;
        for (int32_t c = d - 2; c <= d + 2; c++) {
            uint32_t i = generator_mod(c - r, (uint32_t)count);
            int32_t away = c > d ? c - d : d - c;
            if ((i == 0u || i == s[1] || i == s[2]) && away < distance && c != approach) { best = c; distance = away; }
        }
        d = best;
    }
    if (d == prev) d += approach > d ? 1 : -1;
    return d;
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
        g->n = g->pending_n = g->cursor = 0; g->left = g->step = g->cycles = g->frac = 0;
        memset(g->held, 0, sizeof g->held);
        g->running = song.playing != 0;
    }
    if (action == GA_NEW) g->memory_ready = 0;
    int rebuild = boundary || g->dirty || action || g->range != g->p[GN_RANGE] ||
        g->palette != g->p[GN_PALETTE] || g->color != g->p[GN_COLOR] || g->length != g->p[GN_LENGTH] || g->p[GN_MODE] == GEN_ANSWER;
    int fresh = !g->memory_ready || (boundary && g->seed_used != g->p[GN_SEED]);
    if (fresh) {
        generator_identity(g); rebuild = 1;
        /* Fresh harmonic choices plan their cycle at once, so a bass can see it. */
        if (!g->p[GN_SOURCE] && g->p[GN_HARMONY] && !g->p[GN_JAM]) generator_harmony_restart(t);
    }
    if (boundary) {
        g->length = (uint8_t)g->p[GN_LENGTH]; g->range = (uint8_t)g->p[GN_RANGE];
        g->fill = (uint8_t)g->p[GN_FILL]; g->rate = (uint8_t)g->p[GN_RATE];
        g->gate_value = (uint8_t)g->p[GN_GATE];
        g->color = (uint8_t)g->p[GN_COLOR]; g->palette = (uint8_t)g->p[GN_PALETTE];
        g->answer_style = (uint8_t)g->p[GN_ANSWER]; g->condition = (uint8_t)g->p[GN_CONDITION]; g->dynamics = (uint8_t)g->p[GN_DYNAMICS]; g->strum = (uint8_t)g->p[GN_STRUM];
        g->style = (uint8_t)g->p[generator_style_id(g)];
        if (action == GA_RHYTHM) g->rhythm_shift = (uint8_t)(generator_word(&g->rhythm_random) % g->length);
        uint16_t mask = generator_rhythm(g->length, g->fill);
        uint32_t shift = g->rhythm_shift % g->length;
        g->rhythm = (uint16_t)(((uint32_t)mask << shift | (uint32_t)mask >> (g->length - shift)) & ((1u << g->length) - 1u));
        if (action != GA_RETURN && (fresh || action == GA_REMIX || (!action && g->cycles && g->p[GN_EVOLVE]))) {
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
    gen_bank_t bank; generator_bank(t, g, &bank);
    uint32_t candidates = bank.n, anchor_slot = 0, count = generator_count(t);
    if (g->rhythm) while (!((g->rhythm >> anchor_slot) & 1u)) anchor_slot++;
    const generator_t *source = g->p[GN_SOURCE] ? &generator[g->p[GN_SOURCE]-1] : g;
    if (g->p[GN_MODE] == GEN_ANSWER && ((boundary && action != GA_RETURN) || !g->answer_n) && source->history_n) {
        g->answer_n = source->history_n;
        memcpy(g->answer_memory, source->history, g->answer_n * sizeof g->answer_memory[0]);
    }
    g->answer_wait = g->p[GN_MODE] == GEN_ANSWER && !g->answer_n;
    uint8_t branch[16]; uint32_t branch_n = generator_branch(branch, g->cycles + g->cycle_origin);
    int32_t low = g->anchor - (int32_t)(count * (g->range / 2u));
    int32_t span = (int32_t)(count * g->range);
    uint32_t role = (uint32_t)g->p[GN_MODE], style = g->style, onsets = generator_bits(g->rhythm);
    int drift = role == GEN_CHORD || (role == GEN_LOOP && (style == AS_DRIFT || style == AS_MOTIF)) || (role == GEN_WALK && style == BS_DRIFT);
    /* The harmonic leader: its home (a pedal), its coming chord (a walking approach). */
    uint32_t error; int lead_at = generator_source(trk_index(t), &error);
    const generator_t *lead = lead_at >= 0 ? &generator[lead_at] : g;
    int32_t home = lead->anchor + (lead->p[GN_JAM] && lead->jam_started ? lead->jam_home : 0), goal = g->root, walk_root = g->root;
    if (role == GEN_WALK && style == BS_WALK) {
        /* A walking line keeps going: the root takes the octave nearest the last walked
         * note, and the next root the octave that leaves about one step per onset. */
        int32_t lo = g->anchor - (int32_t)count, hi = g->anchor + (int32_t)(count + count / 2u), best = 1000;
        for (int32_t o = -1; o <= 1; o++) {
            int32_t r = g->root + o * (int32_t)count, away = r > g->walk ? r - g->walk : g->walk - r;
            if (r >= lo && r <= hi && away < best) { best = away; walk_root = r; }
        }
        goal = walk_root;
        uint32_t soon, phrase = seq_div_samples(g->rate) * g->length, next = generator_upcoming(lead, count, phrase, &soon);
        int32_t up = walk_root + (int32_t)generator_mod((int32_t)next - walk_root, count), down = up - (int32_t)count;
        if (soon && up != walk_root) {
            int32_t want = (int32_t)onsets, du = up - walk_root - want, dd = walk_root - down - want;
            goal = (du < 0 ? -du : du) <= (dd < 0 ? -dd : dd) ? up : down;
            if (goal < lo) goal = up;
            if (goal > hi) goal = down;
        }
    }
    uint8_t pool[16]; uint32_t m = 0;
    for (uint32_t j = 0; j < candidates && m < 16u; j++) if (bank.tone[j]) pool[m++] = (uint8_t)j;
    uint32_t per = m ? (m + g->range - 1u) / g->range : 1u;
    /* The first onset, then each slot in order: every note leads from the last heard one. */
    int32_t prev = -1, leap = 0, prev_degree = walk_root, last_walk = g->walk;
    for (uint32_t step = 0, k = 0; step < 16u; step++) {
        uint32_t i = (anchor_slot + step) & 15u;
        uint32_t hit = i < g->length && ((g->rhythm >> i) & 1u);
        g->motif[i] = (uint8_t)(g->value[i] >> 12);
        uint32_t strength = hit && onsets > 1u && k + 1u == onsets ? 3u : !(i & 3u) ? 2u : !(i & 1u) ? 1u : 0u;
        uint32_t mapped = drift ? generator_drift(&bank, g->value[i], prev, leap, strength) : KB_SILENT;
        if (role == GEN_ANSWER && g->answer_n) {
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
                    int32_t d = generator_mapped_degree(t, bank.note[j]); uint32_t eligible = 0;
                    for (uint32_t c = 0; c < g->chord_n; c++) eligible |= generator_mod(d - g->chord[c], count) == 0;
                    if (!eligible) continue;
                    int32_t pitch = generator_pitch(t, d), distance = pitch > target ? pitch - target : target - pitch;
                    if (distance < best) { best = distance; mapped = bank.note[j]; }
                }
            }
        } else if (role == GEN_LOOP && !drift && m) {
            uint32_t j = pool[generator_arp_index(style, k, m, per)];
            /* COLOR lets memory-chosen offbeats take the scale note above: a passing colour. */
            if ((i & 1u) && j + 1u < candidates && !bank.tone[j + 1u] && ((uint32_t)(g->value[i] >> 8) * 100u >> 8) < g->palette / 2u) j++;
            mapped = bank.note[j];
        } else if (role == GEN_WALK && !drift) {
            int32_t d = generator_bass_degree(t, g, style, style == BS_WALK ? walk_root : g->root, k, onsets, g->value[anchor_slot], home, goal, prev_degree);
            mapped = generator_note(t, d);
            if (mapped == KB_SILENT) mapped = generator_note(t, g->root);
            if (hit) prev_degree = d;
            if (hit && boundary && k + 1u == onsets) last_walk = d;
        }
        /* At register limits, the ordinary bank supplies a playable fallback. */
        if (mapped == KB_SILENT) mapped = generator_drift(&bank, g->value[i], prev, leap, strength);
        if (!step && candidates && (drift || role == GEN_ANSWER)) {
            /* The first onset belongs to the active harmony, with the smallest
             * movement from the previous anchor. */
            int32_t previous = g->phrase[anchor_slot] == KB_SILENT ? (int32_t)bank.note[0] : g->phrase[anchor_slot];
            uint32_t best = 0xFFFFFFFFu;
            for (uint32_t j = 0; j < candidates; j++) {
                int32_t d = generator_mapped_degree(t, bank.note[j]);
                uint32_t eligible = role == GEN_WALK ? generator_mod(d - g->root, count) == 0 : 0;
                if (role != GEN_WALK) for (uint32_t c = 0; c < g->chord_n; c++)
                    eligible |= generator_mod(d - g->chord[c], count) == 0;
                uint32_t distance = (uint32_t)((int32_t)bank.note[j] > previous ? (int32_t)bank.note[j] - previous : previous - (int32_t)bank.note[j]);
                if (eligible && distance < best) { best = distance; mapped = bank.note[j]; }
            }
        }
        g->phrase[i] = (uint8_t)mapped;
        if (hit && mapped != KB_SILENT) {
            int32_t at = generator_mapped_degree(t, mapped) - low;
            if (prev >= 0) leap = at - prev;
            prev = at; k++;
        }
    }
    g->walk = (int16_t)last_walk;
    if (role == GEN_LOOP && style == AS_MOTIF) generator_motif(t, g, boundary, low, span, anchor_slot);
}

/* A part that starts while the transport runs waits for the next bar line, so it joins
 * the band (and the patterns) on the one instead of at an arbitrary sample. */
static uint32_t generator_preroll(void)
{
    uint32_t beat = seq_beat_samples(), done = beat_n * beat + beat_pos;
    return song.playing && done && done < 4u * beat ? 4u * beat - done : 0u;
}
static void generator_jam_play(track_t *t);
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
    g->frac = 0;
    if (!g->memory_ready) g->chord_n = 0;
    g->dirty = 1;
    for (uint32_t i = 0; i < 16u; i++) g->heard[i] = -32768;
    generator_voicing(t);
    /* A JAM leader starts its section; a named progression starts at its first chord.
     * PATH/NEAR/MARKOV journeys resume where they were. */
    if (!g->p[GN_SOURCE] && g->p[GN_JAM]) generator_jam_play(t);
    else if (!g->p[GN_SOURCE] && g->p[GN_HARMONY] && (!g->path_valid || generator_template(t, (uint32_t)g->p[GN_HARMONY])))
        generator_harmony_restart(t);
    g->left = generator_preroll();
    g->harmony_left = seq_beat_samples() * 4u * (1u << g->p[GN_CHANGE]) + g->left;
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
    g->mask = 0;
    if (!mode) g->jam_lead = 0;
    t->seq_idx = (uint16_t)(t->p[P_SLEN] - 1);
    t->seq_pos = 0x7FFFFFFFu;
}

/* Pressed notes form one gesture. The first press sets the anchor; additional
 * held keys add preferred scale degrees. Releasing the gesture latches the palette.
 * The next first press replaces it. Input and generated ownership never overlap.
 * On a LOCAL part the first press is also the new home: its harmony starts again. */
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
    if (!held && !g->p[GN_SOURCE] && (g->p[GN_JAM] || g->p[GN_HARMONY])) {
        generator_harmony_restart(t);
        g->jam_count = 0;
    }
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
    if (g->p[GN_MODE] != GEN_ANSWER) saved->p[generator_style_id(g)] = g->style;
    if (g->running && g->source_used) saved->p[GN_SOURCE] = g->route_used;
    generator_keep[tr].valid = 1; generator_keep[tr].group = (uint8_t)group;
}
/* The player's verdict on what plays now: More/Less like this, Keep idea, New idea, a
 * capture. Teaches the leader's taste (progression and every MOTIF part's melody);
 * Less like this also moves a JAM on at the next change and refreshes the motifs.
 * UI calls with audio interrupts guarded. */
static void generator_feedback(uint32_t tr, int32_t signal)
{
    uint32_t lead = generator_jam_owner(tr), error;
    generator_t *L = &generator[lead];
    gen_evo_t *e = &gen_evo[lead];
    track_t *lt = &trk[lead];
    if (signal > 0) e->likes++; else e->dislikes++;
    if (generator_functional(lt) && L->path_valid && L->path_len >= 2u) {
        gen_ctx_t c; uint32_t home = generator_home(L, 7u);
        gen_context(lt, home, &c);
        gen_genome_t x; gen_from_path(L, home, &x);
        int16_t f[GEN_PFEAT]; gen_fitness(&c, e, &x, 0, 0, 0, 0, f);
        gen_learn(e->pref, e->mean, f, GEN_PFEAT, signal);
    }
    for (uint32_t i = 0; i < trk_n(); i++) {
        generator_t *g = &generator[i];
        if (!generator_on(&trk[i]) || generator_source(i, &error) != (int)lead || g->p[GN_MODE] != GEN_LOOP ||
            g->style != AS_MOTIF || !g->motif_ok || !g->length) continue;
        uint8_t slot[16]; uint32_t n = 0, count = generator_count(&trk[i]);
        for (uint32_t s = 0; s < g->length && n < g->motif_n; s++) if ((g->rhythm >> s) & 1u) slot[n++] = (uint8_t)s;
        if (n != g->motif_n) continue;
        int16_t f[GEN_MFEAT];
        gen_motif_fit(&trk[i], g, e, g->motif_rel, n, slot, g->anchor - (int32_t)(count * (g->range / 2u)), (int32_t)(count * g->range), f);
        gen_learn(e->mpref, e->mmean, f, GEN_MFEAT, signal);
        if (signal < 0) g->motif_ok = 0;
    }
    if (signal < 0 && L->p[GN_JAM]) L->jam_skip = 1;
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
    if (action == GA_LIKE || action == GA_DISLIKE) { generator_feedback(tr, action == GA_LIKE ? 1 : -1); return 1; }
    if (action == GA_KEEP) {
        generator_feedback(tr, 1);
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
    if (action == GA_NEW) generator_feedback(tr, -1);
    if (action == GA_NEW && generator[generator_jam_owner(tr)].p[GN_JAM]) {
        generator[generator_jam_owner(tr)].jam_new = 1;   /* a new song from the next change */
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
    cap_on(t, note, velocity);
}
/* DYNAMICS: FLAT, PHRASE accents, a SWELL across the phrase, or HUMAN (memory-chosen
 * small variations, a lift on the beat). Scaled by the part's level (JAM energy). */
static uint32_t generator_velocity(const generator_t *g, uint32_t at, uint32_t k, uint32_t onsets)
{
    int32_t level = g->level ? g->level : 88, v = level;
    if (g->dynamics == GD_PHRASE)
        v = (g->p[GN_MODE] == GEN_CHORD ? (!(at & 3u) ? 96 : 78) : !k ? 100 : !(at & 3u) ? 88 : 72 + (int32_t)(g->value[at] & 15u)) * level / 88;
    else if (g->dynamics == GD_SWELL)
        v = level * (70 + (int32_t)(onsets > 1u ? 50u * k / (onsets - 1u) : 25u)) / 100;
    else if (g->dynamics == GD_HUMAN)
        v = level + (int32_t)((g->value[at] >> 4) & 15u) - 8 + (!(at & 3u) ? 6 : 0);
    return (uint32_t)(v < 1 ? 1 : v > 127 ? 127 : v);
}

static void generator_event(track_t *t, uint32_t period)
{
    generator_t *g = &generator[trk_index(t)];
    for (uint32_t i = 0; i < g->n; i++) generator_off(t, g->note[i]);
    memset(g->note, 0, sizeof g->note); memset(g->gate, 0, sizeof g->gate);
    g->n = g->pending_n = 0;
    uint32_t at = g->cursor;
    g->heard[at] = -32768;
    uint32_t gate = period * g->gate_value / 127u;
    /* Events run before a control block is rendered. A sub-block on/off pair
     * would otherwise vanish before any samples can sound. Round tiny gates
     * up to one audible control block, in the active clock's units. */
    uint32_t min_gate = midi_sync_active ?
        (uint32_t)(((uint64_t)2u * CTL * MIDI_BEAT_UNITS + beat_samples() - 1u) / beat_samples()) : 2u * CTL;
    if (gate < min_gate) gate = min_gate;
    if (gate > period) gate = period;
    uint32_t phase = g->cycles + g->cycle_origin;
    uint32_t allowed = !g->condition || (g->condition == GW_ODD && !(phase & 1u)) ||
        (g->condition == GW_EVEN && (phase & 1u)) || (g->condition == GW_4TH && (phase & 3u) == 3u);
    if (allowed && !g->answer_wait && ((g->rhythm >> at) & 1u)) {
        uint32_t k = generator_bits(g->rhythm & ((1u << at) - 1u)), onsets = generator_bits(g->rhythm);
        uint32_t velocity = generator_velocity(g, at, k, onsets);
        if (g->p[GN_MODE] == GEN_CHORD && g->style == CS_STRIDE && !(k & 1u)) {
            /* STRIDE: the root below the voicing on even onsets, the chord between. */
            uint32_t count = generator_count(t);
            int32_t d = g->chord[0] - (int32_t)generator_mod(g->chord[0] - g->root, count);
            if (d == g->chord[0]) d -= (int32_t)count;
            uint32_t note = generator_note(t, d);
            generator_sound(t, note == KB_SILENT ? generator_note(t, d + (int32_t)count) : note, gate, velocity);
        } else if (g->p[GN_MODE] == GEN_CHORD) {
            uint32_t voices = trk_vmode(t) == V_POLY ? g->chord_n : 1u;
            uint32_t strum = midi_sync_active ? (uint32_t)((uint64_t)g->strum * MIDI_BEAT_UNITS * FS / (1000u * beat_samples())) : (uint32_t)g->strum * FS / 1000u;
            if (strum > gate / (voices * 2u)) strum = gate / (voices * 2u);
            uint32_t max_strum = voices > 1u && gate > min_gate ? (gate - min_gate) / (voices - 1u) : 0u;
            if (strum > max_strum) strum = max_strum;
            for (uint32_t i = 0; i < voices; i++) {
                /* A strummed chord alternates down- and up-strokes, like a guitarist. */
                uint32_t v = strum && (k & 1u) && voices > 1u ? voices - 1u - i : i;
                uint32_t note = generator_note(t, g->chord[v]);
                uint32_t delay = i * strum;
                if (!delay) generator_sound(t, note, gate, velocity);
                else if (note != KB_SILENT) {
                    uint32_t j = g->pending_n++;
                    g->pending_note[j] = (uint8_t)note; g->pending_vel[j] = (uint8_t)(velocity > 3u * i + 1u ? velocity - 3u * i : velocity);
                    g->pending_gate[j] = gate - delay; g->delay[j] = delay;
                }
            }
        } else generator_sound(t, g->phrase[at], gate, velocity);
        uint32_t heard = g->p[GN_MODE] == GEN_CHORD ? generator_note(t, g->root) : g->phrase[at];
        if (heard != KB_SILENT) g->heard[at] = (int16_t)(generator_mapped_degree(t, heard) - g->root);
    }
    g->step++;
    g->cursor = (uint8_t)((at + 1u) % g->length);
}

#include "generator_jam.c"

/* One chronological scheduler for all parts. Harmony and links resolve before
 * any note at that timestamp, independent of track order or block size. */
static void generator_run(uint32_t mask, uint32_t n)
{
    /* Sources begin first: a JAM leader's section settings reach its parts before they plan. */
    for (uint32_t pass = 0; pass < 2u; pass++) for (uint32_t i = 0; i < trk_n(); i++) if (mask & (1u << i)) {
        track_t *t = &trk[i]; generator_t *g = &generator[i];
        if (!generator_on(t) || (g->p[GN_SOURCE] != 0) != (pass != 0)) continue;
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
        cap_ahead = n;                                  /* (the notes of this timestamp, for capture) */
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
                g->harmony_due = g->p[GN_HARMONY] != 0 || g->p[GN_JAM] != 0;
                g->harmony_left = seq_beat_samples() * 4u * (1u << g->p[GN_CHANGE]);
            }
            if (!g->left && !g->cursor && g->step) {
                boundary |= 1u << i; g->cycles++;
                uint32_t history_n = 0;
                for (uint32_t j = 0; j < g->length; j++) if (g->heard[j] != -32768) g->history[history_n++] = g->heard[j];
                if (history_n) g->history_n = (uint8_t)history_n;
                if (!g->p[GN_SOURCE] && g->harmony_due) {
                    if (g->p[GN_JAM]) generator_jam_change(&trk[i]);
                    else if (g->p[GN_HARMONY]) generator_next_chord(&trk[i]);
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
                if (g->root != root || g->mask != generator[source].mask || g->source_used != (uint32_t)source + 1u) {
                    g->root = root; g->mask = generator[source].mask; g->dirty = 1;
                    generator_voicing_for(&trk[i], g->color);
                }
            } else if (source == (int)i && !g->p[GN_HARMONY] && !g->p[GN_JAM]) g->mask = 0;
            g->source_used = (uint8_t)(source + 1); g->route_used = (uint8_t)g->p[GN_SOURCE]; g->harmony_due = g->p[GN_SOURCE] ? 0 : g->harmony_due;
            if ((boundary & (1u << i)) && g->color != g->p[GN_COLOR]) generator_voicing(&trk[i]);
            if (boundary & (1u << i)) generator_plan(&trk[i], 1);
            else if (g->dirty || (g->answer_wait && g->p[GN_SOURCE] && generator[g->p[GN_SOURCE]-1].history_n)) generator_plan(&trk[i], 0);
        }
        for (uint32_t i = 0; i < trk_n(); i++) if ((active & (1u << i)) && !generator[i].left) {
            uint32_t slot = generator_slot(&generator[i]);
            generator_event(&trk[i], slot); generator[i].left = slot;
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
        if (!n || !active) { cap_ahead = 0; break; }
        uint32_t chunk = n;
        for (uint32_t i = 0; i < trk_n(); i++) if (active & (1u << i)) {
            generator_t *g = &generator[i];
            if (chunk > g->left) chunk = g->left;
            if (!g->p[GN_SOURCE] && chunk > g->harmony_left) chunk = g->harmony_left;
            for (uint32_t j = 0; j < g->n; j++) if (chunk > g->gate[j]) chunk = g->gate[j];
            for (uint32_t j = 0; j < g->pending_n; j++) if (chunk > g->delay[j]) chunk = g->delay[j];
        }
        cap_ahead = n - chunk;
        for (uint32_t i = 0; i < trk_n(); i++) if (active & (1u << i)) {
            generator_t *g = &generator[i];
            for (uint32_t j = 0; j < g->n;) {
                if (g->gate[j] <= chunk) {
                    generator_off(&trk[i], g->note[j]); g->n--;
                    g->note[j] = g->note[g->n]; g->gate[j] = g->gate[g->n];
                    g->note[g->n] = 0; g->gate[g->n] = 0;    /* (canonical state: batch-size independent) */
                } else { g->gate[j] -= chunk; j++; }
            }
            g->left -= chunk;
            for (uint32_t j = 0; j < g->pending_n; j++) g->delay[j] -= chunk;
            if (!g->p[GN_SOURCE]) g->harmony_left -= chunk;
        }
        n -= chunk;
    }
    cap_ahead = 0;
}
static void generator_tick(track_t *t, uint32_t n) { generator_run(1u << trk_index(t), n); }
static void generator_advance(uint32_t n) { generator_run((1u << trk_n()) - 1u, n); }
