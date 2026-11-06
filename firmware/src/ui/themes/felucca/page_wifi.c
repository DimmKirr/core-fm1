/* SPDX-License-Identifier: GPL-3.0-only */
/* WIFI page: scan for APs, connect, show connection info.
 * BTN_PLAY: scan (when idle) or connect (when AP selected).
 * Encoder: scroll AP list. */

#define FWIFI_ROW_H    15
#define FWIFI_VISIBLE  7
#define FWIFI_X_PAD    6

static int32_t  fwifi_sel;
static uint32_t fwifi_scroll;
static uint8_t  fwifi_scanning;

#ifndef CONFIG_WIFI_PASSWORD
#define CONFIG_WIFI_PASSWORD 0
#endif

static const char *fwifi_state_str(void)
{
    switch (wifi_get_state()) {
    case WIFI_STATE_OFF:       return "OFF";
    case WIFI_STATE_IDLE:      return "IDLE";
    case WIFI_STATE_SCANNING:  return "SCANNING";
    case WIFI_STATE_AUTH:      return "AUTH";
    case WIFI_STATE_ASSOC:     return "ASSOC";
    case WIFI_STATE_WPA:       return "WPA";
    case WIFI_STATE_CONNECTED: return "CONNECTED";
    default:                   return "???";
    }
}

static void fwifi_log2(const char *prefix, const char *s)
{
    char line[CFM1_LOG_LINE_LEN];
    uint32_t i = 0;
    while (i < CFM1_LOG_LINE_LEN - 1 && prefix[i]) { line[i] = prefix[i]; i++; }
    while (i < CFM1_LOG_LINE_LEN - 1 && *s) line[i++] = *s++;
    line[i] = 0;
    cfm1_log(line);
}

/* what the LCD shows, logged once per change so the ring can be read
 * against the STA state machine's own transitions */
static void fwifi_log_lcd(void)
{
    static const char *last;
    const char *now = fwifi_state_str();
    if (now != last) {
        last = now;
        fwifi_log2("ui: wifi lcd=", now);
    }
}

static void fwifi_fmt_ip(char *buf, uint32_t ip)
{
    uint32_t i, pos = 0;
    for (i = 0; i < 4; i++) {
        uint8_t b = (uint8_t)(ip >> (i * 8));
        if (b >= 100) buf[pos++] = '0' + b / 100;
        if (b >= 10)  buf[pos++] = '0' + (b / 10) % 10;
        buf[pos++] = '0' + b % 10;
        if (i < 3) buf[pos++] = '.';
    }
    buf[pos] = 0;
}

static void fwifi_fmt_mac(char *buf, const uint8_t *mac)
{
    static const char hex[] = "0123456789ABCDEF";
    uint32_t i, pos = 0;
    for (i = 0; i < 6; i++) {
        buf[pos++] = hex[mac[i] >> 4];
        buf[pos++] = hex[mac[i] & 0xF];
        if (i < 5) buf[pos++] = ':';
    }
    buf[pos] = 0;
}

static void fwifi_scan_cb(const char *ssid, int rssi, int channel)
{
    (void)ssid; (void)rssi; (void)channel;
    nav_dirty = 1;
}

