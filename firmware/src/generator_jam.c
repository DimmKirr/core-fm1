/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* JAM: an endless backing band, original Melodee work. The leader (a LOCAL source)
 * writes a song form from three themes of its FEEL (verse, chorus, bridge): each a
 * progression (a named one turned and partly re-harmonised, or a journey), a home that
 * may move to a related degree, a change rate, the parts' styles, voicing step,
 * comping and register. Every song starts from fresh entropy, so the same feel never
 * plays the same song twice. A theme returns recognisably, but each return varies (a
 * substituted chord, another arp or bass style, a new melody, another comping rhythm)
 * with VARY's chance, alternate passes inside a section lift, the second half of a
 * section before a chorus builds, and each round of the form renews themes. Every
 * linked part derives rate, length, fill, gate, register, voicing and dynamics from its
 * role, the theme and three macros (DRIVE, SPICE, VARY). The director only writes
 * ordinary generator settings, so each change commits at the part's own phrase
 * boundary and shows on its knobs; JAM OFF hands the settings back to the player. */
enum { SEC_INTRO, SEC_VERSE, SEC_CHORUS, SEC_BRIDGE, SEC_BREAK };
static const char *const GEN_SECTION[] = {"INTRO", "VERSE", "CHORUS", "BRIDGE", "BREAK"};
static const int8_t GEN_SECTION_LIFT[5] = {-35, -10, 18, 0, -30};
static const uint8_t GEN_SECTION_THEME[5] = {0, 0, 1, 2, 1};
#define V SEC_VERSE
#define C SEC_CHORUS
#define B SEC_BRIDGE
#define K SEC_BREAK
static const uint8_t GEN_FORM[8][8] = {
    {V, V, C, V, C, B, C, C}, {V, C, V, C, K, B, C, C}, {V, V, C, C, B, K, C, V}, {V, C, C, B, V, C, K, C},
    {V, C, V, C, B, C, K, C}, {V, V, C, B, V, C, C, K}, {C, V, C, V, B, B, C, C}, {V, K, V, C, B, C, V, C}};
#undef V
#undef C
#undef B
#undef K
/* Voicing families by SPICE: bright, jazz, wide. */
static const uint8_t GEN_VOICING[3][4] = {
    {GV_TRIAD, GV_ADD9, GV_7TH, GV_69},
    {GV_SHELL, GV_7TH, GV_911, GV_69},
    {GV_OPEN, GV_ADD9, GV_SUS2, GV_QUARTAL}};
