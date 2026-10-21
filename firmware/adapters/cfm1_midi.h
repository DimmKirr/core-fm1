/* SPDX-License-Identifier: MIT */
/* MIDI adapter: routing, learn, LED state for UI. */
#ifndef CFM1_MIDI_H
#define CFM1_MIDI_H
#include <stdint.h>

/* stub API: lineage MIDI overrides these */
static int midi_follow(void) { return 0; }
static uint32_t midi_leds(void) { return 0; }
static int midi_lit(uint32_t ch) { (void)ch; return 0; }
static int midi_note_held(void) { return 0; }
static void midi_out_event(uint32_t msg) { (void)msg; }
static void midi_pc_load(track_t *t, uint32_t pc) { (void)t; (void)pc; }
static int midi_pc_poll(uint32_t *out) { (void)out; return 0; }
static void midi_take(uint32_t ch) { (void)ch; }

#endif /* CFM1_MIDI_H */
