/* SPDX-License-Identifier: GPL-3.0-only */
/* core-fm1 FX page: effects chain overview (stub). */

static void fx_page_draw(void)
{
    boot_text_1x(20, "EFFECTS", 0xFFFF, 0x0000);

    uint32_t k;
    for (k = 0; k < 4; k++) {
        const char *name = fx_say(k);
        uint32_t y = 44 + k * 16;
        char buf[20];
        buf[0] = '1' + (char)k;
        buf[1] = ' ';
        uint32_t i = 2, j = 0;
        while (name[j] && i < sizeof(buf) - 1) buf[i++] = name[j++];
        buf[i] = 0;
        boot_text_1x(y, buf, 0x8410, 0x0000);
    }
}

static int fx_page_knob(uint32_t id, int32_t clicks)
{
    (void)id; (void)clicks;
    return 0;
}

static const cfm1_page_t PAGE_FX = {
    .name  = "FX",
    .title = "EFFECTS",
    .flags = 0,
    .draw   = fx_page_draw,
    .knob   = fx_page_knob,
};
