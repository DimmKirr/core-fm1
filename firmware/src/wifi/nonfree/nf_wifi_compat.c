/* SPDX-License-Identifier: GPL-3.0-only */
/* Compatibility shim: expose the same API surface the clean-room driver
 * provides so page_wifi.c compiles unchanged with CONFIG_WIFI_NONFREE.
 * Maps plat_wifi_* (nf_wifi_station.c) to wifi_* names. */

/* ---- state ---- */

static wifi_state_t wifi_get_state(void)
{
    switch (plat_wifi_state()) {
    case WIFI_OFF:       return WIFI_STATE_OFF;
    case WIFI_IDLE:      return WIFI_STATE_IDLE;
    case WIFI_SCANNING:  return WIFI_STATE_SCANNING;
    case WIFI_JOINING:   return WIFI_STATE_AUTH;
    case WIFI_CONNECTED: return WIFI_STATE_CONNECTED;
    case WIFI_FAILED:    return WIFI_STATE_IDLE;
    default:             return WIFI_STATE_OFF;
    }
}

static const char *wifi_cfg_ssid;

/* ---- BSS table (lazy-synced from nf_scan_list on access) ---- */

static struct wifi_bss wifi_bss_table[WIFI_MAX_BSS];
static uint32_t nf_bss_count_val;
static int nf_bss_synced_gen = -1;

static void nf_sync_bss_table(void)
{
    int i;
    if (nf_bss_synced_gen == nf_scan_n)
        return;
    nf_bss_synced_gen = nf_scan_n;
    nf_bss_count_val = (uint32_t)(nf_scan_n > WIFI_MAX_BSS ? WIFI_MAX_BSS : nf_scan_n);
    for (i = 0; i < (int)nf_bss_count_val; i++) {
        struct wifi_scan_info *src = &nf_scan_list[i];
        struct wifi_bss *dst = &wifi_bss_table[i];
        int k;
        for (k = 0; k < 32 && src->ssid[k]; k++)
            dst->ssid[k] = src->ssid[k];
        dst->ssid[k] = 0;
        dst->ssid_len = (uint8_t)k;
        dst->channel = src->channel;
        dst->rssi = src->rssi;
        dst->has_rsn = src->security != 0;
        for (k = 0; k < 6; k++)
            dst->bssid[k] = src->bssid[k];
        dst->in_use = 1;
    }
}

#define wifi_bss_count (nf_sync_bss_table(), nf_bss_count_val)

/* ---- network info (stubs until lwIP is wired) ---- */

static uint32_t wifi_lwip_get_ip(void)  { return plat_wifi_ip(); }
static uint32_t wifi_lwip_get_gw(void)  { return 0; }
static uint32_t wifi_lwip_get_dns(void) { return 0; }

/* ---- MAC / RF stat stubs ---- */

static struct { uint8_t our_mac[6]; } wifi_sta;
static struct { uint8_t channel; } wifi_rf_stat;

/* ---- scan / connect / disconnect ---- */

static int wifi_scan(void (*cb)(const char *ssid, int rssi, int channel))
{
    (void)cb;
    cfm1_log("nf: scan");
    int rc = plat_wifi_scan();
    return rc;
}

static int wifi_connect(const char *ssid, const char *password)
{
    cfm1_log("nf: connect");
    if (ssid) cfm1_log(ssid);
    wifi_cfg_ssid = ssid;
    return plat_wifi_join(ssid, password ? password : "");
}

static void wifi_disconnect(void)
{
    cfm1_log("nf: disconnect");
    wifi_enter_sta_mode("", "");
    nf_wifi_st = WIFI_IDLE;
    nf_wifi_ip = 0;
    wifi_cfg_ssid = 0;
}

/* ---- debug fill (DAP WiFi snapshot) ---- */

static uint32_t wifi_debug_fill(uint8_t *b)
{
    b[0] = 0;
    b[1] = (uint8_t)wifi_get_state();
    b[2] = 0;
    b[3] = 0;
    { uint32_t i; for (i = 4; i < 40u; i++) b[i] = 0; }
    return 40u;
}

static uint32_t wifi_debug_fill_bss(uint8_t *b, uint32_t max)
{
    (void)max;
    b[0] = 0;
    return 1u;
}
