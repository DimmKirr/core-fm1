/* SPDX-License-Identifier: GPL-3.0-only */
/* WiFi station logic: adapted from fm1-base's device/wifi.c (MPL-2.0).
 * Provides plat_wifi_* API using vendor binary WiFi libraries. */

/* Vendor WiFi API (from wl_wifi_sta_sfc.a, normally in wifi_connect.h) */
enum { STA_MODE = 1, AP_MODE, SMP_CFG_MODE, MP_TEST_MODE, P2P_MODE, NONE_MODE };
enum {
    WIFI_EVENT_MODULE_INIT, WIFI_EVENT_MODULE_START, WIFI_EVENT_MODULE_STOP,
    WIFI_EVENT_MODULE_START_ERR,
    WIFI_EVENT_AP_START, WIFI_EVENT_AP_STOP,
    WIFI_EVENT_STA_START, WIFI_EVENT_STA_STOP,
    WIFI_EVENT_STA_SCAN_COMPLETED,
    WIFI_EVENT_STA_CONNECT_SUCC,
    WIFI_EVENT_STA_CONNECT_TIMEOUT_NOT_FOUND_SSID,
    WIFI_EVENT_STA_CONNECT_ASSOCIAT_FAIL,
    WIFI_EVENT_STA_CONNECT_ASSOCIAT_TIMEOUT,
    WIFI_EVENT_STA_DISCONNECT,
    WIFI_EVENT_SMP_CFG_START, WIFI_EVENT_SMP_CFG_STOP,
    WIFI_EVENT_SMP_CFG_TIMEOUT, WIFI_EVENT_SMP_CFG_COMPLETED,
    WIFI_EVENT_STA_NETWORK_STACK_DHCP_SUCC,
    WIFI_EVENT_STA_NETWORK_STACK_DHCP_TIMEOUT,
};

struct wifi_store_info {
    int mode;
    uint8_t pwd[2][64];
};

struct wifi_mode_info {
    int mode;
};

struct wifi_scan_info {
    char ssid[33];
    uint8_t bssid[6];
    uint8_t channel;
    int8_t rssi;
    uint8_t security;
};

struct wifi_sta_connect_succ {
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
};

extern int wifi_on(void);
extern int wifi_off(void);
extern int wifi_enter_sta_mode(char *ssid, char *pwd);
extern int wifi_scan_req(void);
extern int wifi_get_scan_info(struct wifi_scan_info *info, int max);

/* ---- simplified station state machine ---- */

enum { WIFI_OFF = 0, WIFI_IDLE, WIFI_SCANNING, WIFI_JOINING, WIFI_CONNECTED, WIFI_FAILED };
static int nf_wifi_st;
static char nf_wifi_ssid[33];
static uint32_t nf_wifi_ip;
static int nf_wifi_rssi_val;

#define WF_SCAN_MAX 16
static struct wifi_scan_info nf_scan_list[WF_SCAN_MAX];
static int nf_scan_n;
static uint32_t nf_wifi_scans;

/* vendor event callback (called from vendor lib context) */
void wifi_event_callback(int event, void *info)
{
    switch (event) {
    case WIFI_EVENT_MODULE_START:
        shim_log("nf wifi: module start");
        break;
    case WIFI_EVENT_STA_SCAN_COMPLETED:
        nf_scan_n = wifi_get_scan_info(nf_scan_list, WF_SCAN_MAX);
        nf_wifi_scans++;
        if (nf_wifi_st == WIFI_SCANNING)
            nf_wifi_st = WIFI_IDLE;
        shim_log("nf wifi: scan done");
        break;
    case WIFI_EVENT_STA_CONNECT_SUCC: {
        struct wifi_sta_connect_succ *s = info;
        nf_wifi_rssi_val = s ? s->rssi : 0;
        nf_wifi_st = WIFI_CONNECTED;
        shim_log("nf wifi: connected");
        break;
    }
    case WIFI_EVENT_STA_DISCONNECT:
        nf_wifi_st = WIFI_IDLE;
        nf_wifi_ip = 0;
        shim_log("nf wifi: disconnected");
        break;
    case WIFI_EVENT_STA_NETWORK_STACK_DHCP_SUCC:
        shim_log("nf wifi: DHCP ok");
        break;
    case WIFI_EVENT_STA_CONNECT_TIMEOUT_NOT_FOUND_SSID:
    case WIFI_EVENT_STA_CONNECT_ASSOCIAT_FAIL:
    case WIFI_EVENT_STA_CONNECT_ASSOCIAT_TIMEOUT:
        nf_wifi_st = WIFI_FAILED;
        shim_log("nf wifi: connect failed");
        break;
    default:
        break;
    }
}

/* ---- plat_wifi_* API ---- */

void plat_wifi_on(int on)
{
    if (on && nf_wifi_st == WIFI_OFF) {
        wifi_on();
        wifi_enter_sta_mode("", "");
        nf_wifi_st = WIFI_IDLE;
        shim_log("nf wifi: on");
    } else if (!on && nf_wifi_st != WIFI_OFF) {
        wifi_off();
        nf_wifi_st = WIFI_OFF;
        nf_wifi_ip = 0;
    }
}

int plat_wifi_state(void) { return nf_wifi_st; }
const char *plat_wifi_ssid(void) { return nf_wifi_ssid; }
uint32_t plat_wifi_ip(void) { return nf_wifi_ip; }
int plat_wifi_rssi(void) { return nf_wifi_rssi_val; }
uint32_t plat_wifi_scans(void) { return nf_wifi_scans; }

int plat_wifi_scan(void)
{
    if (nf_wifi_st < WIFI_IDLE) return -1;
    nf_wifi_st = WIFI_SCANNING;
    return wifi_scan_req();
}

int plat_wifi_found(int i, char *ssid, int *rssi, int *secured)
{
    if (i < 0 || i >= nf_scan_n) return 0;
    for (int k = 0; k < 32 && nf_scan_list[i].ssid[k]; k++)
        ssid[k] = nf_scan_list[i].ssid[k];
    ssid[32] = 0;
    *rssi = nf_scan_list[i].rssi;
    *secured = nf_scan_list[i].security != 0;
    return 1;
}

int plat_wifi_join(const char *ssid, const char *pass)
{
    int len = 0;
    while (ssid[len]) len++;
    for (int i = 0; i < len && i < 32; i++)
        nf_wifi_ssid[i] = ssid[i];
    nf_wifi_ssid[len < 32 ? len : 32] = 0;
    nf_wifi_st = WIFI_JOINING;
    return wifi_enter_sta_mode((char *)ssid, (char *)(pass ? pass : ""));
}

int plat_wifi_saved(int i, char *ssid)
{
    (void)i; (void)ssid;
    return 0;
}

void plat_wifi_forget(int i) { (void)i; }
int plat_wifi_kept(const char *ssid) { (void)ssid; return 0; }

void plat_wifi_rx(uint32_t *frames, int *rssi)
{
    *frames = 0;
    *rssi = nf_wifi_rssi_val;
}
