/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared by the application and assembly startup. Explicit build overrides win. */
#ifndef MELODEE_BUILD_OPTIONS_H
#define MELODEE_BUILD_OPTIONS_H
#ifndef MELODEE_DUAL_CORE
#define MELODEE_DUAL_CORE 1     /* paired FM6/Prophet kernels; serial fallback if CPU1 is unavailable */
#endif
#ifndef MELODEE_SYS_CLK
#define MELODEE_SYS_CLK 0       /* 0: boot at the SPL's 360 MHz; 320: retune the SYS PLL at boot (hal/fm1_clock.h).
                                 * JieLi's 320 MHz profile against M-VAVE's 360 one: the same post-div chain at
                                 * 8/9 of the VCO. Lower per-core budget (~11%), kept for the units whose CPU1
                                 * misreads SRAM at 360 (BUILDING.md, "Units whose CPU1 misreads SRAM"). */
#endif
#endif
