/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the Xbox HID report decoder (firmware/src/ble/ble_hid.c):
 *   centered    a centered gamepad produces expected CC values
 *   extremes    stick/trigger extremes map to 0 and 127
 *   hat         all 9 hat positions map to correct d-pad CCs
 *   buttons     each button maps to its CC on/off
 *   shadow      unchanged values are not re-sent
 *   wrong_id    a report with the wrong ID is ignored */

/* --- configuration: before any header, so ble_cfg.h picks these up --- */
#define BLE_API static
#define BLE_DIAG 0
#define BLE_CENTRAL 1
#define BLE_LL_ENC 1
#define BLE_SMP_LEGACY 1

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Include the real headers (ble_hid.c includes them too, but guards prevent double inclusion) */
#include "ble.h"
#include "ble_host.h"
#include "ble_ll.h"
#include "ble_util.h"
#include "ble_diag.h"
#include "ble_hid.h"

/* BLE_CF_NO_HID: used by ble_hid.c but not in upstream Optimist's enum */
#ifndef BLE_CF_NO_HID
#define BLE_CF_NO_HID 100
#endif

/* --- stub implementations for functions ble_hid.c calls --- */

static int ble_ll_send(uint16_t cid, const uint8_t *p, uint16_t n) {
    (void)cid; (void)p; (void)n; return 1;
}
static uint32_t ble_hw_time_us(void) { return 0; }
static uint16_t ble_att_mtu(void) { return 247; }
static void ble_att_set_mtu(uint16_t m) { (void)m; }

/* capture MIDI events */
#define MAX_MIDI 512
static uint32_t midi_pkts[MAX_MIDI];
static unsigned midi_n;
static void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last_ts) {
    (void)ts; (void)last_ts;
    if (midi_n < MAX_MIDI) midi_pkts[midi_n++] = pkt;
}

/* central callbacks (forward-declared in ble_hid.c) */
static void ble_central_hid_ready(void) {}
static void ble_central_hid_fail(uint8_t why, uint8_t code) { (void)why; (void)code; }
static int ble_central_hid_auth(uint8_t code) { (void)code; return 0; }

/* --- include the source under test --- */
#include "../firmware/src/ble/ble_hid.c"

/* --- test infrastructure --- */