typedef struct {
    uint8_t harmony[6], arp[6], bass[4], chord[2], answer[3];
    uint8_t pulse, chord_rate, every, gate, strum, voicing;   /* RATE ids; EVERY id; GATE; ms; family */
} gen_feel_t;
static const gen_feel_t GEN_FEEL[GJ_COUNT - 1] = {
    /* POP */     {{GH_POP, GH_AXIS, GH_EVOLVE, GH_ROYAL, GH_CANON, GH_EVOLVE}, {AS_UP, AS_BOUNCE, AS_MOTIF, AS_PINKY, AS_STAIRS, AS_MOTIF},
                   {BS_ROOT, BS_OCTAVE, BS_FIFTH, BS_ARP}, {CS_BLOCK, CS_BLOCK}, {0, 2, 5}, 1, 0, 0, 80, 10, 0},
    /* ROCK */    {{GH_VAMP, GH_RISE, GH_EVOLVE, GH_FALL, GH_POP, GH_EVOLVE}, {AS_UP, AS_THUMB, AS_MOTIF, AS_STAIRS, AS_PINKY, AS_BOUNCE},
                   {BS_ROOT, BS_ROOT, BS_OCTAVE, BS_FIFTH}, {CS_BLOCK, CS_BLOCK}, {0, 3, 1}, 1, 1, 0, 70, 14, 0},
    /* FUNK */    {{GH_VAMP, GH_HOLD, GH_EVOLVE, GH_MARKOV, GH_JAZZ, GH_EVOLVE}, {AS_MOTIF, AS_THUMB, AS_PINKY, AS_STAIRS, AS_INWARD, AS_DRIFT},
                   {BS_OCTAVE, BS_ROOT, BS_DRIFT, BS_OCTAVE}, {CS_BLOCK, CS_BLOCK}, {2, 0, 1}, 2, 2, 1, 50, 4, 1},
    /* JAZZ */    {{GH_JAZZ, GH_CIRCLE, GH_EVOLVE, GH_JAZZ, GH_EVOLVE, GH_MARKOV}, {AS_MOTIF, AS_DRIFT, AS_STAIRS, AS_MOTIF, AS_INWARD, AS_DRIFT},
                   {BS_WALK, BS_WALK, BS_FIFTH, BS_WALK}, {CS_BLOCK, CS_STRIDE}, {0, 5, 2}, 4, 4, 0, 64, 0, 1},
    /* BALLAD */  {{GH_CANON, GH_FALL, GH_EVOLVE, GH_ROYAL, GH_AXIS, GH_EVOLVE}, {AS_ALBERTI, AS_UP, AS_BOUNCE, AS_MOTIF, AS_INWARD, AS_PINKY},
                   {BS_FIFTH, BS_ARP, BS_ROOT, BS_PEDAL}, {CS_BLOCK, CS_BLOCK}, {5, 0, 2}, 1, 6, 0, 96, 35, 2},
    /* AMBIENT */ {{GH_PATH, GH_NEAR, GH_EVOLVE, GH_MARKOV, GH_EVOLVE, GH_CANON}, {AS_DRIFT, AS_INWARD, AS_MOTIF, AS_UP, AS_BOUNCE, AS_MOTIF},
                   {BS_PEDAL, BS_PEDAL, BS_DRIFT, BS_FIFTH}, {CS_BLOCK, CS_BLOCK}, {5, 4, 0}, 1, 7, 1, 110, 60, 2},
    /* EPIC */    {{GH_POP, GH_RISE, GH_EVOLVE, GH_FALL, GH_ROYAL, GH_EVOLVE}, {AS_UP, AS_BOUNCE, AS_INWARD, AS_STAIRS, AS_MOTIF, AS_ALBERTI},
                   {BS_OCTAVE, BS_ROOT, BS_OCTAVE, BS_FIFTH}, {CS_BLOCK, CS_BLOCK}, {0, 2, 3}, 2, 0, 0, 72, 6, 2},
    /* BLUES */   {{GH_BLUES, GH_BLUES, GH_VAMP, GH_BLUES, GH_EVOLVE, GH_BLUES}, {AS_MOTIF, AS_DRIFT, AS_THUMB, AS_STAIRS, AS_PINKY, AS_MOTIF},
                   {BS_BOOGIE, BS_WALK, BS_BOOGIE, BS_WALK}, {CS_STRIDE, CS_BLOCK}, {0, 1, 2}, 4, 4, 0, 80, 12, 1}};

