/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* JAM: an endless backing band, original Melodee work. The leader (a LOCAL source)
 * writes a song form from three themes of its FEEL (verse, chorus, bridge), each a
 * progression, a change rate and the parts' styles. Sections last whole progression
 * cycles; their energy, VARIETY's renewal of themes and a build into each chorus keep
 * it moving, while a returning chorus keeps its seed and so its melody. Every linked
 * part derives rate, length, fill, gate, register, voicing and dynamics from its role,
 * the theme and three macros (ENERGY, TENSION, VARIETY). The director only writes
 * ordinary generator settings, so each change commits at the part's own phrase
 * boundary and shows on its knobs; JAM OFF hands the settings back to the player. */
enum { SEC_INTRO, SEC_VERSE, SEC_CHORUS, SEC_BRIDGE, SEC_BREAK };
static const char *const GEN_SECTION[] = {"INTRO", "VERSE", "CHORUS", "BRIDGE", "BREAK"};
static const int8_t GEN_SECTION_LIFT[5] = {-35, -10, 18, 0, -30};
static const uint8_t GEN_SECTION_THEME[5] = {0, 0, 1, 2, 1};
static const uint8_t GEN_FORM[4][8] = {
    {SEC_VERSE, SEC_VERSE, SEC_CHORUS, SEC_VERSE, SEC_CHORUS, SEC_BRIDGE, SEC_CHORUS, SEC_CHORUS},
    {SEC_VERSE, SEC_CHORUS, SEC_VERSE, SEC_CHORUS, SEC_BREAK, SEC_BRIDGE, SEC_CHORUS, SEC_CHORUS},
    {SEC_VERSE, SEC_VERSE, SEC_CHORUS, SEC_CHORUS, SEC_BRIDGE, SEC_BREAK, SEC_CHORUS, SEC_VERSE},
    {SEC_VERSE, SEC_CHORUS, SEC_CHORUS, SEC_BRIDGE, SEC_VERSE, SEC_CHORUS, SEC_BREAK, SEC_CHORUS}};
/* Voicing families by TENSION: bright, jazz, wide. */
static const uint8_t GEN_VOICING[3][4] = {
    {GV_TRIAD, GV_ADD9, GV_7TH, GV_69},
    {GV_SHELL, GV_7TH, GV_911, GV_69},
    {GV_OPEN, GV_ADD9, GV_SUS2, GV_QUARTAL}};
