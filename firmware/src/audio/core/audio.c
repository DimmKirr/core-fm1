/* SPDX-License-Identifier: GPL-3.0-only */
/* Minimal audio core: I2S DMA double-buffer, mix_block, master output.
 * Pattern from Melodee audio.c, stripped to the essentials. */

#define HALF_FRAMES BLOCK
#define HALF_WORDS  (HALF_FRAMES * 2u)

static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(32)));
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

void fm1_alnk0_irq(void)
{
    uint8_t p = fm1_audio_pending();
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half();
        int32_t *o = &abuf[half * HALF_WORDS];
        uint32_t i;
        mix_block();
        {
            int32_t mv = (int32_t)song.master_q12;
            for (i = 0; i < HALF_FRAMES; i++) {
#ifdef CONFIG_AUDIO_24BIT
                int32_t l = (int32_t)(((int64_t)mix_buf_l[i] * mv) >> 12);
                int32_t r = (int32_t)(((int64_t)mix_buf_r[i] * mv) >> 12);
                o[2u * i]     = audio_sat(l) >> AUDIO_OUT_SHIFT;
                o[2u * i + 1] = audio_sat(r) >> AUDIO_OUT_SHIFT;
#else
                o[2u * i]     = ((mix_buf_l[i] >> 2) * mv >> 10) << AUDIO_OUT_SHIFT;
                o[2u * i + 1] = ((mix_buf_r[i] >> 2) * mv >> 10) << AUDIO_OUT_SHIFT;
#endif
            }
        }
        fm1_audio_ack_half();
    }
}
extern void isr_alnk0(void);

static void audio_init(void)
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    for (i = 0; i < NTRK; i++) {
        __builtin_memset(&trk[i], 0, sizeof(track_t));
        trk[i].engine = 0;
        params_init(&trk[i]);
    }
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}
