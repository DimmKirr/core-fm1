/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* EVOLVE: original Melodee work. Harmony and melody that develop and learn instead of
 * being drawn afresh each time.
 * - Chord scales: a chord may borrow a parallel mode's scale (modal mixture: iv, bVI,
 *   bVII, II, bII...) or be a secondary dominant (the key with its target's leading tone
 *   and seventh). That scale replaces the key's for every linked part while the chord
 *   sounds (generator_mask), so melodies, arps and bass stay consonant with it. Only
 *   scales that keep one note per letter of the key are used, so degrees keep their
 *   meaning for voicings, answers and the bass.
 * - Progressions (HARMONY EVOLVE): a population of eight genomes per leader breeds at
 *   every section or cycle (tournament selection, one-point crossover, function-aware
 *   mutations, the weakest replaced). Fitness scores root motion, Piston's functional
 *   habits, common tones, the cadence, the colour budget SPICE sets, a section's tension
 *   target, novelty against what was already played, closeness to a parent when a
 *   section develops, and the player's learned taste.
 * - Melodies (ARP MOTIF): a motif develops phrase by phrase through classical operators
 *   (sequence, inversion, retrograde, rotation, fragmentation, ornament, interval
 *   expansion, point mutation and crossover with fresh material); fitness keeps a
 *   singable line, chord tones on strong beats, a climax, a resolution, about half of
 *   the last statement and something not heard lately.
 * - Taste: More/Less like this, Keep idea, New idea and holding REC move weights on the
 *   features of what is playing, measured against what the band has played on average,
 *   so the population drifts toward what the player likes. Session-only, never rolled
 *   back by Return to kept. */
