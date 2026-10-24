/* SPDX-License-Identifier: GPL-3.0-only */
/* core-fm1 SEQ page: step sequencer grid (stub). */

static void seq_page_draw(void)
{
    const track_t *t = &trk[song.sel];
    const step_t *steps = seq_steps(t);
    uint32_t i;
    char buf[4];

    boot_text_1x(20, "SEQUENCER", 0xFFFF, 0x0000);

    /* 16-step row: filled = has note, empty = no note */
    for (i = 0; i < 16; i++) {
        buf[0] = steps[i].note[0] ? '#' : '.';
        buf[1] = 0;
        uint32_t x = 8 + i * 14;
        uint16_t fg = (song.playing && t->seq_idx == i) ? 0x07E0 : 0x8410;
        boot_char_1x(x, 44, buf[0], fg, 0x0000);
    }

    const char *state = song.playing ? "PLAYING" : "STOPPED";
    boot_text_1x(70, state, 0x8410, 0x0000);
}

static int seq_page_knob(uint32_t id, int32_t clicks)
{
    (void)id; (void)clicks;
    return 0;
}

static const cfm1_page_t PAGE_SEQ = {
    .name  = "SEQ",
    .title = "SEQUENCER",
    .flags = 0,
    .draw   = seq_page_draw,
    .knob   = seq_page_knob,
};
