/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Original bounded harmonic planner. Every moving HARMONY plans its whole cycle into
 * path[] up front, so a bass line can see the next chord: PATH supplies an
 * eight-change dramatic arc; NEAR a related chain; MARKOV draws from Walter Piston's
 * table of usual root progressions (Harmony, 1941) with a cadence back home; the
 * named progressions are scale-degree offsets from the played home, in a major and
 * a minor reading chosen by the home chord's third. Candidate weights measure shared
 * tones, actual pitch distance and repetition. Non-diatonic and non-heptatonic scales
 * use the same graph without Western functional families. */
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
    (void)count;
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
/* The triad on a degree: its third and fifth above the root, in actual pitch. */
static int generator_minor(const track_t *t, int32_t root)
{
    uint8_t s[4]; generator_chord_shape(t, root, GV_TRIAD, s);
    return generator_tone(t, root + s[1]) - generator_tone(t, root) < 35000;
}
static int generator_dim(const track_t *t, int32_t root)
{
    uint8_t s[4]; generator_chord_shape(t, root, GV_TRIAD, s);
    return generator_tone(t, root + s[2]) - generator_tone(t, root) < 65000;
}
/* The harmonic stream, seeded from SEED if a journey is planned before the melodic
 * identity exists (a JAM's first section, PLAY's first plan). */
static uint32_t generator_harmonic(generator_t *g)
{
    if (!g->harmonic_random) g->harmonic_random = (0x6D2B79F5u ^ (uint32_t)g->p[GN_SEED] * 0x9E3779B9u) ^ 0xA511E9B3u;
    if (!g->harmonic_random) g->harmonic_random = 1;
    return generator_word(&g->harmonic_random);
}
static uint32_t generator_choose_chord(track_t *t, uint32_t from, uint32_t previous, uint32_t position, uint32_t planned, const int32_t *pitch, const uint8_t (*triad)[3], int32_t period)
{
    generator_t *g = &generator[trk_index(t)];
    uint32_t count = generator_count(t), home = generator_mod(g->anchor, count), sum = 0;
    /* Prolongation -> preparation -> tension -> deceptive departure -> return.
     * Choices within each family vary by seed and the preceding chord. */
    static const uint8_t family[8] = {1u, (1u<<2)|(1u<<5), (1u<<1)|(1u<<3),
        (1u<<4), (1u<<2)|(1u<<3)|(1u<<5), (1u<<0)|(1u<<2)|(1u<<5),
        (1u<<1)|(1u<<3), (1u<<4)};
    int functional = planned && generator_diatonic(t);
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
        if (!functional && planned) {
            if (position == 7u) weight *= 1u + generator_chord_weight(pitch, triad, count, period, to, home) / 10u;
            else if (position >= 2u && position <= 5u && to == home) weight = 1u;
        }
        weights[to] = (uint16_t)weight; sum += weight;
    }
    uint32_t draw = generator_harmonic(g) % sum;
    for (uint32_t to = 0; to < count; to++) {
        if (draw < weights[to]) return to;
        draw -= weights[to];
    }
    return home;
}
/* Piston's usual (6), sometimes (3) and less often (1) root progressions, from
 * the row's degree to the column's, relative to home. The minor table reads the
 * same habits in the natural minor, where VII leads home and v is modal. */
static const uint8_t GEN_MARKOV[2][7][7] = {
    {{0,1,1,6,6,3,0}, {1,0,1,3,6,3,0}, {1,1,0,3,1,6,0}, {3,3,1,0,6,1,0},
     {6,1,1,3,0,3,0}, {1,6,3,3,6,0,0}, {6,0,3,0,0,1,0}},
    {{0,1,3,5,3,5,4}, {1,0,0,0,6,0,2}, {1,0,0,4,0,5,3}, {4,1,0,0,4,2,4},
     {6,0,0,2,0,4,0}, {2,1,3,4,0,0,5}, {5,0,5,1,0,2,0}}};
