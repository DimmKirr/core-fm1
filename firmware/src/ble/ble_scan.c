/* SPDX-License-Identifier: GPL-3.0-only */
/* The AD parser and the found-devices table (ble_scan.h). Extended from Optimist's ble_scan.c
 * with optional HID service detection (CFM1_BLE_HID). Written from the Core Specification
 * Supplement Part A (AD types 0x01, 0x02, 0x03, 0x06, 0x07, 0x08, 0x09, 0x19) and Core Vol 6
 * Part B 2.3 (advertising PDUs). */
#include "ble_scan.h"
#include "ble_host.h"
#include "ble_util.h"

#ifdef CFM1_BLE_HID
enum { BLE_KIND_HID = 3 };
struct ble_ad_ext {
    struct ble_ad base;
    uint8_t hid;
    uint8_t has_appearance;
    uint16_t appearance;
};
#endif

enum { SC_ADV_IND = 0, SC_SCAN_RSP = 4 };

#ifdef CFM1_BLE_HID
static void ble_ad_parse_ext(const uint8_t *ad, uint32_t n, struct ble_ad_ext *ext)
{
    ble_zero((uint8_t *)ext, sizeof *ext);
    ble_ad_parse(ad, n, &ext->base);
    uint32_t i = 0;
    while (i < n) {
        uint32_t len = ad[i], k;
        const uint8_t *d = ad + i + 2;
        if (!len || i + 1u + len > n)
            break;
        switch (ad[i + 1]) {
        case 0x02:
        case 0x03:
            for (k = 0; k + 2u <= len - 1u; k += 2u)
                if (d[k] == 0x12u && d[k + 1u] == 0x18u)
                    ext->hid = 1;
            break;
        case 0x19:
            if (len >= 3u) {
                ext->appearance = ble_rd16(d);
                ext->has_appearance = 1;
            }
            break;
        default:
            break;
        }
        i += 1u + len;
    }
}
#endif

static void sc_name(char *dst, const uint8_t *s, uint32_t n)
{
    uint32_t i = 0, o = 0;
    while (i < n && o < BLE_NAME_MAX) {
        uint8_t c = s[i++];
        if (c >= 0x20u && c < 0x7Fu)
            dst[o++] = (char)c;
        else if ((c == 0xC2u || c == 0xC3u) && i < n && (s[i] & 0xC0u) == 0x80u) {
            uint8_t l = (uint8_t)((c & 3u) << 6 | (s[i++] & 0x3Fu));
            dst[o++] = (char)(l >= 0xA0u ? l : '?');
        } else if (c >= 0x20u || c == 0) {
            if (c == 0)
                break;
            dst[o++] = '?';
            while (i < n && (s[i] & 0xC0u) == 0x80u)
                i++;
        }
    }
    dst[o] = 0;
}

BLE_API void ble_scan_clear(struct ble_scan_tab *t)
{
    uint32_t gen = t->gen;
    ble_zero((uint8_t *)t, sizeof *t);
    t->gen = gen + 1u;
}

static int sc_find(const struct ble_scan_tab *t, const uint8_t *a, uint8_t rnd)
{
    uint32_t i;
    for (i = 0; i < BLE_SCAN_N; i++)
        if (t->e[i].used && t->e[i].addr_rand == rnd && ble_eq(t->e[i].addr, a, 6))
            return (int)i;
    return -1;
}

static int sc_slot(const struct ble_scan_tab *t, int midi, uint32_t now_ms)
{
    uint32_t i;
    int un = -1, old = -1;
    for (i = 0; i < BLE_SCAN_N; i++) {
        const struct ble_found *e = &t->e[i];
        if (!e->used)
            return (int)i;
        if (!e->midi && (un < 0 || e->seen_ms < t->e[un].seen_ms))
            un = (int)i;
        if (e->midi && (old < 0 || e->seen_ms < t->e[old].seen_ms))
            old = (int)i;
    }
    if (un >= 0)
        return un;
    if (midi && old >= 0 && now_ms - t->e[old].seen_ms > 2000u)
        return old;
    return -1;
}

static void sc_listed(struct ble_scan_tab *t, struct ble_found *e)
{
    e->midi = 1;
    e->order = ++t->next_order;
    t->gen++;
}

