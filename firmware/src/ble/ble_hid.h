/* SPDX-License-Identifier: GPL-3.0-only */
/* The HID over GATT client of the central role (BLE_CENTRAL; Core Specification Vol 3 Part F / G, USB HID
 * over GATT Profile 1.0): discovering an HID service (0x1812) on a peripheral (an Xbox Series X|S Wireless
 * Controller), subscribing to its input report notifications, and converting the gamepad reports to MIDI CC
 * packets injected into the firmware's MIDI input (ble_app_midi_in).
 *
 * The procedure mirrors ble_gattc.c's: one request outstanding, the 30 s transaction timeout, security
 * errors handed to ble_central.c.  After the link is up:
 *   1. Exchange MTU;
 *   2. Read By Group Type: the HID service (0x1812) -> its handle range;
 *   3. Read By Type 0x2803 in that range -> Report Map (0x2A4B), Protocol Mode (0x2A4E), HID Information
 *      (0x2A4A), HID Control Point (0x2A4C), Report (0x2A4D) characteristics;
 *   4. Read (+ Read Blob) the Report Map;
 *   5. Read Protocol Mode; write Report Protocol (0x01) if Boot Protocol;
 *   6. For each Report: Find Information for Report Reference (0x2908) and CCCD (0x2902), read the Report
 *      Reference (Report ID, Type), write CCCD = 0x0001 on input reports;
 *   7. Notifications on input report handles are decoded (Xbox Report ID 1) and converted to CC packets. */
#ifndef BLE_HID_H
#define BLE_HID_H
#include <stdint.h>
#include "ble_cfg.h"

BLE_API void ble_hid_reset(void);
BLE_API void ble_hid_start(void);
BLE_API void ble_hid_retry(void);                    /* the link is encrypted: the request that asked again */
BLE_API int ble_hid_rx(const uint8_t *p, uint16_t n); /* 1: a PDU for the HID client (responses, notifications) */
BLE_API void ble_hid_event(uint32_t now);
BLE_API int ble_hid_ready(void);

#endif
