/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Exercise the real panel, sequencer, MIDI, voices and project boundaries. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static uint32_t gen_gates(const track_t *t)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < NVOICE; i++) n += t->v[i].active && t->v[i].gate;
    return n;
}
static void gen_fixture(uint32_t mode)
{
    ui_power_on_general();
    song.ntrk = 2;
    TSEL->p[P_VOICE] = V_POLY;
    TSEL->p[P_SCALE] = 1;
    TSEL->p[P_ROOT] = 0;
    events_block(0); /* finish preset panic before testing generated ownership */
    generator_defaults(0);
    generator_mode(TSEL, mode);
    generator[0].p[GN_HARMONY] = 0;
    generator[0].p[GN_EVOLVE] = 0;
}
static int navigation(void)
{
    ui_power_on_general();
    press(B_ARP);
    int bad = check("ARP tap keeps the ordinary arp pages", cur_page()->scope == SC_TRACK && cur_page()->fam == FAM_ARP);
    hold(B_ARP);
    bad += check("ARP hold enters the JAM page without enabling or advancing on release", cur_page()->scope == SC_GEN && cur_page()->id[0] == GN_JAM && !generator_on(TSEL));
    turn(EN_SELECT, 1);
    bad += check("SELECT reaches the part's own ROLE", cur_page()->scope == SC_GEN && cur_page()->id[0] == GN_MODE);
    turn(EN_K1, 1);
    bad += check("MODE enables generation without starting transport or recording", generator_on(TSEL) && !song.playing && !song.rec);
    int16_t *value; const param_desc_t *d = page_desc(cur_page(), 1, &value);
    int valid = d->names == N_GEN_ASTYLE && value == &generator[song.sel].p[GN_ASTYLE];
    turn(EN_K1, 1); d = page_desc(cur_page(), 1, &value);
    valid &= d->names == N_GEN_BSTYLE && value == &generator[song.sel].p[GN_BSTYLE];
    turn(EN_K2, 4);
    valid &= generator[song.sel].p[GN_BSTYLE] == BS_WALK && generator[song.sel].p[GN_ASTYLE] == AS_DRIFT;
    turn(EN_K1, -1);
    bad += check("STYLE shows and keeps each role's own list", valid);
    turn(EN_SELECT, 1);
    bad += check("SELECT visits phrase controls", cur_page()->scope == SC_GEN && cur_page()->id[0] == GN_LENGTH);
    turn(EN_SELECT, 1);
    bad += check("SELECT visits harmony controls", cur_page()->id[0] == GN_HARMONY);
    turn(EN_SELECT, 1);
    bad += check("SELECT visits the evolution and expression controls", cur_page()->id[0] == GN_EVOLVE);
    press(B_HOME); hold(B_ARP);
    bad += check("holding ARP returns directly to the JAM page", cur_page()->id[0] == GN_JAM && generator_on(TSEL));
    turn(EN_SELECT, 1); turn(EN_K1, -1);
    bad += check("MODE OFF returns ownership to patterns", !generator_on(TSEL));
    press(B_ARP);
    bad += check("short ARP from generator returns to ordinary arp", cur_page()->scope == SC_TRACK && !ui.gen_open);
    set_engine_of(TSEL, ENGI_DRUM); events_block(0); hold(B_ARP); turn(EN_SELECT, 1); turn(EN_K1, 1);
    valid = !generator_on(TSEL) && msg_is("GEN NEEDS A SYNTH");
    turn(EN_SELECT, -1); turn(EN_K1, 1);
    bad += check("kits reject generation and JAM clearly", valid && !generator[song.sel].p[GN_JAM] && msg_is("GEN NEEDS A SYNTH"));
    return bad;
}
static int isolation(void)
{
    gen_fixture(GEN_LOOP);
    track_t *t = TSEL;
    step_t saved[NSTEP];
    t->step[0].time = ST_NOTE; t->step[0].n = 1; t->step[0].note[0] = 12;
    memcpy(saved, t->step, sizeof saved);
    motion_set_event(t, 0, P_PAN, 40);
    motion_store_t saved_motion = motion;
    seq_start(); seq_advance(0);
    int bad = check("generation plays without held keys and bypasses the stored pattern", generator[0].n && !t->seq_n && t->p[P_PAN] != 40);
    rec_set(t, 1); press(B_REC); rec_play(); transport_req = 0;
    input_on(t, 62, 100); input_off(t, 62);
    generator_input(t, 62);
    for (uint32_t i = 0; i < 64u; i++) seq_advance(1000);
    bad += check("REC, REC+PLAY and note input cannot record into a generating track",
        !(song.rec & 1u) && !memcmp(saved, t->step, sizeof saved) && !memcmp(&saved_motion, &motion, sizeof motion) && !recording_tracks);
    song.sel = 1; rec_set(&trk[1], 1);
    bad += check("other tracks can still arm recording", (song.rec & 2u) != 0);
    song.sel = 0; song.rec = 0;
    seq_stop();
    bad += check("STOP releases every generator-owned note", !generator[0].n && !gen_gates(t));
    generator_mode(t, GEN_OFF); seq_start(); seq_advance(0);
    bad += check("disabling resumes the original pattern from its start", t->seq_n && t->seq_notes[0] == 12 && !memcmp(saved, t->step, sizeof saved));
    seq_stop();
    return bad;
}
/* Holding REC keeps what a generating part just played, as it keeps played notes. */
static int capture(void)
{
    gen_fixture(GEN_LOOP);
    track_t *t = TSEL; generator_t *g = &generator[0];
    g->p[GN_LENGTH] = g->p[GN_FILL] = 8; g->p[GN_GATE] = 64;
    seq_start(); seq_advance(0);
    uint8_t heard[8]; memcpy(heard, g->phrase, sizeof heard);
    uint32_t bar = seq_beat_samples() * 4u;
    for (uint32_t i = 0; i < 8u; i++) seq_advance(bar / 4u);
    capture_take();
    int found = 0, other = 0;
    for (uint32_t i = 0; i < RECORD_MAX; i++) if (recording_active(t, i)) {
        int listed = 0;
        for (uint32_t j = 0; j < 8u; j++) listed |= recording[i].note == heard[j];
        found += listed; other += !listed;
    }
    int bad = check("holding REC captures the generated phrase into the pattern", found >= 8 && !other && t->seq_active);
    bad += check("capturing keeps the part generating and REC disarmed", generator_on(t) && g->running && !song.rec && !rec_on(t));
    seq_stop();
    generator_mode(t, GEN_OFF); seq_start();
    int played = 0;
    for (uint32_t q = 0; q < 2u * bar / CTL && !played; q++) {
        seq_advance(CTL);
        for (uint32_t v = 0; v < NVOICE; v++) if (t->v[v].active && t->v[v].gate)
            for (uint32_t j = 0; j < 8u; j++) played |= t->v[v].note == heard[j];
    }
    bad += check("ROLE OFF then plays the captured phrase as an ordinary pattern", played);
    seq_stop();
    return bad;
}
static int repeatability(void)
{
    int bad = 0;
    for (uint32_t mode = GEN_LOOP; mode <= GEN_CHORD; mode++) {
        gen_fixture(mode);
        generator_t *g = &generator[0];
        g->p[GN_FILL] = g->p[GN_LENGTH] = 8;
        g->p[GN_GATE] = 96;
        seq_start(); generator_tick(TSEL, 0);
        uint8_t phrase[8][8], sizes[8];
        uint32_t period = seq_div_samples((uint32_t)g->p[GN_RATE]);
        for (uint32_t i = 0; i < 8u; i++) {
            sizes[i] = g->n; memcpy(phrase[i], g->note, g->n);
            generator_tick(TSEL, period);
        }
        int same = 1;
        for (uint32_t i = 0; i < 8u; i++) {
            same &= sizes[i] == g->n && !memcmp(phrase[i], g->note, g->n);
            generator_tick(TSEL, period);
        }
        bad += check("EVOLVE zero and HOLD harmony repeat the exact phrase in each algorithm", same);
        seq_stop(); seq_start(); generator_tick(TSEL, period * 21u + 37u);
        generator_t batched = *g;
        seq_stop(); seq_start();
        uint32_t left = period * 21u + 37u;
        while (left) { uint32_t chunk = left > 137u ? 137u : left; generator_tick(TSEL, chunk); left -= chunk; }
        bad += check("seeded output and releases are identical for small blocks and MIDI-sized batches", !memcmp(&batched, g, sizeof batched));
        seq_stop();
    }
    return bad;
}
static int harmony_and_scales(void)
{
    int bad = 0, valid = 1, changed = 0;
    for (uint32_t scale = 0; scale <= 6u; scale++) {
        gen_fixture(GEN_CHORD); TSEL->p[P_SCALE] = (int16_t)scale;
        uint8_t shape[4]; generator_chord_shape(TSEL, 0, 0, shape);
        int32_t third = generator_tone(TSEL, shape[1]), fifth = generator_tone(TSEL, shape[2]);
        if (scale == 0 || scale == 1 || scale == 5) valid &= third == 40000 && fifth == 70000;
        if (scale == 2 || scale == 6) valid &= third == 30000 && fifth == 70000;
    }
    bad += check("chromatic, major/minor and pentatonic roots produce real thirds and fifths rather than ordinal clusters", valid);
    valid = 1;
    gen_fixture(GEN_CHORD);
    generator_t *g = &generator[0];
    g->p[GN_HARMONY] = 1; g->p[GN_CHANGE] = 0;
    seq_start(); generator_tick(TSEL, 0);
    for (uint32_t i = 0; i < 80u; i++) {
        int16_t root = g->root;
        generator_tick(TSEL, seq_beat_samples() * 4u);
        changed |= root != g->root;
        for (uint32_t j = 0; j < g->n; j++) valid &= (SCALE_MASK[1] >> (g->note[j] % 12u)) & 1u;
        valid &= g->n <= 8u;
    }
    bad += check("FLOAT changes chords while retaining scale membership and bounded overlap", valid && changed);
    seq_stop();
    valid = 1;
    for (uint32_t scale = 0; scale < SCALE_TOTAL; scale++) {
        TSEL->p[P_SCALE] = (int16_t)scale; TSEL->p[P_QUANT] = Q_SNAP;
        generator_mode(TSEL, GEN_CHORD);
        g->p[GN_HARMONY] = 2;
        generator_input(TSEL, micro_active(TSEL) ? 60 : 62);
        seq_start(); generator_tick(TSEL, 0);
        for (uint32_t i = 0; i < 6u; i++) {
            generator_next_chord(TSEL);
            for (uint32_t j = 0; j < g->chord_n; j++) {
                uint32_t note = generator_note(TSEL, g->chord[j]);
                valid &= note != KB_SILENT && note < 128u;
                if (micro_active(TSEL)) valid &= micro_pitch(TSEL, note) >= 0 && micro_pitch(TSEL, note) <= 1270000;
                else valid &= (scale_mask(TSEL) >> ((note + 12u - TSEL->p[P_ROOT]) % 12u)) & 1u;
            }
        }
        seq_stop();
    }
    bad += check("harmonic selection and voicing work across every built-in and microtonal scale", valid);
    valid = 1;
    for (uint32_t scale = 0; scale < SCALE_TOTAL; scale++) {
        TSEL->p[P_SCALE] = (int16_t)scale; TSEL->p[P_QUANT] = Q_SNAP;
        for (uint32_t end = 0; end < 2u; end++) {
            generator_input(TSEL, end ? 127 : 0);
            generator_input_off(TSEL, end ? 127 : 0);
            for (uint32_t j = 0; j < g->chord_n; j++) valid &= generator_note(TSEL, g->chord[j]) != KB_SILENT;
        }
    }
    bad += check("chord voicings stay usable at both MIDI/degree input limits", valid);
    return bad;
}
static int lifecycle(void)
{
    gen_fixture(GEN_CHORD);
    generator_t *g = &generator[0];
    g->p[GN_FILL] = 16; g->p[GN_GATE] = 127;
    seq_start(); seq_advance(0);
    generator_input(TSEL, 62); generator_input_off(TSEL, 62);
    g->p[GN_COLOR] = 1; generator_voicing(TSEL);
    int bad = check("a key steers the selected scale chord without adding held arp notes", g->anchor == 1 && !TSEL->nheld);
    fm1_in.notes = 1u << 11; kb_prev = 0; keyboard_block();
    bad += check("physical keys steer once without playing/recording an input chord", g->anchor == 2 && kb_chn[11] == 1 && !TSEL->nheld && !song.rec);
    fm1_in.notes = 0; keyboard_block();
    midi_event(0x90, 0, 67, 100);
    bad += check("MIDI steers through the same harmony path", g->anchor == 4);
    midi_event(0x80, 0, 67, 0);
    seq_stop(); seq_start(); seq_advance(0);
    uint32_t period = seq_div_samples((uint32_t)g->p[GN_RATE]);
    seq_advance(period);
    bad += check("CHORD retriggers one bounded voicing without accumulating older chords", g->n == 4 && gen_gates(TSEL));
    seq_stop(); project_capture(&proj_scratch);
    bad += check("project restore succeeds and turns the session-only experiment off", !project_restore_runtime(&proj_scratch) && !generator_on(TSEL));
    events_block(0);
    bad += check("project restore leaves no generated notes held", !gen_gates(TSEL) && !g->n);
    gen_fixture(GEN_LOOP); seq_start(); seq_advance(0);
    set_engine_of(TSEL, ENGI_DRUM); events_block(0);
    bad += check("switching to a kit disables generation and clears owned notes", !generator_on(TSEL) && !g->n);
    seq_stop();
    return bad;
}
static int rhythm_and_transport(void)
{
    int bad = 0, valid = 1;
    for (uint32_t length = 1; length <= 16u; length++) for (uint32_t fill = 0; fill <= 16u; fill++) {
        gen_fixture(GEN_LOOP);
        generator_t *g = &generator[0];
        g->p[GN_LENGTH] = (int16_t)length; g->p[GN_FILL] = (int16_t)fill; g->p[GN_GATE] = 96;
        seq_start(); generator_tick(TSEL, 0);
        uint32_t hits = 0, period = seq_div_samples((uint32_t)g->p[GN_RATE]);
        for (uint32_t i = 0; i < length; i++) { hits += g->n != 0; generator_tick(TSEL, period); }
        valid &= hits == (fill < length ? fill : length);
        seq_stop();
    }
    bad += check("every Euclidean length/fill produces exactly the requested hit count", valid);
    gen_fixture(GEN_LOOP);
    go_title("NOTES");
    track_select(1);
    bad += check("switching from a generator restores step-entry ownership on an ordinary track", song.seq_mode);
    track_select(0);
    bad += check("switching back protects the generating track from step entry", !song.seq_mode);
    midi_clock_mode(2);
    midi_clock_transport(0xFAu, fm1_ms); seq_advance(0);
    bad += check("external MIDI Start starts generation", song.playing && generator[0].n);
    midi_clock_transport(0xFCu, fm1_ms);
    bad += check("external MIDI Stop releases generation while retaining enablement", !song.playing && generator_on(TSEL) && !generator[0].n && !gen_gates(TSEL));
    seq_advance(10000);
    bad += check("stopped generation cannot run freely under external clock", !generator[0].n);
    midi_clock_mode(0);
    gen_fixture(GEN_OFF);
    seq_start(); rec_set(TSEL, 1); /* start a take at the next boundary */
    song.rec_wait = 0; input_on(TSEL, 60, 100); seq_advance(500);
    generator_mode(TSEL, GEN_LOOP);
    uint32_t generation = recording_generation;
    seq_advance(10000); input_on(TSEL, 64, 100); input_off(TSEL, 64);
    bad += check("enabling during a take seals/disarms it before generation, with no later writes", !song.rec && recording_generation == generation && !rec_on(TSEL));
    seq_stop();
    return bad;
}
static int phrase_contract(void)
{
    int bad = 0, valid = 1, varied = 0;
    gen_fixture(GEN_LOOP);
    generator_t *g = &generator[0];
    g->p[GN_FILL] = g->p[GN_LENGTH] = 16;
    g->p[GN_GATE] = 96; g->p[GN_EVOLVE] = 100;
    seq_start(); generator_tick(TSEL, 0);
    uint32_t period = seq_div_samples(g->rate);
    for (uint32_t cycle = 0; cycle < 128u; cycle++) {
        uint16_t before[16]; memcpy(before, g->value, sizeof before);
        generator_tick(TSEL, period * 16u);
        uint32_t edits = 0;
        for (uint32_t i = 0; i < 16u; i++) edits += before[i] != g->value[i];
        valid &= g->length == 16u && g->rhythm == 65535u;
        varied += edits;
    }
    bad += check("fresh memory evolves pitches while phrase length, exact rhythm and gates remain fixed", valid && varied > 16);
    uint32_t gate = period * 96u / 127u;
    generator_tick(TSEL, gate - 1u);
    valid = g->n == 1u && gen_gates(TSEL) == 1u;
    generator_tick(TSEL, 1u);
    valid &= !g->n && !gen_gates(TSEL);
    generator_tick(TSEL, period - gate);
    valid &= g->n == 1u && gen_gates(TSEL) == 1u;
    bad += check("a melodic gate releases at its specified time and retriggers at the next onset", valid);
    seq_stop();
    valid = 1;
    for (uint32_t mode = GEN_LOOP; mode <= GEN_WALK; mode++) {
        gen_fixture(mode); g = &generator[0];
        g->p[GN_EVOLVE] = 100; g->p[GN_GATE] = 127; g->p[GN_HARMONY] = 1; g->p[GN_CHANGE] = 0;
        seq_start(); generator_tick(TSEL, 0); period = seq_div_samples(g->rate);
        for (uint32_t i = 0; i < 32768u; i++) {
            generator_tick(TSEL, period / 2u);
            valid &= g->n <= 1u && gen_gates(TSEL) <= 1u && g->p[GN_MODE] == (int16_t)mode;
            for (uint32_t j = 0; j < g->n; j++) {
                int32_t d = generator_degree(TSEL, g->note[j]);
                valid &= d >= g->anchor && d < g->anchor + (int32_t)(generator_count(TSEL) * g->range);
                valid &= (scale_mask(TSEL) >> (g->note[j] % 12u)) & 1u;
            }
        }
        seq_stop();
    }
    bad += check("hours of simulated evolution and harmony changes cannot turn melodies into held chords or leave range", valid);
    gen_fixture(GEN_LOOP); g = &generator[0];
    g->p[GN_LENGTH] = 8; seq_start(); generator_tick(TSEL, 0);
    period = seq_div_samples(g->rate); generator_tick(TSEL, period * 3u);
    g->p[GN_LENGTH] = 13; g->p[GN_FILL] = 3; g->p[GN_RATE] = 6; g->p[GN_GATE] = 64;
    generator_tick(TSEL, period * 4u);
    valid = g->length == 8u && g->rate == 1u && g->cursor == 0;
    generator_tick(TSEL, period);
    valid &= g->length == 13u && g->fill == 3u && g->rate == 6u && g->gate_value == 64u && g->cursor == 1u;
    bad += check("length/rate/fill/gate changes commit together at the next whole phrase", valid);
    seq_stop();
    valid = 0;
    for (uint32_t seed = 1; seed <= 16u; seed++) {
        g->p[GN_SEED] = (int16_t)seed; g->p[GN_LENGTH] = g->p[GN_FILL] = 16;
        seq_start(); generator_tick(TSEL, 0);
        for (uint32_t i = 0; i < g->length; i++) {
            uint32_t is_chord = 0;
            for (uint32_t c = 0; c < g->chord_n; c++) is_chord |=
                generator_mod(generator_degree(TSEL, g->phrase[i]), generator_count(TSEL)) == generator_mod(g->chord[c], generator_count(TSEL));
            valid |= !is_chord;
        }
        seq_stop();
    }
    bad += check("melodic planning explores scale passing notes as well as chord tones", valid);
    gen_fixture(GEN_LOOP); g = &generator[0];
    g->p[GN_GATE] = 1; g->p[GN_RATE] = 3; song.g[G_BPM] = 240;
    seq_start(); generator_tick(TSEL, CTL);
    valid = g->n == 1 && gen_gates(TSEL) == 1;
    generator_tick(TSEL, CTL);
    valid &= !g->n && !gen_gates(TSEL);
    bad += check("tiny fast gates sound for a control block instead of disappearing before rendering", valid);
    seq_stop();
    return bad;
}
static int memory_contract(void)
{
    int bad = 0, valid = 1;
    gen_fixture(GEN_LOOP);
    generator_t *g = &generator[0];
    g->p[GN_LENGTH] = g->p[GN_FILL] = 16; g->p[GN_PALETTE] = 0;
    seq_start(); generator_tick(TSEL, 0);
    uint32_t period = seq_div_samples(g->rate);
    uint8_t initial[16]; uint16_t values[16];
    memcpy(initial, g->phrase, sizeof initial); memcpy(values, g->value, sizeof values);
    generator_tick(TSEL, period * 16u * 8u);
    valid &= !memcmp(initial, g->phrase, sizeof initial) && !memcmp(values, g->value, sizeof values);
    g->p[GN_RANGE] = 3; g->p[GN_PALETTE] = 100;
    generator_tick(TSEL, period * 16u);
    valid &= memcmp(initial, g->phrase, sizeof initial) != 0 && !memcmp(values, g->value, sizeof values);
    g->p[GN_RANGE] = 1; g->p[GN_PALETTE] = 0;
    generator_tick(TSEL, period * 16u);
    valid &= !memcmp(initial, g->phrase, sizeof initial) && !memcmp(values, g->value, sizeof values);
    bad += check("locked memory repeats exactly and returning COLOR/RANGE recovers the same melody", valid);
    g->p[GN_EVOLVE] = 100;
    generator_tick(TSEL, period * 16u * 3u);
    g->p[GN_EVOLVE] = 0;
    memcpy(initial, g->phrase, sizeof initial); memcpy(values, g->value, sizeof values);
    seq_stop(); seq_start(); generator_tick(TSEL, 0);
    bad += check("STOP/PLAY retains an evolved idea instead of recreating its original seed",
        !memcmp(initial, g->phrase, sizeof initial) && !memcmp(values, g->value, sizeof values));
    generator_tick(TSEL, period * 3u);
    step_t steps[NSTEP]; memcpy(steps, TSEL->step, sizeof steps);
    valid = generator_action(TSEL, GA_KEEP);
    gen_memory_t kept = g->memory;
    g->p[GN_LENGTH] = 13; g->p[GN_RATE] = 6; g->p[GN_PALETTE] = 100;
    generator_action(TSEL, GA_NEW);
    generator_tick(TSEL, period * 16u);
    valid &= g->length == 13 && memcmp(values, g->value, sizeof values) != 0;
    TSEL->p[P_SCALE] = 2; TSEL->p[P_ROOT] = 3;
    generator_tick(TSEL, 0);
    valid &= generator_action(TSEL, GA_RETURN);
    generator_tick(TSEL, seq_div_samples(g->rate) * 13u);
    valid &= !memcmp(initial, g->phrase, sizeof initial) && !memcmp(values, g->value, sizeof values) &&
        !memcmp(&kept, &g->memory, sizeof kept) && TSEL->p[P_SCALE] == 1 && TSEL->p[P_ROOT] == 0 &&
        g->length == 16 && g->rate == 1 && !memcmp(steps, TSEL->step, sizeof steps);
    bad += check("KEEP/RETURN recovers evolved memory, timing and tuning without recording a pattern", valid);
    memcpy(values, g->value, sizeof values); kept = g->memory;
    uint16_t rhythm = g->rhythm;
    valid = generator_action(TSEL, GA_REMIX);
    generator_tick(TSEL, period * 16u);
    valid &= !memcmp(&kept.loop, &g->memory.loop, sizeof kept.loop) && rhythm == g->rhythm;
    for (uint32_t i = 0; i < g->length; i++) {
        int found = 0;
        for (uint32_t j = 0; j < 16u; j++) found |= g->value[i] == kept.loop[j];
        valid &= found;
    }
    bad += check("REMIX uses only retained decisions and leaves the rhythmic phrase intact", valid);
    memcpy(values, g->value, sizeof values);
    generator_action(TSEL, GA_RHYTHM); generator_tick(TSEL, period * 16u);
    uint32_t hits = 0;
    for (uint32_t i = 0; i < g->length; i++) hits += (g->rhythm >> i) & 1u;
    bad += check("explicit rhythm remix preserves pitch memory, phrase duration and exact hit count",
        !memcmp(values, g->value, sizeof values) && hits == g->fill && g->length == 16);
    seq_stop(); project_capture(&proj_scratch); project_restore_runtime(&proj_scratch);
    bad += check("project load clears kept session ideas as well as generated note ownership", !generator_keep[0].valid);
    return bad;
}
static int palette_contract(void)
{
    gen_fixture(GEN_LOOP);
    generator_t *g = &generator[0];
    g->p[GN_PALETTE] = 0; g->p[GN_FILL] = g->p[GN_LENGTH] = 16;
    input_on(TSEL, 60, 100); input_on(TSEL, 62, 100); input_on(TSEL, 69, 100);
    input_off(TSEL, 60); input_off(TSEL, 62); input_off(TSEL, 69);
    seq_start(); generator_tick(TSEL, 0);
    int valid = g->anchor == 0 && g->preferred_n == 3 && !(g->held[0] | g->held[1] | g->held[2] | g->held[3]);
    for (uint32_t i = 0; i < g->length; i++) valid &= g->phrase[i] % 12u == 0 || g->phrase[i] % 12u == 2 || g->phrase[i] % 12u == 9;
    int bad = check("a multi-note gesture latches its preferred palette instead of overwriting the root", valid);
    input_on(TSEL, 67, 100); input_off(TSEL, 67);
    bad += check("the next first press replaces the previous palette", g->anchor == 4 && g->preferred_n == 1);
    seq_stop();
    /* Two independent sources of the same pitch: the first release must not
     * end the gesture while the second still owns that input. */
    uint32_t key = 27;
    for (uint32_t k = 0; k < 27u; k++) if (kb_map(TSEL, k) == 60u) { key = k; break; }
    valid = key < 27;
    if (key < 27) {
        fm1_in.notes = 1u << key; keyboard_block();
        midi_event(0x90, 0, 60, 100);
        fm1_in.notes = 0; keyboard_block();
        valid &= (g->held[1] & (1u << 28)) != 0;
        midi_event(0x80, 0, 60, 0);
        valid &= !(g->held[0] | g->held[1] | g->held[2] | g->held[3]);
        midi_event(0x90, 0, 62, 100);
        valid &= g->anchor == 1 && g->preferred_n == 1;
        midi_event(0x80, 0, 62, 0);
    }
    bad += check("physical and MIDI gesture ownership survives duplicate pitches and independent releases", valid);
    hold(B_ARP); press(B_OCTUP);
    valid = pop.on == POP_SHEET && pop.rows == SHEET_GENERATOR;
    press(B_OCTUP);
    valid &= generator_keep[0].valid;
    bad += check("the generator action sheet keeps an idea without REC or pattern writes", valid && !song.rec);
    pop_close();
    midi_event(0x90, 0, 64, 100);
    generator_mode(TSEL, GEN_OFF);
    trk_note_on(TSEL, 64, 100);
    midi_event(0x80, 0, 64, 0);
    bad += check("a late generator-input release cannot cut a note after returning to patterns", gen_gates(TSEL) == 1);
    trk_all_off(TSEL);
    return bad;
}
static int memory_scales_and_resume(void)
{
    int valid = 1;
    for (uint32_t scale = 0; scale < SCALE_TOTAL; scale++) for (uint32_t edge = 0; edge < 2; edge++) {
        gen_fixture(GEN_LOOP);
        TSEL->p[P_SCALE] = (int16_t)scale; TSEL->p[P_QUANT] = Q_SNAP;
        generator_t *g = &generator[0];
        g->p[GN_LENGTH] = g->p[GN_FILL] = 16; g->p[GN_RANGE] = 4; g->p[GN_PALETTE] = 100;
        generator_input(TSEL, edge ? 127 : 0); generator_input_off(TSEL, edge ? 127 : 0);
        seq_start(); generator_tick(TSEL, 0);
        for (uint32_t i = 0; i < 16u; i++) {
            uint32_t note = g->phrase[i];
            valid &= note < 128u;
            if (note >= 128u) continue;
            if (micro_active(TSEL)) valid &= micro_pitch(TSEL, note) >= 0 && micro_pitch(TSEL, note) <= 1270000;
            else valid &= (scale_mask(TSEL) >> ((note + 12u - TSEL->p[P_ROOT]) % 12u)) & 1u;
        }
        seq_stop();
    }
    int bad = check("weighted memory mapping stays playable in every scale at both input limits", valid);
    gen_fixture(GEN_LOOP);
    generator_t *g = &generator[0];
    g->p[GN_EVOLVE] = 37; g->p[GN_LENGTH] = 8;
    seq_start(); generator_tick(TSEL, 0); generator_action(TSEL, GA_KEEP);
    uint32_t period = seq_div_samples(g->rate);
    generator_tick(TSEL, period * 16u);
    uint16_t expected[16]; memcpy(expected, g->value, sizeof expected);
    uint32_t expected_rng = g->random;
    seq_stop(); generator_action(TSEL, GA_RETURN); seq_start(); generator_tick(TSEL, period * 16u);
    bad += check("RETURN restores random history so future evolution follows the kept route",
        !memcmp(expected, g->value, sizeof expected) && expected_rng == g->random);
    g->p[GN_EVOLVE] = 0;
    uint8_t first[8]; memcpy(first, g->phrase, sizeof first);
    g->p[GN_LENGTH] = 13; generator_tick(TSEL, period * 8u);
    g->p[GN_LENGTH] = 8; generator_tick(TSEL, period * 13u);
    bad += check("changing locked loop length and returning recovers its original memory window", !memcmp(first, g->phrase, sizeof first));
    g->p[GN_SEED]++;
    TSEL->p[P_SCALE] = 2;
    generator_action(TSEL, GA_KEEP);
    bad += check("KEEP captures the heard seed/tuning rather than edits awaiting their clock boundary",
        generator_keep[0].state.p[GN_SEED] == g->seed_used && generator_keep[0].state.tuning[0] == 1);
    seq_stop();
    return bad;
}
static void gen_link_fixture(void)
{
    gen_fixture(GEN_LOOP); song.ntrk = 4;
    for (uint32_t i = 0; i < 4u; i++) {
        track_t *t = &trk[i];
        set_engine_of(t, ENGI_PROPHET); t->engine = t->eng_req;
        t->p[P_SCALE] = 1; t->p[P_ROOT] = t->p[P_TRANS] = 0; t->p[P_QUANT] = Q_SNAP; t->p[P_VOICE] = V_POLY;
        generator_defaults(i); generator_mode(t, i == 1u ? GEN_CHORD : i == 2u ? GEN_WALK : GEN_LOOP);
        generator_t *g = &generator[i];
        g->p[GN_SOURCE] = i == 3u ? 0 : 4; g->p[GN_HARMONY] = i == 3u ? 2 : 0;
        g->p[GN_CHANGE] = 0; g->p[GN_LENGTH] = i == 0u ? 13 : 8;
        g->p[GN_FILL] = 5; g->p[GN_COLOR] = 3;
        generator_input(t, i == 1u ? 48 : i == 2u ? 36 : 60); generator_input_off(t, i == 1u ? 48 : i == 2u ? 36 : 60);
    }
}
static int harmonic_journeys(void)
{
    gen_fixture(GEN_CHORD);
    generator_t *g = &generator[0]; g->p[GN_HARMONY] = 2; g->p[GN_COLOR] = 3;
    seq_start(); generator_tick(TSEL, 0);
    uint32_t visited = 1, varied = 0; uint8_t first[8];
    for (uint32_t i = 0; i < 8u; i++) {
        first[i] = (uint8_t)generator_mod(g->root, 7);
        visited |= 1u << first[i]; generator_next_chord(TSEL);
    }
    int valid = generator_mod(g->root, 7) == 0 && g->chord_n == 4;
    uint32_t distinct = 0; for (uint32_t i = 0; i < 7u; i++) distinct += (visited >> i) & 1u;
    for (uint32_t seed = 1; seed <= 12u; seed++) {
        g->p[GN_SEED] = (int16_t)seed; generator_identity(g); g->root = g->anchor;
        for (uint32_t i = 0; i < 8u; i++) { varied |= generator_mod(g->root, 7) != first[i]; generator_next_chord(TSEL); }
        valid &= generator_mod(g->root, 7) == 0;
    }
    int bad = check("PATH develops across multiple chords, varies by seed, and returns home every eight changes", valid && distinct >= 5u && varied);
    seq_stop();
    gen_link_fixture(); generator[0].p[GN_LENGTH] = 3;
    seq_start(); generator_advance(0);
    uint32_t link_period = seq_div_samples(generator[3].rate);
    generator_advance(link_period); generator_action(&trk[0], GA_KEEP);
    for (uint32_t i = 0; i < 4u; i++) generator[i].p[GN_RANGE] = 2;
    generator_action(&trk[0], GA_RETURN); generator_advance(link_period * 2u);
    bad += check("an earlier follower boundary cannot partially restore or consume a queued group RETURN",
        generator[0].action == GA_RETURN && generator[3].action == GA_RETURN && generator[0].range == 2);
    generator_advance(link_period * 5u);
    int restored_together = 1;
    for (uint32_t i = 0; i < 4u; i++) restored_together &= !generator[i].action && generator[i].range == 1 && generator[i].cursor == 1;
    bad += check("the delayed group RETURN commits only when its leader reaches the boundary", restored_together);
    seq_stop();
    gen_link_fixture(); seq_start(); generator_advance(0);
    uint32_t bar = seq_beat_samples() * 4u; valid = 1;
    for (uint32_t i = 0; i < 24u; i++) {
        generator_advance(bar);
        for (uint32_t j = 0; j < 3u; j++) valid &= !generator[j].link_bad &&
            generator_mod(generator[j].root, 7) == generator_mod(generator[3].root, 7) && generator[j].source_used == 4;
    }
    bad += check("different-length parts follow a later-numbered source before timestamp onsets", valid);
    generator_t start[4]; memcpy(start, generator, sizeof start);
    generator_advance(bar * 3u + 173u);
    generator_t batched[4]; memcpy(batched, generator, sizeof batched);
    for (uint32_t i = 0; i < 4u; i++) generator_release(&trk[i]);
    memcpy(generator, start, sizeof start);
    uint32_t left = bar * 3u + 173u;
    while (left) { uint32_t chunk = left > 137u ? 137u : left; generator_advance(chunk); left -= chunk; }
    bad += check("shared harmony, gates, and all four random histories are independent of batch size", !memcmp(batched, generator, sizeof batched));
    generator_action(&trk[1], GA_KEEP);
    uint16_t saved[4][16]; for (uint32_t i = 0; i < 4u; i++) memcpy(saved[i], generator[i].value, sizeof saved[i]);
    for (uint32_t i = 0; i < 4u; i++) { generator[i].p[GN_EVOLVE] = 75; generator_action(&trk[i], GA_REMIX); }
    generator_advance(bar * 5u);
    generator_action(&trk[0], GA_RETURN);
    uint32_t until = generator[3].left + (generator[3].cursor ? (8u-generator[3].cursor) * seq_div_samples(generator[3].rate) : 0u);
    generator_advance(until); valid = 1;
    for (uint32_t i = 0; i < 4u; i++) valid &= !memcmp(saved[i], generator[i].value, sizeof saved[i]) && generator[i].cursor == 1 && !generator[i].action;
    bad += check("KEEP/RETURN restores all linked parts together at the leader boundary", valid);
    generator_mode(&trk[3], GEN_OFF); generator_advance(0);
    valid = 1; for (uint32_t i = 0; i < 3u; i++) valid &= generator[i].link_bad == 1 && !generator[i].n;
    bad += check("disabling a source releases and visibly suspends its followers", valid);
    generator_mode(&trk[3], GEN_LOOP); generator_advance(0);
    trk[1].p[P_SCALE] = 2; generator_advance(0);
    bad += check("incompatible tuning silences only the dependent part until explicitly matched", generator[1].link_bad == 2 && !generator[1].n && generator[0].running);
    generator_action(&trk[1], GA_MATCH); generator_advance(0);
    bad += check("Match source key restores a compatible linked part", !generator[1].link_bad && generator[1].running && trk[1].p[P_SCALE] == 1);
    generator[3].p[GN_SOURCE] = 1; generator_advance(0); valid = 1;
    for (uint32_t i = 0; i < 4u; i++) valid &= generator[i].link_bad == 1 && !generator[i].n;
    bad += check("source cycles terminate in bounded time and release every affected part", valid);
    generator[3].p[GN_SOURCE] = 0; generator_advance(0); valid = 1;
    for (uint32_t i = 0; i < 4u; i++) valid &= !generator[i].link_bad && generator[i].running;
    bad += check("repairing a source cycle resumes the ensemble without changing roles", valid);
    generator_advance(seq_div_samples(generator[3].rate)); generator_action(&trk[0], GA_KEEP);
    generator_action(&trk[0], GA_RETURN); generator_mode(&trk[0], GEN_OFF); generator_advance(bar * 2u);
    valid = !generator_on(&trk[0]);
    for (uint32_t i = 0; i < 4u; i++) valid &= generator[i].action != GA_RETURN;
    bad += check("disabling a part cancels its queued group RETURN instead of re-enabling it later", valid);
    seq_stop();
    return bad;
}

