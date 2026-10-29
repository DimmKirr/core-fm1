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

#endif /* CONFIG_WIFI */
