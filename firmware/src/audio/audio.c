/* SPDX-License-Identifier: GPL-3.0-only */
/* Audio core stub. DMA ISR, mix_block, master output.
 * TODO: import from felucca audio.c with adaptation. */

static int32_t mix_buf_l[BLOCK];
static int32_t mix_buf_r[BLOCK];

static void mix_block(void)
{
    uint32_t i;
    for (i = 0; i < BLOCK; i++) {
        mix_buf_l[i] = 0;
        mix_buf_r[i] = 0;
    }
    for (i = 0; i < NTRK; i++)
        engine_block(&trk[i], mix_buf_l, mix_buf_r, BLOCK);
}

static void audio_init(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        __builtin_memset(&trk[i], 0, sizeof(track_t));
        trk[i].eng_req = 0;
        trk[i].engine = 0;
    }
}