/* A song's first random word: the clock, a running count and the settings, mixed. */
static uint32_t generator_entropy(const generator_t *g)
{
    static uint32_t songs;
    uint32_t x = fm1_ms * 2654435761u ^ ++songs * 0x9E3779B9u ^ (uint32_t)g->p[GN_SEED] * 40503u ^
        (uint32_t)g->p[GN_JAM] * 0x85EBCA6Bu ^ beat_pos * 0xC2B2AE35u;
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    return x ? x : 1u;
}
static uint32_t generator_jam_length(uint32_t rate, uint32_t bars_max)
{
    static const uint8_t slots[10] = {4, 8, 16, 32, 12, 24, 2, 1, 1, 1};
    uint32_t s = slots[rate < 10u ? rate : 1u], bars = s <= 8u ? 2u : 1u;
    if (bars > bars_max) bars = bars_max;
    s *= bars;
    return s > 16u ? 16u : s;
}
static int16_t generator_jam_fill(uint32_t length, uint32_t density)
{
    uint32_t fill = (length * density + 50u) / 100u;
    return (int16_t)(fill < 1u ? 1u : fill > length ? length : fill);
}
static const gen_feel_t *generator_feel(const generator_t *g) { return &GEN_FEEL[(uint32_t)(g->p[GN_JAM] - 1) % (GJ_COUNT - 1u)]; }
/* Passes of the section's progression so far: alternate passes lift. */
static uint32_t generator_jam_pass(const generator_t *g)
{
    uint32_t unit = (generator_template(&trk[g - generator], g->harmony_used) || g->harmony_used == GH_EVOLVE) && g->path_len ? g->path_len : 4u;
    return g->jam_count / unit;
}
/* One part's settings for the section: everything but its role, register and sound. */
static void generator_jam_part(uint32_t tr, uint32_t lead, uint32_t e)
{
    generator_t *g = &generator[tr], *L = &generator[lead];
    const gen_feel_t *f = generator_feel(L);
    const gen_theme_t *th = &L->theme[L->jam_theme % 3u];
    uint32_t x = (uint32_t)L->p[GN_TENSION], v = (uint32_t)L->p[GN_VARIETY], sec = L->section, rest = 0;
    uint32_t bars_max = tr == lead ? 1u << th->every : 2u, rate, length, density, odd = generator_jam_pass(L) & 1u;
    int32_t level = 60 + (int32_t)e * 45 / 100, voicing = (int32_t)(x * 4u / 101u) + th->voice - 1;
    int16_t *p = g->p;
    g->jam_lead = (uint8_t)(lead + 1u);
    p[GN_SEED] = (int16_t)(1u + (th->seed + tr * 17u) % 127u);
    p[GN_DYNAMICS] = GD_HUMAN; p[GN_STRUM] = 0; p[GN_RANGE] = 1;
    p[GN_PALETTE] = (int16_t)(10u + x * 3u / 5u);
    p[GN_COLOR] = GEN_VOICING[f->voicing][voicing < 0 ? 0 : voicing > 3 ? 3 : voicing];
    switch (g->p[GN_MODE]) {
    case GEN_WALK: {
        uint32_t style = th->bstyle;
        p[GN_BSTYLE] = (int16_t)style;
        rate = style == BS_WALK || style == BS_FIFTH ? 0u : style == BS_PEDAL ? (e < 50u ? 6u : 0u) :
            style == BS_ARP ? 1u : f->pulse == 4u ? 4u : e < 25u ? 0u : f->pulse;
        length = generator_jam_length(rate, 1u);
        density = style == BS_WALK || style == BS_FIFTH || style == BS_PEDAL ? 100u :
            style == BS_BOOGIE ? (rate == 4u ? 67u : 100u) : 45u + e * 55u / 100u - th->comp * 8u;
        p[GN_GATE] = (int16_t)(style == BS_WALK ? 100 : style == BS_PEDAL ? 122 : style == BS_FIFTH ? 90 : f->gate - 6);
        p[GN_PALETTE] = 0; p[GN_EVOLVE] = (int16_t)(v / 4u); p[GN_DYNAMICS] = GD_PHRASE;
        level += 4; rest = sec == SEC_BREAK || (sec == SEC_INTRO && e < 20u);
        g->rhythm_shift = 0;
        break;
    }
    case GEN_CHORD:
        p[GN_CSTYLE] = th->cstyle;
        rate = f->chord_rate;
        if (rate == 7u && e >= 55u) rate = 6u;
        else if (rate == 6u && e < 35u) rate = 7u;
        else if (rate == 1u && e < 40u) rate = 0u;
        else if (rate == 0u && (e >= 75u || (th->comp == 2u && e >= 30u)) && f->voicing != 1u) rate = 1u;
        length = generator_jam_length(rate, bars_max);
        density = rate >= 6u ? 100u : rate == 0u ? (e < 35u || th->comp == 0u ? 50u : 100u) : rate == 2u ? 20u + e * 30u / 100u :
            rate == 4u ? (th->cstyle == CS_STRIDE ? 67u : 17u + e * 25u / 100u) : th->comp == 2u ? 38u : 50u + e / 2u;
        p[GN_GATE] = (int16_t)(rate >= 6u ? 124 : rate == 2u ? 34 : rate == 4u ? 64 : rate == 1u ? 72 : e > 70u ? 84 : 104);
        p[GN_STRUM] = f->strum; p[GN_DYNAMICS] = GD_PHRASE; p[GN_EVOLVE] = 0;
        level -= 6;
        /* Comping rhythms (pushed eighths, funk sixteenths, jazz triplets) take the theme's rotation. */
        g->rhythm_shift = (uint8_t)(rate == 2u || (rate == 4u && th->cstyle != CS_STRIDE) || (rate == 1u && th->comp == 2u) ? th->shift % length : 0u);
        break;
    case GEN_ANSWER:
        p[GN_ANSWER] = th->answer;
        rate = f->pulse; length = generator_jam_length(rate, 2u); density = 30u + e * 40u / 100u;
        p[GN_GATE] = f->gate; p[GN_EVOLVE] = (int16_t)(v / 5u); p[GN_RANGE] = (int16_t)(1u + (e >= 60u));
        rest = sec == SEC_INTRO || sec == SEC_BREAK || (sec == SEC_VERSE && e < 50u);
        g->rhythm_shift = 0;
        break;
    default:
        /* Alternate passes may switch to the theme's second arp style. */
        p[GN_ASTYLE] = odd && v >= 40u ? th->astyle2 : th->astyle;
        rate = f->pulse;
        if (rate == 1u && e >= 72u && f->voicing != 1u) rate = 2u;
        else if (rate == 1u && e < 20u) rate = 0u;
        else if (rate == 2u && e < 30u) rate = 1u;
        length = generator_jam_length(rate, bars_max);
        density = p[GN_ASTYLE] == AS_DRIFT || p[GN_ASTYLE] == AS_MOTIF ? 35u + e * 65u / 100u : 55u + e * 45u / 100u;
        p[GN_GATE] = (int16_t)(f->gate - e / 6u);
        p[GN_RANGE] = (int16_t)(1u + (e >= 55u) + (f->voicing == 2u && e >= 35u) + th->lift);
        if (p[GN_RANGE] > 3) p[GN_RANGE] = 3;
        p[GN_EVOLVE] = (int16_t)(p[GN_ASTYLE] == AS_MOTIF ? 20u + v * 3u / 5u : p[GN_ASTYLE] == AS_DRIFT ? 8u + v / 2u : v / 3u);
        rest = sec == SEC_INTRO;
        g->rhythm_shift = 0;
        break;
    }
    p[GN_RATE] = (int16_t)rate; p[GN_LENGTH] = (int16_t)length; p[GN_FILL] = generator_jam_fill(length, density);
    p[GN_CONDITION] = (int16_t)(rest ? GW_REST : g->p[GN_MODE] == GEN_ANSWER && sec != SEC_CHORUS ? GW_EVEN : GW_ALWAYS);
    g->level = (uint8_t)(level < 30 ? 30 : level > 115 ? 115 : level);
    if (tr == lead) { p[GN_HARMONY] = th->harmony; p[GN_CHANGE] = th->every; }
}
static void generator_jam_apply(track_t *t, uint32_t energy)
{
    uint32_t lead = trk_index(t), error;
    generator[lead].jam_energy = (uint8_t)energy;
    for (uint32_t i = 0; i < trk_n(); i++)
        if (generator_on(&trk[i]) && generator_source(i, &error) == (int)lead) generator_jam_part(i, lead, energy);
}
/* Breed a theme's progression from the leader's population: about 4 or 8 chords, a
 * tension that rises from verse to chorus to bridge; `develop` starts from the
 * theme's own progression, so a returning section grows instead of being replaced. */
