/* SPDX-License-Identifier: GPL-3.0-only */
/* BLE stack integration test: simulates an iPhone-like central connecting to our
 * peripheral and performing BLE-MIDI service discovery + subscription.
 *
 * Exercises the full path: init -> enable -> CONNECT_IND -> ATT MTU exchange ->
 * Discover All Primary Services -> find BLE-MIDI -> subscribe (CCCD write) ->
 * MIDI notification flow. All through the ble_hw callback interface, exactly
 * as the real radio driver would drive the stack.
 *
 * Test sequence matches Apple's BLE-MIDI connection flow (Accessory Design
 * Guidelines 58.x): connect, discover services, find MIDI I/O, enable
 * notifications, exchange MIDI. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* --- BLE stack configuration --- */
#define BLE_API static
#define BLE_DIAG 0
#define BLE_CENTRAL 0
#define BLE_LL_ENC 0
#define BLE_SMP_LEGACY 0
#define BLE_DEVICE_NAME "Test"
#define BLE_NAME_HEX 0

/* --- include the full BLE stack (peripheral only, no encryption) --- */

/* app callbacks: defined before modules so the compiler sees them */
static uint32_t midi_out_pkt;
static uint32_t midi_out_ts;
static int midi_out_pending;

static int ble_app_midi_peek(uint32_t *pkt, uint32_t *t_ms) {
    if (midi_out_pending) {
        *pkt = midi_out_pkt;
        *t_ms = midi_out_ts;
        return 1;
    }
    return 0;
}
static void ble_app_midi_pop(void) { midi_out_pending = 0; }

static int state_changes;
static void ble_app_state(void) { state_changes++; }

static uint32_t midi_in_pkts[64];
static unsigned midi_in_n;
static void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last_ts) {
    (void)ts; (void)last_ts;
    if (midi_in_n < 64) midi_in_pkts[midi_in_n++] = pkt;
}

/* the upstream stack modules */
#include "ble_prim.c"
#include "ble_midi.c"
#include "ble_ll.c"
#include "ble_smp.c"
#include "ble_host.c"
#include "ble_att.c"
#include "ble_store.c"
#include "ble_hw_stub.c"

/* --- test infrastructure --- */

