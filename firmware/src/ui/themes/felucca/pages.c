/* SPDX-License-Identifier: GPL-3.0-only */
/* Felucca shell: page registration grouped by view.
 * Pages within the same view are registered consecutively so the encoder
 * cycles them in a natural order. */

static void fsh_register_pages(void)
{
    /* VIEW_HOME */
    nav_register(&FPAGE_HOME);

    /* VIEW_ENV */
    nav_register(&FPAGE_ENV);

    /* VIEW_LFO */
    nav_register(&FPAGE_LFO);

    /* VIEW_FX */
    nav_register(&FPAGE_FX);

    /* VIEW_SCL */
    nav_register(&FPAGE_SCL);

    /* VIEW_EDIT */
    nav_register(&FPAGE_EDIT);

    /* VIEW_GLO: mixer, voice */
    nav_register(&FPAGE_MIXER);
    nav_register(&FPAGE_VOICE);

    /* VIEW_SAVE */
#ifdef CONFIG_UDISK
    nav_register(&FPAGE_FILES);
#endif

    /* VIEW_ARP */
    nav_register(&FPAGE_ARP);

    /* VIEW_SEQ */
    nav_register(&FPAGE_SEQ);

    /* VIEW_CONN: connectivity */
#ifdef CONFIG_WIFI
    nav_register(&FPAGE_WIFI);
    nav_register(&FPAGE_WEATHER);
#endif
#ifdef CONFIG_BLE
    nav_register(&FPAGE_BLE);
#endif

    /* VIEW_SETTINGS */
#ifdef CONFIG_SYS_MENU
    nav_register(&FPAGE_SYS);
#endif
    nav_register(&FPAGE_ABOUT);
#ifdef CONFIG_DEBUG_LOG
    nav_register(&FPAGE_DEBUG);
#endif

    /* VIEW_DEMO */
    nav_register(&FPAGE_DEMO);
}
