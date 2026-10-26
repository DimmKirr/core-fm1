/* SPDX-License-Identifier: GPL-3.0-only */
/* WiFi STA state machine: scan, auth, assoc, WPA handshake, connected.
 * From Ralink RT30xx GPL architecture adapted to bare-metal. */

/* ---- state ---- */

static wifi_state_t wifi_sta_state = WIFI_STATE_OFF;
static uint8_t wifi_sta_bssid[6];
static char    wifi_sta_ssid[WIFI_MAX_SSID_LEN + 1];
static char    wifi_sta_password[64];
static uint16_t wifi_sta_aid; /* association ID from AP */

/* scan callback */
static void (*wifi_scan_cb)(const char *ssid, int rssi, int channel);

/* scan dwell time per channel (ms) */
#define WIFI_SCAN_DWELL_MS 100

/* ---- scan ---- */

static int wifi_sta_scan(void (*cb)(const char *ssid, int rssi, int channel))
{
    if (wifi_sta_state != WIFI_STATE_IDLE && wifi_sta_state != WIFI_STATE_OFF)
        return -1;

    wifi_scan_cb = cb;
    wifi_bss_count = 0;
    wifi_sta_state = WIFI_STATE_SCANNING;

    for (uint8_t ch = 1; ch <= 14; ch++) {
        wifi_set_channel(ch);
        wifi_mac_tx_probe_req("", ch);
        /* dwell: process incoming beacons/probe responses via polling */
        for (uint32_t d = 0; d < WIFI_SCAN_DWELL_MS; d++) {
            wifi_sdio_poll_irq();
            wifi_tasklet_poll();
            wf_udelay(1000);
        }
    }

    if (cb) {
        for (uint8_t i = 0; i < wifi_bss_count; i++) {
            cb(wifi_bss_table[i].ssid,
               wifi_bss_table[i].rssi,
               wifi_bss_table[i].channel);
        }
    }

    wifi_sta_state = WIFI_STATE_IDLE;
    return (int)wifi_bss_count;
}

/* ---- connect ---- */

static int wifi_sta_connect(const char *ssid, const char *password)
{
    if (wifi_sta_state == WIFI_STATE_CONNECTED)
        wifi_disconnect();

    /* find BSS */
    int idx = -1;
    for (int i = 0; i < (int)wifi_bss_count; i++) {
        int match = 1;
        for (int j = 0; ssid[j] || wifi_bss_table[i].ssid[j]; j++) {
            if (ssid[j] != wifi_bss_table[i].ssid[j]) { match = 0; break; }
        }
        if (match) { idx = i; break; }
    }
    if (idx < 0) return -1;

    for (int i = 0; i < 6; i++)
        wifi_sta_bssid[i] = wifi_bss_table[idx].bssid[i];
    int k;
    for (k = 0; ssid[k] && k < WIFI_MAX_SSID_LEN; k++)
        wifi_sta_ssid[k] = ssid[k];
    wifi_sta_ssid[k] = 0;
    for (k = 0; password[k] && k < 63; k++)
        wifi_sta_password[k] = password[k];
    wifi_sta_password[k] = 0;

    wifi_set_channel(wifi_bss_table[idx].channel);

    /* open system authentication (algorithm 0, seq 1) */
    wifi_sta_state = WIFI_STATE_AUTH;
    wifi_mac_tx_auth(wifi_sta_bssid, 0, 1);

    return 0;
}

/* ---- auth response handler ---- */

static void wifi_sta_on_auth_rsp(const uint8_t *frame, uint32_t len)
{
    if (wifi_sta_state != WIFI_STATE_AUTH) return;
    if (len < sizeof(struct wifi_hdr) + 6) return;

    /* auth body: algorithm(2) + seq(2) + status(2) */
    const uint8_t *body = frame + sizeof(struct wifi_hdr);
    uint16_t status = (uint16_t)body[4] | ((uint16_t)body[5] << 8);

    if (status != 0) {
        /* auth rejected */
        wifi_sta_state = WIFI_STATE_IDLE;
        return;
    }

    wifi_sta_state = WIFI_STATE_ASSOC;
    wifi_mac_tx_assoc_req(wifi_sta_bssid, wifi_sta_ssid,
                          (uint8_t)wifi_bss_table[0].ssid_len);
}

/* ---- assoc response handler ---- */

static void wifi_sta_on_assoc_rsp(const uint8_t *frame, uint32_t len)
{
    if (wifi_sta_state != WIFI_STATE_ASSOC) return;
    if (len < sizeof(struct wifi_hdr) + 6) return;

    /* assoc response body: capability(2) + status(2) + AID(2) + IEs */
    const uint8_t *body = frame + sizeof(struct wifi_hdr);
    uint16_t status = (uint16_t)body[2] | ((uint16_t)body[3] << 8);

    if (status != 0) {
        wifi_sta_state = WIFI_STATE_IDLE;
        return;
    }

    wifi_sta_aid = ((uint16_t)body[4] | ((uint16_t)body[5] << 8)) & 0x3FFF;

    wifi_sta_state = WIFI_STATE_WPA;
    /* WPA 4-way handshake starts when AP sends EAPOL msg1 */
}

/* ---- WPA complete handler (called from wifi_wpa.c) ---- */

static void wifi_sta_on_wpa_done(int success)
{
    if (wifi_sta_state != WIFI_STATE_WPA) return;

    if (success) {
        wifi_sta_state = WIFI_STATE_CONNECTED;
        netif_carrier_on((void *)0);
        wifi_lwip_dhcp_start();
    } else {
        wifi_sta_state = WIFI_STATE_IDLE;
    }
}

/* ---- disconnect ---- */

static void wifi_sta_disconnect(void)
{
    if (wifi_sta_state >= WIFI_STATE_AUTH) {
        wifi_mac_tx_deauth(wifi_sta_bssid, 3 /* DEAUTH_LEAVING */);
    }
    wifi_lwip_dhcp_stop();
    netif_carrier_off((void *)0);
    wifi_sta_state = WIFI_STATE_IDLE;
}

/* ---- RX dispatch (called from wifi_mac_rx for management frames) ---- */

static void wifi_sta_rx_mgmt(const uint8_t *frame, uint32_t len)
{
    if (len < sizeof(struct wifi_hdr)) return;
    const struct wifi_hdr *hdr = (const struct wifi_hdr *)frame;
    uint8_t stype = hdr->frame_control & 0xF0;

    if (stype == WIFI_FC_STYPE_AUTH) {
        wifi_sta_on_auth_rsp(frame, len);
    } else if (stype == WIFI_FC_STYPE_ASSOC_RSP) {
        wifi_sta_on_assoc_rsp(frame, len);
    } else if (stype == WIFI_FC_STYPE_DEAUTH || stype == WIFI_FC_STYPE_DISASSOC) {
        wifi_sta_state = WIFI_STATE_IDLE;
        wifi_lwip_dhcp_stop();
        netif_carrier_off((void *)0);
    }
}
