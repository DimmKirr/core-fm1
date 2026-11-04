/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

/* Shared WiFi types for UI pages.  Both the clean-room and non-free drivers
 * provide functions with these names so page_wifi.c compiles unchanged. */

#if defined(CONFIG_WIFI) || defined(CONFIG_WIFI_NONFREE)

typedef enum {
    WIFI_STATE_OFF = 0,
    WIFI_STATE_IDLE,
    WIFI_STATE_SCANNING,
    WIFI_STATE_AUTH,
    WIFI_STATE_ASSOC,
    WIFI_STATE_WPA,
    WIFI_STATE_CONNECTED,
} wifi_state_t;

#define WIFI_MAX_SSID_LEN 32
#define WIFI_MAX_BSS      16

struct wifi_bss {
    uint8_t  bssid[6];
    char     ssid[WIFI_MAX_SSID_LEN + 1];
    uint8_t  ssid_len;
    uint8_t  channel;
    int8_t   rssi;
    uint8_t  has_rsn;
    uint16_t beacon_interval;
    uint16_t capability;
    uint8_t  in_use;
};

#endif /* CONFIG_WIFI || CONFIG_WIFI_NONFREE */

/* Clean-room driver forward declarations */
#ifdef CONFIG_WIFI

static void wifi_init(void);
static int  wifi_scan(void (*cb)(const char *ssid, int rssi, int channel));
static int  wifi_connect(const char *ssid, const char *password);
static void wifi_disconnect(void);
static wifi_state_t wifi_get_state(void);
static uint32_t wifi_get_ip(void);

#elif !defined(CONFIG_WIFI_NONFREE)

static inline void wifi_init(void) {}
static inline int  wifi_get_state(void) { return 0; }

#endif
