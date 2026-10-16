/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Comparable native dry voices, a pinned upstream reference trace, performance
 * actions and a four-part ambient mix. Rendered checks do not judge musical taste. */
#define GENERATOR_NO_MAIN 1
#include "generator_test.c"

/* JAM scenes: one leader track switched to a FEEL forms the band; each part gets a
 * sound that suits its role. Native engine renders, checked for health and release;
 * the section/chord log goes to stdout for listening notes. */
static int jam_scene(const char *dir, uint32_t feel)
{
    static const char *const names[] = {"", "jam-pop", "jam-rock", "jam-funk", "jam-jazz", "jam-ballad", "jam-ambient", "jam-epic", "jam-blues"};
    /* {engine, preset} for CHORD, BASS, ARP, ANSWER */
    static const uint8_t sound[9][4][2] = {{{0}},
        {{ENGI_FM6, 0}, {ENGI_FM6, 10}, {ENGI_FM6, 7}, {ENGI_FM6, 1}},
        {{ENGI_CZ, 11}, {ENGI_CZ, 14}, {ENGI_PROPHET, 19}, {ENGI_FM6, 17}},
        {{ENGI_FM6, 13}, {ENGI_CZ, 12}, {ENGI_FM6, 7}, {ENGI_FM6, 9}},
        {{ENGI_FM6, 0}, {ENGI_CZ, 14}, {ENGI_CZ, 42}, {ENGI_FM6, 20}},
        {{ENGI_FM6, 0}, {ENGI_FM6, 10}, {ENGI_FM6, 18}, {ENGI_FM6, 20}},
        {{ENGI_FM6, 16}, {ENGI_FM6, 10}, {ENGI_FM6, 11}, {ENGI_FM6, 19}},
        {{ENGI_FM6, 15}, {ENGI_FM6, 22}, {ENGI_PROPHET, 19}, {ENGI_FM6, 9}},
        {{ENGI_FM6, 14}, {ENGI_CZ, 14}, {ENGI_CZ, 10}, {ENGI_CZ, 22}}};
    static const uint8_t bpm[9] = {0, 100, 120, 104, 132, 72, 70, 92, 96}, scale[9] = {0, 1, 2, 3, 1, 1, 1, 2, 4};
    static const uint8_t root[9] = {0, 0, 9, 2, 5, 7, 4, 2, 7}, reverb[9] = {0, 14, 8, 6, 12, 30, 42, 24, 10};
    gen_fixture(GEN_OFF); song.ntrk = 4;
    song.g[G_BPM] = bpm[feel]; song.master_q12 = 9216;
    for (uint32_t tr = 0; tr < 4u; tr++) {
        track_t *t = &trk[tr];
        set_engine_of(t, ENGI_FM6); t->engine = t->eng_req;   /* synths: the band forms on every track */
        t->p[P_SCALE] = scale[feel]; t->p[P_ROOT] = root[feel]; t->p[P_TRANS] = 0; t->p[P_QUANT] = Q_SNAP;
        generator_defaults(tr); generator_mode(t, GEN_OFF);
    }
    generator_input(&trk[0], 60u + root[feel]); generator_input_off(&trk[0], 60u + root[feel]);
    generator_jam_set(0, feel);
    for (uint32_t tr = 0; tr < 4u; tr++) {
        track_t *t = &trk[tr];
        uint32_t role = generator[tr].p[GN_MODE] == GEN_CHORD ? 0u : generator[tr].p[GN_MODE] == GEN_WALK ? 1u :
            generator[tr].p[GN_MODE] == GEN_LOOP ? 2u : 3u;
        set_engine_of(t, sound[feel][role][0]); apply_preset_to(t, sound[feel][role][1]);
        t->p[P_LEVEL] = role == 0u ? 58 : role == 1u ? 74 : role == 2u ? 56 : 50;
        if (role != 1u) t->p[P_VOICE] = V_POLY;            /* (no unison stacks: one voice a note) */
        t->p[P_FXTYPE] = FX_REVERB; t->p[P_FXMIX] = reverb[feel]; t->p[P_FXRATE] = 40; t->p[P_FXCHAR] = 48;
    }
    events_block(0);
    uint32_t play = 96u, frames = (play + 4u) * FS, peak = 0, max_gates[NTRK] = {0}, last_section = 0xFF, last_root = 0xFFFF;
    uint64_t energy = 0;
    FILE *file = 0; char path[512];
    if (dir) {
        snprintf(path, sizeof path, "%s/%s.wav", dir, names[feel]);
        file = fopen(path, "wb");
        if (!file) { perror(path); return 1; }
        wav_hdr(file, frames);
    }
    seq_start(); generator_advance(0);
    printf("  %s at %u BPM:", names[feel], bpm[feel]);
    for (uint32_t frame = 0; frame < frames; frame += CTL) {
        int32_t samples[CTL * 2u];
        if (frame >= play * FS && song.playing) seq_stop();
        mix_block(samples, CTL);
        const generator_t *lead = &generator[0];
        if (song.playing && lead->section != last_section) { printf("\n    %5.1fs %s:", (double)frame / FS, GEN_SECTION[lead->section % 5u]); last_section = lead->section; last_root = 0xFFFF; }
        if (song.playing && (uint32_t)generator_mod(lead->root, 7) != last_root) {
            static const char *const R[7] = {"I", "ii", "iii", "IV", "V", "vi", "vii"};
            last_root = generator_mod(lead->root, 7);
            printf(" %s", R[(last_root + 7u - generator_mod(lead->anchor, 7)) % 7u]);
        }
        for (uint32_t tr = 0; tr < 4u; tr++) { uint32_t held = gen_gates(&trk[tr]); if (held > max_gates[tr]) max_gates[tr] = held; }
        for (uint32_t i = 0; i < CTL && frame + i < frames; i++) {
            int32_t l = samples[2u*i], r = samples[2u*i+1u];
            uint32_t magnitude = (uint32_t)(l < 0 ? -l : l);
            if (magnitude > peak) peak = magnitude;
            energy += magnitude;
            if (file) wav_put(file, l, r);
        }
    }
    if (file) fclose(file);
    int bounded = 1;
    for (uint32_t tr = 0; tr < 4u; tr++) bounded &= max_gates[tr] <= (generator[tr].p[GN_MODE] == GEN_CHORD ? 4u : 2u) && !gen_gates(&trk[tr]);
    printf("\n  generator audio %s: peak %u, mean absolute %llu, held %u/%u/%u/%u\n", names[feel], peak,
        (unsigned long long)(energy / frames), max_gates[0], max_gates[1], max_gates[2], max_gates[3]);
    return check("a JAM band renders audibly, bounded, without clipping, and releases on STOP", energy > FS * 1000u && peak < 32768u && bounded);
}

