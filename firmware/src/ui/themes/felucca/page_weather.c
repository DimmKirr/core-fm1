/* SPDX-License-Identifier: GPL-3.0-only */
/* WEATHER page: current weather fetched via WiFi (ip-api + open-meteo).
 * Layout: colored background area with glyph, temperature, condition,
 * city/country, humidity, and wind speed. Spinner during fetch. */

#include "ui/themes/felucca/weather_colors.h"
#include "ui/themes/felucca/weather_glyphs.h"

static uint32_t fwx_frame;

/* format temp_c (x10 int) into string like "22.5" or "-3.0" */
static void fwx_fmt_temp(char *buf, int16_t tc)
{
    int neg = tc < 0;
    uint32_t i = 0;
    uint16_t v;
    if (neg) { buf[i++] = '-'; tc = -tc; }
    v = (uint16_t)tc;
    if (v >= 1000) buf[i++] = '0' + (char)(v / 1000);
    if (v >= 100)  buf[i++] = '0' + (char)((v / 100) % 10);
    buf[i++] = '0' + (char)((v / 10) % 10);
    buf[i++] = '.';
    buf[i++] = '0' + (char)(v % 10);
    buf[i] = 0;
}

/* format humidity as "65%" */
static void fwx_fmt_hum(char *buf, uint8_t h)
{
    uint32_t i = 0;
    if (h >= 100) buf[i++] = '1';
    if (h >= 10)  buf[i++] = '0' + (char)((h / 10) % 10);
    buf[i++] = '0' + (char)(h % 10);
    buf[i++] = '%';
    buf[i] = 0;
}

/* format wind x10 as "12.5" */
static void fwx_fmt_wind(char *buf, uint16_t w)
{
    uint32_t i = 0;
    if (w >= 1000) buf[i++] = '0' + (char)(w / 1000);
    if (w >= 100)  buf[i++] = '0' + (char)((w / 100) % 10);
    buf[i++] = '0' + (char)((w / 10) % 10);
    buf[i++] = '.';
    buf[i++] = '0' + (char)(w % 10);
    buf[i] = 0;
}

static void fwx_draw_loading(void)
{
    uint32_t h = Y_FOOT - Y_GRAPH;
    cv_begin(240, h, T_BG);

    fui_draw_spinner(120, (int32_t)(h / 2) - 10, 12,
                     fwx_frame, T_THEME, T_DIM);
    cv_text_in(0, (int32_t)(h / 2) + 8, 240, &AF_S,
               "FETCHING WEATHER...", T_MID, T_BG);

    cv_blit(0, Y_GRAPH);
}

static void fwx_draw_error(void)
{
    uint32_t h = Y_FOOT - Y_GRAPH;
    cv_begin(240, h, T_BG);

    cv_text_in(0, (int32_t)(h / 2) - 14, 240, &AF_S,
               "NO WEATHER DATA", T_DIM, T_BG);
    cv_text_in(0, (int32_t)(h / 2) + 4, 240, &AF_S,
               "CHECK WIFI CONNECTION", T_DIM, T_BG);

    cv_blit(0, Y_GRAPH);
}