static void generator_jam_breed(track_t *t, uint32_t which, int develop)
{
    static const uint8_t TENSION[3] = {35, 50, 72};
    generator_t *g = &generator[trk_index(t)];
    gen_theme_t *th = &g->theme[which];
    if (!generator_functional(t)) return;
    const gen_feel_t *f = generator_feel(g);
    gen_ctx_t c; gen_context(t, generator_mod(g->anchor + th->home, generator_count(t)), &c);
    gen_genome_t parent = th->genome;
    uint32_t len = develop && parent.len ? parent.len : (generator_word(&g->jam_random) % 100u < 60u ? 4u : 8u);
    gen_breed(&c, &gen_evo[trk_index(t)], len, TENSION[which % 3u], (uint32_t)g->p[GN_TENSION], (uint32_t)g->p[GN_VARIETY],
        develop && parent.len ? &parent : 0, develop ? 2u : 4u, f->harmony, 6u, &th->genome);
}
/* A fresh theme for slot `which` (verse, chorus, bridge). VARY sets how far it strays:
 * a rotated progression, substituted chords, a bridge or verse on a related degree. */
static void generator_jam_theme(track_t *t, uint32_t which, const gen_feel_t *f)
{
    static const uint8_t AWAY[2][3] = {{5, 3, 4}, {2, 5, 3}};   /* major home: vi IV V; minor home: III VI iv */
    generator_t *g = &generator[trk_index(t)];
    gen_theme_t *th = &g->theme[which];
    uint32_t r = generator_word(&g->jam_random), q = generator_word(&g->jam_random), v = (uint32_t)g->p[GN_VARIETY];
    th->harmony = f->harmony[r % 6u];
    for (uint32_t i = 1; i < 6u && th->harmony != GH_BLUES; i++) {   /* (themes differ; twelve bars may recur) */
        uint32_t clash = 0;
        for (uint32_t w = 0; w < 3u; w++) clash |= w != which && g->theme[w].harmony == th->harmony;
        if (!clash) break;
        th->harmony = f->harmony[(r + i) % 6u];
    }
    th->every = (uint8_t)(f->every + (which == 2u && ((r >> 3) & 1u) && f->every < 2u));
    th->astyle = f->arp[(r >> 5) % 6u]; th->astyle2 = f->arp[(r >> 8) % 6u]; th->bstyle = f->bass[(r >> 11) & 3u];
    th->cstyle = f->chord[(r >> 13) & 1u]; th->answer = f->answer[(r >> 14) % 3u];
    th->seed = (uint8_t)(1u + (r >> 16) % 127u); th->shift = (uint8_t)((r >> 23) & 15u);
    th->voice = (uint8_t)(q % 3u); th->comp = (uint8_t)((q >> 2) % 3u); th->lift = (uint8_t)((q >> 4) % 100u < v / 3u);
    th->mut = (uint8_t)(q >> 8);
    th->muts = (uint8_t)(((q >> 16) % 100u < v) + ((q >> 16) % 100u < v / 2u));
    th->rot = (uint8_t)((q >> 24) % 100u < v / 2u ? 1u + (q >> 26) % 3u : 0u);
    uint32_t away = which == 2u ? 55u : which == 0u ? v / 4u : 0u;
    th->home = (uint8_t)((q >> 12) % 100u < away ?
        AWAY[generator_home_minor(t, generator_mod(g->anchor, generator_count(t)))][(q >> 20) % 3u] : 0u);
    memset(&th->genome, 0, sizeof th->genome);
    if (th->harmony == GH_EVOLVE) generator_jam_breed(t, which, 0);
}
/* A returning theme varies with VARY's chance: one to three of a substituted chord,
 * another arp, bass or answer style, a voicing step, a new melody, another comping
 * rhythm or register. Its progression, change rate and home stay: it is recognisable. */
