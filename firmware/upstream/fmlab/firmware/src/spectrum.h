/* SPDX-License-Identifier: GPL-3.0-only
 * The spectrum analyser: a 1024-point fixed-point FFT of what the instrument is actually putting
 * out, so that the sound you built can be looked at as well as heard.
 *
 * This is the one screen that is here purely to teach. FM makes its harmonics by a rule you can
 * write down -- a carrier at fc modulated at fm grows sidebands at fc +/- k*fm, and the number of
 * them that matter grows with the modulation index -- and that rule is almost invisible from the
 * numbers on the other pages. On a linear frequency axis it is a row of evenly spaced lines that
 * spreads outward as you raise a modulator's level, and that picture is the whole of FM in one
 * glance.
 *
 * Linear, therefore, and not logarithmic. A log axis fits 20 Hz to 20 kHz on the screen at once but
 * bunches the sidebands closer and closer together as they go up, which is exactly the structure
 * worth seeing. The span is on a knob instead, so the top end is a turn away.
 *
 * Integers only, as everywhere else here. The twiddle factors and the window come from the engine's
 * own sine table, so this file adds no table of its own beyond 17 entries of a logarithm.
 */
#pragma once

#define SP_LG 10u
#define SP_N (1u << SP_LG)                      /* 1024 samples: 23 ms, 43 Hz between bins */
#define SP_BINS (SP_N / 2u)
#define SP_H 136u                               /* the plot, the same box the diagram uses */

/* ---- the tap. The ISR drops the finished sample in as it goes past; the main loop takes the last
 * SP_N of them whenever it is ready to draw. Nothing is locked: a copy takes a few microseconds
 * against the 23 ms the ring holds, and the only samples that can be torn are the oldest ones,
 * which the window has already multiplied down to nothing. */
static int16_t sp_ring[SP_N];
static volatile uint32_t sp_wr;
static volatile uint8_t sp_want;                /* the screen is showing it: keep the ring fed */

static inline void sp_feed(int32_t s)
{
    sp_ring[sp_wr & (SP_N - 1u)] = (int16_t)s;
    sp_wr++;
}

/* ---- the transform. Values are kept inside 16 bits by halving at every one of the ten stages,
 * which is also exactly the growth a 1024-point FFT has, so a full-scale sine comes out at a known
 * height and the whole thing stays in 32-bit arithmetic: 15 bits of value by 14 of twiddle is 29. */
static int32_t sp_re[SP_N] __attribute__((section(".pool")));
static int32_t sp_im[SP_N] __attribute__((section(".pool")));
static int16_t sp_tw_r[SP_N / 2u], sp_tw_i[SP_N / 2u];      /* Q14 */
static uint16_t sp_win[SP_N / 2u + 1u];                     /* Q15 Hann, symmetric about the middle */

static void sp_init(void)
{
    uint32_t i;
    for (i = 0; i < SP_N / 2u; i++) {
        /* dx_sin takes a Q24 phase and a whole cycle is 1 << 24, so k/N of a turn is k << (24 - LG) */
        int32_t p = (int32_t)(i << (24u - SP_LG));
        sp_tw_r[i] = (int16_t)(dx_sin(p + (1 << 22)) >> 10);        /* cos, Q14 */
        sp_tw_i[i] = (int16_t)(-(dx_sin(p) >> 10));                 /* -sin, Q14: a forward DFT */
    }
    for (i = 0; i <= SP_N / 2u; i++) {
        int32_t c = dx_sin((int32_t)(i << (24u - SP_LG)) + (1 << 22)) >> 10;   /* cos, Q14 */
        int32_t w = 16384 - c;                  /* Hann: (1 - cos) / 2, in Q15 */
        sp_win[i] = (uint16_t)(w > 32767 ? 32767 : w);
    }
}

