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

    /* Running-indicator rows at every init boundary (iteration 12i). The
     * first sample is "rf", after the clock tree: the SoC block has only been
     * read on hardware from that point on. */
    wifi_ind_arm();

    /* RF bring-up: clocks, analog block, VCO scan, BBP, LUT (14-group
     * sequence from docs/hardware/wifi/SPECSHEET.md) */
    wifi_rf_init();
    wifi_ind_log("rf", 0);

    /* SDIO internal bus: clock/reset, enumeration, DMA */
    wifi_sdio_init();

    /* MAC: set initial channel, enable RX */
    wifi_mac_init();
    /* Re-arm the RF die after the last MAC/PHY reset [SPECSHEET §14 row 7,
     * §4.2.1 A17-A23]: the vendor's LUT load and auto control follow its
     * final PHY init with no reset afterwards; ours ran before the resets
     * in wifi_sdio_init(), so the tail runs once more here (12m). */
    wifi_rf_arm();
    wifi_ind_log("mac", 1);
    wifi_ind_done();

    /* STA state machine: start in IDLE */
    wifi_sta_init(wifi_mac_addr);

    /* lwIP: share MAC with netif, then init stack */
    wifi_lwip_set_mac(wifi_sta.our_mac);
    wifi_lwip_init();

    /* weather: reset state machine */
    weather_init();

    wifi_cfg_ssid = 0;
    wifi_cfg_pass = 0;

    /* diagnostic: confirm WiFi subsystem state after init */
    cfm1_log_int("wifi: init mac_sys=", (int32_t)WIFI_MAC_SYS_CTRL);
    cfm1_log_int("wifi: init wdma_ctl=", (int32_t)WDMA_CTL);
    cfm1_log_int("wifi: init wdma_cfg0=", (int32_t)WDMA_CFG0);
    cfm1_log_int("wifi: init mac0=", (int32_t)WIFI_MAC_ADDR_DW0);
    cfm1_log_int("wifi: init mac1=", (int32_t)WIFI_MAC_ADDR_DW1);
    cfm1_log_int("wifi: init sta_st=", (int32_t)wifi_sta.state);
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
    wifi_mac_tighten_filter();
    wifi_lwip_start_dhcp();
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

#ifdef CONFIG_WIFI_DEBUG_AUTOSCAN
/* Debug: scan without the panel. First scan 2 s after the task starts, then
 * every 15 s while idle, so a power-cycle alone produces the scan
 * diagnostics for task debug:dump / debug:follow. */
static void wifi_autoscan_poll(void)
{
    static uint32_t next_ms = 2000u;
    uint32_t now = wifi_sta_now_ms();
    if (wifi_sta.state != WIFI_STATE_IDLE || (int32_t)(now - next_ms) < 0)
        return;
    next_ms = now + 15000u;
    cfm1_log("wifi: autoscan");
    wifi_scan(0);
}
#endif

#ifdef CONFIG_WIFI_DEBUG_WLCTL0_PROBE
/* WL_CTL0 release probe (iteration 12i, 2026-11-04) [observations
 * 2026-11-04T00:04]. The running vendor radio reads 0x14000 = 0 while its MAC
 * registers stay readable; core-fm1 keeps the 0xFFFF (reads 0xF103) written
 * at bring-up, and the five read-only "running" indicators never assert.
 * Host-triggered only (DAP D_WL0, `task debug:wl0`): fm1_fault() halts the
 * unit on the FAULT screen, so a probe that re-ran by itself after a
 * power-cycle could keep the device unreachable; on request it runs once,
 * from the WiFi task, with the indicator rows before and after. SoC-block
 * reads come first (always safe), the MAC read last: if WL_CTL0 is a level
 * gate after all, that read faults and the rows already in the ring plus the
 * crash record (task logs:hello) say so. NOINIT marker bit 0 is set for the
 * duration of the write so a halted attempt is visible next time. One scan
 * follows so the counters can move. */
