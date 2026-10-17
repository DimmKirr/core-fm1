/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* MPC-only octave ownership, physical button paths, held releases, and saved-state compatibility. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include "fixtures/mpc_sample_pad_notes.h"
#include <assert.h>

static void setup_mpc(void)
{
    ui_power_on();
    for (uint32_t k = 0; k < NTRK; k++) {
        set_engine_of(&trk[k], 3);
        trk[k].p[P_QUANT] = Q_MPC; trk[k].p[P_SCALE] = 1;
        trk[k].p[P_ROOT] = trk[k].p[P_TRANS] = 0;
        trk[k].p[P_MPCDEG] = 1; trk[k].p[P_CHRD] = 0;
        trk[k].p[P_AMODE] = 0; trk[k].p[P_VOICE] = V_POLY;
    }
    song.octave = 2; go_home();
}

static int gate(const track_t *t, uint32_t note)
{
    for (uint32_t k = 0; k < NVOICE; k++) if (t->v[k].active && t->v[k].gate && t->v[k].note == note) return 1;
    return 0;
}

static void buttons_and_mapping(void)
{
    setup_mpc();
    static const int8_t oct[] = {-2, -1, 1, 2};
    for (uint32_t k = 0; k < NTRK; k++) {
        song.sel = (uint8_t)k;
        for (int32_t n = 0; n < (oct[k] < 0 ? -oct[k] : oct[k]); n++) press(oct[k] < 0 ? B_OCTDN : B_OCTUP);
        assert(trk[k].mpc_octave == oct[k] && song.octave == 2);
        assert(oct_leds() == (oct[k] < 0 ? 1u : 2u));
        assert(midi_map(&trk[k], MPC_PAD_NOTES[k][1]) == (uint32_t)(60 + 12 * oct[k]));
        assert(kb_map(&trk[k], 7) == (uint32_t)(60 + 12 * oct[k]));
    }
    /* Clamps and the reset chord affect just the selected bank. */
    for (uint32_t k = 0; k < 5u; k++) press(B_OCTUP);
    assert(trk[3].mpc_octave == 3 && trk[2].mpc_octave == 1);
    fm1_in.buttons |= (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
    host_pressed |= fm1_in.buttons; frame();
    fm1_in.buttons = 0; frame();
    assert(!trk[3].mpc_octave && !oct_leds() && song.octave == 2 && trk[0].mpc_octave == -2);
    for (uint32_t k = 0; k < 5u; k++) press(B_OCTDN);
    assert(trk[3].mpc_octave == -3);
    /* The press's bank owns a deferred Stage tap even if selection changes before release. */
    song.sel = 0;
    fm1_in.buttons |= 1u << panel.btn[B_OCTUP]; host_pressed |= fm1_in.buttons; frame();
    assert(oct_deferred);
    song.sel = 1;
    fm1_in.buttons = 0; frame();
    assert(trk[0].mpc_octave == -1 && trk[1].mpc_octave == -1);
    go_title("PATTERN"); press(B_OCTDN);
    assert(ui.home && trk[1].mpc_octave == -1 && song.octave == 2);
    /* All other QNT modes retain the global input octave. */
    for (uint32_t q = Q_OFF; q < Q_MPC; q++) {
        TSEL->p[P_QUANT] = (int16_t)q; song.octave = 0;
        press(B_OCTUP);
        assert(song.octave == 1 && trk[1].mpc_octave == -1 && input_octave(TSEL) == 1);
    }
    TSEL->p[P_QUANT] = Q_MPC;
    assert(input_octave(TSEL) == -1 && oct_leds() == 1u);
    apply_preset_to(TSEL, 0);
    assert(trk[1].mpc_octave == -1);
    /* Releases use their original tones after an octave change. */
    midi_enqueue(0x09u | 0x90u << 8 | (uint32_t)MPC_PAD_NOTES[1][1] << 16 | 100u << 24, 1u);
    events_block(CTL);
    assert(gate(&trk[1], 48));
    press(B_OCTUP); assert(trk[1].mpc_octave == 0);
    midi_enqueue(0x08u | 0x80u << 8 | (uint32_t)MPC_PAD_NOTES[1][1] << 16, 1u);
    events_block(CTL);
    assert(!gate(&trk[1], 48) && !midi_owners[1]);
    puts("MPC octaves: independent buttons/LEDs/keys/pads, bounds/reset, deferred target, presets and original releases: OK");
}

static void persistence(void)
{
    static project_t p, restored;
    static project_store_t wire;
    static tmpl_t saved;
    setup_mpc();
    for (uint32_t k = 0; k < NTRK; k++) trk[k].mpc_octave = (int8_t)((int32_t)k * 2 - 3);
    project_capture(&p);
    assert(proj_pack(&wire, &p) && proj_unpack(&restored, wire.raw));
    for (uint32_t k = 0; k < NTRK; k++) assert(restored.t[k].mpc_octave == trk[k].mpc_octave);
    for (uint32_t k = 0; k < NTRK; k++) trk[k].mpc_octave = 0;
    assert(!project_restore_runtime(&restored));
    for (uint32_t k = 0; k < NTRK; k++) assert(trk[k].mpc_octave == (int32_t)k * 2 - 3);
    /* An old project's zero tail restores neutral bank octaves; malformed tagged state is rejected. */
    wire.raw[PROJ_MPC_OCT_WIRE] = wire.raw[PROJ_MPC_OCT_WIRE+1u] = 0;
    uint32_t hash = proj_hash(wire.raw, PROJ_STORE_SIZE - 4u);
    memcpy(wire.raw + PROJ_STORE_SIZE - 4u, &hash, 4);
    assert(proj_unpack(&restored, wire.raw));
    for (uint32_t k = 0; k < NTRK; k++) assert(!restored.t[k].mpc_octave);
    wire.raw[PROJ_MPC_OCT_WIRE] = 7; wire.raw[PROJ_MPC_OCT_WIRE+1u] = 0xA0;
    hash = proj_hash(wire.raw, PROJ_STORE_SIZE - 4u); memcpy(wire.raw+PROJ_STORE_SIZE-4u, &hash, 4);
    assert(!proj_unpack(&restored, wire.raw));
    template_save(); saved = tmpl;
    assert(!saved.master[17] && !saved.master[18] && !saved.master[19]); /* older readers retain the FX layout */
    assert(tmpl_blob_valid((const uint8_t *)&saved, sizeof saved));
    memset(&tmpl, 0, sizeof tmpl);
    assert(tmpl_take((const uint8_t *)&saved, sizeof saved));
    for (uint32_t k = 0; k < NTRK; k++) trk[k].mpc_octave = 0;
    template_load();
    for (uint32_t k = 0; k < NTRK; k++) assert(trk[k].mpc_octave == (int32_t)k * 2 - 3);
    saved.g[TMPL_MPC_OCT_G] = (int16_t)0xA007u;
    assert(!tmpl_blob_valid((const uint8_t *)&saved, sizeof saved));
    saved.g[TMPL_MPC_OCT_G] = 0;
    assert(tmpl_take((const uint8_t *)&saved, sizeof saved)); template_load();
    for (uint32_t k = 0; k < NTRK; k++) assert(!trk[k].mpc_octave);
    /* Removing a track carries the octave with its sound, and adding a track starts neutral. */
    for (uint32_t k = 0; k < NTRK; k++) trk[k].mpc_octave = (int8_t)k;
    assert(!track_remove(1));
    assert(trk[0].mpc_octave == 0 && trk[1].mpc_octave == 2 && trk[2].mpc_octave == 3 && trk[3].mpc_octave == 0);
    track_blank(3); assert(!trk[3].mpc_octave);
    puts("MPC octaves: project/template roundtrips, old zero fields, malformed state, and track lifecycle: OK");
}

int main(void)
{
    buttons_and_mapping();
    persistence();
    return 0;
}
