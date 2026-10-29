/* SPDX-License-Identifier: MIT */
/* Element API: theme-agnostic drawing primitives.
 * Pages call these functions; the active theme's renderers do the work.
 * Each function dispatches through the registered ui_theme_t. */
#ifndef UI_ELEMENTS_H
#define UI_ELEMENTS_H
#include <stdint.h>

void ui_header(void);
void ui_footer(const char *eng, const char *preset, const char *title);
void ui_card(uint32_t slot, const char *label, const char *val,
             const char *unit, uint16_t color, int32_t ratio, uint32_t icon);

#endif /* UI_ELEMENTS_H */