static void sp_fft(void)
{
    uint32_t i, j, half, step, k;
    for (i = 1u, j = 0u; i < SP_N; i++) {       /* in-place bit reversal */
        uint32_t bit = SP_N >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            int32_t t = sp_re[i]; sp_re[i] = sp_re[j]; sp_re[j] = t;
            t = sp_im[i]; sp_im[i] = sp_im[j]; sp_im[j] = t;
        }
    }
    for (half = 1u; half < SP_N; half <<= 1) {
        step = (SP_N / 2u) / half;
        for (i = 0; i < SP_N; i += half << 1) {
            for (k = 0; k < half; k++) {
                int32_t wr = sp_tw_r[k * step], wi = sp_tw_i[k * step];
                int32_t ar = sp_re[i + k], ai = sp_im[i + k];
                int32_t br = sp_re[i + k + half], bi = sp_im[i + k + half];
                int32_t tr = (br * wr - bi * wi) >> 14;
                int32_t ti = (br * wi + bi * wr) >> 14;
                sp_re[i + k] = (ar + tr) >> 1;
                sp_im[i + k] = (ai + ti) >> 1;
                sp_re[i + k + half] = (ar - tr) >> 1;
                sp_im[i + k + half] = (ai - ti) >> 1;
            }
        }
    }
}

/* log2 in Q8, from the leading bit and a seventeen-point table of log2(1 + m/16) read with linear
 * interpolation. Good to about a fiftieth of a decibel, which is far better than 136 pixels. */
static const uint16_t SP_LOG2[17] = {0, 22, 44, 63, 82, 100, 118, 134, 150, 165, 179, 193, 207, 220,
                                     232, 244, 256};

static uint32_t sp_log2_q8(uint32_t x)
{
    uint32_t p = 0, m, hi, lo;
    if (!x)
        return 0;
    while (x >> (p + 1u))
        p++;
    m = (p >= 8u ? x >> (p - 8u) : x << (8u - p)) & 255u;   /* the bits under the leading one */
    hi = m >> 4; lo = m & 15u;
    return (p << 8) + SP_LOG2[hi] + ((uint32_t)(SP_LOG2[hi + 1u] - SP_LOG2[hi]) * lo >> 4);
}

/* Decibels in sixteenths. A full-scale sine lands one bin at 8192 -- half of it from the window,
 * then N/2 of accumulation against ten halvings -- which is 13 octaves, so that is where zero is. */
#define SP_REF 1245                             /* 16 * 6.0206 * 13, less the 0.4 dB the alpha-max
                                                 * approximation and ten truncating halvings cost:
                                                 * measured, so a full-scale sine reads exactly 0 */
#define SP_RANGE (72 * 16)                      /* how far down the bottom of the screen is */

static int32_t sp_db16(uint32_t mag)
{
    return (int32_t)((sp_log2_q8(mag) * 24660u) >> 16);     /* 16 * 20 * log10(2) / 256 */
}

/* One pass: take the last SP_N samples, window them, transform, and leave each bin's height in
 * pixels above the bottom of the plot in sp_bin[]. */
static uint8_t sp_bin[SP_BINS];

static void sp_run(void)
{
    uint32_t i, r = sp_wr - SP_N;
    for (i = 0; i < SP_N; i++) {
        int32_t s = sp_ring[(r + i) & (SP_N - 1u)];
        uint32_t w = sp_win[i <= SP_N / 2u ? i : SP_N - i];
        sp_re[i] = (s * (int32_t)w) >> 15;
        sp_im[i] = 0;
    }
    sp_fft();
    for (i = 0; i < SP_BINS; i++) {
        int32_t re = sp_re[i] < 0 ? -sp_re[i] : sp_re[i];
        int32_t im = sp_im[i] < 0 ? -sp_im[i] : sp_im[i];
        int32_t a = re > im ? re : im, b = re > im ? im : re;
        int32_t db = sp_db16((uint32_t)((a * 61 + b * 25) >> 6)) - SP_REF;   /* |z| to about 3 % */
        int32_t y = (db + SP_RANGE) * (int32_t)SP_H / SP_RANGE;
        sp_bin[i] = (uint8_t)(y < 0 ? 0 : y > (int32_t)SP_H ? (int32_t)SP_H : y);
    }
}
