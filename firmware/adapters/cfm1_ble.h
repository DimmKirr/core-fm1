/* SPDX-License-Identifier: MIT */
/* BLE adapter: stubs when the BLE subsystem is off. */
#ifndef CFM1_BLE_H
#define CFM1_BLE_H
#include <stdint.h>

#ifndef CFM1_BLE_MIDI
static int ble_midi_ready(void) { return 0; }
static int ble_connected(void) { return 0; }
static void ble_midi_out(uint32_t msg) { (void)msg; }
static void ble_midi_poll(void) {}
static void ble_bond_poll(void) {}
#endif

#endif /* CFM1_BLE_H */