static uint32_t gen_bits(uint32_t m) { uint32_t n = 0; while (m) { n += m & 1u; m >>= 1; } return n; }
static uint32_t gen_rotl(uint32_t m, uint32_t s) { s %= 12u; return s ? ((m << s) | (m >> (12u - s))) & 0xFFFu : m; }
/* Semitones above the scale root of a degree of a seven-note mask (any octave). */
static int32_t gen_pc(uint32_t mask, int32_t deg)
{
    int32_t oct = deg >= 0 ? deg / 7 : -((6 - deg) / 7), d = deg - oct * 7;
    for (int32_t i = 0; i < 12; i++) if ((mask >> i) & 1u) { if (!d) return oct * 12 + i; d--; }
    return oct * 12;
}
typedef struct { uint32_t key, home, hpc, minor; uint16_t mask[GM_SEC]; } gen_ctx_t;
static void gen_context(const track_t *t, uint32_t home, gen_ctx_t *c)
{
    static const uint16_t MODE[GM_SEC] = {0, 0, 0x6B5, 0x6AD, 0xAD5, 0x9AD, 0x5AB};
    c->key = scale_mask(t); c->home = home % 7u;
    c->hpc = (uint32_t)(gen_pc(c->key, (int32_t)c->home) % 12);
    c->minor = gen_pc(c->key, (int32_t)c->home + 2) - gen_pc(c->key, (int32_t)c->home) == 3;
    c->mask[GM_KEY] = (uint16_t)c->key;
    for (uint32_t m = 1; m < GM_SEC; m++) {
        uint32_t mask = gen_rotl(m == GM_MIX ? (c->minor ? 0xAB5u : 0x5ADu) : MODE[m], c->hpc), ok = mask != c->key;
        for (int32_t k = 0; k < 7 && ok; k++) { int32_t d = gen_pc(mask, k) - gen_pc(c->key, k); ok = d >= -1 && d <= 1; }
        c->mask[m] = (uint16_t)(ok ? mask : 0u);
    }
}
typedef struct { uint16_t pcs, mask; uint8_t root, quality, ok, func, mode, rel; } gen_chord_t;
/* A gene as a chord: its scale, root, triad and function (0 tonic, 1 subdominant, 2 dominant). */
static void gen_chord(const gen_ctx_t *c, const gen_genome_t *x, uint32_t i, gen_chord_t *o, uint32_t depth)
{
    uint32_t len = x->len ? x->len : 1u, m = x->mode[i] % GM_COUNT, rel = x->deg[i] % 7u, mask = c->key;
    o->ok = 1;
    if (m == GM_SEC && depth < 3u && len > 1u) {
        gen_chord_t tgt; gen_chord(c, x, (i + 1u) % len, &tgt, depth + 1u);
        uint32_t T = tgt.root, lead = (T + 11u) % 12u, below = (T + 10u) % 12u, sev = (T + 5u) % 12u, above = (T + 6u) % 12u;
        if (!((mask >> lead) & 1u) && ((mask >> below) & 1u)) mask = (mask & ~(1u << below)) | 1u << lead;
        if (!((mask >> sev) & 1u) && ((mask >> above) & 1u)) mask = (mask & ~(1u << above)) | 1u << sev;
        rel = (tgt.rel + 4u) % 7u;
        o->ok = tgt.ok && tgt.quality < 2u && gen_bits(mask) == 7u &&
            (uint32_t)(gen_pc(mask, (int32_t)(c->home + rel)) % 12) == (T + 7u) % 12u;
    } else {
        if (m == GM_SEC || (m && !c->mask[m])) { o->ok = 0; m = GM_KEY; }
        mask = c->mask[m];
    }
    int32_t a = (int32_t)(c->home + rel), r = gen_pc(mask, a), third = gen_pc(mask, a + 2) - r, fifth = gen_pc(mask, a + 4) - r;
    o->quality = (uint8_t)(third == 4 && fifth == 7 ? 0u : third == 3 && fifth == 7 ? 1u : third == 3 && fifth == 6 ? 2u : 3u);
    o->root = (uint8_t)(r % 12);
    o->pcs = (uint16_t)(1u << o->root | 1u << ((r + third) % 12) | 1u << ((r + fifth) % 12));
    if (m == GM_SEC && o->quality) o->ok = 0;          /* a secondary dominant is a major chord */
    o->mask = (uint16_t)mask; o->rel = (uint8_t)rel; o->mode = (uint8_t)(o->ok ? m : GM_KEY);
    if (o->ok && x->mode[i] == GM_SEC) o->mode = GM_SEC;
    o->func = (uint8_t)(o->mode == GM_SEC || rel == 4u || rel == 6u ? 2u : rel == 1u || rel == 3u ? 1u : 0u);
    if (o->mode && o->mode != GM_SEC && rel >= 5u) o->func = 1;   /* bVI, bVII lean subdominant */
}
static void gen_normalize(gen_genome_t *x)
{
    for (uint32_t pass = 0; pass < 2u; pass++) for (uint32_t i = x->len; i--;)
        if (x->mode[i] == GM_SEC) x->deg[i] = (uint8_t)((x->deg[(i + 1u) % x->len] + 4u) % 7u);
}
static uint32_t gen_distance(const gen_genome_t *a, const gen_genome_t *b)
{
    uint32_t len = a->len ? a->len : 1u, d = 0;
    for (uint32_t i = 0; i < len; i++) {
        uint32_t j = b->len ? i % b->len : 0;
        d += (a->deg[i] != b->deg[j]) * 2u + (a->mode[i] != b->mode[j]);
    }
    return d * 16u / (3u * len);
}
/* Progression features: the colours, motions and shape the fitness scores and the
 * player's taste weighs. */
