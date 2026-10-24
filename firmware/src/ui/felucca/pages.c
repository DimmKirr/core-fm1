/* SPDX-License-Identifier: GPL-3.0-only */
/* Felucca shell: page registration.
 * Matches Felucca's tab order: HOME, ENV, LFO, FX, SCL, EDIT, ARP, SEQ, MIXER, VOICE, ABOUT. */

static void fsh_register_pages(void)
{
    fsh_register(&FPAGE_HOME);
    fsh_register(&FPAGE_ENV);
    fsh_register(&FPAGE_LFO);
    fsh_register(&FPAGE_FX);
    fsh_register(&FPAGE_SCL);
    fsh_register(&FPAGE_EDIT);
    fsh_register(&FPAGE_ARP);
    fsh_register(&FPAGE_SEQ);
    fsh_register(&FPAGE_MIXER);
    fsh_register(&FPAGE_VOICE);
    fsh_register(&FPAGE_ABOUT);
#ifdef CONFIG_SYS_MENU
    fsh_register(&FPAGE_SYS);
#endif
#ifdef CONFIG_UDISK
    fsh_register(&FPAGE_FILES);
#endif
#ifdef CONFIG_BLE
    fsh_register(&FPAGE_BLE);
#endif
#ifdef CONFIG_DEBUG_LOG
    fsh_register(&FPAGE_DEBUG);
#endif
}
