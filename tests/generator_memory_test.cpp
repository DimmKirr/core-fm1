/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#include <cstdio>
#include <cmath>
#include "reference/marbles/random_sequence.h"
#include "reference/marbles/random/quantizer.h"
#include "../firmware/src/generator_memory.h"
int main(int argc, char **argv)
{
    unsigned errors = 0;
    for (unsigned length = 1; length <= 16; ++length)
        for (unsigned setting = 0; setting < 5; ++setting) {
            marbles::RandomStream stream;
            gen_memory_t port = {};
            for (unsigned i = 0; i < 32; ++i) stream.words[i] = (i * 7919u + 107u) & 65535u;
            for (unsigned i = 0; i < 16; ++i) port.loop[i] = stream.words[i];
            marbles::RandomSequence ref; ref.Init(&stream); ref.set_length(length);
            const float dv[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
            const unsigned probability[] = {100, 25, 0, 25, 100};
            ref.set_deja_vu(dv[setting]);
            for (unsigned i = 0; i < 4096; ++i) {
                unsigned at = stream.at;
                uint16_t decision = stream.words[at & 31u], fresh = stream.words[(at + 1) & 31u];
                float actual = ref.NextValue(false, 0.0f);
                auto expected = gen_memory_next(&port, length, probability[setting], setting > 2, decision, fresh);
                errors += actual != expected / 65536.0f;
            }
        }
    std::printf("Marbles unmodified reference vs fixed-point memory: %u mismatches over 327680 decisions\n", errors);
    /* A pinned upstream memory + quantizer trace for the same dry-pluck audio
     * comparison. Same SEED 11, register, anchor, rhythm and sound as Melodee.
     * Reference pitch shaping uses Marbles' factory major weights at STEPS .7;
     * Melodee instead uses its scale-constrained inverse CDF at COLOR 35. */
    if (argc > 1) {
        FILE *file = std::fopen(argv[1], "w");
        if (!file) return 2;
        marbles::RandomStream stream;
        uint32_t random = 0x6D2B79F5u ^ 11u * 0x9E3779B9u;
        for (auto &word : stream.words) {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            word = random >> 16;
        }
        marbles::RandomSequence memory; memory.Init(&stream); memory.set_length(8);
        memory.Reset(); memory.set_deja_vu(.5f);
        marbles::Scale scale; scale.InitMajor();
        marbles::Quantizer quantizer; quantizer.Init(scale);
        for (unsigned phrase = 0; phrase < 64; ++phrase) for (unsigned slot = 0; slot < 8; ++slot) {
            float decision = memory.NextValue(false, 0);
            int note = slot ? 60 + std::lround(12 * quantizer.Process(decision * 11.f / 12.f, .7f, false)) : 60;
            errors += note < 60 || note > 72 || !(0xAB5u & (1u << (note % 12)));
            std::fprintf(file, "%d\n", note);
        }
        std::fclose(file);
    }
    return errors ? 1 : 0;
}