enum { PF_MIX, PF_MODAL, PF_SEC, PF_FIFTH, PF_STEP, PF_THIRD, PF_DISTINCT, PF_TENSION, PF_TONIC, PF_CADENCE };
static int32_t gen_fitness(const gen_ctx_t *c, const gen_evo_t *e, const gen_genome_t *x, const gen_genome_t *parent,
                           uint32_t spice, uint32_t vary, uint32_t tension, int16_t *feat)
{
    gen_chord_t ch[8];
    uint32_t len = x->len, borrowed = 0, tsum = 0, distinct = 0;
    int32_t s = 0;
    int16_t f[GEN_PFEAT] = {0};
    for (uint32_t i = 0; i < len; i++) gen_chord(c, x, i, &ch[i], 0);
    for (uint32_t i = 0; i < len; i++) {
        const gen_chord_t *a = &ch[i], *b = &ch[(i + 1u) % len];
        if (!a->ok) s -= 120;
        if (a->quality == 2u) s -= a->func == 2u && !b->func ? 6 : 30;
        if (a->quality == 3u) s -= 40;
        if (a->mode == GM_SEC) { f[PF_SEC]++; s += 22 * (int32_t)spice / 100; }
        else if (a->mode == GM_MIX || a->mode == GM_HARMONIC) f[PF_MIX]++;
        else if (a->mode) f[PF_MODAL]++;
        borrowed += a->mode != 0;
        tsum += 1u + a->func + (a->mode != 0) + (a->mode == GM_SEC);
        if (len > 1u) {
            uint32_t motion = (b->rel + 7u - a->rel) % 7u, common = gen_bits(a->pcs & b->pcs);
            if (!motion) s += a->mode != b->mode ? 16 : -60;       /* IV -> iv is a classic, a repeat is not */
            else if (motion == 3u) { s += 26; f[PF_FIFTH]++; }    /* down a fifth */
            else if (motion == 4u) s += 10;
            else if (motion == 1u || motion == 6u) { s += motion == 1u ? 12 : 9; f[PF_STEP]++; }
            else { s += motion == 5u ? 16 : 8; f[PF_THIRD]++; }
            if (!a->mode && !b->mode) s += GEN_MARKOV[c->minor][a->rel][b->rel] * 4;
            s += (int32_t)common * 5;
            if (!common && motion != 3u && motion != 4u) s -= 10;
        }
        int seen = 0;
        for (uint32_t j = 0; j < i; j++) seen |= ch[j].rel == a->rel && ch[j].mode == a->mode;
        distinct += !seen;
    }
    f[PF_DISTINCT] = (int16_t)distinct; s += (int32_t)distinct * 8;
    if (len == 8u) {                                    /* A A' beats A A */
        uint32_t same = 0;
        for (uint32_t i = 0; i < 4u; i++) same += x->deg[i] == x->deg[i + 4u] && x->mode[i] == x->mode[i + 4u];
        s += same == 4u ? -50 : same >= 2u ? 10 : 0;
    }
    f[PF_TONIC] = !ch[0].rel && !ch[0].mode;
    s += f[PF_TONIC] ? (tension < 60u ? 25 : 5) : (tension >= 60u ? 12 : 0);
    f[PF_CADENCE] = (int16_t)(ch[len - 1u].func == 2u ? 2 : ch[len - 1u].func);
    s += f[PF_CADENCE] == 2 ? 30 : f[PF_CADENCE] ? 14 : -10;
    uint32_t want = (spice * len + 60u) / 120u;
    s -= (14 + (100 - (int32_t)spice) / 4) * (int32_t)(borrowed > want ? borrowed - want : want - borrowed);
    f[PF_TENSION] = (int16_t)(tsum * 16u / len);
    int32_t target = 16 + (int32_t)tension * 48 / 100;
    s -= (f[PF_TENSION] > target ? f[PF_TENSION] - target : target - f[PF_TENSION]) * 2;
    uint32_t near = 16;
    for (uint32_t k = 0; k < e->archive_n; k++) { uint32_t d = gen_distance(x, &e->archive[k]); if (d < near) near = d; }
    s += (int32_t)(near * vary) / 8;
    if (parent) { int32_t d = (int32_t)gen_distance(x, parent) - 6; s -= (d < 0 ? -d : d) * 4; }
    for (uint32_t k = 0; k < GEN_PFEAT; k++) s += e->pref[k] * f[k] / 4;
    if (feat) memcpy(feat, f, sizeof f);
    return s;
}
static void gen_mutate(const gen_ctx_t *c, gen_genome_t *x, uint32_t *rnd)
{
    static const uint8_t SAME[7][2] = {{2, 5}, {3, 3}, {0, 5}, {1, 1}, {6, 6}, {0, 2}, {4, 4}};
    uint32_t r = generator_word(rnd), len = x->len, i = len > 1u && (r >> 28) ? 1u + r % (len - 1u) : 0u;
    switch ((r >> 8) % 8u) {
    case 0: x->deg[i] = SAME[x->deg[i] % 7u][(r >> 12) & 1u]; x->mode[i] = GM_KEY; break;   /* same function */
    case 1: x->deg[i] = (uint8_t)((x->deg[(i + len - 1u) % len] + 3u) % 7u); x->mode[i] = GM_KEY; break;   /* down a fifth */
    case 2: { uint32_t m = 1u + (r >> 12) % 6u; x->mode[i] = (uint8_t)(c->mask[m] ? m : GM_KEY); break; }   /* borrow */
    case 3: if (len > 1u) x->mode[i] = GM_SEC; break;                                     /* secondary dominant */
    case 4: if (len > 2u && i + 1u < len) {
            uint8_t d = x->deg[i], m = x->mode[i];
            x->deg[i] = x->deg[i + 1u]; x->mode[i] = x->mode[i + 1u]; x->deg[i + 1u] = d; x->mode[i + 1u] = m;
        } break;
    case 5: x->deg[i] = (uint8_t)((r >> 12) % 7u); x->mode[i] = GM_KEY; break;
    case 6: x->mode[i] = (uint8_t)(x->mode[i] ? GM_KEY : GM_MIX); break;                   /* IV <-> iv */
    default: x->deg[i] = (uint8_t)((x->deg[i] + ((r >> 12) & 1u ? 1u : 6u)) % 7u); x->mode[i] = GM_KEY; break;
    }
    gen_normalize(x);
}
static void gen_resize(gen_genome_t *x, uint32_t len)
{
    uint32_t old = x->len ? x->len : 1u;
    for (uint32_t i = old; i < len; i++) { x->deg[i] = x->deg[i % old]; x->mode[i] = x->mode[i % old]; }
    x->len = (uint8_t)len;
    gen_normalize(x);
}
/* Seeds: the feel's own progressions, then Piston walks from home. */
static void gen_seed(const gen_ctx_t *c, gen_evo_t *e, uint32_t len, const uint8_t *types, uint32_t ntypes)
{
    for (uint32_t k = 0; k < 8u; k++) {
        gen_genome_t *x = &e->pop[k];
        uint32_t type = k < ntypes ? types[k] : GH_MARKOV;
        memset(x, 0, sizeof *x); x->len = (uint8_t)len;
        if (type >= GH_POP && type < GH_EVOLVE) {
            const int8_t *off = c->minor ? GEN_TEMPLATE[type - GH_POP].minor : GEN_TEMPLATE[type - GH_POP].major;
            for (uint32_t i = 0; i < len; i++) x->deg[i] = (uint8_t)generator_mod(off[i % GEN_TEMPLATE[type - GH_POP].len], 7u);
        } else for (uint32_t i = 1; i < len; i++) {
            uint32_t from = x->deg[i - 1u], sum = 0, w[7];
            for (uint32_t to = 0; to < 7u; to++) { w[to] = GEN_MARKOV[c->minor][from][to]; sum += w[to]; }
            uint32_t draw = generator_word(&e->random) % (sum ? sum : 1u), to = 0;
            while (to < 6u && draw >= w[to]) draw -= w[to++];
            x->deg[i] = (uint8_t)to;
        }
    }
    e->pop_n = 8; e->pop_len = (uint8_t)len;
}
/* Breed the leader's population and return its fittest progression of `len` chords.
 * With a parent, half the population starts as its variants: the section develops. */
