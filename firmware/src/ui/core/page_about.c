/* SPDX-License-Identifier: GPL-3.0-only */
/* core-fm1 ABOUT page: firmware info. */

static void about_draw(void)
{
    boot_text_1x(20, "core-fm1", 0xFFFF, 0x0000);
    boot_text_1x(36, CONFIG_FIRMWARE_VERSION, 0x8410, 0x0000);

    char buf[24];
    uint32_t i = 0;
    const char *pre = "engines: ";
    while (pre[i]) { buf[i] = pre[i]; i++; }
    buf[i++] = '0' + (char)(NENGINES % 10);
    buf[i] = 0;
    boot_text_1x(56, buf, 0x8410, 0x0000);

    boot_text_1x(76, "pages:", 0x8410, 0x0000);
    i = 0;
    buf[i++] = '0' + (char)(shell_npage % 10);
    buf[i] = 0;
    boot_text_1x(86, buf, 0x8410, 0x0000);
}

static const cfm1_page_t PAGE_ABOUT = {
    .name  = "ABOUT",
    .title = "ABOUT",
    .flags = PAGEF_SYSTEM,
    .draw  = about_draw,
};