static uint8_t wifi_wl0_probe_req;   /* set by the DAP command, consumed by the poll */
static uint8_t wifi_wl0_retry_req;   /* mode 2: retry the writes refused on a held block */

static void wifi_wl0_probe_request(uint32_t mode)
{
    if (mode == 2u) {
        wifi_wl0_retry_req = 1;
        return;
    }
    wifi_wl0_probe_req = 1;
}

/* 12k: the writes refused in iterations 12e/12h (0x14050 b23, 0x14060 b23,
 * 0x14058 vendor value, WDMA_CTL b16) were all attempted on a held block
 * (WL_CTL0 0xF103). Retry them on a released block, read each back, bracket
 * with indicator rows, then scan. A write that sticks now is an enable that
 * the hold was gating. Releases first so the result does not depend on how
 * this boot was started. */
static void wifi_wl0_retry_poll(void)
{
    if (!wifi_wl0_retry_req)
        return;
    wifi_wl0_retry_req = 0;
    cfm1_log("wifi: wl0 retry refused writes");
    wifi_ind_log("pre2", 1);
    *(volatile uint32_t *)0x14000u = 0u;
    fm1_delay_us(1000);
    *(volatile uint32_t *)0x14050u |= (1u << 23);
    *(volatile uint32_t *)0x14060u |= (1u << 23);
    *(volatile uint32_t *)0x14058u = 0x009F827Du;
    WDMA_CTL = 0x00010037u;
    fm1_delay_us(100);
    cfm1_log_int("wifi: r2 sirq=", (int32_t)(*(volatile uint32_t *)0x14050u));
    cfm1_log_int("wifi: r2 cfg60=", (int32_t)(*(volatile uint32_t *)0x14060u));
    cfm1_log_int("wifi: r2 cfg58=", (int32_t)(*(volatile uint32_t *)0x14058u));
    cfm1_log_int("wifi: r2 wctl=", (int32_t)WDMA_CTL);
    wifi_ind_log("pst2", 1);
    if (wifi_sta.state == WIFI_STATE_IDLE)
        wifi_scan(0);
}

static void wifi_wlctl0_probe_poll(void)
{
    volatile uint32_t *wlctl0 = (volatile uint32_t *)0x14000u;
    if (!wifi_wl0_probe_req)
        return;
    wifi_wl0_probe_req = 0;
    if (cfm1_dbg_nv.probe & 1u)
        cfm1_log("wifi: wl0 probe: previous attempt crashed");
    cfm1_log("wifi: wl0 probe");
    wifi_ind_log("pre0", 1);
    cfm1_dbg_nv.probe |= 1u;
    *wlctl0 = 0u;
    fm1_delay_us(100);
    cfm1_log_int("wifi: wl0 post=", (int32_t)*wlctl0);
    wifi_ind_log("soc0", 0);
    wifi_ind_log("mac0", 1);
    cfm1_dbg_nv.probe &= ~1u;
    cfm1_log("wifi: wl0 probe done");
    if (wifi_sta.state == WIFI_STATE_IDLE)
        wifi_scan(0);
}
#endif

static void wifi_poll(void)
{
    wifi_defer_poll();
    wifi_timer_poll();
    wifi_sdio_poll_irq();
    wifi_sta_poll();
    wifi_lwip_poll();
    http_poll();
    weather_poll();
#ifdef CONFIG_WIFI_DEBUG_WLCTL0_PROBE
    wifi_wlctl0_probe_poll();
    wifi_wl0_retry_poll();
#endif
#ifdef CONFIG_WIFI_DEBUG_AUTOSCAN
    wifi_autoscan_poll();
#endif
}

#ifdef CONFIG_SCHED
static void wifi_task(void)
{
    cfm1_log("wifi: task poll enabled");
    for (;;) {
        wifi_poll();
        yield();
    }
}
#endif
