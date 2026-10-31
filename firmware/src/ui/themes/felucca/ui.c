/* SPDX-License-Identifier: GPL-3.0-only */
/* Felucca-styled UI shell: uses upstream gfx.c renderer (AA fonts, SURF
 * cards, palettes) with mock/adapter data through cfm1_page_t pages.
 * Visual style matches Felucca; content comes from adapters. */

/* icon rendering: extracted from upstream icons.c (cv_icon_*, cv_trk only;
 * the label-lookup tables are excluded: they depend on full modules) */
#include "ui/themes/felucca/fui_icons.c"

/* Felucca layout constants */
#define H_HEAD   24
#define Y_CARDS  28
#define CARD_W   57
#define CARD_H   44
#define CARD_X(c) (3 + 59 * (int32_t)(c))
#define Y_GRAPH  76
#define H_FOOT   38
#define Y_FOOT   (240 - H_FOOT)

#include "ui/themes/felucca/draw.c"
#include "ui/themes/felucca/theme.c"
#include "ui/themes/felucca/shell.c"
#include "ui/themes/felucca/page_home.c"
#include "ui/themes/felucca/page_params.c"
#include "ui/themes/felucca/page_seq.c"
#include "ui/themes/felucca/page_mixer.c"
#include "ui/themes/felucca/page_about.c"
#ifdef CONFIG_UDISK
#include "ui/themes/felucca/page_files.c"
#endif
#ifdef CONFIG_BLE
#include "ui/themes/felucca/page_ble.c"
#endif
#ifdef CONFIG_WIFI
#include "ui/themes/felucca/page_wifi.c"
#include "ui/themes/felucca/page_weather.c"
#endif
#ifdef CONFIG_DEBUG_LOG
#include "ui/themes/felucca/page_debug.c"
#endif
#ifdef CONFIG_SYS_MENU
#include "ui/themes/felucca/page_sys.c"
#endif
#include "ui/themes/felucca/kbd_piano.c"
#include "ui/themes/felucca/page_demo.c"
#include "ui/themes/felucca/pages.c"