static void generator_jam_vary(generator_t *g, uint32_t which, const gen_feel_t *f)
{
    gen_theme_t *th = &g->theme[which];
    uint32_t v = (uint32_t)g->p[GN_VARIETY], n = 1u + (v >= 40u) + (v >= 80u);
    if (generator_word(&g->jam_random) % 100u >= v) return;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t r = generator_word(&g->jam_random);
        switch (r & 7u) {
        case 0: th->mut = (uint8_t)(r >> 8); th->muts = (uint8_t)(1u + (r >> 16) % 2u);
            if (th->harmony == GH_EVOLVE) generator_jam_breed(&trk[g - generator], which, 1);   /* develop it */
            break;
        case 1: th->astyle = f->arp[(r >> 8) % 6u]; break;
        case 2: th->bstyle = f->bass[(r >> 8) & 3u]; break;
        case 3: th->voice = (uint8_t)((r >> 8) % 3u); break;
        case 4: th->seed = (uint8_t)(1u + (r >> 8) % 127u); break;
        case 5: th->shift = (uint8_t)((r >> 8) & 15u); th->comp = (uint8_t)((r >> 12) % 3u); break;
        case 6: th->lift ^= 1u; th->astyle2 = f->arp[(r >> 8) % 6u]; break;
        default: th->answer = f->answer[(r >> 8) % 3u]; th->cstyle = f->chord[(r >> 11) & 1u]; break;
        }
    }
}
static uint32_t generator_jam_peek(const generator_t *g) { return g->form_pos < 7u ? g->form[g->form_pos + 1u] : SEC_VERSE; }
/* Section length in chord changes: about 4, 8 or 16 bars (VARY high to low), whole
 * cycles of a named or bred progression, an even number of bars so two-bar phrases stay
 * aligned. A journey (PATH, NEAR, MARKOV) may stop anywhere: the next section starts
 * from its own first chord. VARY zero keeps the current section forever. */
