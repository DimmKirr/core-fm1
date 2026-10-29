/* SPDX-License-Identifier: GPL-3.0-only */
/* Felucca theme: visual identity for the Felucca shell.
 * Exposes layout metrics, palette, font pointers, and the
 * ui_theme_t descriptor that the element layer dispatches through.
 * Included after gfx.c and draw.c in the unity build. */

static const ui_theme_t felucca_theme = {
    .screen_w  = 240,
    .screen_h  = 240,
    .header_h  = H_HEAD,
    .footer_h  = H_FOOT,
    .card_h    = CARD_H,
    .card_w    = CARD_W,

    .font_s    = &AF_S,
    .font_m    = &AF_M,
    .font_l    = &AF_L,

    .render_header = fui_draw_head,
    .render_footer = fui_draw_foot,
    .render_card   = fui_draw_card,
};
