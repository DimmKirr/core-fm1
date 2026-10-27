/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#ifdef CONFIG_WIFI

typedef enum {
    WIFI_STATE_OFF = 0,
    WIFI_STATE_IDLE,
    WIFI_STATE_SCANNING,
    WIFI_STATE_AUTH,
    WIFI_STATE_ASSOC,
    WIFI_STATE_WPA,
    WIFI_STATE_CONNECTED,
} wifi_state_t;

static void wifi_init(void);
static int  wifi_scan(void (*cb)(const char *ssid, int rssi, int channel));
static int  wifi_connect(const char *ssid, const char *password);
static void wifi_disconnect(void);
static wifi_state_t wifi_get_state(void);
static uint32_t wifi_get_ip(void);

#else

static inline void wifi_init(void) {}
static inline int  wifi_get_state(void) { return 0; }

#endif
