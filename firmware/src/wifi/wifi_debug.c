/* SPDX-License-Identifier: GPL-3.0-only */
/* WiFi debug snapshot: fills a fixed-size buffer with key state for DAP query.
 * Included in the unity build after wifi_api.c so all WiFi state is visible.
 * Called by debug_dap.c (forward-declared there). */

#ifdef CONFIG_WIFI

static uint32_t wifi_debug_fill(uint8_t *b)
{
    b[0]  = 0;                                     /* status OK */
    b[1]  = (uint8_t)wifi_sta.state;
    b[2]  = wifi_sdio_state.initialized;
    b[3]  = wifi_sdio_state.error_count;
    /* MAC address */
    { uint32_t i; for (i = 0; i < 6u; i++) b[4 + i] = wifi_sta.our_mac[i]; }
    /* IP / GW / DNS */
    dbg_wr32(b + 10, wifi_lwip_get_ip());
    dbg_wr32(b + 14, wifi_lwip_get_gw());
    dbg_wr32(b + 18, wifi_lwip_get_dns());
    /* scan / BSS */
    b[22] = (uint8_t)wifi_sta.nresults;
    b[23] = (uint8_t)wifi_bss_count;
#ifdef CONFIG_WIFI_LWIP
    b[24] = weather_state;
    b[25] = weather_dat.valid;
    dbg_wr32(b + 26, (uint32_t)(int32_t)weather_dat.temp_c);
    b[30] = weather_dat.humidity;
    b[31] = weather_dat.wmo_code;
    dbg_wr32(b + 32, (uint32_t)weather_dat.wind_kmh);
    dbg_wr32(b + 36, weather_dat.updated_ms);
#else
    { uint32_t i; for (i = 24; i < 40u; i++) b[i] = 0; }
#endif
    return 40u;
}

/* BSS table dump for D_WIFI_BSS (0x26).
 * Each entry: 6 BSSID + 1 ch + 1 rssi + 1 has_rsn + 1 ssid_len + ssid[ssid_len].
 * Returns total bytes written; fits up to ~5 entries in 256-byte DAP payload. */
static uint32_t wifi_debug_fill_bss(uint8_t *b, uint32_t max)
{
    uint32_t off = 1, i;
    b[0] = 0; /* status OK */
    for (i = 0; i < WIFI_MAX_BSS && off + 42u < max; i++) {
        struct wifi_bss *e = &wifi_bss_table[i];
        uint32_t j, slen;
        if (!e->in_use)
            continue;
        for (j = 0; j < 6u; j++)
            b[off + j] = e->bssid[j];
        off += 6;
        b[off++] = e->channel;
        b[off++] = (uint8_t)(e->rssi);
        b[off++] = e->has_rsn;
        slen = e->ssid_len;
        if (slen > WIFI_MAX_SSID_LEN) slen = WIFI_MAX_SSID_LEN;
        b[off++] = (uint8_t)slen;
        for (j = 0; j < slen; j++)
            b[off++] = (uint8_t)e->ssid[j];
    }
    return off;
}

/* DAP D_SWEEP (0x28): `count` consecutive words from `base` [tools/fm1log.py --sweep,
 * task debug:sweep]. Request: <u32 base><u16 count>. Reply: [0] status, [4..7] base,
 * [8..] words, all little-endian. Windows: SoC clocks 0x10000-0x10FFF and wireless
 * control 0x14000-0x14FFF always; MAC 0x30000-0x31FFF only once the clock tree is up
 * (reads fault before that). 0x40000-0x40FFF is the flash controller [SPECSHEET
 * erratum 3, 2026-11-04] and is never swept.
 * Read-to-clear counters (RX/TX_STA_CNT*, TX_STA_FIFO) are consumed by a sweep.
 * Returns 0 to refuse. */
static uint32_t wifi_sweep_fill(uint8_t *b, uint32_t max, const uint8_t *req, uint32_t len)
{
    uint32_t base, count, end, i;
    int ok;
    if (len < 6u || max < 12u)
        return 0;
    base = dbg_rd32(req) & ~3u;
    count = (uint32_t)req[4] | ((uint32_t)req[5] << 8);
    if (count > (max - 8u) / 4u)
        count = (max - 8u) / 4u;
    end = base + 4u * count;
    ok = (base >= 0x10000u && end <= 0x11000u) || (base >= 0x14000u && end <= 0x15000u);
    if (!ok && wifi_sdio_state.initialized)
        ok = (base >= 0x30000u && end <= 0x32000u);
    if (!ok)
        return 0;
    b[0] = 0;
    b[1] = 0;
    b[2] = 0;
    b[3] = 0;
    dbg_wr32(b + 4, base);
    for (i = 0; i < count; i++)
        dbg_wr32(b + 8 + 4u * i, *(volatile uint32_t *)(base + 4u * i));
    return 8u + 4u * count;
}

#endif /* CONFIG_WIFI */
