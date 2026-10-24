/* SPDX-License-Identifier: GPL-3.0-only */
/* Melodee USB Audio stub: class-compliant stereo/4-track capture.
 * TODO: port from upstream melodee (usb_audio.c, usb_audio_stream.c,
 * usb_audio_desc.h). */

static struct {
    uint8_t active;
    uint8_t alt_play, alt_cap;
    uint32_t poll_max_ticks;
    uint32_t service_max_ticks;
} ua;

static void ua_init(void)
{
    __builtin_memset(&ua, 0, sizeof(ua));
}

static void ua_service(void) {}
static void ua_start(void) {}
static void ua_stop(void) {}
