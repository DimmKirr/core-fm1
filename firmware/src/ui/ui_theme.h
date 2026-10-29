/* SPDX-License-Identifier: MIT */
/* Theme descriptor: a shell's visual identity.
 * Each shell provides one static ui_theme_t with its layout metrics,
 * font pointers, and element render functions. The element layer
 * dispatches through whichever theme is active. */
#ifndef UI_THEME_H
#define UI_THEME_H
#include <stdint.h>

typedef struct ui_theme {
    uint16_t screen_w, screen_h;
    uint16_t header_h, footer_h;
    uint16_t card_h, card_w;

    const void *font_s;
    const void *font_m;
    const void *font_l;

    void (*render_header)(void);
    void (*render_footer)(const char *eng, const char *preset, const char *title);
    void (*render_card)(uint32_t slot, const char *label, const char *val,
                        const char *unit, uint16_t color, int32_t ratio,
                        uint32_t icon);
} ui_theme_t;

#endif /* UI_THEME_H */
