/* SPDX-License-Identifier: GPL-3.0-only */
/* Confirm dialog modal: YES/NO or OK acknowledgment.
 * Overlays the graph area with a centered dialog box.
 * YES/NO: OCT+ = YES, OCT- = NO (direct button mapping).
 * OK: OCT+ or OCT- both dismiss. */

#define DLG_YES_NO  0
#define DLG_OK      1

static struct {
    const char *title;
    const char *body;
    uint8_t    mode;
    void (*on_done)(uint8_t result);
} dlg;

#define DLG_BOX_W  210
#define DLG_BOX_H   84
#define DLG_BOX_X  ((240 - DLG_BOX_W) / 2)
#define DLG_BOX_Y  (((int32_t)(Y_FOOT - Y_GRAPH) - DLG_BOX_H) / 2)
#define DLG_BTN_W   85
#define DLG_BTN_H   18
#define DLG_BTN_R    4
#define DLG_BTN_Y  (DLG_BOX_Y + DLG_BOX_H - DLG_BTN_H - 8)

static void dlg_draw(void)
{
    uint32_t gh = Y_FOOT - Y_GRAPH;

    cv_begin(240, gh, T_BG);

    cv_rrect(DLG_BOX_X, DLG_BOX_Y, DLG_BOX_W, DLG_BOX_H, 6, T_SURF, T_BG);

    if (dlg.title && dlg.title[0])
        cv_text_in(DLG_BOX_X, DLG_BOX_Y + 10, DLG_BOX_W,
                   &AF_M, dlg.title, T_THEME, T_SURF);

    if (dlg.body && dlg.body[0])
        cv_text_in(DLG_BOX_X, DLG_BOX_Y + 34, DLG_BOX_W,
                   &AF_S, dlg.body, T_TEXT, T_SURF);

    if (dlg.mode == DLG_YES_NO) {
        int32_t nx = DLG_BOX_X + 10;
        int32_t yx = DLG_BOX_X + DLG_BOX_W - DLG_BTN_W - 10;

        cv_rrect(nx, DLG_BTN_Y, DLG_BTN_W, DLG_BTN_H, DLG_BTN_R,
                 T_RAISE, T_SURF);
        cv_text_in(nx, DLG_BTN_Y + 3, DLG_BTN_W, &AF_S, "NO", T_MID, T_RAISE);

        cv_rrect(yx, DLG_BTN_Y, DLG_BTN_W, DLG_BTN_H, DLG_BTN_R,
                 T_THEME, T_SURF);
        cv_text_in(yx, DLG_BTN_Y + 3, DLG_BTN_W, &AF_S, "YES", T_INK, T_THEME);
    } else {
        int32_t ox = DLG_BOX_X + (DLG_BOX_W - DLG_BTN_W) / 2;

        cv_rrect(ox, DLG_BTN_Y, DLG_BTN_W, DLG_BTN_H, DLG_BTN_R,
                 T_THEME, T_SURF);
        cv_text_in(ox, DLG_BTN_Y + 3, DLG_BTN_W, &AF_S, "OK", T_INK, T_THEME);
    }

    cv_blit(0, Y_GRAPH);

    if (dlg.mode == DLG_YES_NO)
        ui_footer("NO", "", "YES");
    else
        ui_footer("", "", "OK");
}

static int dlg_knob(uint32_t id, int32_t clicks)
{
    (void)id; (void)clicks;
    return 1;
}

static int dlg_button(uint32_t id, uint32_t down)
{
    if (down) return 1;
    switch (id) {
    case BTN_OCTUP: {
        void (*cb)(uint8_t) = dlg.on_done;
        nav_modal_close();
        if (cb) cb(1u);
        return 1;
    }
    case BTN_OCTDN: {
        uint8_t r = (dlg.mode == DLG_OK) ? 1u : 0u;
        void (*cb)(uint8_t) = dlg.on_done;
        nav_modal_close();
        if (cb) cb(r);
        return 1;
    }
    default:
        break;
    }
    return 1;
}

static int dlg_key(uint32_t note, uint32_t vel)
{
    (void)note; (void)vel;
    return 1;
}

static const cfm1_modal_t dlg_confirm_modal = {
    .draw   = dlg_draw,
    .knob   = dlg_knob,
    .button = dlg_button,
    .key    = dlg_key,
};

static void dlg_confirm(const char *title, const char *body,
                        void (*on_done)(uint8_t result))
{
    dlg.title   = title;
    dlg.body    = body;
    dlg.mode    = DLG_YES_NO;
    dlg.on_done = on_done;
    nav_modal_open(&dlg_confirm_modal);
}

static void dlg_ok(const char *title, const char *body,
                   void (*on_done)(uint8_t result))
{
    dlg.title   = title;
    dlg.body    = body;
    dlg.mode    = DLG_OK;
    dlg.on_done = on_done;
    nav_modal_open(&dlg_confirm_modal);
}