static int fails;
static void check(const char *what, int ok) {
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static int find_cc(uint8_t cc_num, uint8_t *val_out) {
    unsigned i;
    for (i = 0; i < midi_n; i++) {
        uint32_t p = midi_pkts[i];
        if ((p & 0xFF) == 0x0Bu && ((p >> 8) & 0xF0u) == 0xB0u &&
            ((p >> 16) & 0x7Fu) == cc_num) {
            *val_out = (uint8_t)(p >> 24);
            return 1;
        }
    }
    return 0;
}

static void make_report(uint8_t *rpt, uint16_t lx, uint16_t ly,
                        uint16_t rx, uint16_t rz, uint16_t lt, uint16_t rt,
                        uint8_t hat, uint16_t buttons) {
    memset(rpt, 0, 16);
    rpt[0] = (uint8_t)lx; rpt[1] = (uint8_t)(lx >> 8);
    rpt[2] = (uint8_t)ly; rpt[3] = (uint8_t)(ly >> 8);
    rpt[4] = (uint8_t)rx; rpt[5] = (uint8_t)(rx >> 8);
    rpt[6] = (uint8_t)rz; rpt[7] = (uint8_t)(rz >> 8);
    rpt[8] = (uint8_t)lt; rpt[9] = (uint8_t)(lt >> 8);
    rpt[10] = (uint8_t)rt; rpt[11] = (uint8_t)(rt >> 8);
    rpt[12] = hat;
    rpt[13] = (uint8_t)buttons; rpt[14] = (uint8_t)(buttons >> 8);
}

static void setup_hid(void) {
    memset(&bhid, 0, sizeof bhid);
    bhid.st = H_READY;
    bhid.rpt_n = 1;
    bhid.rpt[0].val_h = 0x0020;
    bhid.rpt[0].report_id = 1;
    bhid.rpt[0].report_type = 1;
    bhid.rpt[0].subscribed = 1;
    bhid.shadow_valid = 0;
}

/* --- tests --- */

static void test_centered(void) {
    uint8_t rpt[16], val;
    setup_hid();
    midi_n = 0;
    make_report(rpt, 0x8000, 0x8000, 0x8000, 0x8000, 0, 0, 0, 0);
    hid_report_in(0x0020, rpt, 16);
    check("centered: LX CC85 = 64", find_cc(85, &val) && val == 64);
    check("centered: LT CC89 = 0", find_cc(89, &val) && val == 0);
    check("centered: RT CC90 = 0", find_cc(90, &val) && val == 0);
    check("centered: hat center -> DU CC113 = 0", find_cc(113, &val) && val == 0);
    check("centered: no buttons -> A CC102 = 0", find_cc(102, &val) && val == 0);
}

static void test_extremes(void) {
    uint8_t rpt[16], val;
    setup_hid();
    midi_n = 0;
    make_report(rpt, 0xFFFF, 0x0000, 0xFFFF, 0x0000, 0x03FF, 0x03FF, 1, 0xFFFF);
    hid_report_in(0x0020, rpt, 16);
    check("max LX -> CC85 = 127", find_cc(85, &val) && val == 127);
    check("min LY -> CC86 = 127", find_cc(86, &val) && val == 127);
    check("max LT -> CC89 = 127", find_cc(89, &val) && val == 127);
    check("max RT -> CC90 = 127", find_cc(90, &val) && val == 127);
    check("hat 1 (N) -> DU CC113 = 127", find_cc(113, &val) && val == 127);
    check("all buttons -> A CC102 = 127", find_cc(102, &val) && val == 127);
    check("all buttons -> B CC103 = 127", find_cc(103, &val) && val == 127);
}

static void test_hat_positions(void) {
    uint8_t rpt[16], val;
    uint8_t hat;
    /* HID_HAT_MAP: 0=center, 1=N, 2=NE, 3=E, 4=SE, 5=S, 6=SW, 7=W, 8=NW */
    for (hat = 0; hat <= 8; hat++) {
        setup_hid();
        midi_n = 0;
        make_report(rpt, 0x8000, 0x8000, 0x8000, 0x8000, 0, 0, hat, 0);
        hid_report_in(0x0020, rpt, 16);
        if (hat == 0) { /* center */
            check("hat 0 (center): all d-pad = 0",
                  find_cc(113, &val) && val == 0 &&
                  find_cc(114, &val) && val == 0 &&
                  find_cc(115, &val) && val == 0 &&
                  find_cc(116, &val) && val == 0);
        } else if (hat == 1) { /* N */
            check("hat 1 (N): DU=127 DD=0",
                  find_cc(113, &val) && val == 127 &&
                  find_cc(114, &val) && val == 0);
        } else if (hat == 5) { /* S */
            check("hat 5 (S): DU=0 DD=127",
                  find_cc(113, &val) && val == 0 &&
                  find_cc(114, &val) && val == 127);
        }
    }
}

static void test_shadow(void) {
    uint8_t rpt[16];
    setup_hid();
    midi_n = 0;
    make_report(rpt, 0x8000, 0x8000, 0x8000, 0x8000, 0, 0, 0, 0);
    hid_report_in(0x0020, rpt, 16);
    midi_n = 0;
    hid_report_in(0x0020, rpt, 16);
    check("shadow: identical report produces no new CC events", midi_n == 0);
    midi_n = 0;
    make_report(rpt, 0x9000, 0x8000, 0x8000, 0x8000, 0, 0, 8, 0);
    hid_report_in(0x0020, rpt, 16);
    check("shadow: changed LX produces events", midi_n >= 1);
}

static void test_wrong_report_id(void) {
    uint8_t rpt[16];
    setup_hid();
    bhid.rpt[0].report_id = 2;
    midi_n = 0;
    make_report(rpt, 0x8000, 0x8000, 0x8000, 0x8000, 0, 0, 0, 0);
    hid_report_in(0x0020, rpt, 16);
    check("report ID 2 (not Xbox): ignored", midi_n == 0);
}

static void test_short_report(void) {
    uint8_t rpt[8];
    setup_hid();
    midi_n = 0;
    memset(rpt, 0, 8);
    hid_report_in(0x0020, rpt, 8);
    check("short report (< 16 bytes): ignored", midi_n == 0);
}

static void test_unknown_handle(void) {
    uint8_t rpt[16];
    setup_hid();
    midi_n = 0;
    make_report(rpt, 0x8000, 0x8000, 0x8000, 0x8000, 0, 0, 0, 0);
    hid_report_in(0x9999, rpt, 16);
    check("unknown handle: ignored", midi_n == 0);
}

int main(void) {
    test_centered();
    test_extremes();
    test_hat_positions();
    test_shadow();
    test_wrong_report_id();
    test_short_report();
    test_unknown_handle();
    printf("%s\n", fails ? "ble_hid: FAILED" : "ble_hid: all passed");
    return fails != 0;
}
