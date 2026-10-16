/* SPDX-License-Identifier: GPL-3.0-only
 * DX7 voice data: the 128-byte packed form a .syx carries, and the unpacked form dx_init() reads --
 * the DX7's 156 bytes and FM Lab's six wave bytes after them, DX_VSZ in all. Taken from sloopDX's eng_dx7.c (Copyright (C) 2026 Sven Trogus), only these three functions.
 *
 * The unpacked layout, as tools/gen_voices.py writes it and dx7_core.c reads it:
 *   u[op * 21 + 0..20]   operator, stored OP6 first: op index 0 = OP6 ... 5 = OP1
 *                          0..3   EG rate 1..4          11     left curve
 *                          4..7   EG level 1..4         12     right curve
 *                          8      break point           13     rate scaling
 *                          9      left depth            14     amplitude mod sens
 *                          10     right depth           15     key velocity sens
 *                          16     output level          17     oscillator mode (1 = fixed)
 *                          18     coarse (0 = x0.5)     19     fine        20  detune (0..14, 7 = none)
 *   u[126..129] pitch EG rates     u[130..133] pitch EG levels
 *   u[134] algorithm (0..31)       u[135] feedback (0..7)      u[136] oscillator key sync
 *   u[137] LFO speed  u[138] LFO delay  u[139] LFO PMD  u[140] LFO AMD
 *   u[141] LFO key sync  u[142] LFO wave  u[143] pitch mod sens  u[144] transpose (24 = middle C)
 *   u[145..154] name   u[155] unused
 *   u[156..161] FM Lab's: each operator's wave (0 = sine), in the same OP6-first order
 *
 * The waves are not the DX7's, and a DX7 has nowhere to put them -- except that it does. Packed, an
 * operator's byte 11 holds the two keyboard curves in its low four bits, and MIDI gives every byte
 * seven, so bits 4..6 travel in every .syx and a DX7 and Dexed ignore them. Eight waves is three
 * bits. A bank saved here is still a DX7 bank, and on a DX7 it plays every operator as the sine it
 * would have been anyway. The single-voice dump is the DX7's unpacked 155 with nothing spare, so a
 * voice sent that way goes as sines, and one received that way arrives as sines.
 */
#pragma once

#define DX_WAVE 156u                            /* u[DX_WAVE + op]: the waves */
#define DX_NWAVE 8u
#define DX_VSZ (DX_WAVE + 6u)

/* an unpacked voice (DX_VSZ) -> the DX7's packed 128 bytes, the waves in byte 11's spare bits */
static void dx_pack(const uint8_t *u, uint8_t *b)
{
    uint32_t op, i;
    for (op = 0; op < 6u; op++) {
        const uint8_t *o = u + op * 21u;
        uint8_t *q = b + op * 17u;
        for (i = 0; i < 11u; i++)
            q[i] = o[i];
        q[11] = (uint8_t)(o[11] | o[12] << 2 | (u[DX_WAVE + op] & 7u) << 4);
        q[12] = (uint8_t)(o[13] | o[20] << 3);
        q[13] = (uint8_t)(o[14] | o[15] << 2);
        q[14] = o[16];
        q[15] = (uint8_t)(o[17] | o[18] << 1);
        q[16] = o[19];
    }
    for (i = 0; i < 8u; i++)
        b[102 + i] = u[126 + i];
    b[110] = u[134];
    b[111] = (uint8_t)(u[135] | u[136] << 3);
    for (i = 0; i < 4u; i++)
        b[112 + i] = u[137 + i];
    b[116] = (uint8_t)(u[141] | u[142] << 1 | u[143] << 4);
    b[117] = u[144];
    for (i = 0; i < 10u; i++)
        b[118 + i] = u[145 + i];
}

