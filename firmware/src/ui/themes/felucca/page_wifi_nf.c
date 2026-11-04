/* SPDX-License-Identifier: GPL-3.0-only */
/* Minimal WiFi status page for the nonfree vendor driver.
 * Shows driver state via the plat_wifi_* API from nf_wifi_station.c.
 * The full WiFi/Weather pages use clean-room APIs and aren't available. */

static const char *fwnf_state_name(void)
{
    switch (plat_wifi_state()) {
    case 0: return "OFF";
    case 1: return "IDLE";
    case 2: return "SCANNING";
    case 3: return "JOINING";
    case 4: return "CONNECTED";
    case 5: return "FAILED";
    default: return "?";
    }
}

static void fwnf_draw(void)
{
    cv_fill(T_BG);
    cv_text(4, 2, &AF_M, "WIFI (VENDOR)", T_THEME);

    char buf[40];
    int y = 18;

    cv_text(4, y, &AF_S, "STATUS", T_DIM);
    cv_text(60, y, &AF_S, fwnf_state_name(), T_TEXT);
    y += 12;

    const char *ssid = plat_wifi_ssid();
    if (ssid && ssid[0]) {
        cv_text(4, y, &AF_S, "SSID", T_DIM);
        cv_text_fit(60, y, &AF_S, ssid, T_TEXT, T_BG, 176);
        y += 12;
    }

    int rssi = plat_wifi_rssi();
    if (plat_wifi_state() == 4 && rssi) {
        snpf(buf, sizeof buf, "%d dBm", rssi);
        cv_text(4, y, &AF_S, "RSSI", T_DIM);
        cv_text(60, y, &AF_S, buf, T_TEXT);
        y += 12;
    }

    snpf(buf, sizeof buf, "SCANS: %u", (unsigned)plat_wifi_scans());
    cv_text(4, y, &AF_S, buf, T_DIM);
    y += 16;

    cv_text(4, y, &AF_S, "INIT NOT WIRED YET", T_DIM);
}

static const cfm1_page_t FPAGE_WIFI_NF = {
    .name   = "NF",
    .title  = "WIFI VENDOR",
    .view   = VIEW_CONN,
    .draw   = fwnf_draw,
};