static void fwx_draw_weather(const struct weather_data *w)
{
    uint32_t h = Y_FOOT - Y_GRAPH;
    uint16_t bg = weather_color(w->wmo_code);
    int glyph = weather_glyph_for_code(w->wmo_code);
    const char *cond = weather_condition_name(w->wmo_code);
    char temp[12], hum[8], wind[12];

    fwx_fmt_temp(temp, w->temp_c);
    fwx_fmt_hum(hum, w->humidity);
    fwx_fmt_wind(wind, w->wind_kmh);

    cv_begin(240, h, bg);

    /* glyph: 32x32, left side */
    weather_draw_glyph(12, 8, glyph, WTHR_FG, bg);

    /* temperature: large font, right of glyph */
    cv_text_on(56, 4, &AF_L, temp, WTHR_FG, bg);
    {
        int32_t tx = 56 + text_w(&AF_L, temp) + 4;
        cv_text_on(tx, 8, &AF_S, "C", WTHR_FG_DIM, bg);
    }

    /* condition name */
    cv_text_fit(56, 34, &AF_S, cond, WTHR_FG_DIM, bg, 180);

    /* city, country (bottom left) */
    if (w->city[0]) {
        char loc[32];
        uint32_t li = 0;
        const char *s = w->city;
        while (*s && li < sizeof(loc) - 6) loc[li++] = *s++;
        if (w->country[0]) {
            loc[li++] = ',';
            loc[li++] = ' ';
            s = w->country;
            while (*s && li < sizeof(loc) - 1) loc[li++] = *s++;
        }
        loc[li] = 0;
        cv_text_fit(12, 52, &AF_S, loc, WTHR_FG_DIM, bg, 120);
    }

    /* humidity + wind (bottom right) */
    {
        char info[24];
        uint32_t ii = 0;
        const char *s;
        s = hum;
        while (*s) info[ii++] = *s++;
        info[ii++] = ' ';
        info[ii++] = ' ';
        s = wind;
        while (*s) info[ii++] = *s++;
        s = " km/h";
        while (*s) info[ii++] = *s++;
        info[ii] = 0;
        {
            int32_t tw = text_w(&AF_S, info);
            cv_text_on(228 - tw, 52, &AF_S, info, WTHR_FG_DIM, bg);
        }
    }

    /* spinner overlay during refresh */
    if (weather_fetching()) {
        fui_draw_spinner(228, 10, 6, fwx_frame, WTHR_FG, bg);
    }

    cv_blit(0, Y_GRAPH);
}

static void fwx_draw(void)
{
    const struct weather_data *w = weather_get();

    ui_header();

    /* cards: temp, humidity, wind, condition */
    if (w->valid) {
        char temp[8], hum[8], wind[8];
        fwx_fmt_temp(temp, w->temp_c);
        fwx_fmt_hum(hum, w->humidity);
        fwx_fmt_wind(wind, w->wind_kmh);

        ui_card(0, "TEMP", temp, "C",
                      T_THEME, -1, ICON_GENERIC);
        ui_card(1, "HUMID", hum, "",
                      T_THEME, (int32_t)w->humidity * 10, ICON_GENERIC);
        ui_card(2, "WIND", wind, "km/h",
                      T_MID, -1, ICON_GENERIC);
        ui_card(3, "WMO", weather_condition_name(w->wmo_code), "",
                      T_MID, -1, ICON_GENERIC);
    } else {
        ui_card(0, "TEMP", "---", "", T_DIM, -1, ICON_GENERIC);
        ui_card(1, "HUMID", "---", "", T_DIM, -1, ICON_GENERIC);
        ui_card(2, "WIND", "---", "", T_DIM, -1, ICON_GENERIC);
        ui_card(3, "WMO", "---", "", T_DIM, -1, ICON_GENERIC);
    }

    /* main area */
    if (weather_fetching() && !w->valid)
        fwx_draw_loading();
    else if (w->valid)
        fwx_draw_weather(w);
    else
        fwx_draw_error();

    fwx_frame++;

    ui_footer("WIFI", w->valid ? w->city : "---", "WEATHER");
}

static void fwx_enter(void)
{
    fwx_frame = 0;
    if (!weather_get()->valid && !weather_fetching())
        weather_fetch_start();
}

static int fwx_button(uint32_t id, uint32_t down)
{
    if (!down && id == BTN_PLAY) {
        weather_fetch_start();
        nav_dirty = 1;
        return 1;
    }
    return 0;
}

static const cfm1_page_t FPAGE_WEATHER = {
    .name   = "WX",
    .title  = "WEATHER",
    .view   = VIEW_CONN,
    .flags  = PAGEF_SYSTEM,
    .enter  = fwx_enter,
    .draw   = fwx_draw,
    .button = fwx_button,
};