int main(int argc, char **argv)
{
    static const char *const names[] = {"reference-dry-pluck", "memory-dry-pluck", "memory-evolving-pluck",
        "memory-dry-bass", "memory-dry-pad", "memory-ambient", "memory-performance",
        "journey-major-dry", "journey-minor-dry", "journey-ambient", "conversation-dry", "branch-ambient", "companion-ambient"};
    uint8_t reference[64][8];
    const char *reference_path = argc > 2 ? argv[2] : "build/host/generator_reference_notes.txt";
    FILE *trace = fopen(reference_path, "r");
    if (!trace) { perror(reference_path); return 1; }
    for (uint32_t i = 0; i < 64u * 8u; i++) {
        unsigned note;
        if (fscanf(trace, "%u", &note) != 1 || note < 60 || note > 72 || !(0xAB5u & (1u << (note % 12u)))) {
            fclose(trace); fputs("invalid upstream reference trace\n", stderr); return 1;
        }
        reference[i / 8u][i % 8u] = (uint8_t)note;
    }
    fclose(trace);
    int bad = 0;
    for (uint32_t scene = 0; scene < NELEM(names); scene++) {
        gen_fixture(scene == 3u ? GEN_WALK : scene == 4u ? GEN_CHORD : GEN_LOOP);
        uint32_t journey = scene >= 7u;
        song.ntrk = scene == 5u || journey ? 4u : 1u;
        song.g[G_BPM] = scene == 9u || scene >= 11u ? 72 : 90; song.master_q12 = journey ? 12288 : 4096;
        for (uint32_t tr = 0; tr < trk_n(); tr++) {
            track_t *t = &trk[tr];
            uint32_t pad = scene == 4u || ((scene == 5u || journey) && tr == 1u);
            uint32_t bass = scene == 3u || ((scene == 5u || journey) && tr == 2u);
            set_engine_of(t, pad || bass || scene == 5u || journey ? ENGI_FM6 : ENGI_PROPHET);
            apply_preset_to(t, pad ? 4u : bass ? 2u : scene == 5u || journey ? 1u : 3u);
            t->p[P_LEVEL] = scene == 5u || journey ? (pad ? 64 : bass ? 70 : 62) : 82;
            t->p[P_VOICE] = V_POLY; t->p[P_SCALE] = scene >= 8u ? 2 : 1; t->p[P_ROOT] = t->p[P_TRANS] = 0;
            t->p[P_FXTYPE] = FX_REVERB; t->p[P_FXMIX] = scene == 5u || scene == 9u || scene >= 11u ? 30 : 0;
            t->p[P_FXRATE] = 40; t->p[P_FXCHAR] = 48;
            generator_mode(t, pad ? GEN_CHORD : bass ? GEN_WALK : GEN_LOOP);
            generator_t *g = &generator[tr];
            g->p[GN_RATE] = pad ? 6 : 1; g->p[GN_LENGTH] = tr == 3u ? 13 : 8;
            g->p[GN_FILL] = pad ? 2 : bass ? 3 : tr == 3u ? 3 : 5;
            g->p[GN_GATE] = pad ? 120 : bass ? 64 : 80;
            g->p[GN_RANGE] = 1; g->p[GN_PALETTE] = bass ? 0 : 35;
            g->p[GN_EVOLVE] = scene == 2u || scene == 5u ? 15 : 0;
            g->p[GN_SEED] = (int16_t)(11 + tr * 16u); g->p[GN_COLOR] = 0;
            g->p[GN_HARMONY] = 0;
            if (journey) {
                g->p[GN_HARMONY] = tr == 3u ? 2 : 0;
                g->p[GN_SOURCE] = tr == 3u ? 0 : 4;
                g->p[GN_CHANGE] = 1; g->p[GN_COLOR] = 3;
                g->p[GN_LENGTH] = pad ? 4 : bass ? 8 : tr == 3u ? 8 : 13;
                g->p[GN_FILL] = pad ? 4 : bass ? 2 : tr == 3u ? 3 : 5;
                g->p[GN_RATE] = pad ? 6 : bass ? 0 : 1;
                g->p[GN_PALETTE] = bass ? 0 : 18;
                g->p[GN_EVOLVE] = pad || bass ? 0 : 12;
                g->p[GN_GATE] = pad ? 116 : bass ? 90 : 74;
                g->p[GN_SEED] = (int16_t)((scene == 7u ? 11 : 43) + tr * 17u);
            }
            if (scene >= 10u) {
                if (tr == 0u) {
                    generator_mode(t, GEN_ANSWER); g->p[GN_ANSWER] = scene == 10u ? 1 : scene == 11u ? 4 : 5;
                    g->p[GN_LENGTH] = scene == 10u ? 8 : 13; g->p[GN_FILL] = 4;
                    g->p[GN_CONDITION] = scene == 10u ? 2 : 0;
                }
                if (scene == 10u && tr == 3u) g->p[GN_CONDITION] = 1;
                if (pad) g->p[GN_STRUM] = 45;
            }
            generator_input(t, bass ? 36 : pad ? (journey ? 60 : 48) : tr == 3u ? 72 : 60);
            generator_input_off(t, bass ? 36 : pad ? (journey ? 60 : 48) : tr == 3u ? 72 : 60);
        }
        events_block(0); /* finish preset panics; generation begins only with PLAY */
        FILE *file = 0;
        uint32_t play_seconds = journey ? (scene == 9u || scene >= 11u ? 60u : 48u) : scene == 6u ? 24u : scene == 5u ? 20u : 12u;
        uint32_t frames = (play_seconds + 4u) * FS, peak = 0, max_gates[NTRK] = {0};
        uint64_t energy = 0;
        char path[512];
        if (argc > 1) {
            snprintf(path, sizeof path, "%s/%s.wav", argv[1], names[scene]);
            file = fopen(path, "wb");
            if (!file) { perror(path); return 1; }
            wav_hdr(file, frames);
        }
        seq_start(); generator_advance(0);
        if (!scene) memcpy(generator[0].phrase, reference[0], 8);
        uint32_t last_cycle = 0, actions = 0, last_change = 0;
        for (uint32_t frame = 0; frame < frames; frame += CTL) {
            int32_t samples[CTL * 2u];
            generator_t *g = &generator[0];
            if (scene == 6u) {
                if (frame >= 4u * FS && !(actions & 1u)) { generator_action(&trk[0], GA_KEEP); actions |= 1; }
                if (frame >= 8u * FS && !(actions & 2u)) { generator_action(&trk[0], GA_REMIX); actions |= 2; }
                if (frame >= 12u * FS && !(actions & 4u)) { g->p[GN_RANGE] = 2; g->p[GN_PALETTE] = 100; actions |= 4; }
                if (frame >= 16u * FS && !(actions & 8u)) { generator_action(&trk[0], GA_RETURN); actions |= 8; }
            }
            if (frame >= play_seconds * FS && song.playing) seq_stop();
            mix_block(samples, CTL);
            if (!scene && g->cycles != last_cycle) {
                /* Both sources explicitly anchor slot zero to C; replacing the
                 * other slots after boundary scheduling cannot change that onset. */
                last_cycle = g->cycles;
                memcpy(g->phrase, reference[g->cycles % 64u], 8);
            }
            if (journey && generator[3].changes != last_change) {
                last_change = generator[3].changes;
                printf("  %s at %.2fs: root degree %u, path %u, voices %u\n", names[scene], (double)frame/FS,
                    generator_mod(generator[3].root, 7), generator[3].path_pos, generator[1].chord_n);
            }
            for (uint32_t tr = 0; tr < trk_n(); tr++) {
                uint32_t held = gen_gates(&trk[tr]);
                if (held > max_gates[tr]) max_gates[tr] = held;
            }
            for (uint32_t i = 0; i < CTL && frame + i < frames; i++) {
                int32_t l = samples[2u*i], r = samples[2u*i+1u];
                uint32_t magnitude = (uint32_t)(l < 0 ? -l : l);
                if (magnitude > peak) peak = magnitude;
                energy += magnitude;
                if (file) wav_put(file, l, r);
            }
        }
        if (file) fclose(file);
        int bounded = 1;
        for (uint32_t tr = 0; tr < trk_n(); tr++) bounded &=
            max_gates[tr] <= (generator[tr].p[GN_MODE] == GEN_CHORD ? 4u : 1u) && !gen_gates(&trk[tr]);
        printf("generator audio %s: peak %u, mean absolute %llu, held %u/%u/%u/%u, post-STOP %u\n", names[scene], peak,
            (unsigned long long)(energy / frames), max_gates[0], max_gates[1], max_gates[2], max_gates[3], gen_gates(TSEL));
        bad += check("native memory audio is audible, bounded, and releases on STOP", energy > FS * 1000u && peak < 32768u && bounded);
    }
    for (uint32_t feel = GJ_POP; feel < GJ_COUNT; feel++) bad += jam_scene(argc > 1 ? argv[1] : 0, feel);
    return bad ? 1 : 0;
}