static uint32_t generator_markov(generator_t *g, uint32_t from, uint32_t position, uint32_t minor, uint32_t visited)
{
    uint32_t weight[7], sum = 0;
    for (uint32_t pass = 0; pass < 2u && !sum; pass++) for (uint32_t to = 0; to < 7u; to++) {
        uint32_t w = GEN_MARKOV[minor][from][to] * 4u;
        if (!pass) {
            /* The last chord before home is a dominant (or plagal) preparation. */
            if (position == 7u) w *= to == 4u || to == 6u ? 4u : to == 3u ? 2u : 0u;
            else {
                if (!to) w /= 2u;                       /* travel before returning */
                if ((visited >> to) & 1u) w /= 2u;      /* and prefer unheard chords */
            }
        }
        weight[to] = w; sum += w;
    }
    uint32_t draw = generator_harmonic(g) % sum;
    for (uint32_t to = 0; to < 7u; to++) {
        if (draw < weight[to]) return to;
        draw -= weight[to];
    }
    return 4u;
}
/* Named progressions: length, major-home offsets, minor-home offsets. */
static const struct { uint8_t len; int8_t major[12], minor[12]; } GEN_TEMPLATE[GH_COUNT - GH_POP] = {
    {4, {0,4,5,3}, {0,5,2,6}},                          /* POP     I V vi IV / i VI III VII */
    {4, {5,3,0,4}, {3,5,0,6}},                          /* AXIS    vi IV I V / iv VI i VII */
    {4, {0,5,3,4}, {0,5,3,4}},                          /* DOOWOP  I vi IV V */
    {8, {0,4,5,2,3,0,3,4}, {0,4,5,2,3,0,3,4}},          /* CANON   Pachelbel */
    {4, {3,4,2,5}, {5,6,4,0}},                          /* ROYAL   IV V iii vi (the royal road) */
    {7, {0,3,6,2,5,1,4}, {0,3,6,2,5,1,4}},              /* CIRCLE  diatonic descending fifths */
    {4, {1,4,0,5}, {1,4,0,5}},                          /* JAZZ    ii V I vi turnaround */
    {4, {3,2,1,0}, {0,6,5,4}},                          /* FALL    IV iii ii I / Andalusian i VII VI V */
    {4, {0,1,3,4}, {0,3,5,6}},                          /* RISE    I ii IV V / i iv VI VII */
    {2, {0,3}, {0,3}},                                  /* VAMP    I IV / i iv (dorian: i IV) */
    {12, {0,0,0,0,3,3,0,0,4,3,0,4}, {0,0,0,0,3,3,0,0,4,3,0,4}}};   /* BLUES twelve bars */
/* A diminished triad where a progression expects a stable chord takes the chord a
 * third below, whose seventh it is (vii -> V, ii in minor -> VII), else the chord a
 * third above (ii in harmonic minor -> iv). CIRCLE and JAZZ keep their
 * half-diminished colour on purpose. */
static uint32_t generator_stable(const track_t *t, uint32_t to)
{
    if (!generator_dim(t, (int32_t)to)) return to;
    uint32_t below = generator_mod((int32_t)to - 2, 7u), above = (to + 2u) % 7u;
    return !generator_dim(t, (int32_t)below) ? below : !generator_dim(t, (int32_t)above) ? above : to;
}
static uint32_t generator_template_chord(const track_t *t, uint32_t type, uint32_t home, uint32_t minor, uint32_t i)
{
    int32_t offset = minor ? GEN_TEMPLATE[type - GH_POP].minor[i] : GEN_TEMPLATE[type - GH_POP].major[i];
    uint32_t to = generator_mod((int32_t)home + offset, 7u);
    return type != GH_CIRCLE && type != GH_JAZZ ? generator_stable(t, to) : to;
}
/* Named progressions and MARKOV speak Western functional harmony: twelve-tone
 * seven-note scales only. Other scales and tunings take PATH's pitch graph. */
