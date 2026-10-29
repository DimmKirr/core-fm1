/* SPDX-License-Identifier: MIT */
/* View IDs: a view is a button-addressable group of related pages.
 * Pressing a button opens the view; the encoder cycles pages within it.
 * See docs/UI.md for the full view/page/element architecture. */
#ifndef UI_VIEW_H
#define UI_VIEW_H

#define VIEW_HOME      0
#define VIEW_ENV       1
#define VIEW_LFO       2
#define VIEW_FX        3
#define VIEW_SCL       4
#define VIEW_EDIT      5
#define VIEW_GLO       6
#define VIEW_SAVE      7
#define VIEW_ARP       8
#define VIEW_SEQ       9
#define VIEW_CONN     10
#define VIEW_SETTINGS 11
#define VIEW_COUNT    12
#define VIEW_NONE    255

#endif /* UI_VIEW_H */
