/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Original bounded harmonic planner. PATH supplies an eight-change dramatic arc;
 * candidate weights measure shared tones, actual pitch distance and repetition.
 * Non-diatonic scales use the same graph without Western functional families. */
static uint32_t generator_chord_weight(const int32_t *pitch, const uint8_t (*triad)[3], uint32_t count, int32_t period, uint32_t from, uint32_t to)
{
    uint32_t common = 0, movement = 0;
    for (uint32_t i = 0; i < 3u; i++) {
        uint32_t a = triad[from][i], nearest = (uint32_t)period;
        for (uint32_t j = 0; j < 3u; j++) {
            uint32_t b = triad[to][j];
            common += a == b;
            uint32_t distance = (uint32_t)(pitch[a] > pitch[b] ? pitch[a] - pitch[b] : pitch[b] - pitch[a]);
            if ((uint32_t)period - distance < distance) distance = (uint32_t)period - distance;
            if (distance < nearest) nearest = distance;
        }
        movement += nearest / 1000u;
    }
    return 8u + common * 9u + 2400u / (30u + movement);
}
static int generator_diatonic(const track_t *t)
{
    if (micro_active(t) || generator_count(t) != 7u) return 0;
    uint32_t mask = scale_mask(t);
    for (uint32_t i = 0; i < 12u; i++)
        if (((0xAB5u << i | 0xAB5u >> (12u-i)) & 0xFFFu) == mask) return 1;
    return 0;
}
static uint32_t generator_choose_chord(track_t *t, uint32_t from, uint32_t previous, uint32_t position, const int32_t *pitch, const uint8_t (*triad)[3], int32_t period)
{
    generator_t *g = &generator[trk_index(t)];
    uint32_t count = generator_count(t), home = generator_mod(g->anchor, count), sum = 0;
    /* Prolongation -> preparation -> tension -> deceptive departure -> return.
     * Choices within each family vary by seed and the preceding chord. */
    static const uint8_t family[8] = {1u, (1u<<2)|(1u<<5), (1u<<1)|(1u<<3),
        (1u<<4), (1u<<2)|(1u<<3)|(1u<<5), (1u<<0)|(1u<<2)|(1u<<5),
        (1u<<1)|(1u<<3), (1u<<4)};
    int functional = g->p[GN_HARMONY] == 2 && generator_diatonic(t);
    uint32_t allowed = functional ? family[position] : 0u, visited = 0;
    if (functional && position != 3u && position != 7u) {
        for (uint32_t i = 0; i < position; i++) visited |= 1u << ((g->path[i] + count - home) % count);
        if (allowed & ~visited) allowed &= ~visited;
    }
    uint16_t weights[128];
    for (uint32_t to = 0; to < count; to++) {
        uint32_t relative = (to + count - home) % count;
        weights[to] = 0;
        if (functional && !(allowed & (1u << relative))) continue;
        uint32_t weight = generator_chord_weight(pitch, triad, count, period, from, to);
        if (to == from) weight = 1u;
        if (to == previous) weight = weight / 3u + 1u;
        if (!functional && g->p[GN_HARMONY] == 2) {
            if (position == 7u) weight *= 1u + generator_chord_weight(pitch, triad, count, period, to, home) / 10u;
            else if (position >= 2u && position <= 5u && to == home) weight = 1u;
        }
        weights[to] = (uint16_t)weight; sum += weight;
    }
    uint32_t draw = generator_word(&g->harmonic_random) % sum;
    for (uint32_t to = 0; to < count; to++) {
        if (draw < weights[to]) return to;
        draw -= weights[to];
    }
    return home;
}
static void generator_next_chord(track_t *t)
{
    generator_t *g = &generator[trk_index(t)];
    uint32_t count = generator_count(t), from = generator_mod(g->root, count), to;
    int32_t pitch[128], period = 120000;
    if (micro_active(t)) {
        const micro_scale_t *scale = micro_scale(t);
        memcpy(pitch, scale->pitch, count * sizeof pitch[0]); period = scale->pitch[count];
    } else {
        uint32_t mask = scale_mask(t), at = 0;
        for (uint32_t i = 0; i < 12u; i++) if ((mask >> i) & 1u) pitch[at++] = (int32_t)i * 10000;
    }
    uint8_t triad[128][3];
    for (uint32_t i = 0; i < count; i++) {
        uint8_t shape[4]; generator_chord_shape(t, (int32_t)i, 0, shape);
        for (uint32_t j = 0; j < 3u; j++) triad[i][j] = (uint8_t)((i + shape[j]) % count);
    }
    if (g->p[GN_HARMONY] == 2) {
        if (!g->path_valid) {
            g->path[0] = (uint8_t)generator_mod(g->anchor, count);
            for (uint32_t i = 1; i < 8u; i++) g->path[i] = (uint8_t)generator_choose_chord(t,
                g->path[i-1u], g->path[i>1u ? i-2u : 0u], i, pitch, triad, period);
            g->path_valid = 1;
        }
        g->path_pos = (uint8_t)((g->path_pos + 1u) & 7u);
        to = g->path[g->path_pos];
        /* Complete the cadence before planning a fresh, related journey. */
        if (!g->path_pos) g->path_valid = 0;
    } else to = generator_choose_chord(t, from, from, 0, pitch, triad, period);
    g->root += (int16_t)((int32_t)to - (int32_t)from);
    g->changes++;
    generator_voicing(t);
    g->dirty = 1;
}

/* Follow explicit sources through at most four tracks. A cycle, a disabled
 * source, or an incompatible tuning silences the dependent part until repaired. */
static int generator_source(uint32_t tr, uint32_t *error)
{
    uint32_t seen = 0, at = tr;
    *error = 0;
    for (uint32_t depth = 0; depth < NTRK; depth++) {
        if (at >= trk_n() || (seen & (1u << at)) || !generator_on(&trk[at]) || chord_kit(&trk[at])) { *error = 1; return -1; }
        seen |= 1u << at;
        if (trk[at].p[P_SCALE] != trk[tr].p[P_SCALE] || trk[at].p[P_ROOT] != trk[tr].p[P_ROOT] ||
            trk[at].p[P_TRANS] != trk[tr].p[P_TRANS] || micro_active(&trk[at]) != micro_active(&trk[tr])) { *error = 2; return -1; }
        uint32_t source = (uint32_t)generator[at].p[GN_SOURCE];
        if (!source) {
            if (generator[at].p[GN_MODE] == GEN_ANSWER) { *error = 1; return -1; }
            return (int)at;
        }
        at = source - 1u;
    }
    *error = 1; return -1;
}
