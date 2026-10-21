/* SPDX-License-Identifier: GPL-3.0-only */
/* core-fm1 HOME page: track overview, engine name, transport state.
 * Reads through cfm1_audio.h adapters (trk[], song, ENGINES[]). */

static void home_enter(void) { }
static void home_leave(void) { }

static void home_draw(void)
{
    uint32_t k;
    char buf[32];
    uint32_t y = 20;

    for (k = 0; k < NTRK; k++) {
        const track_t *t = &trk[k];
        const engine_t *eng = (t->eng_req < NENGINES)
            ? ENGINES[t->eng_req] : 0;

        /* track number */
        buf[0] = '1' + (char)k;
        buf[1] = ' ';

        /* engine name */
        const char *name = eng ? eng->name : "---";
        uint32_t i = 2, j = 0;
        while (name[j] && i < sizeof(buf) - 1)
            buf[i++] = name[j++];
        buf[i] = 0;

        uint16_t fg = (song.sel == k) ? 0x07E0 : 0x8410;
        boot_text_1x(y, buf, fg, 0x0000);
        y += 12;
    }

    /* transport */
    const char *state = song.playing ? "> PLAY" : "_ STOP";
    boot_text_1x(y + 8, state, 0xFFFF, 0x0000);
}

static int home_knob(uint32_t id, int32_t clicks)
{
    if (id == 1) {
        /* select track */
        int32_t s = (int32_t)song.sel + clicks;
        if (s < 0) s = 0;
        if (s >= (int32_t)NTRK) s = NTRK - 1;
        song.sel = (uint8_t)s;
        shell_dirty = 1;
        return 1;
    }
    return 0;
}

static int home_button(uint32_t id, uint32_t down)
{
    (void)id; (void)down;
    return 0;
}

static const cfm1_page_t PAGE_HOME = {
    .name  = "HOME",
    .title = "HOME",
    .flags = PAGE_HOME,
    .enter  = home_enter,
    .leave  = home_leave,
    .draw   = home_draw,
    .status = 0,
    .knob   = home_knob,
    .button = home_button,
    .key    = 0,
};
