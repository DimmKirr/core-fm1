/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Bounded Fibonacci word: A -> AB, B -> A. Five generations produce at most
 * thirteen symbols. Grow and retreat over twelve phrases; never expand on heap. */
static uint32_t generator_branch(uint8_t word[16], uint32_t cycle)
{
    uint32_t phase = cycle % 12u, depth = phase <= 5u ? phase : (phase < 11u ? 10u - phase : 0u);
    uint32_t n = 1; word[0] = 0;
    for (uint32_t generation = 0; generation < depth; generation++) {
        uint8_t next[16]; uint32_t count = 0;
        for (uint32_t i = 0; i < n && count < 16u; i++) {
            next[count++] = 0;
            if (!word[i] && count < 16u) next[count++] = 1;
        }
        memcpy(word, next, count); n = count;
    }
    return n;
}
