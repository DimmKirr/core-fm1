/* SPDX-License-Identifier: GPL-3.0-only
 * DX7 voice data: the 128-byte packed form a .syx carries, and the 156-byte unpacked form dx_init()
 * reads. Taken from sloopDX's eng_dx7.c (Copyright (C) 2026 Sven Trogus), only these three functions.
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
 */
#pragma once

/* an unpacked voice (156) -> the DX7's packed 128 bytes */
static void dx_pack(const uint8_t *u, uint8_t *b)
{
    uint32_t op, i;
    for (op = 0; op < 6u; op++) {
        const uint8_t *o = u + op * 21u;
        uint8_t *q = b + op * 17u;
        for (i = 0; i < 11u; i++)
            q[i] = o[i];
        q[11] = (uint8_t)(o[11] | o[12] << 2);
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

/* the DX7's packed 128 bytes (one voice of a 32-voice .syx) -> unpacked 156 */
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
}
