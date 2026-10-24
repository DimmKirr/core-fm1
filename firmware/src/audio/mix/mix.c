/* SPDX-License-Identifier: GPL-3.0-only */
/* Stereo mixer stub: engine output -> FX inserts -> track level -> master.
 * TODO: port from upstream melodee mix.c. */

static int32_t mix_l[CTL], mix_r[CTL];

static void mix_init(void)
{
    __builtin_memset(mix_l, 0, sizeof(mix_l));
    __builtin_memset(mix_r, 0, sizeof(mix_r));
}

static void mix_clear(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        mix_l[i] = 0;
        mix_r[i] = 0;
    }
}

static void mix_add_mono(int32_t sample, uint32_t i)
{
    mix_l[i] += sample;
    mix_r[i] += sample;
}

static void mix_master(int32_t *out_l, int32_t *out_r, uint32_t n, uint32_t gain)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        out_l[i] = (int32_t)((int64_t)mix_l[i] * gain >> 12);
        out_r[i] = (int32_t)((int64_t)mix_r[i] * gain >> 12);
    }
}