BLE_API void ble_scan_add(struct ble_scan_tab *t, const uint8_t *pdu, uint32_t len, uint16_t rssi, uint32_t now_ms)
{
    struct ble_ad ad;
    struct ble_found *e;
    uint8_t type, rnd;
    int i;
#ifdef CFM1_BLE_HID
    struct ble_ad_ext adx;
    int is_hid = 0;
#endif
    t->reports++;
    if (len < 8u || pdu[1] < 6u || pdu[1] > 37u || len < 2u + pdu[1]) {
        t->bad_ad++;
        return;
    }
    type = (uint8_t)(pdu[0] & 0x0Fu);
    rnd = (uint8_t)(pdu[0] >> 6 & 1u);
    if (type != SC_ADV_IND && type != SC_SCAN_RSP) {
        t->ignored_type++;
        return;
    }
    ble_ad_parse(pdu + 8, pdu[1] - 6u, &ad);
#ifdef CFM1_BLE_HID
    ble_ad_parse_ext(pdu + 8, pdu[1] - 6u, &adx);
    is_hid = adx.hid;
#endif
    if (ad.bad)
        t->bad_ad++;
    i = sc_find(t, pdu + 2, rnd);
    if (i < 0) {
        if (type != SC_ADV_IND)
            return;
        i = sc_slot(t, ad.midi
#ifdef CFM1_BLE_HID
                       || is_hid
#endif
                       , now_ms);
        if (i < 0) {
            t->dropped_full++;
            return;
        }
        e = &t->e[i];
        if (e->used && e->midi)
            t->gen++;
        ble_zero((uint8_t *)e, sizeof *e);
        e->used = 1;
        e->addr_rand = rnd;
        ble_cpy(e->addr, pdu + 2, 6);
    }
    e = &t->e[i];
    t->kept++;
    e->seen_ms = now_ms;
    if (rssi)
        e->rssi = rssi;
    if (e->hits < 255u)
        e->hits++;
    if (ad.name_type && (ad.name_type == 0x09u || e->name_type != 0x09u)) {
        char nm[BLE_NAME_MAX + 1u];
        uint32_t k;
        sc_name(nm, ad.name, ad.name_len);
        for (k = 0; k <= BLE_NAME_MAX && nm[k] == e->name[k] && nm[k]; k++)
            ;
        if (nm[k] != e->name[k]) {
            ble_cpy((uint8_t *)e->name, (const uint8_t *)nm, sizeof nm);
            if (e->midi)
                t->gen++;
        }
        e->name_type = ad.name_type;
    }
#ifdef CFM1_BLE_HID
    if (!(e->midi && e->kind == BLE_KIND_HID))
#endif
        e->kind = (uint8_t)(BLE_KIND_MIDI | (ble_eq((const uint8_t *)e->name, (const uint8_t *)"Optimist ", 9) ||
                                                     ble_eq((const uint8_t *)e->name, (const uint8_t *)"FM-1 ", 5) ?
                                             BLE_KIND_FM1 : 0u));
    if (ad.midi && !e->midi)
        sc_listed(t, e);
#ifdef CFM1_BLE_HID
    if (is_hid && !e->midi) {
        e->kind = BLE_KIND_HID;
        sc_listed(t, e);
    }
#endif
}

BLE_API void ble_scan_age(struct ble_scan_tab *t, uint32_t now_ms)
{
    uint32_t i;
    for (i = 0; i < BLE_SCAN_N; i++) {
        struct ble_found *e = &t->e[i];
        if (!e->used)
            continue;
        if (now_ms - e->seen_ms > (e->midi ? BLE_SCAN_AGE_MS : BLE_SCAN_AGE_UNCONF_MS)) {
            if (e->midi)
                t->gen++;
            e->used = 0;
            continue;
        }
        e->hits = (uint8_t)((e->hits + 1u) / 2u);
    }
}

BLE_API uint32_t ble_scan_list(const struct ble_scan_tab *t, uint8_t idx[BLE_SCAN_N])
{
    uint32_t n = 0, i, k;
    for (i = 0; i < BLE_SCAN_N; i++)
        if (t->e[i].used && t->e[i].midi) {
            for (k = n; k > 0 && t->e[idx[k - 1u]].order > t->e[i].order; k--)
                idx[k] = idx[k - 1u];
            idx[k] = (uint8_t)i;
            n++;
        }
    return n;
}

BLE_API uint32_t ble_scan_strength(uint16_t rssi)
{
    return 255u - ((rssi & 63u) + (rssi >> 8 & 63u));
}

static uint32_t sc_metric(const struct ble_scan_tab *t, uint32_t i, int by_rssi)
{
    return by_rssi ? ble_scan_strength(t->e[i].rssi) : t->e[i].hits;
}

BLE_API uint32_t ble_scan_bars(const struct ble_scan_tab *t, uint32_t i)
{
    uint8_t idx[BLE_SCAN_N];
    uint32_t n = ble_scan_list(t, idx), k, lo = 0xFFFFFFFFu, hi = 0, m;
    int by_rssi = 1;
    if (i >= BLE_SCAN_N || !t->e[i].used || !t->e[i].midi)
        return 0;
    for (k = 0; k < n; k++)
        if (!t->e[idx[k]].rssi)
            by_rssi = 0;
    for (k = 0; k < n; k++) {
        m = sc_metric(t, idx[k], by_rssi);
        lo = m < lo ? m : lo;
        hi = m > hi ? m : hi;
    }
    if (hi == lo)
        return 3;
    m = sc_metric(t, i, by_rssi);
    return 1u + (2u * (m - lo) + (hi - lo) / 2u) / (hi - lo);
}
