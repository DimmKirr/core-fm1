/* SPDX-License-Identifier: GPL-3.0-only */
/* WiFi public API: thin wrapper orchestrating all layers. */

static void wifi_init(void)
{
    /* read MAC address from hardware (OTP or default) */
    for (int i = 0; i < 6; i++)
        wifi_mac_addr[i] = (uint8_t)(0x02 + i); /* locally administered default */

    /* init layers bottom-up */
    wifi_lwip_init();

    /* RF and SDIO init */
    wf_clk_rst_init();
    int rf_ret = wifi_rf_init();
    (void)rf_ret;

    wifi_sdio_init();

    wifi_sta_state = WIFI_STATE_IDLE;
}

static int wifi_scan(void (*cb)(const char *ssid, int rssi, int channel))
{
    return wifi_sta_scan(cb);
}

static int wifi_connect(const char *ssid, const char *password)
{
    wifi_wpa_init(password, ssid);
    return wifi_sta_connect(ssid, password);
}

static void wifi_disconnect(void)
{
    wifi_lwip_dhcp_stop();
    wifi_sta_disconnect();
}

static wifi_state_t wifi_get_state(void)
{
    return wifi_sta_state;
}

static uint32_t wifi_get_ip(void)
{
    return wifi_ip_addr;
}

/* main loop poll: call from firmware main loop */
static void wifi_poll(void)
{
    wifi_tasklet_poll();
    wifi_sdio_poll_irq();
}