/* the DX7's packed 128 bytes (one voice of a 32-voice .syx) -> unpacked DX_VSZ */
static void dx_unpack(const uint8_t *b, uint8_t *u)
{
    uint32_t op, i;
    for (op = 0; op < 6u; op++) {
        const uint8_t *s = b + op * 17u;
        uint8_t *o = u + op * 21u;
        for (i = 0; i < 11u; i++)
            o[i] = s[i];
        o[11] = s[11] & 3u;
        o[12] = (s[11] >> 2) & 3u;
        u[DX_WAVE + op] = (s[11] >> 4) & 7u;
        o[13] = s[12] & 7u;
        o[20] = (s[12] >> 3) & 15u;
        o[14] = s[13] & 3u;
        o[15] = (s[13] >> 2) & 7u;
        o[16] = s[14];
        o[17] = s[15] & 1u;
        o[18] = (s[15] >> 1) & 31u;
        o[19] = s[16];
    }
    for (i = 0; i < 8u; i++)
        u[126 + i] = b[102 + i];
    u[134] = b[110] & 31u;
    u[135] = b[111] & 7u;
    u[136] = (b[111] >> 3) & 1u;
    for (i = 0; i < 4u; i++)
        u[137 + i] = b[112 + i];
    u[141] = b[116] & 1u;
    u[142] = (b[116] >> 1) & 7u;
    u[143] = (b[116] >> 4) & 7u;
    u[144] = b[117];
    for (i = 0; i < 10u; i++)
        u[145 + i] = b[118 + i];
    u[155] = 0;
}

/* every value inside its DX7 range (a damaged dump must not index past a table). The two tables are
 * also the edit ranges: OPMAX[i] is the top of operator byte i, GMAX[i] of u[126 + i]. */
static const uint8_t DX_OPMAX[21] = {99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14};
static const uint8_t DX_GMAX[19] = {99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48};

static void dx_sanitize(uint8_t *u)
{
    uint32_t op, i;
    for (op = 0; op < 6u; op++)
        for (i = 0; i < 21u; i++)
            if (u[op * 21u + i] > DX_OPMAX[i])
                u[op * 21u + i] = DX_OPMAX[i];
    for (i = 0; i < 19u; i++)
        if (u[126 + i] > DX_GMAX[i])
            u[126 + i] = DX_GMAX[i];
    for (op = 0; op < 6u; op++)
        if (u[DX_WAVE + op] >= DX_NWAVE)
            u[DX_WAVE + op] = 0;
}

/* ---- the rest of this file is FM Lab's, not sloopDX's ----
 *
 * INIT VOICE: what a DX7 gives you when you press the two buttons that clear the edit buffer. A
 * single operator at full level on algorithm 1, every envelope instant, nothing else doing
 * anything -- so it is a plain sine wave, and every sound you build from it is something you added.
 * It is also what an unwritten slot of the bank holds, which saves the bank needing to say which of
 * its slots are empty: they are all a voice, and that voice happens to be a sine. */
static void dx_init_voice(uint8_t *u)
{
    uint32_t op, i;
    static const uint8_t G[30] = {99, 99, 99, 99, 50, 50, 50, 50,   /* pitch EG: rates, then levels */
                                  0,                                 /* algorithm 1 */
                                  0, 1,                              /* feedback, oscillator key sync */
                                  35, 0, 0, 0, 1, 0, 3,              /* LFO: speed delay PMD AMD sync wave PMS */
                                  24,                                /* transpose: middle C */
                                  'I', 'N', 'I', 'T', ' ', 'V', 'O', 'I', 'C', 'E', 0};
    for (op = 0; op < 6u; op++) {
        uint8_t *o = u + op * 21u;
        for (i = 0; i < 4u; i++) { o[i] = 99; o[4 + i] = i < 3u ? 99u : 0u; }
        o[8] = 39;                              /* break point C3, with no scaling either side of it */
        for (i = 9; i < 16u; i++) o[i] = 0;     /* no keyboard scaling, no rate scaling, no sensitivity */
        o[16] = op == 5u ? 99u : 0u;            /* block 5 is OP1: on algorithm 1 the only one heard */
        o[17] = 0; o[18] = 1; o[19] = 0; o[20] = 7;   /* ratio x1, no detune */
    }
    for (i = 0; i < 30u; i++)
        u[126 + i] = G[i];
    for (op = 0; op < 6u; op++)
        u[DX_WAVE + op] = 0;                    /* and they are all sines */
}

/* the checksum a .syx carries after its data: the low 7 bits of the negated sum */
static uint8_t dx_syx_sum(const uint8_t *p, uint32_t n)
{
    uint32_t s = 0;
    while (n--)
        s += *p++;
    return (uint8_t)((~s + 1u) & 0x7Fu);
}
