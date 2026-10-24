/* SPDX-License-Identifier: GPL-3.0-only */
/* BLUETOOTH page: BLE status, connection info, and scanned devices.
 * Modelled on Optimist's HOME > SYSTEM > BLUETOOTH > DEVICES
 * (upstream/optimist/firmware/src/ui/sloop/ui_menu.c MI_BLE/MI_BLEDEV). */

#define FBLE_ROW_H    15
#define FBLE_VISIBLE  7
#define FBLE_X_PAD    6

static uint32_t fble_scroll;
static uint32_t fble_last_sig;

static const char *fble_status_str(void)
{
    if (!ble_connected() && !ble_midi_ready())
        return "ADVERTISING";
    if (ble_connected() && !ble_midi_ready())
        return "CONNECTED";
    if (ble_midi_ready())
        return "MIDI READY";
    return "OFF";
}

static void fble_draw_info(void)
{
    uint32_t row = 0;
    cv_begin(240, FBLE_VISIBLE * FBLE_ROW_H + 4, T_BG);

    /* connection status */
    {
        int32_t y = (int32_t)row * FBLE_ROW_H;
        cv_text_fit(FBLE_X_PAD, y + 2, &AF_S, "STATUS", T_DIM, T_BG, 60);
        cv_text_fit(70, y + 2, &AF_S, fble_status_str(), T_THEME, T_BG, 166);
        row++;
    }

    /* device name */
    {
        int32_t y = (int32_t)row * FBLE_ROW_H;
        cv_text_fit(FBLE_X_PAD, y + 2, &AF_S, "NAME", T_DIM, T_BG, 60);
        cv_text_fit(70, y + 2, &AF_S, CONFIG_FIRMWARE_NAME, T_TEXT, T_BG, 166);
        row++;
    }

    /* MIDI status */
    {
        int32_t y = (int32_t)row * FBLE_ROW_H;
        cv_text_fit(FBLE_X_PAD, y + 2, &AF_S, "MIDI", T_DIM, T_BG, 60);
        cv_text_fit(70, y + 2, &AF_S,
                    ble_midi_ready() ? "SUBSCRIBED" : "WAITING",
                    ble_midi_ready() ? T_THEME : T_MID, T_BG, 166);
        row++;
    }

    /* separator */
    {
        int32_t y = (int32_t)row * FBLE_ROW_H + FBLE_ROW_H / 2;
        cv_rect(FBLE_X_PAD, y, 240 - 2 * FBLE_X_PAD, 1, T_LINE);
        row++;
    }

#if BLE_CENTRAL
    /* scanned devices header */
    {
        int32_t y = (int32_t)row * FBLE_ROW_H;
        cv_text_fit(FBLE_X_PAD, y + 2, &AF_S, "NEARBY DEVICES", T_MID, T_BG, 230);
        row++;
    }

    /* list scanned devices */
    {
        uint8_t idx[BLE_SCAN_N];
        uint32_t n = ble_scan_list(&ble_scan_tab, idx);
        uint32_t i;
        if (n == 0) {
            int32_t y = (int32_t)row * FBLE_ROW_H;
            cv_text_fit(FBLE_X_PAD, y + 2, &AF_S, "(scanning...)", T_DIM, T_BG, 230);
        }
        for (i = fble_scroll; i < n && row < FBLE_VISIBLE; i++) {
            int32_t y = (int32_t)row * FBLE_ROW_H;
            const struct ble_found *e = &ble_scan_tab.e[idx[i]];
            uint32_t bars = ble_scan_bars(&ble_scan_tab, idx[i]);

            /* signal strength indicator */
            {
                uint32_t b;
                for (b = 0; b < 4; b++) {
                    int32_t bx = FBLE_X_PAD + (int32_t)b * 4;
                    int32_t bh = 3 + (int32_t)b * 2;
                    int32_t by = y + FBLE_ROW_H - 2 - bh;
                    cv_rect(bx, by, 3, bh, b < bars ? T_THEME : T_LINE);
                }
            }

            /* device name */
            cv_text_fit(FBLE_X_PAD + 20, y + 2, &AF_S,
                        e->name[0] ? e->name : "(unnamed)",
                        e->midi ? T_TEXT : T_DIM, T_BG, 200);
            row++;
        }
    }
#else
    /* peripheral only: show services */
    {
        int32_t y = (int32_t)row * FBLE_ROW_H;
        cv_text_fit(FBLE_X_PAD, y + 2, &AF_S, "SERVICES", T_MID, T_BG, 230);
        row++;
    }
    {
        int32_t y = (int32_t)row * FBLE_ROW_H;
        cv_text_fit(FBLE_X_PAD + 8, y + 2, &AF_S, "BLE-MIDI", T_TEXT, T_BG, 200);
        row++;
    }
    if (row < FBLE_VISIBLE) {
        int32_t y = (int32_t)row * FBLE_ROW_H;
        cv_text_fit(FBLE_X_PAD + 8, y + 2, &AF_S, "GATT Server",
                    T_DIM, T_BG, 200);
    }
#endif

    cv_blit(0, Y_GRAPH);
}

static void fble_draw(void)
{
    fui_draw_head();

    /* cards: BLE status, connection, MIDI, role */
    fui_draw_card(0, "BLE",  ble_connected() ? "ON" : "ADV",
                  "", ble_connected() ? T_THEME : T_MID, -1, ICON_GENERIC);
    fui_draw_card(1, "LINK", ble_connected() ? "YES" : "NO",
                  "", ble_connected() ? T_THEME : T_DIM, -1, ICON_GENERIC);
    fui_draw_card(2, "MIDI", ble_midi_ready() ? "OK" : "---",
                  "", ble_midi_ready() ? T_THEME : T_DIM, -1, ICON_MIDI);
#if BLE_CENTRAL
    fui_draw_card(3, "ROLE", "C+P", "", T_MID, -1, ICON_GENERIC);
#else
    fui_draw_card(3, "ROLE", "PRPH", "", T_MID, -1, ICON_GENERIC);
#endif

    fble_draw_info();

    fui_draw_foot("BLE", "---", "BLUETOOTH");
}

#if BLE_CENTRAL
static int fble_knob(uint32_t id, int32_t clicks)
{
    if (id == 0) {
        uint8_t idx[BLE_SCAN_N];
        uint32_t n = ble_scan_list(&ble_scan_tab, idx);
        int32_t s = (int32_t)fble_scroll + clicks;
        if (s < 0) s = 0;
        if (n > 2 && (uint32_t)s > n - 2)
            s = (int32_t)(n - 2);
        fble_scroll = (uint32_t)s;
        fsh_dirty = 1;
        return 1;
    }
    return 0;
}
#endif

static void fble_enter(void)
{
    fble_scroll = 0;
}

static const cfm1_page_t FPAGE_BLE = {
    .name   = "BLE",
    .title  = "BLUETOOTH",
    .flags  = PAGEF_SYSTEM,
    .enter  = fble_enter,
    .draw   = fble_draw,
#if BLE_CENTRAL
    .knob   = fble_knob,
#endif
};