typedef struct {
    uint8_t harmony[4], arp[4], bass[4], chord[2], answer[2];
    uint8_t pulse, chord_rate, every, gate, strum, voicing;   /* RATE ids; EVERY id; GATE; ms; family */
} gen_feel_t;
static const gen_feel_t GEN_FEEL[GJ_COUNT - 1] = {
    /* POP */     {{GH_POP, GH_AXIS, GH_DOOWOP, GH_ROYAL}, {AS_UP, AS_BOUNCE, AS_DRIFT, AS_PINKY},
                   {BS_ROOT, BS_OCTAVE, BS_FIFTH, BS_ROOT}, {CS_BLOCK, CS_BLOCK}, {0, 2}, 1, 0, 0, 80, 10, 0},
    /* ROCK */    {{GH_VAMP, GH_RISE, GH_POP, GH_FALL}, {AS_UP, AS_THUMB, AS_DRIFT, AS_STAIRS},
                   {BS_ROOT, BS_ROOT, BS_OCTAVE, BS_FIFTH}, {CS_BLOCK, CS_BLOCK}, {0, 3}, 1, 1, 0, 70, 14, 0},
    /* FUNK */    {{GH_VAMP, GH_HOLD, GH_JAZZ, GH_MARKOV}, {AS_DRIFT, AS_THUMB, AS_PINKY, AS_STAIRS},
                   {BS_OCTAVE, BS_ROOT, BS_DRIFT, BS_OCTAVE}, {CS_BLOCK, CS_BLOCK}, {2, 0}, 2, 2, 1, 50, 4, 1},
    /* JAZZ */    {{GH_JAZZ, GH_CIRCLE, GH_MARKOV, GH_JAZZ}, {AS_DRIFT, AS_DRIFT, AS_STAIRS, AS_UP},
                   {BS_WALK, BS_WALK, BS_WALK, BS_WALK}, {CS_BLOCK, CS_STRIDE}, {0, 5}, 4, 4, 0, 64, 0, 1},
    /* BALLAD */  {{GH_CANON, GH_FALL, GH_DOOWOP, GH_ROYAL}, {AS_ALBERTI, AS_UP, AS_BOUNCE, AS_DRIFT},
                   {BS_FIFTH, BS_ARP, BS_ROOT, BS_FIFTH}, {CS_BLOCK, CS_BLOCK}, {5, 0}, 1, 6, 0, 96, 35, 2},
    /* AMBIENT */ {{GH_PATH, GH_NEAR, GH_MARKOV, GH_NEAR}, {AS_DRIFT, AS_INWARD, AS_DRIFT, AS_UP},
                   {BS_PEDAL, BS_PEDAL, BS_DRIFT, BS_PEDAL}, {CS_BLOCK, CS_BLOCK}, {5, 4}, 1, 7, 1, 110, 60, 2},
    /* EPIC */    {{GH_POP, GH_RISE, GH_FALL, GH_ROYAL}, {AS_UP, AS_BOUNCE, AS_INWARD, AS_STAIRS},
                   {BS_OCTAVE, BS_ROOT, BS_OCTAVE, BS_ROOT}, {CS_BLOCK, CS_BLOCK}, {0, 2}, 2, 0, 0, 72, 6, 2},
    /* BLUES */   {{GH_BLUES, GH_BLUES, GH_VAMP, GH_JAZZ}, {AS_DRIFT, AS_DRIFT, AS_THUMB, AS_STAIRS},
                   {BS_BOOGIE, BS_WALK, BS_BOOGIE, BS_WALK}, {CS_STRIDE, CS_BLOCK}, {0, 1}, 4, 4, 0, 80, 12, 1}};

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
/* One part's settings for the section: everything but its role, register and sound. */
static void generator_jam_part(uint32_t tr, uint32_t lead, uint32_t e)
{
    generator_t *g = &generator[tr], *L = &generator[lead];
    const gen_feel_t *f = generator_feel(L);
    const gen_theme_t *th = &L->theme[GEN_SECTION_THEME[L->section % 5u]];
    uint32_t x = (uint32_t)L->p[GN_TENSION], v = (uint32_t)L->p[GN_VARIETY], sec = L->section, rest = 0;
    uint32_t bars_max = tr == lead ? 1u << th->every : 2u, rate, length, density;
    int32_t level = 60 + (int32_t)e * 45 / 100;
    int16_t *p = g->p;
    g->jam_lead = (uint8_t)(lead + 1u);
    p[GN_SEED] = (int16_t)(1u + (th->seed + tr * 17u) % 127u);
    p[GN_DYNAMICS] = GD_HUMAN; p[GN_STRUM] = 0; p[GN_RANGE] = 1;
    p[GN_PALETTE] = (int16_t)(10u + x * 3u / 5u);
    p[GN_COLOR] = GEN_VOICING[f->voicing][x * 4u / 101u];
    switch (g->p[GN_MODE]) {
    case GEN_WALK: {
        uint32_t style = th->bstyle;
        p[GN_BSTYLE] = (int16_t)style;
        rate = style == BS_WALK || style == BS_FIFTH ? 0u : style == BS_PEDAL ? (e < 50u ? 6u : 0u) :
            style == BS_ARP ? 1u : f->pulse == 4u ? 4u : e < 25u ? 0u : f->pulse;
        length = generator_jam_length(rate, 1u);
        density = style == BS_WALK || style == BS_FIFTH || style == BS_PEDAL ? 100u :
            style == BS_BOOGIE ? (rate == 4u ? 67u : 100u) : 45u + e * 55u / 100u;
        p[GN_GATE] = (int16_t)(style == BS_WALK ? 100 : style == BS_PEDAL ? 122 : style == BS_FIFTH ? 90 : f->gate - 6);
        p[GN_PALETTE] = 0; p[GN_EVOLVE] = (int16_t)(v / 6u); p[GN_DYNAMICS] = GD_PHRASE;
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
        else if (rate == 0u && e >= 75u && f->voicing != 1u) rate = 1u;
        length = generator_jam_length(rate, bars_max);
        density = rate >= 6u ? 100u : rate == 0u ? (e < 35u ? 50u : 100u) : rate == 2u ? 20u + e * 30u / 100u :
            rate == 4u ? (th->cstyle == CS_STRIDE ? 67u : 17u + e * 25u / 100u) : 50u + e / 2u;
        p[GN_GATE] = (int16_t)(rate >= 6u ? 124 : rate == 2u ? 34 : rate == 4u ? 64 : rate == 1u ? 72 : e > 70u ? 84 : 104);
        p[GN_STRUM] = f->strum; p[GN_DYNAMICS] = GD_PHRASE; p[GN_EVOLVE] = 0;
        level -= 6;
        /* Comping rhythms (funk sixteenths, jazz triplets) take the theme's rotation. */
        g->rhythm_shift = (uint8_t)(rate == 2u || (rate == 4u && th->cstyle != CS_STRIDE) ? th->shift % length : 0u);
        break;
    case GEN_ANSWER:
        p[GN_ANSWER] = th->answer;
        rate = f->pulse; length = generator_jam_length(rate, 2u); density = 30u + e * 40u / 100u;
        p[GN_GATE] = f->gate; p[GN_EVOLVE] = 0; p[GN_RANGE] = (int16_t)(1u + (e >= 60u));
        rest = sec == SEC_INTRO || sec == SEC_BREAK || (sec == SEC_VERSE && e < 50u);
        g->rhythm_shift = 0;
        break;
    default:
        p[GN_ASTYLE] = th->astyle;
        rate = f->pulse;
        if (rate == 1u && e >= 72u && f->voicing != 1u) rate = 2u;
        else if (rate == 1u && e < 20u) rate = 0u;
        else if (rate == 2u && e < 30u) rate = 1u;
        length = generator_jam_length(rate, bars_max);
        density = th->astyle == AS_DRIFT ? 35u + e * 65u / 100u : 55u + e * 45u / 100u;
        p[GN_GATE] = (int16_t)(f->gate - e / 6u);
        p[GN_RANGE] = (int16_t)(1u + (e >= 55u) + (f->voicing == 2u && e >= 35u));
        p[GN_EVOLVE] = (int16_t)(th->astyle == AS_DRIFT ? 5u + v / 3u : v / 5u);
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
static void generator_jam_theme(generator_t *g, uint32_t which, const gen_feel_t *f)
{
    gen_theme_t *th = &g->theme[which];
    uint32_t r = generator_word(&g->jam_random);
    th->harmony = f->harmony[r & 3u];
    for (uint32_t i = 1; i < 4u && which && th->harmony == g->theme[which - 1u].harmony; i++)
        th->harmony = f->harmony[(r + i) & 3u];
    th->every = (uint8_t)(f->every + (which == 2u && ((r >> 3) & 1u) && f->every < 2u));
    th->astyle = f->arp[(r >> 5) & 3u]; th->bstyle = f->bass[(r >> 7) & 3u];
    th->cstyle = f->chord[(r >> 9) & 1u]; th->answer = f->answer[(r >> 10) & 1u];
    th->seed = (uint8_t)(1u + (r >> 11) % 127u); th->shift = (uint8_t)((r >> 20) & 15u);
}
static uint32_t generator_jam_peek(const generator_t *g) { return g->form_pos < 7u ? g->form[g->form_pos + 1u] : SEC_VERSE; }
/* Section length in chord changes: about 4, 8 or 16 bars (VARIETY high to low), whole
 * cycles of a named progression, an even number of bars so two-bar phrases stay
 * aligned. A journey (PATH, NEAR, MARKOV) may stop anywhere: the next section starts
 * from its own first chord. VARIETY zero keeps the current section forever. */
static uint32_t generator_jam_len(const generator_t *g)
{
    uint32_t v = (uint32_t)g->p[GN_VARIETY];
    if (!v) return 0;
    uint32_t bars = v < 34u ? 16u : v < 67u ? 8u : 4u, every = 1u << g->p[GN_CHANGE];
    if (g->section == SEC_INTRO || g->section == SEC_BREAK) bars /= 2u;
    uint32_t len = generator_template(&trk[g - generator], g->harmony_used) && g->path_len ? g->path_len : 1u;
    uint32_t changes = (bars + every - 1u) / every;
    if (changes < len) changes = len;
    changes = (changes + len - 1u) / len * len;
    if (every == 1u && (changes & 1u)) changes += len;
    return changes > 48u ? 48u : changes;
}
/* Section energy: ENERGY, the section's lift and a build through the second half of
 * a section that leads into a chorus. */
static uint32_t generator_jam_energy(const generator_t *g)
{
    int32_t e = g->p[GN_ENERGY] + GEN_SECTION_LIFT[g->section % 5u];
    if (generator_jam_peek(g) == SEC_CHORUS && g->section != SEC_CHORUS && g->jam_len >= 2u && g->jam_count >= g->jam_len / 2u)
        e += 24 * (int32_t)(g->jam_count - g->jam_len / 2u + 1u) / (int32_t)(g->jam_len - g->jam_len / 2u);
    return (uint32_t)(e < 0 ? 0 : e > 100 ? 100 : e);
}
static void generator_jam_section(track_t *t, uint32_t section)
{
    generator_t *g = &generator[trk_index(t)];
    g->section = (uint8_t)section; g->jam_count = 0;
    generator_jam_apply(t, generator_jam_energy(g));
    generator_harmony_restart(t);
    g->jam_len = (uint8_t)generator_jam_len(g);
    uint32_t count = generator_count(t), next = GEN_SECTION_THEME[generator_jam_peek(g)];
    g->jam_first = (uint8_t)generator_first_chord(t, g->theme[next].harmony, generator_mod(g->anchor, count));
}
static void generator_jam_song(track_t *t)
{
    generator_t *g = &generator[trk_index(t)];
    const gen_feel_t *f = generator_feel(g);
    if (!g->jam_started || g->jam_feel != g->p[GN_JAM]) {
        g->jam_random = ((uint32_t)g->p[GN_SEED] * 2654435761u) ^ ((uint32_t)g->p[GN_JAM] * 40503u) ^ 0x51ED270Bu;
        if (!g->jam_random) g->jam_random = 1;
    }
    for (uint32_t w = 0; w < 3u; w++) generator_jam_theme(g, w, f);
    memcpy(g->form, GEN_FORM[generator_word(&g->jam_random) & 3u], sizeof g->form);
    g->form_pos = 0; g->jam_cycle = 0;
    uint32_t first = g->jam_started ? g->form[0] : SEC_INTRO;
    g->jam_started = 1; g->jam_feel = (uint8_t)g->p[GN_JAM]; g->jam_new = 0;
    generator_jam_section(t, first);
}
static void generator_jam_next(track_t *t)
{
    generator_t *g = &generator[trk_index(t)];
    if (++g->form_pos >= 8u) {
        /* Round again: VARIETY renews each theme with its chance; a new form. */
        const gen_feel_t *f = generator_feel(g);
        g->form_pos = 0; g->jam_cycle++;
        for (uint32_t w = 0; w < 3u; w++)
            if (generator_word(&g->jam_random) % 100u < (uint32_t)g->p[GN_VARIETY]) generator_jam_theme(g, w, f);
        memcpy(g->form, GEN_FORM[generator_word(&g->jam_random) & 3u], sizeof g->form);
    }
    generator_jam_section(t, g->form[g->form_pos]);
}
/* PLAY: a new song the first time (or after a FEEL change), else the section again. */
static void generator_jam_play(track_t *t)
{
    generator_t *g = &generator[trk_index(t)];
    if (!g->jam_started || g->jam_feel != g->p[GN_JAM] || g->jam_new) generator_jam_song(t);
    else generator_jam_section(t, g->section);
}
/* The leader's change event: the next chord, or the next section after its last. */
static void generator_jam_change(track_t *t)
{
    generator_t *g = &generator[trk_index(t)];
    if (!g->jam_started || g->jam_feel != g->p[GN_JAM] || g->jam_new) { generator_jam_song(t); return; }
    g->jam_len = (uint8_t)generator_jam_len(g);
    if (g->jam_len && g->jam_count + 1u >= g->jam_len) { generator_jam_next(t); return; }
    g->jam_count++;
    generator_next_chord(t);
    generator_jam_apply(t, generator_jam_energy(g));   /* the macros now, a build */
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