static int answers_and_expression(void)
{
    gen_link_fixture(); generator_t *g = &generator[0], *source = &generator[3];
    generator_mode(&trk[0], GEN_ANSWER); source->p[GN_HARMONY] = 0;
    g->p[GN_ANSWER] = 1; g->p[GN_FILL] = 13;
    seq_start(); generator_advance(0);
    int bad = check("ANSWER waits for a completed source phrase instead of inventing unrelated startup notes", g->answer_wait && !g->n);
    uint32_t period = seq_div_samples(source->rate);
    generator_advance(period * 8u);
    int valid = !g->answer_wait && source->history_n == 5u && g->n == 1;
    for (uint32_t i = 1; i < 16u; i++) {
        uint32_t at = g->answer_n - 1u - i % g->answer_n;
        int32_t degree = g->anchor + (int32_t)generator_mod(g->root + g->answer_memory[at] - g->anchor, 7);
        valid &= g->phrase[i] == generator_note(&trk[0], degree);
    }
    bad += check("ANSWER reverses only the source's heard onsets and maps them into its own current harmony/register", valid);
    generator_t saved[4]; memcpy(saved, generator, sizeof saved);
    generator_advance(period * 40u + 11u); generator_t batch[4]; memcpy(batch, generator, sizeof batch);
    for (uint32_t i = 0; i < 4u; i++) generator_release(&trk[i]); memcpy(generator, saved, sizeof saved);
    uint32_t remaining = period * 40u + 11u;
    while (remaining) { uint32_t n = remaining > 131u ? 131u : remaining; generator_advance(n); remaining -= n; }
    bad += check("related phrase histories and answer output are independent of clock batch size", !memcmp(batch, generator, sizeof batch));
    for (uint32_t style = 0; style < 6u; style++) {
        g->p[GN_ANSWER] = (int16_t)style;
        for (uint32_t cycle = 0; cycle < 24u; cycle++) {
            generator_advance(period * g->length);
            for (uint32_t i = 0; i < g->length; i++) {
                uint32_t note = g->phrase[i];
                valid &= note < 128u && ((scale_mask(&trk[0]) >> (note % 12u)) & 1u);
                if (style == 5u) {
                    uint32_t member = 0;
                    for (uint32_t c = 0; c < g->chord_n; c++) member |= generator_mod(generator_mapped_degree(&trk[0], note) - g->chord[c], 7) == 0;
                    valid &= member;
                }
            }
        }
    }
    bad += check("all answer styles stay in scale, and the harmonic companion uses only active chord tones", valid);
    g->p[GN_ANSWER] = 4; generator_advance(period * g->length * 3u);
    generator_action(&trk[0], GA_KEEP); uint8_t kept_phrase[16]; memcpy(kept_phrase, g->phrase, sizeof kept_phrase);
    generator_advance(period * 40u); generator_action(&trk[0], GA_RETURN);
    uint32_t until = source->left + (source->cursor ? (source->length - source->cursor) * period : 0u);
    generator_advance(until);
    bad += check("RETURN restores a branched answer's parent history, growth phase and heard phrase", !memcmp(kept_phrase, g->phrase, sizeof kept_phrase));
    uint8_t word[16], original[16]; uint32_t branch_valid = 1;
    static const uint8_t sizes[12] = {1,2,3,5,8,13,8,5,3,2,1,1};
    for (uint32_t i = 0; i < 120u; i++) {
        uint32_t size = generator_branch(word, i); branch_valid &= size == sizes[i % 12u] && size <= 16;
        for (uint32_t j = 0; j < size; j++) branch_valid &= word[j] <= 1u;
        if (!i) memcpy(original, word, size);
        if (!(i % 12u)) branch_valid &= !memcmp(original, word, size);
    }
    bad += check("ambient rewriting grows within thirteen symbols and retreats to its parent on schedule", branch_valid);
    seq_stop();
    valid = 1;
    for (uint32_t condition = 0; condition < 4u; condition++) {
        gen_fixture(GEN_LOOP); g = &generator[0];
        g->p[GN_CONDITION] = (int16_t)condition; g->p[GN_LENGTH] = g->p[GN_FILL] = 4;
        seq_start(); generator_tick(TSEL, 0); period = seq_div_samples(g->rate);
        for (uint32_t cycle = 0; cycle < 8u; cycle++) {
            uint32_t active = !condition || (condition == 1 && !(cycle & 1u)) || (condition == 2 && (cycle & 1u)) || (condition == 3 && (cycle & 3u) == 3u);
            for (uint32_t step = 0; step < 4u; step++) {
                valid &= (g->n != 0) == active; generator_tick(TSEL, period);
            }
        }
        seq_stop();
    }
    bad += check("ODD/EVEN/4TH conditions alternate whole phrases with exact FILL on active phrases", valid);
    gen_fixture(GEN_CHORD); g = &generator[0];
    g->p[GN_COLOR] = 3; g->p[GN_STRUM] = 80; g->p[GN_GATE] = 96;
    seq_start(); generator_tick(TSEL, 0); period = seq_div_samples(g->rate);
    uint32_t delay = g->delay[0];
    valid = g->n == 1 && g->pending_n == 3 && delay > 0;
    generator_tick(TSEL, delay); valid &= g->n == 2 && g->pending_n == 2;
    generator_tick(TSEL, period * 96u / 127u - delay); valid &= !g->n && !g->pending_n;
    bad += check("strum schedules finite chord onsets whose gates all end within the slot", valid);
    generator_tick(TSEL, period - period * 96u / 127u); seq_stop();
    generator_advance(period * 4u);
    bad += check("STOP cancels pending strum events as well as sounding notes", !g->n && !g->pending_n && !gen_gates(TSEL));
    gen_fixture(GEN_LOOP); g = &generator[0]; g->p[GN_CONDITION] = 2;
    seq_start(); generator_tick(TSEL, 0); period = seq_div_samples(g->rate);
    generator_tick(TSEL, period * 8u); generator_action(TSEL, GA_KEEP);
    generator_tick(TSEL, period * 8u); generator_action(TSEL, GA_RETURN); generator_tick(TSEL, period * 8u);
    bad += check("RETURN retains the heard conditional phase instead of muting an active kept phrase", g->n == 1 && g->condition == 2);
    seq_stop();
    return bad;
}