static uint32_t generator_jam_len(const generator_t *g)
{
    uint32_t v = (uint32_t)g->p[GN_VARIETY];
    if (!v) return 0;
    uint32_t bars = v < 34u ? 16u : v < 67u ? 8u : 4u, every = 1u << g->p[GN_CHANGE];
    if (g->section == SEC_INTRO || g->section == SEC_BREAK) bars /= 2u;
    uint32_t len = (generator_template(&trk[g - generator], g->harmony_used) || g->harmony_used == GH_EVOLVE) && g->path_len ? g->path_len : 1u;
    uint32_t changes = (bars + every - 1u) / every;
    if (changes < len) changes = len;
    changes = (changes + len - 1u) / len * len;
    if (every == 1u && (changes & 1u)) changes += len;
    return changes > 48u ? 48u : changes;
}
/* Section energy: DRIVE, the section's lift, a lift on alternate passes and a build
 * through the second half of a section that leads into a chorus. */
static uint32_t generator_jam_energy(const generator_t *g)
{
    int32_t e = g->p[GN_ENERGY] + GEN_SECTION_LIFT[g->section % 5u];
    if (g->p[GN_VARIETY] && (generator_jam_pass(g) & 1u)) e += 8;
    if (generator_jam_peek(g) == SEC_CHORUS && g->section != SEC_CHORUS && g->jam_len >= 2u && g->jam_count >= g->jam_len / 2u)
        e += 24 * (int32_t)(g->jam_count - g->jam_len / 2u + 1u) / (int32_t)(g->jam_len - g->jam_len / 2u);
    return (uint32_t)(e < 0 ? 0 : e > 100 ? 100 : e);
}
/* The first chord the next section plays (a walking bass approaches it). */
static uint32_t generator_jam_first(track_t *t, const gen_theme_t *th)
{
    generator_t *g = &generator[trk_index(t)];
    uint32_t count = generator_count(t), home = generator_mod(g->anchor + th->home, count);
    if (th->harmony == GH_EVOLVE && th->genome.len && generator_functional(t)) return (home + th->genome.deg[0]) % 7u;
    if (!generator_template(t, th->harmony)) return home;
    return generator_template_chord(t, th->harmony, home, generator_home_minor(t, home),
        th->harmony == GH_BLUES ? 0u : th->rot % GEN_TEMPLATE[th->harmony - GH_POP].len);
}
static void generator_jam_section(track_t *t, uint32_t section, int vary)
{
    generator_t *g = &generator[trk_index(t)];
    uint32_t which = GEN_SECTION_THEME[section % 5u];
    /* A theme heard before returns varied. */
    if (vary && ((g->jam_heard >> which) & 1u)) generator_jam_vary(g, which, generator_feel(g));
    g->jam_heard |= (uint8_t)(1u << which);
    g->section = (uint8_t)section; g->jam_count = 0; g->jam_theme = (uint8_t)which;
    g->jam_home = section == SEC_INTRO ? 0 : g->theme[which].home;   /* (the intro states the key) */
    generator_jam_apply(t, generator_jam_energy(g));
    generator_harmony_restart(t);
    g->jam_len = (uint8_t)generator_jam_len(g);
    g->jam_first = (uint8_t)generator_jam_first(t, &g->theme[GEN_SECTION_THEME[generator_jam_peek(g)]]);
}
static void generator_jam_song(track_t *t)
{
    generator_t *g = &generator[trk_index(t)];
    const gen_feel_t *f = generator_feel(g);
    uint32_t first = g->jam_started && g->jam_feel == g->p[GN_JAM] ? 1u : 0u;   /* (New idea: no second intro) */
    if (g->jam_feel != g->p[GN_JAM]) gen_evo[trk_index(t)].pop_n = 0;   /* another feel breeds from its own seeds; taste stays */
    g->jam_random = generator_entropy(g);
    memset(g->theme, 0, sizeof g->theme);
    for (uint32_t w = 0; w < 3u; w++) generator_jam_theme(t, w, f);
    memcpy(g->form, GEN_FORM[generator_word(&g->jam_random) & 7u], sizeof g->form);
    g->form_pos = 0; g->jam_cycle = 0; g->jam_heard = 0;
    g->jam_started = 1; g->jam_feel = (uint8_t)g->p[GN_JAM]; g->jam_new = 0; g->jam_skip = 0;
    generator_jam_section(t, first ? g->form[0] : SEC_INTRO, 0);
}
static void generator_jam_next(track_t *t)
{
    generator_t *g = &generator[trk_index(t)];
    if (++g->form_pos >= 8u) {
        /* Round again: VARY renews each theme with its chance, the bridge more often; a new form. */
        const gen_feel_t *f = generator_feel(g);
        uint32_t v = (uint32_t)g->p[GN_VARIETY];
        g->form_pos = 0; g->jam_cycle++;
        for (uint32_t w = 0; w < 3u; w++)
            if (generator_word(&g->jam_random) % 100u < (w == 2u ? v + (100u - v) / 2u : v)) {
                generator_jam_theme(t, w, f);
                g->jam_heard &= (uint8_t)~(1u << w);
            }
        memcpy(g->form, GEN_FORM[generator_word(&g->jam_random) & 7u], sizeof g->form);
    }
    generator_jam_section(t, g->form[g->form_pos], 1);
}
/* PLAY: a new song the first time (or after a FEEL change, or New idea), else the song
 * goes on with its next section. (A stopped Keep idea only re-reads the section.) */
