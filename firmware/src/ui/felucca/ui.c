/* SPDX-License-Identifier: GPL-3.0-only */
/* Felucca UI shim: includes upstream subtree sources in unity-build order.
 * Felucca's felucca.c include order: lcd, gfx, [audio...], panel, ui,
 * menu_items, icons, ui_graph, ui_draw, ui_menu, ui_input, ui_layer.
 * lcd.c and gfx.c are included by core_fm1.c before this file. */

#include "upstream/felucca/firmware/src/panel.c"
#include "upstream/felucca/firmware/src/ui.c"
#include "upstream/felucca/firmware/src/menu_items.c"
#include "upstream/felucca/firmware/src/icons.c"
#include "upstream/felucca/firmware/src/ui_graph.c"
#include "upstream/felucca/firmware/src/ui_draw.c"
#include "upstream/felucca/firmware/src/ui_menu.c"
#include "upstream/felucca/firmware/src/ui_input.c"
#include "upstream/felucca/firmware/src/ui_layer.c"