static int generator_functional(const track_t *t) { return !micro_active(t) && generator_count(t) == 7u; }
static int generator_template(const track_t *t, uint32_t type) { return type >= GH_POP && type < GH_COUNT && generator_functional(t); }
static uint32_t generator_home_minor(const track_t *t, uint32_t home) { return generator_minor(t, (int32_t)home) || generator_dim(t, (int32_t)home); }
/* The first chord a HARMONY plays from home (JAM's next section, a bass approach). */
static uint32_t generator_first_chord(const track_t *t, uint32_t type, uint32_t home)
{
    return generator_template(t, type) ? generator_template_chord(t, type, home, generator_home_minor(t, home), 0) : home;
}
static int32_t generator_scale_graph(const track_t *t, int32_t *pitch, uint8_t (*triad)[3])
{
    uint32_t count = generator_count(t);
    int32_t period = 120000;
    if (micro_active(t)) {
        const micro_scale_t *scale = micro_scale(t);
        memcpy(pitch, scale->pitch, count * sizeof pitch[0]); period = scale->pitch[count];
    } else {
        uint32_t mask = scale_mask(t), at = 0;
        for (uint32_t i = 0; i < 12u; i++) if ((mask >> i) & 1u) pitch[at++] = (int32_t)i * 10000;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint8_t shape[4]; generator_chord_shape(t, (int32_t)i, GV_TRIAD, shape);
        for (uint32_t j = 0; j < 3u; j++) triad[i][j] = (uint8_t)((i + shape[j]) % count);
    }
    return period;
}
/* Plan one complete harmonic cycle from `from`: path[0] plays first, then each change. */
static void generator_plan_path(track_t *t, uint32_t from, const int32_t *pitch, const uint8_t (*triad)[3], int32_t period)
{
    generator_t *g = &generator[trk_index(t)];
    uint32_t count = generator_count(t), home = generator_mod(g->anchor, count), type = (uint32_t)g->p[GN_HARMONY];
    g->harmony_used = (uint8_t)type;
    if (type == GH_HOLD) { g->path[0] = (uint8_t)from; g->path_len = 1; }
    else if (generator_template(t, type)) {
        uint32_t minor = generator_home_minor(t, home);
        g->path_len = GEN_TEMPLATE[type - GH_POP].len;
        for (uint32_t i = 0; i < g->path_len; i++) g->path[i] = (uint8_t)generator_template_chord(t, type, home, minor, i);
    } else if (type == GH_MARKOV && generator_functional(t)) {
        uint32_t minor = generator_home_minor(t, home), visited = 1;
        g->path[0] = (uint8_t)home; g->path_len = 8;
        for (uint32_t i = 1; i < 8u; i++) {
            uint32_t to = generator_markov(g, (g->path[i-1u] + 7u - home) % 7u, i, minor, visited);
            uint32_t chord = generator_stable(t, (home + to) % 7u);
            g->path[i] = (uint8_t)chord; visited |= 1u << ((chord + 7u - home) % 7u);
        }
    } else {
        uint32_t planned = type != GH_NEAR;
        g->path[0] = (uint8_t)(planned ? home : from); g->path_len = 8;
        for (uint32_t i = 1; i < 8u; i++) g->path[i] = (uint8_t)generator_choose_chord(t,
            g->path[i-1u], g->path[i>1u ? i-2u : 0u], planned ? i : 0u, planned, pitch, triad, period);
    }
    g->path_valid = 1;
}
static void generator_set_chord(track_t *t, uint32_t to)
{
    generator_t *g = &generator[trk_index(t)];
    uint32_t from = generator_mod(g->root, generator_count(t));
    g->root += (int16_t)((int32_t)to - (int32_t)from);
    generator_voicing(t);
    g->dirty = 1;
}
static void generator_next_chord(track_t *t)
{
    generator_t *g = &generator[trk_index(t)];
    uint32_t count = generator_count(t), from = generator_mod(g->root, count);
    int32_t pitch[128]; uint8_t triad[128][3];
    int32_t period = generator_scale_graph(t, pitch, triad);
    if (!g->path_valid) generator_plan_path(t, from, pitch, triad, period);
    else if (g->harmony_used != g->p[GN_HARMONY]) {   /* a new HARMONY starts from its own first chord */
        generator_plan_path(t, from, pitch, triad, period);
        g->path_pos = (uint8_t)(g->path_len - 1u);
    }
    g->path_pos = (uint8_t)((g->path_pos + 1u) % g->path_len);
    uint32_t to = g->path[g->path_pos];
    /* Complete the cadence, then plan a fresh, related cycle at once (the bass sees it). */
    if (!g->path_pos && !generator_template(t, g->harmony_used)) generator_plan_path(t, to, pitch, triad, period);
    generator_set_chord(t, to);
    g->changes++;
}
/* Begin a LOCAL harmony from its first chord (PLAY, a new home key, a JAM section). */
static void generator_harmony_restart(track_t *t)
{
    generator_t *g = &generator[trk_index(t)];
    if (g->p[GN_SOURCE]) { g->path_valid = g->path_pos = 0; return; }
    int32_t pitch[128]; uint8_t triad[128][3];
    int32_t period = generator_scale_graph(t, pitch, triad);
    generator_plan_path(t, generator_mod(g->anchor, generator_count(t)), pitch, triad, period);   /* (HOLD, NEAR: home) */
    g->path_pos = 0;
    generator_set_chord(t, g->path[0]);
}
/* The chord the source plays after its coming change (degree class), and whether that
 * change falls within `within` samples: a walking bass then approaches it. */
static uint32_t generator_upcoming(const generator_t *s, uint32_t count, uint32_t within, uint32_t *soon)
{
    uint32_t root = generator_mod(s->root, count);
    *soon = 0;
    if (!s->path_valid || !s->path_len || (!s->p[GN_HARMONY] && !s->p[GN_JAM])) return root;
    *soon = s->harmony_due || s->harmony_left <= within + CTL;
    if (s->p[GN_JAM] && s->jam_len && s->jam_count + 1u >= s->jam_len) return s->jam_first;
    return s->path[(s->path_pos + 1u) % s->path_len];
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