static void generator_jam_play(track_t *t)
{
    generator_t *g = &generator[trk_index(t)];
    if (!g->jam_started || g->jam_feel != g->p[GN_JAM] || g->jam_new) generator_jam_song(t);
    else if (song.playing) generator_jam_next(t);
    else generator_jam_section(t, g->section, 0);
}
/* The leader's change event: the next chord, or the next section after its last. */
static void generator_jam_change(track_t *t)
{
    generator_t *g = &generator[trk_index(t)];
    if (!g->jam_started || g->jam_feel != g->p[GN_JAM] || g->jam_new) { generator_jam_song(t); return; }
    g->jam_len = (uint8_t)generator_jam_len(g);
    if (g->jam_skip) {                                  /* Less like this: renew this theme, move on */
        g->jam_skip = 0;
        generator_jam_theme(t, g->jam_theme % 3u, generator_feel(g));
        g->jam_heard &= (uint8_t)~(1u << (g->jam_theme % 3u));
        generator_jam_next(t);
        return;
    }
    if (g->jam_len && g->jam_count + 1u >= g->jam_len) { generator_jam_next(t); return; }
    g->jam_count++;
    generator_next_chord(t);
    generator_jam_apply(t, generator_jam_energy(g));   /* the macros now, alternate passes, a build */
}
/* JAM on a part: it becomes the leader (CHORD unless it has a role) and every other
 * synth track joins. Free tracks take the missing band roles in track order (BASS,
 * ARP, then an ANSWER to the arp), each in its own register around the leader's home;
 * every member follows the leader's key. Called with audio interrupts guarded. */