static void fwifi_draw_info(void)
{
    uint32_t row = 0;
    char tmp[20];

    cv_begin(240, FWIFI_VISIBLE * FWIFI_ROW_H + 4, T_BG);

    /* status */
    {
        int32_t y = (int32_t)row * FWIFI_ROW_H;
        cv_text_fit(FWIFI_X_PAD, y + 2, &AF_S, "STATUS", T_DIM, T_BG, 54);
        cv_text_fit(60, y + 2, &AF_S, fwifi_state_str(), T_THEME, T_BG, 176);
        row++;
    }

    /* SSID */
    if (wifi_get_state() >= WIFI_STATE_AUTH && wifi_sta.target_ssid_len) {
        int32_t y = (int32_t)row * FWIFI_ROW_H;
        cv_text_fit(FWIFI_X_PAD, y + 2, &AF_S, "SSID", T_DIM, T_BG, 54);
        cv_text_fit(60, y + 2, &AF_S, (const char *)wifi_sta.target_ssid,
                    T_TEXT, T_BG, 176);
        row++;
    }

    /* IP address */
    if (wifi_get_state() == WIFI_STATE_CONNECTED) {
        uint32_t ip = wifi_lwip_get_ip();
        int32_t y;
        if (ip) {
            fwifi_fmt_ip(tmp, ip);
            y = (int32_t)row * FWIFI_ROW_H;
            cv_text_fit(FWIFI_X_PAD, y + 2, &AF_S, "IP", T_DIM, T_BG, 54);
            cv_text_fit(60, y + 2, &AF_S, tmp, T_TEXT, T_BG, 176);
            row++;
        }

        /* gateway */
        {
            uint32_t gw = wifi_lwip_get_gw();
            if (gw) {
                fwifi_fmt_ip(tmp, gw);
                y = (int32_t)row * FWIFI_ROW_H;
                cv_text_fit(FWIFI_X_PAD, y + 2, &AF_S, "GW", T_DIM, T_BG, 54);
                cv_text_fit(60, y + 2, &AF_S, tmp, T_TEXT, T_BG, 176);
                row++;
            }
        }

        /* DNS */
        {
            uint32_t dns = wifi_lwip_get_dns();
            if (dns) {
                fwifi_fmt_ip(tmp, dns);
                y = (int32_t)row * FWIFI_ROW_H;
                cv_text_fit(FWIFI_X_PAD, y + 2, &AF_S, "DNS", T_DIM, T_BG, 54);
                cv_text_fit(60, y + 2, &AF_S, tmp, T_TEXT, T_BG, 176);
                row++;
            }
        }

        /* MAC */
        {
            fwifi_fmt_mac(tmp, wifi_sta.our_mac);
            y = (int32_t)row * FWIFI_ROW_H;
            cv_text_fit(FWIFI_X_PAD, y + 2, &AF_S, "MAC", T_DIM, T_BG, 54);
            cv_text_fit(60, y + 2, &AF_S, tmp, T_TEXT, T_BG, 176);
            row++;
        }
    }

    cv_blit(0, Y_GRAPH);
}

static void fwifi_draw_scan(void)
{
    uint32_t i, n = wifi_bss_count;
    uint32_t visible = FWIFI_VISIBLE;

    cv_begin(240, visible * FWIFI_ROW_H + 4, T_BG);

    if (n == 0) {
        cv_text_in(0, (int32_t)(visible * FWIFI_ROW_H) / 2 - 6, 240, &AF_S,
                   fwifi_scanning ? "SCANNING..." : "NO APS FOUND",
                   T_DIM, T_BG);
        cv_blit(0, Y_GRAPH);
        return;
    }

    for (i = 0; i < visible && (i + fwifi_scroll) < n; i++) {
        uint32_t idx = i + fwifi_scroll;
        struct wifi_bss *b = &wifi_bss_table[idx];
        int32_t y = (int32_t)i * FWIFI_ROW_H;
        uint16_t bg = ((int32_t)idx == fwifi_sel) ? T_SURF : T_BG;
        uint16_t fg = ((int32_t)idx == fwifi_sel) ? T_THEME : T_TEXT;
        char ch[4];

        if (bg != T_BG)
            cv_rect(0, (uint32_t)y, 240, FWIFI_ROW_H, bg);

        cv_text_fit(FWIFI_X_PAD, y + 2, &AF_S,
                    b->ssid[0] ? b->ssid : "(hidden)", fg, bg, 170);

        /* channel number */
        ch[0] = '0' + b->channel / 10;
        ch[1] = '0' + b->channel % 10;
        ch[2] = 0;
        if (b->channel < 10) { ch[0] = ch[1]; ch[1] = 0; }
        cv_text_on(200, y + 2, &AF_S, ch, T_DIM, bg);

        /* lock icon for encrypted */
        if (b->has_rsn)
            cv_text_on(220, y + 2, &AF_S, "*", T_DIM, bg);
    }

    cv_blit(0, Y_GRAPH);
}

static void fwifi_draw(void)
{
    ui_header();
    fwifi_log_lcd();

    /* cards row */
    ui_card(0, "STATE", fwifi_state_str(), "",
                  wifi_get_state() == WIFI_STATE_CONNECTED ? T_THEME : T_MID,
                  -1, ICON_GENERIC);
    {
        char ipbuf[16];
        uint32_t ip = wifi_lwip_get_ip();
        if (ip) fwifi_fmt_ip(ipbuf, ip);
        else { ipbuf[0] = '-'; ipbuf[1] = '-'; ipbuf[2] = '-'; ipbuf[3] = 0; }
        ui_card(1, "IP", ipbuf, "", ip ? T_THEME : T_DIM, -1, ICON_GENERIC);
    }
    {
        char aps[4];
        aps[0] = '0' + (char)(wifi_bss_count / 10);
        aps[1] = '0' + (char)(wifi_bss_count % 10);
        aps[2] = 0;
        if (wifi_bss_count < 10) { aps[0] = aps[1]; aps[1] = 0; }
        ui_card(2, "APS", aps, "", T_MID, -1, ICON_GENERIC);
    }
    ui_card(3, "CH",
                  wifi_rf_stat.channel ? "SET" : "---", "",
                  T_DIM, -1, ICON_GENERIC);

    /* main area: connected -> info, otherwise -> AP list */
    if (wifi_get_state() == WIFI_STATE_CONNECTED)
        fwifi_draw_info();
    else
        fwifi_draw_scan();

    ui_footer("WIFI",
                  wifi_get_state() == WIFI_STATE_CONNECTED ? "INFO" : "SCAN",
                  "WIFI");
}