/* The chord roots a LOCAL part plays at each of its next `n` changes (degree classes). */
static void gen_roots(uint32_t *out, uint32_t n)
{
    generator_t *g = &generator[0];
    out[0] = generator_mod(g->root, generator_count(TSEL));
    for (uint32_t i = 1; i < n; i++) { generator_next_chord(TSEL); out[i] = generator_mod(g->root, generator_count(TSEL)); }
}
static int progressions(void)
{
    static const uint8_t pop_major[4] = {0, 4, 5, 3}, pop_minor[4] = {5, 3, 0, 4}, royal[4] = {3, 4, 2, 5};
    uint32_t roots[16];
    gen_fixture(GEN_CHORD); generator_t *g = &generator[0];
    g->p[GN_HARMONY] = GH_POP; generator_input(TSEL, 60); generator_input_off(TSEL, 60);
    seq_start(); generator_tick(TSEL, 0); gen_roots(roots, 8);
    int valid = 1;
    for (uint32_t i = 0; i < 8u; i++) valid &= roots[i] == pop_major[i % 4u];
    seq_stop();
    generator_input(TSEL, 69); generator_input_off(TSEL, 69);
    seq_start(); generator_tick(TSEL, 0); gen_roots(roots, 4);
    for (uint32_t i = 0; i < 4u; i++) valid &= roots[i] == pop_minor[i];
    int bad = check("POP plays I V vi IV from a major home and i VI III VII from a minor one", valid);
    seq_stop();
    g->p[GN_HARMONY] = GH_ROYAL; generator_input(TSEL, 60); generator_input_off(TSEL, 60);
    seq_start(); generator_tick(TSEL, 0); gen_roots(roots, 4); valid = 1;
    for (uint32_t i = 0; i < 4u; i++) valid &= roots[i] == royal[i];
    uint32_t bar = seq_beat_samples() * 4u; g->p[GN_CHANGE] = 0;
    seq_stop(); seq_start(); generator_tick(TSEL, 0);
    valid &= generator_mod(g->root, 7) == 3;
    generator_tick(TSEL, bar); valid &= generator_mod(g->root, 7) == 4;
    generator_tick(TSEL, bar); valid &= generator_mod(g->root, 7) == 2;
    bad += check("a named progression starts on its own first chord at PLAY and changes on each EVERY", valid);
    seq_stop();
    valid = 1;
    for (uint32_t scale = 0; scale < SCALE_TOTAL; scale++) {
        TSEL->p[P_SCALE] = (int16_t)scale; TSEL->p[P_QUANT] = Q_SNAP;
        if (!generator_functional(TSEL)) continue;
        for (uint32_t type = GH_MARKOV; type < GH_EVOLVE; type++) for (uint32_t home = 0; home < 7u; home++) {
            g->p[GN_HARMONY] = (int16_t)type; g->anchor = g->root = (int16_t)home; g->path_valid = 0;
            generator_identity(g);
            for (uint32_t i = 0; i < 12u; i++) {
                generator_next_chord(TSEL);
                if (type != GH_CIRCLE && type != GH_JAZZ) valid &= !generator_dim(TSEL, g->root) || generator_dim(TSEL, (int32_t)home);
                valid &= g->path_len >= 1u && g->path_len <= 16u && g->chord_n >= 3u;
                for (uint32_t j = 0; j < g->chord_n; j++) valid &= generator_note(TSEL, g->chord[j]) != KB_SILENT;
            }
        }
    }
    bad += check("named progressions and MARKOV avoid diminished stops in every seven-note scale", valid);
    TSEL->p[P_SCALE] = 1;
    valid = 1; uint32_t distinct = 0, seen = 0, varied = 0, first[8];
    for (uint32_t seed = 1; seed <= 12u; seed++) {
        g->p[GN_HARMONY] = GH_MARKOV; g->p[GN_SEED] = (int16_t)seed; g->anchor = g->root = 0; generator_identity(g);
        generator_harmony_restart(TSEL);
        for (uint32_t cycle = 0; cycle < 3u; cycle++) {
            for (uint32_t i = 0; i < 8u; i++) {
                uint32_t r = generator_mod(g->root, 7);
                seen |= 1u << r;
                if (seed == 1u && !cycle) first[i] = r; else if (!cycle) varied |= first[i] != r;
                if (i < 7u) {
                    uint32_t soon, next = generator_upcoming(g, 7, 0, &soon);
                    generator_next_chord(TSEL);
                    valid &= next == generator_mod(g->root, 7);
                } else generator_next_chord(TSEL);
            }
            valid &= generator_mod(g->root, 7) == 0;
        }
    }
    for (uint32_t i = 0; i < 7u; i++) distinct += (seen >> i) & 1u;
    bad += check("MARKOV journeys vary by seed, return home every eight changes and announce each next chord", valid && varied && distinct >= 5u);
    valid = 1;
    for (uint32_t v = 0; v < GV_COUNT; v++) {
        g->p[GN_COLOR] = (int16_t)v; g->anchor = g->root = 0; g->chord_n = 0; generator_voicing(TSEL);
        valid &= g->chord_n == generator_voices(v);
        for (uint32_t j = 1; j < g->chord_n; j++) valid &= g->chord[j] > g->chord[j - 1u];
        for (uint32_t j = 0; j < g->chord_n; j++) valid &= generator_note(TSEL, g->chord[j]) != KB_SILENT;
    }
    bad += check("every VOICING builds distinct ascending playable voices", valid);
    g->p[GN_COLOR] = GV_7TH; g->p[GN_HARMONY] = GH_CIRCLE; g->anchor = g->root = 0; g->chord_n = 0; generator_identity(g);
    generator_voicing(TSEL); valid = 1;
    for (uint32_t i = 0; i < 70u; i++) {
        generator_next_chord(TSEL);
        for (uint32_t j = 0; j < g->chord_n; j++) valid &= g->chord[j] >= -7 && g->chord[j] <= 14;
    }
    bad += check("voice leading keeps a long cycle of fifths in the played register", valid);
    return bad;
}
static uint32_t gen_onsets(uint8_t *out, uint32_t max)
{
    generator_t *g = &generator[0]; uint32_t n = 0;
    for (uint32_t i = 0; i < g->length && n < max; i++) if ((g->rhythm >> i) & 1u) out[n++] = g->phrase[i];
    return n;
}
static int styles(void)
{
    static const uint8_t expect[9][8] = {{0}, {60,64,67,60,64,67,60,64}, {67,64,60,67,64,60,67,64}, {60,64,67,64,60,64,67,64},
        {60,67,64,60,67,64,60,67}, {60,67,64,67,60,67,64,67}, {60,64,60,67,60,64,60,67}, {60,67,64,67,60,67,64,67},
        {60,64,67,64,67,60,67,60}};
    int valid = 1, bad = 0;
    for (uint32_t style = AS_UP; style <= AS_STAIRS; style++) {
        gen_fixture(GEN_LOOP); generator_t *g = &generator[0];
        g->p[GN_ASTYLE] = (int16_t)style; g->p[GN_PALETTE] = 0; g->p[GN_LENGTH] = g->p[GN_FILL] = 8;
        seq_start(); generator_tick(TSEL, 0);
        uint8_t notes[8]; uint32_t n = gen_onsets(notes, 8);
        valid &= n == 8u && !memcmp(notes, expect[style], 8);
        seq_stop();
    }
    bad += check("UP/DOWN/BOUNCE/INWARD/PINKY/THUMB/ALBERTI/STAIRS arpeggiate the chord in their orders", valid);
    valid = 1;
    for (uint32_t style = AS_DRIFT; style < AS_COUNT; style++) {
        gen_fixture(GEN_LOOP); generator_t *g = &generator[0];
        g->p[GN_ASTYLE] = (int16_t)style; g->p[GN_EVOLVE] = 60; g->p[GN_RANGE] = 3; g->p[GN_HARMONY] = GH_MARKOV; g->p[GN_CHANGE] = 0;
        g->p[GN_FILL] = 7; g->p[GN_LENGTH] = 12; g->p[GN_GATE] = 127;
        seq_start(); generator_tick(TSEL, 0); uint32_t period = seq_div_samples(g->rate);
        for (uint32_t i = 0; i < 4096u; i++) {
            generator_tick(TSEL, period / 2u);
            valid &= g->n <= 1u && gen_gates(TSEL) <= 1u;
            for (uint32_t j = 0; j < g->n; j++) {
                int32_t d = generator_degree(TSEL, g->note[j]);
                valid &= d >= g->anchor - 7 && d < g->anchor + 14 && ((scale_mask(TSEL) >> (g->note[j] % 12u)) & 1u);
            }
        }
        seq_stop();
    }
    bad += check("every ARP style holds one note in its range and scale through evolving journeys", valid);
    /* DRIFT sings: mostly steps and thirds, and the phrase ends on a chord tone. */
    uint32_t steps = 0, leaps = 0, cadences = 0, phrases = 0;
    for (uint32_t seed = 1; seed <= 24u; seed++) {
        gen_fixture(GEN_LOOP); generator_t *g = &generator[0];
        g->p[GN_SEED] = (int16_t)seed; g->p[GN_RANGE] = 2; g->p[GN_LENGTH] = g->p[GN_FILL] = 16;
        seq_start(); generator_tick(TSEL, 0);
        uint8_t notes[16]; uint32_t n = gen_onsets(notes, 16);
        for (uint32_t i = 1; i < n; i++) {
            int32_t d = generator_degree(TSEL, notes[i]) - generator_degree(TSEL, notes[i - 1u]);
            if (d < 0) d = -d;
            steps += d <= 2; leaps += d >= 5;
        }
        uint32_t last = generator_mod(generator_degree(TSEL, notes[n - 1u]), 7), member = 0;
        for (uint32_t c = 0; c < g->chord_n; c++) member |= last == generator_mod(g->chord[c], 7);
        cadences += member; phrases++;
        seq_stop();
    }
    bad += check("DRIFT melodies move mostly by step and resolve each phrase on a chord tone", steps > leaps * 4u && cadences == phrases);
    /* Bass figures. */
    static const int8_t figure[4][8] = {{0,0,0,0,0,0,0,0}, {0,7,0,7,0,7,0,7}, {0,-3,0,-3,0,-3,0,-3}, {0,2,4,7,4,2,0,2}};
    static const uint8_t ids[4] = {BS_ROOT, BS_OCTAVE, BS_FIFTH, BS_ARP};
    valid = 1;
    for (uint32_t f = 0; f < 4u; f++) {
        gen_fixture(GEN_WALK); generator_t *g = &generator[0];
        g->p[GN_BSTYLE] = ids[f]; g->p[GN_LENGTH] = g->p[GN_FILL] = 8;
        generator_input(TSEL, 36); generator_input_off(TSEL, 36);
        seq_start(); generator_tick(TSEL, 0);
        uint8_t notes[8]; uint32_t n = gen_onsets(notes, 8);
        valid &= n == 8u;
        for (uint32_t i = 0; i < 8u; i++) valid &= generator_degree(TSEL, notes[i]) - g->root == figure[f][i];
        seq_stop();
    }
    bad += check("ROOT/OCTAVE/FIFTH/ARP basses play their figures from the chord root", valid);
    gen_fixture(GEN_WALK); generator_t *g = &generator[0];
    g->p[GN_BSTYLE] = BS_WALK; g->p[GN_HARMONY] = GH_POP; g->p[GN_CHANGE] = 0;
    g->p[GN_RATE] = 0; g->p[GN_LENGTH] = g->p[GN_FILL] = 4; g->p[GN_GATE] = 110;
    generator_input(TSEL, 36); generator_input_off(TSEL, 36);
    seq_start(); generator_tick(TSEL, 0); valid = 1;
    uint32_t bar = seq_beat_samples() * 4u, beat = seq_beat_samples();
    for (uint32_t b = 0; b < 16u; b++) {
        int32_t walk[4];
        for (uint32_t i = 0; i < 4u; i++) {
            walk[i] = g->n ? generator_degree(TSEL, g->note[0]) : -100;
            if (i) { int32_t d = walk[i] - walk[i - 1u]; valid &= d != 0 && d >= -4 && d <= 4; }
            generator_tick(TSEL, beat);
        }
        int32_t next = g->n ? generator_degree(TSEL, g->note[0]) : -100, d = next - walk[3];
        valid &= generator_mod(next - g->root, 7) == 0 && (d == 1 || d == -1);
    }
    (void)bar;
    bad += check("WALK steps through the bar and approaches each next root by a scale step", valid);
    seq_stop();
    gen_fixture(GEN_CHORD); g = &generator[0];
    g->p[GN_CSTYLE] = CS_STRIDE; g->p[GN_LENGTH] = g->p[GN_FILL] = 4; g->p[GN_COLOR] = GV_TRIAD;
    seq_start(); generator_tick(TSEL, 0); valid = 1;
    uint32_t period = seq_div_samples(g->rate);
    for (uint32_t i = 0; i < 8u; i++) {
        valid &= g->n == (i & 1u ? 3u : 1u);
        if (!(i & 1u)) valid &= generator_degree(TSEL, g->note[0]) < g->chord[0] && generator_mod(generator_degree(TSEL, g->note[0]) - g->root, 7) == 0;
        generator_tick(TSEL, period);
    }
    bad += check("STRIDE alternates the root below the voicing with the chord", valid);
    seq_stop();
    return bad;
}
static void gen_jam_fixture(uint32_t feel)
{
    gen_fixture(GEN_OFF); song.ntrk = 4;
    for (uint32_t i = 0; i < 4u; i++) {
        track_t *t = &trk[i];
        set_engine_of(t, ENGI_PROPHET); t->engine = t->eng_req;
        t->p[P_SCALE] = i == 2u ? 2 : 1; t->p[P_ROOT] = t->p[P_TRANS] = 0; t->p[P_QUANT] = Q_SNAP; t->p[P_VOICE] = V_POLY;
        generator_defaults(i); generator_mode(t, GEN_OFF);
    }
    events_block(0);
    generator_input(&trk[0], 62); generator_input_off(&trk[0], 62);
    generator_jam_set(0, feel);
}
static int jam_band(void)
{
    gen_jam_fixture(GJ_POP);
    int valid = generator[0].p[GN_MODE] == GEN_CHORD && generator[1].p[GN_MODE] == GEN_WALK &&
        generator[2].p[GN_MODE] == GEN_LOOP && generator[3].p[GN_MODE] == GEN_ANSWER &&
        generator[1].p[GN_SOURCE] == 1 && generator[2].p[GN_SOURCE] == 1 && generator[3].p[GN_SOURCE] == 3 && trk[2].p[P_SCALE] == 1;
    valid &= generator_note(&trk[1], generator[1].anchor) < generator_note(&trk[0], generator[0].anchor) &&
        generator_note(&trk[0], generator[0].anchor) < generator_note(&trk[2], generator[2].anchor);
    for (uint32_t i = 1; i < 4u; i++) valid &= generator_mod(generator[i].anchor, 7) == 1;
    int bad = check("JAM forms a band: chords, bass, arp and an answer in their registers, in the leader's key", valid);
    uint32_t bar = seq_beat_samples() * 4u, sections = 0, seen = 0, silent = 0, worst = 0;
    valid = 1;
    seq_start(); generator_advance(0);
    bad += check("PLAY starts a JAM with an intro on its first chord", generator[0].section == SEC_INTRO && generator[0].jam_started);
    uint32_t last = 0xFF;
    for (uint32_t b = 0; b < 320u; b++) {
        uint32_t heard = 0;
        for (uint32_t q = 0; q < 16u; q++) {
            generator_advance(q < 15u ? bar / 16u : bar - 15u * (bar / 16u));
            for (uint32_t i = 0; i < 4u; i++) {
                generator_t *g = &generator[i];
                heard += g->n;
                valid &= g->n <= (g->p[GN_MODE] == GEN_CHORD ? 4u : 1u) && !g->link_bad;
                for (uint32_t j = 0; j < g->n; j++) valid &= (generator_mask(&trk[i]) >> (g->note[j] % 12u)) & 1u;
            }
        }
        /* Each part starts a slot on every bar line. */
        for (uint32_t i = 0; i < 4u; i++) valid &= generator[i].left == seq_div_samples(generator[i].rate) || !generator[i].running;
        silent = heard ? 0 : silent + 1; if (silent > worst) worst = silent;
        if (generator[0].section != last) { sections++; seen |= 1u << generator[0].section; last = generator[0].section; }
    }
    bad += check("an endless JAM stays bounded, in key and on the bar grid", valid);
    bad += check("JAM moves through verses, choruses and bridges without going silent", sections >= 12u && (seen & (1u << SEC_CHORUS)) &&
        (seen & (1u << SEC_VERSE)) && (seen & (1u << SEC_BRIDGE)) && worst < 2u);
    generator_t before[4]; memcpy(before, generator, sizeof before);
    static gen_evo_t evo_before[NTRK]; memcpy(evo_before, gen_evo, sizeof evo_before);
    generator_advance(bar * 7u + 333u);
    generator_t batched[4]; memcpy(batched, generator, sizeof batched);
    for (uint32_t i = 0; i < 4u; i++) generator_release(&trk[i]);
    memcpy(generator, before, sizeof before); memcpy(gen_evo, evo_before, sizeof evo_before);
    uint32_t left = bar * 7u + 333u;
    while (left) { uint32_t n = left > 173u ? 173u : left; generator_advance(n); left -= n; }
    bad += check("a JAM is identical for small blocks and MIDI-sized batches", !memcmp(batched, generator, sizeof batched));
    gen_theme_t themes[3]; memcpy(themes, generator[0].theme, sizeof themes);
    generator_action(&trk[2], GA_NEW);
    generator_advance(bar * 4u);
    bad += check("New idea on any member starts a new song", memcmp(themes, generator[0].theme, sizeof themes) != 0 && !generator[0].jam_new);
    generator[0].p[GN_VARIETY] = 0;
    generator_advance(bar * 16u); uint32_t hold = generator[0].section, form = generator[0].form_pos;
    generator_advance(bar * 64u);
    bad += check("VARIETY zero holds the current section", generator[0].section == hold && generator[0].form_pos == form);
    generator[0].p[GN_VARIETY] = 50;
    seq_stop();
    /* JAM switched on while playing: the band comes in together on the next bar line. */
    gen_jam_fixture(GJ_OFF);
    generator_mode(&trk[0], GEN_CHORD);
    seq_start(); seq_advance(0); seq_advance(bar / 3u);
    fm1_irq_off(); generator_jam_set(0, GJ_JAZZ); fm1_irq_on();
    seq_advance(bar / 3u); valid = 1;
    for (uint32_t i = 0; i < 4u; i++) valid &= !generator[i].n;
    seq_advance(bar - 2u * (bar / 3u)); valid &= generator[1].p[GN_BSTYLE] == BS_WALK || generator[1].p[GN_BSTYLE] == BS_FIFTH;
    for (uint32_t i = 0; i < 3u; i++) valid &= generator[i].n || generator[i].condition == GW_REST;
    bad += check("JAM joined mid-bar starts the whole band on the next bar line", valid);
    seq_stop();
    return bad;
}

