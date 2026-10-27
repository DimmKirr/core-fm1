/* SPDX-License-Identifier: GPL-3.0-only */
/* BLE HW stub with connection simulation state machine.
 * Wraps the passive stub (ble_hw_stub.c) and adds the ability to inject
 * a CONNECT_IND, ATT exchanges (MTU, MIDI CCCD) and a disconnect —
 * enough for an e2e test to exercise the full BLE-MIDI path without
 * hardware or a real central.
 *
 * Unity-build file: #include'd from core_fm1.c, all functions static. */

#include "ble_hw.h"

/* ------------------------------------------------------------------ */
/* Simulation clock                                                    */
/* ------------------------------------------------------------------ */

static uint32_t bsim_clock_us;

/* ------------------------------------------------------------------ */
/* Passive stub (copied from upstream ble_hw_stub.c, with time_us     */
/* modified to return the simulation clock)                            */
/* ------------------------------------------------------------------ */

BLE_API void ble_hw_adv_start(const struct ble_hw_adv *a) { (void)a; }
BLE_API void ble_hw_adv_stop(void) {}
BLE_API void ble_hw_conn_start(const struct ble_hw_conn *c) { (void)c; }
BLE_API void ble_hw_conn_stop(void) {}
BLE_API void ble_hw_conn_update(const struct ble_hw_conn_upd *u) { (void)u; }
BLE_API void ble_hw_chmap_update(const uint8_t chm[5], uint16_t instant)
{
    (void)chm;
    (void)instant;
}
BLE_API void ble_hw_set_lengths(uint8_t max_tx, uint8_t max_rx)
{
    (void)max_tx;
    (void)max_rx;
}
BLE_API void ble_hw_tx_kick(void) {}
BLE_API uint8_t ble_hw_addr(uint8_t addr[6])
{
    static const uint8_t a[6] = {0x01, 0x00, 0x00, 0x00, 0x00, 0xC0};
    uint32_t i;
    for (i = 0; i < 6u; i++)
        addr[i] = a[i];
    return 1;
}
BLE_API uint32_t ble_hw_time_us(void) { return bsim_clock_us; }
BLE_API uint32_t ble_hw_diag_now(void) { return bsim_clock_us; }
BLE_API void ble_hw_rand(uint8_t *out, uint8_t n)
{
    while (n--)
        *out++ = 0;
}
#if BLE_CENTRAL
BLE_API void ble_hw_scan_start(const struct ble_hw_scan *s) { (void)s; }
BLE_API void ble_hw_scan_stop(void) {}
BLE_API void ble_hw_init_start(const struct ble_hw_init *i) { (void)i; }
BLE_API void ble_hw_init_stop(void) {}
#endif

/* ------------------------------------------------------------------ */
/* Simulation state machine                                            */
/* ------------------------------------------------------------------ */

enum bsim_state {
    SIM_IDLE,
    SIM_CONNECTED,
    SIM_MTU_SENT,
    SIM_MIDI_SUBSCRIBED,
    SIM_MIDI_NOTE_SENT
};

static enum bsim_state bsim_st;
static uint16_t        bsim_evt_counter;
static uint32_t        bsim_tick_count;
static uint8_t         bsim_ui_dirty;

/* Peer address: configurable via SysEx before connect. */
static uint8_t bsim_peer_addr[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xC1};

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Drain the peripheral's TX queue: keep calling ble_ll_hw_tx and
 * acknowledging until there is nothing left. */
static void bsim_drain_tx(void)
{
    uint8_t pdu[2 + 255];
    uint8_t len;
    uint32_t guard;
    for (guard = 0; guard < 16u; guard++) {
        len = ble_ll_hw_tx(pdu);
        if (!len)
            break;
        ble_ll_hw_tx_acked();
    }
}

/* One connection event cycle: drain TX, advance time by one interval
 * (30 ms = 30000 us), close the event. */
static void bsim_event(void)
{
    bsim_drain_tx();
    bsim_clock_us += 30000u;   /* 30 ms interval */
    ble_ll_hw_event_end(bsim_evt_counter, 1);
    bsim_evt_counter++;
}

/* Inject an ATT PDU wrapped in L2CAP framing as a data-channel RX. */
static void bsim_inject_att(const uint8_t *att, uint8_t att_len)
{
    uint8_t pdu[2 + 4 + 251];   /* LL header + L2CAP header + ATT */
    pdu[0] = 0x02;              /* LLID 2: start of L2CAP */
    pdu[1] = (uint8_t)(4u + att_len);   /* LL payload length */
    pdu[2] = att_len;           /* L2CAP length lo */
    pdu[3] = 0x00;              /* L2CAP length hi */
    pdu[4] = 0x04;              /* CID ATT lo */
    pdu[5] = 0x00;              /* CID ATT hi */
    {
        uint8_t i;
        for (i = 0; i < att_len; i++)
            pdu[6 + i] = att[i];
    }
    ble_ll_hw_rx(pdu, (uint8_t)(2u + 4u + att_len));
}

/* ------------------------------------------------------------------ */
/* Public simulation API                                               */
/* ------------------------------------------------------------------ */

/* Inject a CONNECT_IND so the stack transitions from advertising to
 * connected.  Returns the link layer's accept (1) or reject (0). */
