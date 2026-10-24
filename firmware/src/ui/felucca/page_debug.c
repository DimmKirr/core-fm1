/* SPDX-License-Identifier: GPL-3.0-only */
/* DEBUG page: scrollable view of the cfm1_log ring buffer. */

#define DBG_VISIBLE   8
#define DBG_ROW_H     15
#define DBG_Y_START   Y_GRAPH
#define DBG_X_PAD     6
#define DBG_TS_W      66
#define DBG_MSG_X     (DBG_X_PAD + DBG_TS_W)
#define DBG_MSG_W     (240 - DBG_MSG_X)
#define DBG_WRAP_AT   24

static uint32_t fdbg_last_count;

static void fdbg_fmt_ts(char *out, uint32_t ms)
{
    uint32_t mm  = (ms / 60000u) % 100u;
    uint32_t ss  = (ms / 1000u) % 60u;
    uint32_t frac = ms % 1000u;
    out[0] = '0' + (char)(mm / 10u);
    out[1] = '0' + (char)(mm % 10u);
    out[2] = ':';
    out[3] = '0' + (char)(ss / 10u);
    out[4] = '0' + (char)(ss % 10u);
    out[5] = '.';
    out[6] = '0' + (char)(frac / 100u);
    out[7] = '0' + (char)((frac / 10u) % 10u);
    out[8] = '0' + (char)(frac % 10u);
    out[9] = 0;
}

static uint32_t fdbg_strlen(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static void fdbg_draw_lines(void)
{
    uint32_t row = 0;
    uint32_t i;
    cv_begin(240, DBG_VISIBLE * DBG_ROW_H + 4, T_BG);

    if (cfm1_log_count == 0) {
        cv_text_in(0, DBG_ROW_H, 240, &AF_S, "(no log entries)", T_DIM, T_BG);
    }

    for (i = 0; row < DBG_VISIBLE && (cfm1_log_scroll + i) < cfm1_log_count; i++) {
        uint32_t idx = cfm1_log_scroll + i;
        const char *line = cfm1_log_line(idx);
        uint32_t len = fdbg_strlen(line);
        int32_t row_y = (int32_t)row * DBG_ROW_H;

        /* MM:SS.mmm timestamp */
        {
            char ts[10];
            fdbg_fmt_ts(ts, cfm1_log_time(idx));
            cv_text_fit(DBG_X_PAD, row_y + 2, &AF_S, ts, T_DIM, T_BG, DBG_TS_W - 2);
        }

        cv_text_fit(DBG_MSG_X, row_y + 2, &AF_S, line, T_TEXT, T_BG, DBG_MSG_W);
        row++;

        /* wrap: if message is longer than one line, continue without timestamp */
        if (len > DBG_WRAP_AT && row < DBG_VISIBLE) {
            char cont[CFM1_LOG_LINE_LEN];
            uint32_t j;
            for (j = 0; j + DBG_WRAP_AT < len && j < CFM1_LOG_LINE_LEN - 1; j++)
                cont[j] = line[j + DBG_WRAP_AT];
            cont[j] = 0;
            row_y = (int32_t)row * DBG_ROW_H;
            cv_text_fit(DBG_MSG_X, row_y + 2, &AF_S, cont, T_TEXT, T_BG, DBG_MSG_W);
            row++;
        }
    }

    cv_blit(0, DBG_Y_START);
}

static void fdbg_draw(void)
{
    fui_draw_head();

    /* info cards */
    {
        char cnt[8];
        fui_fmt_val(cnt, (int16_t)cfm1_log_count);
        fui_draw_card(0, "LOG", cnt, "", T_THEME, -1, ICON_GENERIC);
    }
    {
        char scr[8];
        fui_fmt_val(scr, (int16_t)(cfm1_log_scroll + 1));
        fui_draw_card(1, "FROM", scr, "", T_MID, -1, ICON_GENERIC);
    }
    {
        char buf[8];
        fui_fmt_val(buf, (int16_t)CFM1_LOG_LINES);
        fui_draw_card(2, "BUF", buf, "", T_DIM, -1, ICON_GENERIC);
    }
    {
        char len[8];
        fui_fmt_val(len, (int16_t)CFM1_LOG_LINE_LEN);
        fui_draw_card(3, "LEN", len, "", T_DIM, -1, ICON_GENERIC);
    }

    fdbg_draw_lines();

    fui_draw_foot("DBG", "---", "DEBUG");
}

static int fdbg_knob(uint32_t id, int32_t clicks)
{
    if (id == 0 && cfm1_log_count > 0) {
        int32_t s = (int32_t)cfm1_log_scroll + clicks;
        if (s < 0) s = 0;
        if (cfm1_log_count > DBG_VISIBLE) {
            if (s > (int32_t)(cfm1_log_count - DBG_VISIBLE))
                s = (int32_t)(cfm1_log_count - DBG_VISIBLE);
        } else {
            s = 0;
        }
        cfm1_log_scroll = (uint32_t)s;
        fsh_dirty = 1;
        return 1;
    }
    return 0;
}

static void fdbg_enter(void)
{
    fdbg_last_count = cfm1_log_count;
    if (cfm1_log_count > DBG_VISIBLE)
        cfm1_log_scroll = cfm1_log_count - DBG_VISIBLE;
    else
        cfm1_log_scroll = 0;
}

static int fdbg_at_bottom(void)
{
    if (cfm1_log_count <= DBG_VISIBLE)
        return 1;
    return cfm1_log_scroll >= cfm1_log_count - DBG_VISIBLE;
}

static void fdbg_status(void)
{
    if (cfm1_log_count != fdbg_last_count) {
        int was_bottom = fdbg_at_bottom();
        fdbg_last_count = cfm1_log_count;
        if (was_bottom) {
            if (cfm1_log_count > DBG_VISIBLE)
                cfm1_log_scroll = cfm1_log_count - DBG_VISIBLE;
            else
                cfm1_log_scroll = 0;
        }
        fsh_dirty = 1;
    }
}

static const cfm1_page_t FPAGE_DEBUG = {
    .name   = "DEBUG",
    .title  = "DEBUG",
    .flags  = PAGEF_SYSTEM,
    .enter  = fdbg_enter,
    .draw   = fdbg_draw,
    .status = fdbg_status,
    .knob   = fdbg_knob,
};
