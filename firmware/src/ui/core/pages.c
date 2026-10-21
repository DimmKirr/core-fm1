/* SPDX-License-Identifier: GPL-3.0-only */
/* core-fm1 shell: page registration.
 * Include order matters: pages reference shell_*, boot_text_1x, adapters. */

#include "ui/core/page_home.c"
#include "ui/core/page_engine.c"
#include "ui/core/page_seq.c"
#include "ui/core/page_fx.c"
#include "ui/core/page_about.c"

static void shell_register_pages(void)
{
    shell_register(&PAGE_HOME);
    shell_register(&PAGE_ENGINE);
    shell_register(&PAGE_SEQ);
    shell_register(&PAGE_FX);
    shell_register(&PAGE_ABOUT);
}