static int ble_hw_stub_connect(void)
{
    uint8_t pdu[36];
    uint8_t our_addr[6];
    uint32_t i;

    if (bsim_st != SIM_IDLE)
        return 0;

    ble_hw_addr(our_addr);

    /* header */
    pdu[0] = 0x05 | 0x80 | 0x40;   /* CONNECT_IND, RxAdd random, TxAdd random */
    pdu[1] = 34;                     /* payload length */

    /* InitA: central's address (from bsim_peer_addr, set via SysEx) */
    for (i = 0; i < 6u; i++)
        pdu[2 + i] = bsim_peer_addr[i];

    /* AdvA: must match our address */
    for (i = 0; i < 6u; i++)
        pdu[8 + i] = our_addr[i];

    /* LLData */
    /* Access Address (0x50654A6B, little-endian) */
    pdu[14] = 0x6B;
    pdu[15] = 0x4A;
    pdu[16] = 0x65;
    pdu[17] = 0x50;

    /* CRC init (0x123456, little-endian, 3 bytes) */
    pdu[18] = 0x56;
    pdu[19] = 0x34;
    pdu[20] = 0x12;

    pdu[21] = 2;     /* WinSize */

    /* WinOffset (3, little-endian) */
    pdu[22] = 3;
    pdu[23] = 0;

    /* Interval (24 = 30 ms, little-endian) */
    pdu[24] = 24;
    pdu[25] = 0;

    /* Latency (0) */
    pdu[26] = 0;
    pdu[27] = 0;

    /* Timeout (0x02D0 = 720, little-endian) */
    pdu[28] = 0xD0;
    pdu[29] = 0x02;

    /* Channel map: all 37 channels */
    pdu[30] = 0xFF;
    pdu[31] = 0xFF;
    pdu[32] = 0xFF;
    pdu[33] = 0xFF;
    pdu[34] = 0x1F;

    /* Hop (7) | SCA (1 << 5) */
    pdu[35] = 7 | (1 << 5);

    if (!ble_ll_hw_connect_ind(pdu, 36))
        return 0;

    bsim_st          = SIM_CONNECTED;
    bsim_evt_counter = 0;
    bsim_tick_count  = 0;
    bsim_clock_us    = 1250u + 3u * 1250u;   /* 1.25 ms + WinOffset */

    /* first connection event immediately */
    bsim_event();

    return 1;
}

/* Inject LL_TERMINATE_IND to cleanly disconnect. */
static void ble_hw_stub_disconnect(void)
{
    uint8_t pdu[4];
    if (bsim_st == SIM_IDLE)
        return;

    pdu[0] = 0x03;   /* LLID 3: LL control */
    pdu[1] = 2;      /* length */
    pdu[2] = 0x02;   /* LL_TERMINATE_IND opcode */
    pdu[3] = 0x13;   /* reason: remote user terminated connection */

    ble_ll_hw_rx(pdu, 4);
    bsim_event();

    bsim_st = SIM_IDLE;
}

/* Advance the simulation: call repeatedly from the main loop or a
 * test driver.  Internally paces connection events and injects ATT
 * packets at the appropriate moments. */
static void ble_hw_stub_tick(void)
{
    if (bsim_st == SIM_IDLE)
        return;

    bsim_tick_count++;

    if (bsim_st == SIM_CONNECTED && bsim_evt_counter >= 2) {
        static const uint8_t mtu_req[] = {0x02, 0xF7, 0x00};
        bsim_inject_att(mtu_req, 3);
        bsim_event();
        bsim_st = SIM_MTU_SENT;
        bsim_ui_dirty = 1;
        return;
    }

    if (bsim_st == SIM_MTU_SENT && bsim_evt_counter >= 4) {
        static const uint8_t cccd_write[] = {0x12, 15, 0, 1, 0};
        bsim_inject_att(cccd_write, 5);
        bsim_event();
        bsim_st = SIM_MIDI_SUBSCRIBED;
        bsim_ui_dirty = 1;
        return;
    }

    if (bsim_st == SIM_MIDI_SUBSCRIBED && bsim_evt_counter >= 6) {
        static const uint8_t midi_note[] = {
            0x52, 14, 0, 0x80, 0x80, 0x90, 0x3C, 0x7F
        };
        bsim_inject_att(midi_note, 8);
        bsim_event();
        bsim_st = SIM_MIDI_NOTE_SENT;
        bsim_ui_dirty = 1;
        return;
    }

    /* Keepalive: run a connection event every 2nd tick */
    if ((bsim_tick_count & 1u) == 0)
        bsim_event();
}

/* Set the simulated central's address from a SysEx command.
 * Called from usb.c sysex handler.  Format:
 *   F0 7D 42 4C a0h a0l a1h a1l a2h a2l a3h a3l a4h a4l a5h a5l F7
 * Each address byte is split into two 7-bit nibbles (hi, lo). */
static void ble_hw_stub_set_peer(const uint8_t *sx, uint8_t len)
{
    uint32_t i;
    if (len != 16)
        return;
    for (i = 0; i < 6u; i++)
        bsim_peer_addr[i] = (uint8_t)((sx[4 + i * 2] << 4) | sx[5 + i * 2]);
}