static int fails;
static void check(const char *what, int ok) {
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

/* TX capture: intercept ble_ll_send to capture ATT responses */
static uint8_t tx_buf[512];
static uint16_t tx_len;
static uint16_t tx_cid;
static int tx_count;

/* We need to intercept the LL TX path. The stack queues L2CAP frames via
 * ble_ll_send(). Since we built with the stub driver, we override the TX
 * path by calling the LL's hw_tx callback to drain queued frames. */

/* Build a minimal CONNECT_IND PDU (Core Vol 6 Part B 2.3.3.1):
 * Header (2) + InitA (6) + AdvA (6) + LLData (22) = 36 octets total */
static void make_connect_ind(uint8_t *pdu)
{
    memset(pdu, 0, 36);
    pdu[0] = 0x85;          /* type 5 = CONNECT_IND, RxAdd=1 (our addr is random static) */
    pdu[1] = 34;             /* length */
    /* InitA (central's address): 11:22:33:44:55:66 */
    pdu[2] = 0x11; pdu[3] = 0x22; pdu[4] = 0x33;
    pdu[5] = 0x44; pdu[6] = 0x55; pdu[7] = 0x66;
    /* AdvA (our address): filled by the stub, C0:00:00:00:00:01 */
    pdu[8] = 0x01; pdu[9] = 0x00; pdu[10] = 0x00;
    pdu[11] = 0x00; pdu[12] = 0x00; pdu[13] = 0xC0;
    /* LLData (22 octets) */
    /* AA (4) */
    pdu[14] = 0xAA; pdu[15] = 0xBB; pdu[16] = 0xCC; pdu[17] = 0xDD;
    /* CRCInit (3) */
    pdu[18] = 0x55; pdu[19] = 0x55; pdu[20] = 0x55;
    /* WinSize (1) */
    pdu[21] = 2;
    /* WinOffset (2) */
    pdu[22] = 0; pdu[23] = 0;
    /* Interval (2) = 9 (11.25 ms) */
    pdu[24] = 9; pdu[25] = 0;
    /* Latency (2) = 0 */
    pdu[26] = 0; pdu[27] = 0;
    /* Timeout (2) = 100 (1 s) */
    pdu[28] = 100; pdu[29] = 0;
    /* ChM (5) = all 37 channels */
    pdu[30] = 0xFF; pdu[31] = 0xFF; pdu[32] = 0xFF; pdu[33] = 0xFF;
    pdu[34] = 0x1F;
    /* Hop (5 bits) + SCA (3 bits) */
    pdu[35] = 7;  /* hop=7, sca=0 */
}

/* Build an L2CAP frame around an ATT PDU and feed it to the LL as a data rx */
static void feed_att(const uint8_t *att, uint16_t att_len)
{
    uint8_t pdu[256];
    uint16_t l2cap_len = att_len;
    /* Data channel PDU header: LLID=2 (L2CAP start), length */
    pdu[0] = 0x02;  /* LLID=2 (start of L2CAP), NESN=0, SN=0, MD=0 */
    pdu[1] = (uint8_t)(4 + att_len);  /* LL payload length */
    /* L2CAP header: length (2) + CID (2) */
    pdu[2] = (uint8_t)l2cap_len; pdu[3] = (uint8_t)(l2cap_len >> 8);
    pdu[4] = 0x04; pdu[5] = 0x00;  /* CID = 4 (ATT) */
    memcpy(pdu + 6, att, att_len);
    ble_ll_hw_rx(pdu, (uint8_t)(6 + att_len));
}

/* Drain the LL TX queue: call ble_ll_hw_tx to get queued PDUs, then ack them */
static int drain_tx(void)
{
    uint8_t pdu[260];
    uint8_t len;
    int drained = 0;
    while (ble_ll_hw_tx_pending()) {
        len = ble_ll_hw_tx(pdu);
        if (len < 6) break;
        /* pdu[0]: LLID + flags, pdu[1]: length */
        uint8_t llid = pdu[0] & 0x03;
        if (llid == 2) {  /* L2CAP start */
            uint16_t l2_len = (uint16_t)(pdu[2] | (pdu[3] << 8));
            tx_cid = (uint16_t)(pdu[4] | (pdu[5] << 8));
            if (tx_cid == 4 && l2_len > 0) {  /* ATT */
                memcpy(tx_buf, pdu + 6, l2_len);
                tx_len = l2_len;
                tx_count++;
            }
        }
        ble_ll_hw_tx_acked();
        drained++;
    }
    return drained;
}

/* Simulate a connection event (the radio fires event_end after each interval) */
static uint16_t event_counter;
static void sim_event(void)
{
    ble_ll_hw_event_end(event_counter++, 1);
    drain_tx();
}

/* --- the tests --- */

static void test_init_and_enable(void)
{
    uint8_t addr[6];
    ble_hw_addr(addr);
    ble_init(addr, 1);
    ble_enable(1);
    check("init + enable: no crash", 1);
}

static void test_connect(void)
{
    uint8_t cind[36];
    make_connect_ind(cind);
    int taken = ble_ll_hw_connect_ind(cind, 36);
    check("CONNECT_IND accepted", taken == 1);
    check("ble_connected() = 1", ble_connected() == 1);
    check("ble_app_state called", state_changes > 0);
}

static void test_mtu_exchange(void)
{
    /* ATT Exchange MTU Request: opcode 0x02, client MTU = 247 */
    uint8_t req[] = {0x02, 247 & 0xFF, 247 >> 8};
    event_counter = 1;
    feed_att(req, 3);
    sim_event();

    /* expect ATT Exchange MTU Response: opcode 0x03, server MTU */
    check("MTU response opcode = 0x03", tx_len >= 3 && tx_buf[0] == 0x03);
    if (tx_len >= 3) {
        uint16_t srv_mtu = (uint16_t)(tx_buf[1] | (tx_buf[2] << 8));
        check("server MTU >= 23", srv_mtu >= 23);
    }
}

static void test_discover_services(void)
{
    /* ATT Read By Group Type Request: opcode 0x10, start=0x0001, end=0xFFFF,
     * type=0x2800 (Primary Service) */
    uint8_t req[] = {0x10, 0x01, 0x00, 0xFF, 0xFF, 0x00, 0x28};
    tx_count = 0;
    feed_att(req, 7);
    sim_event();

    /* The response should contain GAP (0x1800), GATT (0x1801), and BLE-MIDI
     * services. GAP and GATT have 16-bit UUIDs (element length 6), BLE-MIDI
     * has a 128-bit UUID (element length 20). They may come in separate
     * responses since element lengths can't be mixed. */
    check("service discovery: got ATT response", tx_len > 0);

    /* The first response has 16-bit UUID services (GAP, GATT) */
    int found_gap = 0, found_gatt = 0;
    if (tx_len > 2 && tx_buf[0] == 0x11) {  /* Read By Group Type Response */
        uint8_t el = tx_buf[1];
        uint16_t i;
        for (i = 2; i + el <= tx_len; i = (uint16_t)(i + el)) {
            if (el == 6) {
                uint16_t uuid = (uint16_t)(tx_buf[i+4] | (tx_buf[i+5] << 8));
                if (uuid == 0x1800) found_gap = 1;
                if (uuid == 0x1801) found_gatt = 1;
            }
        }
    }
    check("found GAP service (0x1800)", found_gap);
    check("found GATT service (0x1801)", found_gatt);

    /* Continue discovery from after the last handle to find BLE-MIDI */
    /* The GATT service ends at handle 11, so start from 12 */
    uint8_t req2[] = {0x10, 0x0C, 0x00, 0xFF, 0xFF, 0x00, 0x28};
    tx_count = 0;
    tx_len = 0;
    feed_att(req2, 7);
    sim_event();

    /* This response should have the BLE-MIDI service (128-bit UUID, el=20) */
    int found_midi = 0;
    /* The BLE-MIDI UUID 03B80E5A-EDE8-4B33-A751-6CE34EC4C700, LSO first */
    static const uint8_t midi_uuid[16] = {
        0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7,
        0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03
    };
    if (tx_len > 2 && tx_buf[0] == 0x11) {
        uint8_t el = tx_buf[1];
        uint16_t i;
        for (i = 2; i + el <= tx_len; i = (uint16_t)(i + el)) {
            if (el == 20 && memcmp(tx_buf + i + 4, midi_uuid, 16) == 0) {
                found_midi = 1;
            }
        }
    }
    check("found BLE-MIDI service UUID", found_midi);
}

static void test_subscribe_midi(void)
{
    /* Write Request to CCCD handle 15 (MIDI I/O CCCD): enable notifications */
    uint8_t req[] = {0x12, 0x0F, 0x00, 0x01, 0x00};  /* opcode 0x12, handle 15, value 0x0001 */
    tx_count = 0;
    tx_len = 0;
    feed_att(req, 5);
    sim_event();

    /* expect Write Response (opcode 0x13) */
    check("CCCD write response opcode = 0x13", tx_len >= 1 && tx_buf[0] == 0x13);
    check("ble_midi_ready() = 1", ble_midi_ready() == 1);
}

static void test_midi_notification(void)
{
    /* Queue a MIDI Note On for output */
    midi_out_pkt = 0x7F3C9009u;  /* ch0 Note On C4 vel=127 (USB-MIDI format) */
    midi_out_ts = 100;
    midi_out_pending = 1;

    /* Drive a connection event: the stack should pick up the MIDI and send
     * a notification (ATT Handle Value Notification, opcode 0x1B) */
    tx_count = 0;
    tx_len = 0;
    /* ble_att_event is called per connection event to flush MIDI notifications.
     * In the real driver, the LL calls it. Simulate by driving multiple events. */
    sim_event();
    sim_event();

    /* The notification PDU: opcode 0x1B, handle 14 (MIDI I/O), BLE-MIDI payload */
    int found_ntf = 0;
    if (tx_len > 3 && tx_buf[0] == 0x1B) {
        uint16_t h = (uint16_t)(tx_buf[1] | (tx_buf[2] << 8));
        if (h == 14) found_ntf = 1;  /* handle 14 = MIDI I/O value */
    }
    check("MIDI notification sent on handle 14", found_ntf);
    check("MIDI data consumed (peek returns 0)", !midi_out_pending || ble_app_midi_peek(&midi_out_pkt, &midi_out_ts) == 0);
}

static void test_midi_write_in(void)
{
    /* Write Command (no response) to MIDI I/O handle 14 with a BLE-MIDI packet
     * containing a Note On. BLE-MIDI format: header (timestamp high) + timestamp
     * low + status + data1 + data2 */
    uint8_t req[] = {
        0x52,                    /* ATT Write Command */
        0x0E, 0x00,              /* handle 14 */
        0x80,                    /* BLE-MIDI header: timestamp high = 0 */
        0x80,                    /* timestamp low = 0 */
        0x90, 0x3C, 0x7F         /* Note On ch0 C4 vel=127 */
    };
    midi_in_n = 0;
    feed_att(req, 8);
    sim_event();

    check("MIDI Write Command decoded: got events", midi_in_n > 0);
    if (midi_in_n > 0) {
        /* USB-MIDI event packet: cable 0, Note On ch0, C4, 127 */
        uint32_t pkt = midi_in_pkts[0];
        uint8_t status = (uint8_t)(pkt >> 8);
        uint8_t note = (uint8_t)(pkt >> 16);
        uint8_t vel = (uint8_t)(pkt >> 24);
        check("decoded Note On status 0x90", (status & 0xF0) == 0x90);
        check("decoded note = 60 (C4)", note == 60);
        check("decoded velocity = 127", vel == 127);
    }
}

int main(void)
{
    test_init_and_enable();
    test_connect();
    test_mtu_exchange();
    test_discover_services();
    test_subscribe_midi();
    test_midi_notification();
    test_midi_write_in();
    printf("%s\n", fails ? "ble_gatt: FAILED" : "ble_gatt: all passed");
    return fails != 0;
}