static void gen_breed(const gen_ctx_t *c, gen_evo_t *e, uint32_t len, uint32_t tension, uint32_t spice, uint32_t vary,
                      const gen_genome_t *parent, uint32_t gens, const uint8_t *types, uint32_t ntypes, gen_genome_t *out)
{
    int32_t fit[8];
    if (!e->random) e->random = 0x2545F491u;
    if (len < 2u) len = 2u;
    if (len > 8u) len = 8u;
    if (e->pop_n != 8u) gen_seed(c, e, len, types, ntypes);
    else if (e->pop_len != len) { for (uint32_t k = 0; k < 8u; k++) gen_resize(&e->pop[k], len); e->pop_len = (uint8_t)len; }
    if (parent && parent->len) for (uint32_t k = 4; k < 8u; k++) {
        e->pop[k] = *parent; gen_resize(&e->pop[k], len);
        for (uint32_t m = 0; m <= (k & 1u); m++) gen_mutate(c, &e->pop[k], &e->random);
    }
    for (uint32_t k = 0; k < 8u; k++) fit[k] = gen_fitness(c, e, &e->pop[k], parent, spice, vary, tension, 0);
    for (uint32_t g = 0; g < gens; g++) {
        /* Immigrants keep the gene pool open: the weakest becomes a fresh seed. */
        uint32_t worst = 0;
        for (uint32_t k = 1; k < 8u; k++) if (fit[k] < fit[worst]) worst = k;
        gen_genome_t pool[8]; memcpy(pool, e->pop, sizeof pool);
        uint8_t save = e->pop_n;
        gen_seed(c, e, len, types, ntypes);
        gen_genome_t fresh = e->pop[generator_word(&e->random) & 7u];
        memcpy(e->pop, pool, sizeof pool); e->pop_n = save;
        for (uint32_t m = 0; m < 2u; m++) gen_mutate(c, &fresh, &e->random);
        e->pop[worst] = fresh; fit[worst] = gen_fitness(c, e, &fresh, parent, spice, vary, tension, 0);
    for (uint32_t child = 0; child < 6u; child++) {
        uint32_t r = generator_word(&e->random), a = r & 7u, b = (r >> 3) & 7u, a2 = (r >> 6) & 7u, b2 = (r >> 9) & 7u;
        if (fit[a2] > fit[a]) a = a2;
        if (fit[b2] > fit[b]) b = b2;
        gen_genome_t x = e->pop[a];
        uint32_t cut = 1u + (r >> 12) % (len - 1u);
        for (uint32_t i = cut; i < len; i++) { x.deg[i] = e->pop[b].deg[i]; x.mode[i] = e->pop[b].mode[i]; }
        gen_normalize(&x);
        for (uint32_t m = 0; m < 1u + ((r >> 16) % 100u < vary); m++) gen_mutate(c, &x, &e->random);
        int32_t fx = gen_fitness(c, e, &x, parent, spice, vary, tension, 0);
        uint32_t weak = 0;
        for (uint32_t k = 1; k < 8u; k++) if (fit[k] < fit[weak]) weak = k;
        if (fx > fit[weak]) { e->pop[weak] = x; fit[weak] = fx; }
    }
    }
    /* The fittest that is not one of the last three heard (when one exists). */
    uint32_t best = 8;
    for (uint32_t k = 0; k < 8u; k++) {
        uint32_t fresh = 1;
        for (uint32_t a = 0; a < 3u && a < e->archive_n; a++) fresh &= gen_distance(&e->pop[k], &e->archive[(e->archive_at + 7u - a) & 7u]) >= 4u;
        if (fresh && (best == 8u || fit[k] > fit[best])) best = k;
    }
    if (best == 8u) {                                   /* all heard lately: vary the fittest until it is new */
        best = 0;
        for (uint32_t k = 1; k < 8u; k++) if (fit[k] > fit[best]) best = k;
        for (uint32_t tries = 0; tries < 8u; tries++) {
            gen_genome_t x = e->pop[best];
            gen_mutate(c, &x, &e->random); gen_mutate(c, &x, &e->random);
            uint32_t fresh = 1;
            for (uint32_t a = 0; a < 3u && a < e->archive_n; a++) fresh &= gen_distance(&x, &e->archive[(e->archive_at + 7u - a) & 7u]) >= 4u;
            if (fresh) { *out = x; return; }
        }
    }
    *out = e->pop[best];
}
/* What the band plays now enters the novelty archive and the running feature means. */
static void gen_remember(const gen_ctx_t *c, gen_evo_t *e, const gen_genome_t *x)
{
    if (e->archive_n && !gen_distance(x, &e->archive[(e->archive_at + 7u) & 7u])) return;
    int16_t f[GEN_PFEAT];
    gen_fitness(c, e, x, 0, 0, 0, 0, f);
    e->archive[e->archive_at] = *x;
    e->archive_at = (uint8_t)((e->archive_at + 1u) & 7u);
    if (e->archive_n < 8u) e->archive_n++;
    for (uint32_t k = 0; k < GEN_PFEAT; k++) e->mean[k] = (int16_t)(e->mean[k] + (f[k] * 16 - e->mean[k]) / 8);
}
static void gen_to_path(generator_t *g, const gen_ctx_t *c, const gen_genome_t *x)
{
    g->path_len = x->len;
    for (uint32_t i = 0; i < x->len; i++) {
        gen_chord_t ch; gen_chord(c, x, i, &ch, 0);
        g->path[i] = (uint8_t)((c->home + ch.rel) % 7u);
        g->path_mode[i] = ch.mode;
        g->path_mask[i] = (uint16_t)(ch.mode ? ch.mask : 0u);
    }
}
static void gen_from_path(const generator_t *g, uint32_t home, gen_genome_t *x)
{
    memset(x, 0, sizeof *x);
    x->len = (uint8_t)(g->path_len < 8u ? g->path_len : 8u);
    for (uint32_t i = 0; i < x->len; i++) { x->deg[i] = (uint8_t)((g->path[i] + 7u - home % 7u) % 7u); x->mode[i] = g->path_mode[i]; }
}
/* The chord a path entry plays, as a numeral from home: case by quality, a flat or
 * sharp when its root left the key (bVI, #iv), for the page picture. */