/* The fingerprint of what the band plays: the leader's cycle and every part's settings. */
static uint32_t gen_jam_print(void)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < generator[0].path_len; i++) h = (h ^ generator[0].path[i]) * 16777619u;
    for (uint32_t tr = 0; tr < 4u; tr++) {
        const generator_t *g = &generator[tr];
        h = (h ^ (uint32_t)g->p[generator_style_id(g)]) * 16777619u; h = (h ^ (uint32_t)g->p[GN_SEED]) * 16777619u;
        h = (h ^ (uint32_t)g->p[GN_COLOR]) * 16777619u; h = (h ^ (uint32_t)g->p[GN_FILL]) * 16777619u;
    }
    return h;
}
static int jam_variety(void)
{
    uint32_t bar, songs[8], distinct = 0;
    for (uint32_t run = 0; run < 8u; run++) {
        gen_jam_fixture(GJ_POP); fm1_ms += 7919u * (run + 1u);
        generator_jam_set(0, GJ_OFF); generator_jam_set(0, GJ_POP);
        bar = seq_beat_samples() * 4u; songs[run] = 2166136261u;
        seq_start(); generator_advance(0);
        for (uint32_t b = 0; b < 32u; b++) { songs[run] = (songs[run] ^ gen_jam_print()) * 16777619u; generator_advance(bar); }
        seq_stop();
    }
    for (uint32_t i = 0; i < 8u; i++) { int seen = 0; for (uint32_t j = 0; j < i; j++) seen |= songs[i] == songs[j]; distinct += !seen; }
    int bad = check("each JAM start of the same feel writes a different song", distinct == 8u);
    gen_jam_fixture(GJ_JAZZ); bar = seq_beat_samples() * 4u;
    seq_start(); generator_advance(0);
    uint32_t prints[128], n = 0, kinds = 0, returned = 0, last_form = 0xFF, cycles[3][16], ncycle[3] = {0};
    for (uint32_t b = 0; b < 480u; b++) {
        const generator_t *g = &generator[0];
        if (!g->jam_count && g->form_pos != last_form) {
            last_form = g->form_pos;
            uint32_t print = gen_jam_print(), cycle = 2166136261u;
            for (uint32_t i = 0; i < g->path_len; i++) cycle = (cycle ^ g->path[i]) * 16777619u;
            int seen = 0; for (uint32_t i = 0; i < n; i++) seen |= prints[i] == print;
            if (!seen && n < 128u) prints[n++] = print;
            kinds++;
            uint32_t w = g->jam_theme % 3u, again = 0;
            for (uint32_t i = 0; i < ncycle[w]; i++) again |= cycles[w][i] == cycle;
            returned += again;
            if (ncycle[w] < 16u) cycles[w][ncycle[w]++] = cycle;
        }
        generator_advance(bar);
    }
    bad += check("a long JAM keeps changing its sections while themes still return", kinds >= 30u && n * 2u >= kinds && returned >= 3u);
    uint32_t form = generator[0].form_pos, section_print = gen_jam_print();
    seq_stop(); seq_start(); generator_advance(0);
    bad += check("PLAY after STOP carries the song on with its next section",
        (generator[0].form_pos != form || gen_jam_print() != section_print) && !generator[0].jam_count);
    seq_stop();
    return bad;
}

