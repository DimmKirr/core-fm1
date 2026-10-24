/* SPDX-License-Identifier: GPL-3.0-only */
/* Melodee SysEx editor stub: web editor protocol (F0 7D 46 4C cmd.. F7).
 * TODO: port from upstream melodee (editor.c, editor_native.c,
 * editor_preferences.c, editor_backup.c, editor_fm6.c, editor_cz.c,
 * editor_prophet.c, editor_library.c, sysex_library.c). */

#define ED_HDR0 0x7Du
#define ED_HDR1 0x46u
#define ED_HDR2 0x4Cu

static void editor_frame(const uint8_t *data, uint32_t len)
{
    (void)data; (void)len;
}

static void editor_poll(void) {}

static void editor_init(void) {}
