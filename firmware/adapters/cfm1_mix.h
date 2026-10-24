/* SPDX-License-Identifier: MIT */
/* Mixer adapter: stereo mix bus stubs when no explicit mixer is included. */
#ifndef CFM1_MIX_H
#define CFM1_MIX_H
#include <stdint.h>

#ifndef CFM1_HAS_MIX
static int32_t mix_l[CTL], mix_r[CTL];
static void mix_init(void)
{
    __builtin_memset(mix_l, 0, sizeof(mix_l));
    __builtin_memset(mix_r, 0, sizeof(mix_r));
}
static void mix_clear(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) { mix_l[i] = 0; mix_r[i] = 0; }
}
#endif

#endif /* CFM1_MIX_H */
