/* SPDX-License-Identifier: GPL-3.0-only */
/* Clean-room implementation from docs/hardware/wifi/SPECSHEET.md.
 * No vendor code, IR or disassembly.
 *
 * Public API wrapper implementing cfm1_wifi.h. Orchestrates:
 *   wifi_rtos.c  -- bare-metal RTOS shim (buffer pool, deferred work, IRQ lock)
 *   wifi_rf.c    -- RF analog init, VCO scan, channel set, TX power
 *   wifi_sdio.c  -- SDIO bus driver, DMA, IRQ
 *   wifi_mac.c   -- 802.11 MAC frame handling
 *   wifi_sta.c   -- STA state machine
 *   wifi_data.c  -- TX/RX data path
 *   wifi_wpa.c   -- WPA2-PSK (4-way handshake, CCMP)
 *   wifi_lwip.c  -- lwIP netif, DHCP */

/* stored credentials for the connect sequence */
static const char *wifi_cfg_ssid;
static const char *wifi_cfg_pass;

static void wifi_init(void)
{
    cfm1_log("wifi: init start");
    /* RTOS shim: buffer pool, deferred work queue */
    wifi_rtos_init();

    /* RF bring-up: clocks, analog block, VCO scan, BBP, LUT (14-group
     * sequence from docs/hardware/wifi/SPECSHEET.md) */
    wifi_rf_init();

    /* SDIO internal bus: clock/reset, enumeration, DMA */
    wifi_sdio_init();

    /* MAC: set initial channel, enable RX */
    wifi_mac_init();

    /* STA state machine: start in IDLE */
    wifi_sta_init(wifi_mac_addr);

    /* lwIP: share MAC with netif, then init stack */
    wifi_lwip_set_mac(wifi_sta.our_mac);
    wifi_lwip_init();

    /* weather: reset state machine */
    weather_init();

    wifi_cfg_ssid = 0;
    wifi_cfg_pass = 0;
    cfm1_log("wifi: init done");
}

static int wifi_scan(void (*cb)(const char *ssid, int rssi, int channel))
{
    if (wifi_sta.state == WIFI_STATE_OFF)
        return -1;
    return wifi_sta_start_scan(cb);
}

static int wifi_connect(const char *ssid, const char *password)
{
    int idx;

    if (!ssid)
        return -1;
    cfm1_log("wifi: connect");
    if (wifi_sta.state != WIFI_STATE_IDLE &&
        wifi_sta.state != WIFI_STATE_SCANNING)
        return -1;

    /* look up the AP in scan results so the PMK can be derived before
     * authentication starts, instead of racing the EAPOL handshake */
    idx = wifi_sta_find_ap(ssid);
    if (idx < 0) {
        cfm1_log("wifi: AP not found");
        return -1;
    }

    wifi_cfg_ssid = ssid;
    wifi_cfg_pass = password;

#ifdef CONFIG_WIFI_WPA
    if (password) {
        uint32_t plen = 0, slen = 0;
        const char *p = password, *s = ssid;
        while (*p++) plen++;
        while (*s++) slen++;
        wifi_wpa_init((const uint8_t *)password, plen,
                      (const uint8_t *)ssid, slen,
                      wifi_sta.our_mac,
                      wifi_sta.results[idx].bssid);
    }
#endif

    return wifi_sta_start_connect(ssid, password);
}

static void wifi_disconnect(void)
{
    cfm1_log("wifi: disconnect");
    wifi_lwip_stop_dhcp();
    wifi_sta_do_disconnect();
    wifi_cfg_ssid = 0;
    wifi_cfg_pass = 0;
}

static wifi_state_t wifi_get_state(void)
{
    return wifi_sta.state;
}

static uint32_t wifi_get_ip(void)
{
    return wifi_lwip_get_ip();
}

/* ---- event callbacks from lower layers ---- */

/* wifi_sta.c calls this when association + WPA completes */
static void wifi_on_connected(void)
{
    cfm1_log("wifi: associated, starting DHCP");
    wifi_lwip_start_dhcp();
    weather_fetch_start();
}

/* wifi_sta.c calls this on disconnect / deauth */
static void wifi_on_disconnected(void)
{
    cfm1_log("wifi: disconnected");
    wifi_lwip_stop_dhcp();
}

/* wifi_data.c calls this for each received Ethernet frame */
static void wifi_on_rx(const uint8_t *data, uint32_t len)
{
    wifi_lwip_input(data, len);
}

/* ---- main loop poll ---- */

static void wifi_poll(void)
{
    /* deferred work from ISRs */
    wifi_defer_poll();

    /* software timers */
    wifi_timer_poll();

    /* SDIO: check for pending IRQs */
    wifi_sdio_poll_irq();

    /* STA: advance the state machine (auth/assoc/wpa timeouts) */
    wifi_sta_poll();

    /* lwIP: run protocol timers (ARP, DHCP, TCP retransmit) */
    wifi_lwip_poll();

    /* HTTP client: check timeouts */
    http_poll();

    /* weather: advance fetch state machine */
    weather_poll();
}

#ifdef CONFIG_SCHED
static void wifi_task(void)
{
    sleep_ms(3000);
    cfm1_log("wifi: task init GO");
    wifi_rtos_init(); yield();
    wifi_rf_init(); yield();
    wifi_sdio_init(); yield();
    wifi_mac_init(); yield();
    wifi_sta_init(wifi_mac_addr);
    wifi_lwip_set_mac(wifi_sta.our_mac);
    wifi_lwip_init();
    weather_init();
    wifi_cfg_ssid = 0;
    wifi_cfg_pass = 0;
    cfm1_log("wifi: task init done");
    for (;;) {
        wifi_cmd_poll();
        wifi_poll();
        yield();
    }
}
#endif
