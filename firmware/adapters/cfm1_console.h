/* SPDX-License-Identifier: MIT */
/* Console adapter: CDC serial console stubs. */
#ifndef CFM1_CONSOLE_H
#define CFM1_CONSOLE_H
#include <stdint.h>

#ifndef CONFIG_CONSOLE
static void console_init(void) {}
static void console_poll(void) {}
#endif

#endif /* CFM1_CONSOLE_H */
