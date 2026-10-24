/* SPDX-License-Identifier: MIT */
/* MIDI adapter: routing, learn, LED state for UI.
 * Stubs are active only when no MIDI routing lineage is configured. */
#ifndef CFM1_MIDI_H
#define CFM1_MIDI_H
#include <stdint.h>

#if !defined(CONFIG_MIDI_MELODEE) && !defined(CONFIG_MIDI_FELUCCA) && !defined(CONFIG_MIDI_SLOOP)
static int midi_follow(void) { return 0; }
static uint32_t midi_leds(void) { return 0; }
static int midi_lit(uint32_t ch) { (void)ch; return 0; }
static int midi_note_held(void) { return 0; }
#ifndef CFM1_USB_MIDI
static void midi_out_event(uint32_t msg) { (void)msg; }
#endif
static void midi_pc_load(track_t *t, uint32_t pc) { (void)t; (void)pc; }
static int midi_pc_poll(uint32_t *out) { (void)out; return 0; }
static void midi_take(uint32_t ch) { (void)ch; }
static void midi_routing_init(void) {}
static void midi_clock_init(void) {}
static void midi_clock_tick(void) {}
#endif

#endif /* CFM1_MIDI_H */
