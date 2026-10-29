/* SPDX-License-Identifier: MIT */
/* Element dispatch: routes element calls to the active theme.
 * Included in the unity build after ui_theme.h and before the shell. */

static const ui_theme_t *ui_active_theme;

static void ui_set_theme(const ui_theme_t *t) { ui_active_theme = t; }

static void ui_header(void)
{
    if (ui_active_theme && ui_active_theme->render_header)
        ui_active_theme->render_header();
}

static void ui_footer(const char *eng, const char *preset, const char *title)
{
    if (ui_active_theme && ui_active_theme->render_footer)
        ui_active_theme->render_footer(eng, preset, title);
}

static void ui_card(uint32_t slot, const char *label, const char *val,
                    const char *unit, uint16_t color, int32_t ratio,
                    uint32_t icon)
{
    if (ui_active_theme && ui_active_theme->render_card)
        ui_active_theme->render_card(slot, label, val, unit, color, ratio, icon);
}