static void generator_jam_band(uint32_t lead)
{
    static const uint8_t ROLES[4] = {GEN_CHORD, GEN_WALK, GEN_LOOP, GEN_ANSWER};
    static const uint8_t REGISTER[5] = {0, 72, 36, 60, 67};   /* by role: the MIDI note it centres on */
    track_t *L = &trk[lead]; generator_t *gl = &generator[lead];
    if (gl->p[GN_MODE] == GEN_OFF || gl->p[GN_MODE] == GEN_ANSWER) generator_mode(L, GEN_CHORD);
    gl->p[GN_SOURCE] = 0;
    uint32_t taken = 1u << gl->p[GN_MODE], count = generator_count(L), home = generator_mod(gl->anchor, count);
    int arp = gl->p[GN_MODE] == GEN_LOOP ? (int)lead : -1;
    for (uint32_t i = 0; i < trk_n(); i++) if (i != lead && !chord_kit(&trk[i])) {
        track_t *t = &trk[i]; generator_t *g = &generator[i];
        generator_defaults(i);
        if (!generator_on(t)) {
            uint32_t role = GEN_LOOP;
            for (uint32_t r = 0; r < 4u; r++)
                if (!(taken & (1u << ROLES[r])) && (ROLES[r] != GEN_ANSWER || arp >= 0)) { role = ROLES[r]; break; }
            generator_mode(t, role);
        }
        taken |= 1u << g->p[GN_MODE];
        if (g->p[GN_MODE] == GEN_LOOP && arp < 0) arp = (int)i;
        t->p[P_SCALE] = L->p[P_SCALE]; t->p[P_ROOT] = L->p[P_ROOT];
        t->p[P_TRANS] = L->p[P_TRANS]; t->p[P_QUANT] = L->p[P_QUANT];
        /* The home pitch class in the role's register. */
        int32_t target = REGISTER[g->p[GN_MODE] % 5u], best = 0, distance = 1000;
        for (int32_t d = (int32_t)home - 6 * (int32_t)count; d <= (int32_t)home + 6 * (int32_t)count; d += (int32_t)count) {
            uint32_t note = generator_note(t, d);
            int32_t away = note == KB_SILENT ? 1000 : (int32_t)note > target ? (int32_t)note - target : target - (int32_t)note;
            if (away < distance) { best = d; distance = away; }
        }
        g->anchor = g->root = g->walk = (int16_t)best;
        g->preferred_n = 0; g->chord_n = 0; g->dirty = 1;
        g->p[GN_SOURCE] = (int16_t)(lead + 1u);
    }
    for (uint32_t i = 0; i < trk_n(); i++)
        if (i != lead && generator[i].p[GN_MODE] == GEN_ANSWER && generator_on(&trk[i]))
            generator[i].p[GN_SOURCE] = (int16_t)((arp >= 0 && (uint32_t)arp != i ? (uint32_t)arp : lead) + 1u);
}
static void generator_jam_set(uint32_t lead, uint32_t feel)
{
    generator_t *L = &generator[lead];
    uint32_t was = (uint32_t)L->p[GN_JAM];
    L->p[GN_JAM] = (int16_t)feel;
    if (!feel) {
        for (uint32_t i = 0; i < NTRK; i++) if (generator[i].jam_lead == lead + 1u) generator[i].jam_lead = 0;
        return;
    }
    if (was) return;                                /* another FEEL: a new song at the next change */
    L->jam_started = 0;
    generator_jam_band(lead);
    /* While playing, the band restarts together on the next bar line. */
    uint32_t error;
    for (uint32_t i = 0; i < trk_n(); i++)
        if (generator_on(&trk[i]) && generator_source(i, &error) == (int)lead) generator_release(&trk[i]);
}