static void generator_numeral(const track_t *t, uint32_t home, uint32_t degree, uint32_t mask, char *b)
{
    static const char *const R[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
    uint32_t key = scale_mask(t), rel = (degree + 7u - home % 7u) % 7u;
    if (!mask) mask = key;
    int32_t r = gen_pc(mask, (int32_t)degree), shift = r - gen_pc(key, (int32_t)degree);
    uint32_t minor = gen_pc(mask, (int32_t)degree + 2) - r == 3, at = 0;
    if (shift) b[at++] = shift < 0 ? 'b' : '#';
    for (const char *s = R[rel]; *s; s++) b[at++] = (char)(minor ? *s + 32 : *s);
    b[at] = 0;
}

/* MOTIF: develop the motif by one classical operator (candidate kind k). */
static void gen_motif_op(const int8_t *m, const int8_t *fresh, uint32_t n, int8_t *out, uint32_t k, uint32_t *rnd)
{
    uint32_t r = generator_word(rnd);
    memcpy(out, m, n);
    switch (k & 7u) {
    case 0: { int32_t step = (int32_t)(r % 4u) - 2; if (!step) step = 1;               /* sequence */
        for (uint32_t i = 0; i < n; i++) out[i] = (int8_t)(m[i] + step); } break;
    case 1: for (uint32_t i = 0; i < n; i++) out[i] = (int8_t)(2 * m[0] - m[i]); break;       /* inversion */
    case 2: for (uint32_t i = 0; i < n; i++) out[i] = m[n - 1u - i]; break;                  /* retrograde */
    case 3: if (n > 1u) { uint32_t s = 1u + r % (n - 1u);                                  /* rotation */
        for (uint32_t i = 0; i < n; i++) out[i] = m[(i + s) % n]; } break;
    case 4: { uint32_t half = (n + 1u) / 2u, from = (r & 1u) ? n - half : 0u;              /* fragment, twice */
        for (uint32_t i = 0; i < n; i++) out[i] = m[from + i % half]; } break;
    case 5: for (uint32_t j = 0; j < 2u && n > 1u; j++) {                                  /* ornament */
            uint32_t i = 1u + (r >> (j * 8u)) % (n - 1u);
            out[i] = (int8_t)(out[i - 1u] + (((r >> (j + 20u)) & 1u) ? 1 : -1));
        } break;
    case 6:                                                                                /* expand / contract */
        for (uint32_t i = 0; i < n; i++) out[i] = (int8_t)(m[0] + ((r & 1u) ? (m[i] - m[0]) * 3 / 2 : (m[i] - m[0]) / 2));
        break;
    default:
        if (r & 1u) for (uint32_t i = (r >> 1) % 2u ? 0u : n / 2u, e = i + n / 2u; i < e && i < n; i++) out[i] = fresh[i];   /* crossover */
        else for (uint32_t j = 0; j < 2u; j++) { uint32_t i = (r >> (j * 8u + 2u)) % n;    /* point mutation */
            out[i] = (int8_t)(out[i] + (int32_t)((r >> (j + 24u)) % 7u) - 3); }
        break;
    }
    for (uint32_t i = 0; i < n; i++) out[i] = (int8_t)(out[i] < -14 ? -14 : out[i] > 14 ? 14 : out[i]);
}
enum { MF_STEP, MF_LEAP, MF_RANGE, MF_CHORD, MF_SIM, MF_ARCH };
static int32_t gen_motif_fit(const track_t *t, const generator_t *g, const gen_evo_t *e, const int8_t *rel, uint32_t n,
                             const uint8_t *slot, int32_t low, int32_t span, int16_t *feat)
{
    int32_t count = (int32_t)generator_count(t), d[16], s = 0, top = -1000, low_d = 1000, peak = 0, peaks = 0;
    int16_t f[GEN_MFEAT] = {0};
    uint32_t strong = 0, strong_tone = 0;
    for (uint32_t k = 0; k < n; k++) {
        int32_t x = g->root + rel[k];
        while (x < low) x += count;
        while (x >= low + span) x -= count;
        d[k] = x;
        if (x > top) { top = x; peak = (int32_t)k; peaks = 1; } else if (x == top) peaks++;
        if (x < low_d) low_d = x;
        uint32_t tone = 0;
        for (uint32_t c = 0; c < g->chord_n; c++) tone |= generator_mod(x - g->chord[c], (uint32_t)count) == 0;
        if (!(slot[k] & 3u)) { strong++; strong_tone += tone; s += tone ? 10 : -6; }
        if (k + 1u == n) s += tone ? (generator_mod(x - g->root, (uint32_t)count) ? 12 : 18) : -12;
    }
    for (uint32_t k = 1; k < n; k++) {
        int32_t step = d[k] - d[k - 1u], a = step < 0 ? -step : step;
        if (a <= 2) { f[MF_STEP]++; s += 6; }
        if (a >= 4) { f[MF_LEAP]++; s -= 8;
            if (k + 1u < n) { int32_t next = d[k + 1u] - d[k]; if ((step > 0) != (next > 0) && next && next >= -2 && next <= 2) s += 6; } }
        if (!step && k > 1u && d[k - 1u] == d[k - 2u]) s -= 8;
    }
    f[MF_STEP] = (int16_t)(n > 1u ? f[MF_STEP] * 16 / (int32_t)(n - 1u) : 16);
    f[MF_RANGE] = (int16_t)(top - low_d);
    s += f[MF_RANGE] <= count + 2 ? 8 : f[MF_RANGE] > 2 * count ? -20 : 0;
    f[MF_ARCH] = peaks == 1 && peak && peak + 1 < (int32_t)n;
    s += f[MF_ARCH] ? 10 : 0;
    f[MF_CHORD] = (int16_t)(strong ? strong_tone * 16u / strong : 8u);
    uint32_t same = 0, near = 16;
    for (uint32_t k = 0; k < n; k++) same += rel[k] == g->motif_rel[k];
    f[MF_SIM] = (int16_t)(same * 16u / n);
    int32_t want = 8 - (int32_t)g->p[GN_EVOLVE] / 25, off = f[MF_SIM] - want;
    s -= (off < 0 ? -off : off) * 2;
    for (uint32_t a = 0; a < g->motif_arch_n; a++) {
        uint32_t diff = 0;
        for (uint32_t k = 0; k < n; k++) diff += rel[k] != g->motif_arch[a][k];
        diff = diff * 16u / n;
        if (diff < near) near = diff;
    }
    s += (int32_t)near / 2;
    for (uint32_t k = 0; k < GEN_MFEAT; k++) s += e->mpref[k] * f[k] / 4;
    if (feat) memcpy(feat, f, sizeof f);
    return s;
}
/* After a phrase is planned (as DRIFT), replace its onsets with the motif: the first
 * statement is that DRIFT line; at each phrase start CHANGE is the chance it develops
 * into the fittest of eight variants. The motif is relative to the chord root, so it
 * follows the harmony like an answer does. */
static void generator_motif(track_t *t, generator_t *g, int boundary, int32_t low, int32_t span, uint32_t first)
{
    uint32_t n = 0, count = generator_count(t);
    uint8_t slot[16]; int8_t fresh[16];
    for (uint32_t step = 0; step < 16u; step++) {
        uint32_t i = (first + step) & 15u;
        if (i < g->length && ((g->rhythm >> i) & 1u)) slot[n++] = (uint8_t)i;
    }
    if (!n) return;
    for (uint32_t k = 0; k < n; k++) {
        int32_t r = g->phrase[slot[k]] == KB_SILENT ? 0 : generator_mapped_degree(t, g->phrase[slot[k]]) - g->root;
        fresh[k] = (int8_t)(r < -14 ? -14 : r > 14 ? 14 : r);
    }
    if (!g->motif_random) g->motif_random = g->random ^ 0x3C6EF372u ? g->random ^ 0x3C6EF372u : 1u;
    gen_evo_t *e = &gen_evo[generator_jam_owner(trk_index(t))];
    if (!g->motif_ok || g->motif_n != n) {
        memcpy(g->motif_rel, fresh, n); g->motif_n = (uint8_t)n; g->motif_ok = 1;
    } else if (boundary && generator_word(&g->motif_random) % 100u < (uint32_t)g->p[GN_EVOLVE]) {
        int8_t cand[16], best_rel[16];
        int32_t best = -0x7FFFFFFF;
        for (uint32_t k = 0; k < 8u; k++) {
            gen_motif_op(g->motif_rel, fresh, n, cand, k, &g->motif_random);
            int32_t fit = gen_motif_fit(t, g, e, cand, n, slot, low, span, 0);
            if (fit > best) { best = fit; memcpy(best_rel, cand, n); }
        }
        memcpy(g->motif_arch[g->motif_arch_at], g->motif_rel, 16);
        g->motif_arch_at = (uint8_t)((g->motif_arch_at + 1u) & 3u);
        if (g->motif_arch_n < 4u) g->motif_arch_n++;
        memcpy(g->motif_rel, best_rel, n);
        int16_t f[GEN_MFEAT];
        gen_motif_fit(t, g, e, g->motif_rel, n, slot, low, span, f);
        for (uint32_t k = 0; k < GEN_MFEAT; k++) e->mmean[k] = (int16_t)(e->mmean[k] + (f[k] * 16 - e->mmean[k]) / 8);
    }
    for (uint32_t k = 0; k < n; k++) {
        int32_t x = g->root + g->motif_rel[k];
        while (x < low) x += (int32_t)count;
        while (x >= low + span) x -= (int32_t)count;
        uint32_t note = generator_note(t, x);
        if (note != KB_SILENT) g->phrase[slot[k]] = (uint8_t)note;
    }
}
/* Taste: move each weight by how far the playing material's feature sits from what the
 * band usually plays, toward it (signal > 0) or away (signal < 0). */
static void gen_learn(int8_t *pref, const int16_t *mean, const int16_t *f, uint32_t n, int32_t signal)
{
    for (uint32_t k = 0; k < n; k++) {
        int32_t d = (f[k] * 16 - mean[k]) / 16, w = pref[k] + signal * (d > 2 ? 2 : d < -2 ? -2 : d) * 3;
        pref[k] = (int8_t)(w > 48 ? 48 : w < -48 ? -48 : w);
    }
}
