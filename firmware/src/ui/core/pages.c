/* SPDX-License-Identifier: GPL-3.0-only */
/* core-fm1 shell: page registration with view assignments. */

#include "ui/core/page_home.c"
#include "ui/core/page_engine.c"
#include "ui/core/page_seq.c"
#include "ui/core/page_fx.c"
#include "ui/core/page_about.c"

static void shell_register_pages(void)
{
    nav_register(&PAGE_HOME);
    nav_register(&PAGE_ENGINE);
    nav_register(&PAGE_SEQ);
    nav_register(&PAGE_FX);
    nav_register(&PAGE_ABOUT);
}