static uint32_t gen_colour(const gen_ctx_t *c, const gen_genome_t *x)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < x->len; i++) { gen_chord_t ch; gen_chord(c, x, i, &ch, 0); n += ch.mode != 0; }
    return n;
}
static int evolution(void)
{
    static const uint8_t seeds[6] = {GH_POP, GH_AXIS, GH_EVOLVE, GH_ROYAL, GH_CANON, GH_EVOLVE};
    gen_fixture(GEN_CHORD); track_t *t = TSEL; t->p[P_SCALE] = 1;
    gen_ctx_t c; gen_context(t, 0, &c);
    gen_chord_t ch[4];
    gen_genome_t x = {4, {3, 5, 6, 0}, {GM_MIX, GM_MIX, GM_MIX, 0}}, y = {4, {0, 1, 4, 5}, {0, GM_SEC, 0, 0}},
        z = {4, {0, 2, 5, 4}, {0, GM_SEC, 0, 0}}, w = {4, {0, 1, 4, 0}, {0, GM_LYDIAN, 0, 0}};
    gen_normalize(&y); gen_normalize(&z);
    int valid = 1;
    for (uint32_t i = 0; i < 3u; i++) gen_chord(&c, &x, i, &ch[i], 0);
    valid &= ch[0].root == 5 && ch[0].quality == 1 && ch[1].root == 8 && !ch[1].quality && ch[2].root == 10 && !ch[2].quality;
    gen_chord(&c, &y, 1, &ch[0], 0); gen_chord(&c, &z, 1, &ch[1], 0); gen_chord(&c, &w, 1, &ch[2], 0);
    valid &= ch[0].ok && ch[0].root == 2 && !ch[0].quality && ((ch[0].mask >> 6) & 1u);     /* V/V: D, with F# */
    valid &= ch[1].ok && ch[1].root == 4 && !ch[1].quality && ((ch[1].mask >> 8) & 1u);     /* V/vi: E, with G# */
    valid &= ch[2].ok && ch[2].root == 2 && !ch[2].quality;                                  /* lydian II: D major */
    int bad = check("chord scales give iv, bVI, bVII, II and secondary dominants with their own notes", valid);
    gen_evo_t *e = &gen_evo[0];
    uint32_t colour[2] = {0, 0}, distinct = 0, repeats = 0; valid = 1;
    gen_genome_t seen[16];
    for (uint32_t spice = 0; spice < 2u; spice++) {
        memset(e, 0, sizeof *e); e->random = 4242;
        for (uint32_t k = 0; k < 16u; k++) {
            gen_genome_t out; gen_breed(&c, e, k & 1u ? 8u : 4u, 40, spice ? 100u : 0u, 60, 0, 4, seeds, 6, &out);
            for (uint32_t i = 0; i < out.len; i++) {
                gen_chord_t a, b; gen_chord(&c, &out, i, &a, 0); gen_chord(&c, &out, (i + 1u) % out.len, &b, 0);
                valid &= a.ok && a.quality != 3u;
                if (out.mode[i] == GM_SEC) valid &= (b.root + 7u) % 12u == a.root;   /* resolves a fifth down */
            }
            colour[spice] += gen_colour(&c, &out);
            if (spice) {
                for (uint32_t a = 0; a < 3u && a < e->archive_n; a++) repeats += !gen_distance(&out, &e->archive[(e->archive_at + 7u - a) & 7u]);
                int dup = 0; for (uint32_t j = 0; j < k; j++) dup |= seen[j].len == out.len && !gen_distance(&out, &seen[j]);
                distinct += !dup; seen[k] = out;
            }
            gen_remember(&c, e, &out);
        }
    }
    bad += check("bred progressions are valid, secondary dominants resolve, and SPICE sets their colour", valid && colour[1] > colour[0] * 2u + 4u);
    bad += check("breeding never replays one of the last progressions and keeps finding new ones", !repeats && distinct >= 12u);
    gen_genome_t parent, child; uint32_t near = 1;
    gen_breed(&c, e, 8, 50, 50, 50, 0, 4, seeds, 6, &parent);
    for (uint32_t k = 0; k < 6u; k++) {
        gen_breed(&c, e, 8, 50, 50, 50, &parent, 2, seeds, 6, &child);
        uint32_t d = gen_distance(&child, &parent);
        near &= d <= 10u; parent = child;
    }
    bad += check("a developing section stays related to the progression it grows from", near);
    uint32_t trained[2] = {0, 0};
    for (uint32_t run = 0; run < 2u; run++) {
        memset(e, 0, sizeof *e); e->random = 777;
        for (uint32_t k = 0; k < 60u; k++) {
            gen_genome_t out; gen_breed(&c, e, 4, 40, 30, 50, 0, 4, seeds, 6, &out); gen_remember(&c, e, &out);
            uint32_t n = gen_colour(&c, &out);
            if (k >= 30u) trained[run] += n;
            if (run && k < 30u) { int16_t f[GEN_PFEAT]; gen_fitness(&c, e, &out, 0, 0, 0, 0, f); gen_learn(e->pref, e->mean, f, GEN_PFEAT, n ? 1 : -1); }
        }
    }
    bad += check("liking coloured progressions teaches the band to play more of them", trained[1] >= trained[0] * 2u + 8u);
    /* A linked ensemble on EVOLVE: borrowed chords reach every part with their scale. */
    gen_link_fixture(); generator[3].p[GN_HARMONY] = GH_EVOLVE; generator[3].p[GN_TENSION] = 100; generator[3].p[GN_VARIETY] = 80;
    memset(&gen_evo[3], 0, sizeof gen_evo[3]);
    seq_start(); generator_advance(0); valid = 1;
    uint32_t bar = seq_beat_samples() * 4u, borrowed = 0, cycles = 0, last = 0;
    gen_genome_t prev = {0};
    for (uint32_t b = 0; b < 96u; b++) {
        for (uint32_t q = 0; q < 8u; q++) {
            generator_advance(q < 7u ? bar / 8u : bar - 7u * (bar / 8u));
            for (uint32_t i = 0; i < 4u; i++) {
                valid &= generator[i].mask == generator[3].mask;
                for (uint32_t j = 0; j < generator[i].n; j++) valid &= (generator_mask(&trk[i]) >> ((generator[i].note[j] + 12u * 10u) % 12u)) & 1u;
            }
            borrowed += generator[3].mask != 0;
        }
        if (generator[3].path_valid && generator[3].changes != last && !generator[3].path_pos) {
            gen_genome_t now; gen_from_path(&generator[3], generator_mod(generator[3].anchor, 7), &now);
            if (prev.len && gen_distance(&now, &prev)) cycles++;
            prev = now;
        }
        last = generator[3].changes;
    }
    bad += check("EVOLVE grows a new related cycle each time and every part plays the borrowed scales", valid && borrowed > 0 && cycles >= 3u);
    seq_stop();
    /* MOTIF develops phrase by phrase, within its range, and holds with CHANGE zero. */
    gen_fixture(GEN_LOOP); generator_t *g = &generator[0];
    g->p[GN_ASTYLE] = AS_MOTIF; g->p[GN_EVOLVE] = 70; g->p[GN_LENGTH] = g->p[GN_FILL] = 8; g->p[GN_RANGE] = 2;
    seq_start(); generator_tick(TSEL, 0);
    uint32_t period = seq_div_samples(g->rate), developed = 0, related = 0;
    int8_t before[16]; memcpy(before, g->motif_rel, sizeof before); valid = g->motif_ok && g->motif_n == 8u;
    for (uint32_t p = 0; p < 40u; p++) {
        generator_tick(TSEL, period * 8u);
        if (memcmp(before, g->motif_rel, 8)) {
            uint32_t same = 0; for (uint32_t k = 0; k < 8u; k++) same += before[k] == g->motif_rel[k];
            developed++; related += same >= 1u && same <= 7u;
        }
        memcpy(before, g->motif_rel, sizeof before);
        for (uint32_t i = 0; i < 16u; i++) if ((g->rhythm >> i) & 1u) {
            int32_t d = generator_degree(TSEL, g->phrase[i]);
            valid &= d >= g->anchor - 7 && d < g->anchor + 7;
        }
    }
    g->p[GN_EVOLVE] = 0; memcpy(before, g->motif_rel, sizeof before);
    generator_tick(TSEL, period * 8u * 6u);
    bad += check("MOTIF develops a related line phrase by phrase, in range, and holds when CHANGE is zero",
        valid && developed >= 10u && related * 10u >= developed * 7u && !memcmp(before, g->motif_rel, 8));
    seq_stop();
    /* Taste from the player: More/Less like this. */
    gen_jam_fixture(GJ_POP); seq_start(); generator_advance(0);
    bar = seq_beat_samples() * 4u; generator_advance(bar * 6u);
    hold(B_ARP); press(B_OCTUP);
    valid = pop.on == POP_SHEET && pop.rows == SHEET_GENERATOR;
    uint32_t like_row = 0;
    for (uint32_t r = 0; r < pop.n; r++) if (!strcmp(SHEET_GENERATOR[r].label, "More like this")) like_row = r;
    pop_close();
    uint16_t likes = gen_evo[0].likes;
    generator_action(&trk[2], GA_LIKE);
    valid &= like_row && gen_evo[0].likes == likes + 1u;
    uint32_t section = generator[0].section, form = generator[0].form_pos;
    generator_action(&trk[1], GA_DISLIKE);
    generator_advance(bar * 2u);
    valid &= gen_evo[0].dislikes >= 1u && !generator[0].jam_skip && (generator[0].form_pos != form || generator[0].section != section);
    bad += check("More/Less like this teach the leader and Less moves the jam on at the next change", valid);
    seq_stop();
    return bad;
}

#ifndef GENERATOR_NO_MAIN
int main(void)
{
    int bad = progressions() + styles() + jam_band() + jam_variety() + evolution() + capture() + answers_and_expression() + harmonic_journeys() + navigation() + isolation() + repeatability() + harmony_and_scales() + lifecycle() + rhythm_and_transport() + phrase_contract() + memory_contract() + palette_contract() + memory_scales_and_resume();
    if (bad) { printf("generator test FAILED: %d\n", bad); return 1; }
    puts("generator test passed");
    return 0;
}
#endif
