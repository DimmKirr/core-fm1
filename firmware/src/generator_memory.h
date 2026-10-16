/* SPDX-License-Identifier: MIT
 * Portions Copyright (c) 2015 Emilie Gillet.
 * Copyright (c) 2026 Kerem Kilic (Ellic Studio).
 * Fixed-point C adaptation of Marbles RandomSequence::NextValue (random mode).
 * https://github.com/pichenettes/eurorack/blob/f4cfa7697095a28d292bff758beb51deb49b5a86/marbles/random/random_sequence.h
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE. */
#pragma once
#include <stdint.h>

typedef struct { uint16_t loop[16]; uint8_t head, step; } gen_memory_t;
/* Inputs are supplied by independent per-track streams. Probability is linear
 * 0..100 here; the reference's squared Deja Vu law belongs to its knob mapping.
 * Fresh replaces the oldest cell, remix jumps within the same stored material.
 * A full LENGTH calls is always one Melodee phrase, including rest slots. */
static uint16_t gen_memory_next(gen_memory_t *m, uint32_t length, uint32_t chance,
                               int remix, uint16_t decision, uint16_t fresh)
{
    int mutate = (uint32_t)decision * 100u < chance * 65536u;
    if (mutate && !remix) {
        m->loop[m->head] = fresh;
        m->head = (uint8_t)((m->head + 1u) & 15u);
        m->step = (uint8_t)(length - 1u);
    } else if (mutate) m->step = (uint8_t)((uint32_t)fresh * length >> 16);
    else m->step = (uint8_t)((m->step + 1u) % length);
    return m->loop[(m->head + 16u - length + m->step) & 15u];
}
