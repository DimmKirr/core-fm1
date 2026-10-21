/* SPDX-License-Identifier: GPL-3.0-only */
/* Felucca-styled UI shell: uses upstream gfx.c renderer (AA fonts, SURF
 * cards, palettes) with mock/adapter data through cfm1_page_t pages.
 * Visual style matches Felucca; content comes from adapters. */

/* icon rendering: extracted from upstream icons.c (cv_icon_*, cv_trk only;
 * the label-lookup tables are excluded: they depend on full subsystems) */
#include "ui/felucca/fui_icons.c"

/* Felucca layout constants */
#define H_HEAD   24
#define Y_CARDS  28
#define CARD_W   57
#define CARD_H   44
#define CARD_X(c) (3 + 59 * (int32_t)(c))
#define Y_GRAPH  76
#define H_FOOT   38
#define Y_FOOT   (240 - H_FOOT)

#include "ui/felucca/draw.c"
#include "ui/felucca/shell.c"
#include "ui/felucca/page_home.c"
#include "ui/felucca/page_params.c"
#include "ui/felucca/page_seq.c"
#include "ui/felucca/page_mixer.c"
#include "ui/felucca/page_about.c"
#include "ui/felucca/pages.c"