static void fwifi_enter(void)
{
    fwifi_sel = 0;
    fwifi_scroll = 0;
    fwifi_scanning = 0;
    fwifi_log2("ui: wifi page enter, sta=", fwifi_state_str());
    if (wifi_get_state() == WIFI_STATE_IDLE) {
        cfm1_log("ui: wifi enter -> scan");
        wifi_scan(fwifi_scan_cb);
    }
}

static int fwifi_knob(uint32_t id, int32_t clicks)
{
    uint32_t n;

    if (id != ENC_SELECT)
        return 0;

    n = wifi_bss_count;
    if (!n) return 1;

    fwifi_sel += clicks;
    if (fwifi_sel < 0) fwifi_sel = 0;
    if (fwifi_sel >= (int32_t)n) fwifi_sel = (int32_t)n - 1;

    if ((uint32_t)fwifi_sel < fwifi_scroll)
        fwifi_scroll = (uint32_t)fwifi_sel;
    if ((uint32_t)fwifi_sel >= fwifi_scroll + FWIFI_VISIBLE)
        fwifi_scroll = (uint32_t)fwifi_sel - FWIFI_VISIBLE + 1;

    nav_dirty = 1;
    return 1;
}

static int fwifi_button(uint32_t id, uint32_t down)
{
    if (down) return 0;

    if (id == BTN_PLAY) {
        wifi_state_t st = wifi_get_state();

        fwifi_log2("ui: wifi PLAY, sta=", fwifi_state_str());

        if (st == WIFI_STATE_CONNECTED) {
            cfm1_log("ui: wifi PLAY -> disconnect");
            wifi_disconnect();
            nav_dirty = 1;
            return 1;
        }

        if (st == WIFI_STATE_IDLE || st == WIFI_STATE_SCANNING) {
            if (wifi_bss_count > 0 && fwifi_sel < (int32_t)wifi_bss_count) {
                struct wifi_bss *b = &wifi_bss_table[fwifi_sel];
                const char *pass = CONFIG_WIFI_PASSWORD;
                int rc;
                if (!pass || !pass[0])
                    pass = 0;
                fwifi_log2("ui: wifi PLAY -> connect ", b->ssid);
                cfm1_log_hex("ui: wifi sel bssid=", b->bssid, 6);
                cfm1_log_int("ui: wifi sel ch=", (int32_t)b->channel);
                cfm1_log_int("ui: wifi sel rsn=", (int32_t)b->has_rsn);
                cfm1_log_int("ui: wifi pass=", pass ? 1 : 0);
                rc = wifi_connect(b->ssid, b->has_rsn ? pass : 0);
                cfm1_log_int("ui: wifi connect rc=", (int32_t)rc);
            } else {
                cfm1_log("ui: wifi PLAY -> scan");
                wifi_scan(fwifi_scan_cb);
                fwifi_scanning = 1;
            }
            nav_dirty = 1;
            return 1;
        }
        return 0;
    }

    if (id == BTN_HOME) {
        if (wifi_get_state() == WIFI_STATE_IDLE) {
            cfm1_log("ui: wifi HOME -> scan");
            wifi_scan(fwifi_scan_cb);
            fwifi_scanning = 1;
            nav_dirty = 1;
            return 1;
        }
    }

    return 0;
}

static void fwifi_status(void)
{
    static wifi_state_t last;
    wifi_state_t now = wifi_get_state();
    if (now != last) {
        last = now;
        nav_dirty = 1;
    }
}

static const cfm1_page_t FPAGE_WIFI = {
    .name   = "WIFI",
    .title  = "WIFI",
    .view   = VIEW_CONN,
    .flags  = PAGEF_SYSTEM,
    .enter  = fwifi_enter,
    .draw   = fwifi_draw,
    .knob   = fwifi_knob,
    .button = fwifi_button,
    .status = fwifi_status,
};
