/* SPDX-License-Identifier: MIT */
/* USB Audio adapter: stubs when CONFIG_USB_AUDIO is off. */
#ifndef CFM1_USB_AUDIO_H
#define CFM1_USB_AUDIO_H
#include <stdint.h>

#ifndef CONFIG_USB_AUDIO
static void ua_init(void) {}
static void ua_service(void) {}
static void ua_start(void) {}
static void ua_stop(void) {}
#endif

#endif /* CFM1_USB_AUDIO_H */
