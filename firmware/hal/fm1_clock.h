/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-1 system clock: retune the SYS PLL at boot, before CPU1 starts and
 * before the audio ISR exists (MELODEE_SYS_CLK, fm1_clock_downclock()).
 *
 * The AC79 SYS PLL is a single VCO with fixed output taps; the system clock
 * comes off one tap through the mux in CLK_CON2 and the HSB/LSB/SFC dividers
 * in SYS_DIV. The FM-1 boots at 360 MHz (M-VAVE's isd_config SYS_CLK; measured
 * by X0X: 329 dependent adds/us): the SPL programs the VCO (PLL_CON1 FBDS,
 * 0x119A4 [11:0]) so that the same tap and post-div chain yields 360. JieLi's
 * own 320 MHz profile (isd_config_rule.c) leaves HSB_DIV=1 and only lowers
 * LSB to /3 — every divider value the SPL left keeps working for 320 when the
 * VCO scales by 8/9: sys 360->320, HSB 180->160, LSB 60->53.3, SFC 90->80.
 *
 * The retune is a downward FBDS step, which is the safe direction (per JieLi's
 * own clock.c: program the PLL, wait for lock, then the dividers — and here
 * the dividers do not change at all). TIMER4 hangs off the 24 MHz crystal
 * (fm1_time.h), the audio I2S off its own ALNK PLL, USB/UART off PLL48M: none
 * of them move. The only consumer of the system clock that matters before
 * audio_init() is the LCD SPI1 (LSB/(BAUD+1)): it is initialised after this
 * call, at the new LSB.
 *
 *   fm1_clock_downclock()   0 on success (or no retune needed); -1 on a
 *                           timeout waiting for lock (the caller should
 *                           count it and keep running at 360: the PLL is
 *                           still on its old, known-good setting).
 *
 * Registers (SDK WL82.h / clock_hw.h, addresses verified against the map):
 *   PLL_CON0  0x119A0   [0] PLL_EN, [1] PLL_REST; lock status is the CPU-side
 *                       0x1EEE008 [14] the SDK's own sys_clock_update polls.
 *   PLL_CON1  0x119A4   [11:0] FBDS (the VCO multiplier), [12] LDO12A...
 *                       upper fields left untouched.
 *   SYS_DIV   0x10008   [7:0] HSB /n+1, [10:8] LSB, [14:12] SFC — unchanged.
 *   0x1EEE008 [14]      clock-stable handshake the SDK polls; [9:8] are
 *                       cleared and restored around the change (rc_clk_on).
 */
#pragma once
#include <stdint.h>
#include "fm1_time.h"

#define FM1_PLL_CON0    (*(volatile uint32_t *)0x119A0u)
#define FM1_PLL_CON1    (*(volatile uint32_t *)0x119A4u)
#define FM1_SYS_DIV     (*(volatile uint32_t *)0x10008u)
#define FM1_CLK_STABLE  (*(volatile uint32_t *)0x1EEE008u)

#ifndef MELODEE_SYS_CLK
#define MELODEE_SYS_CLK 0              /* 0: boot at the SPL's 360, as shipped */
#endif

/* The SPL leaves the system at ~360 MHz. MELODEE_SYS_CLK=320 scales the VCO
 * by 8/9 (240/270, JieLi's 320 MHz profile against M-VAVE's 360 MHz one);
 * every post-div chain scales with it. Call once, from fm1_main(), before
 * lcd_init(): IRQs are off, CPU1 is not started, no audio ISR exists.
 * Returns 0 on success (or no retune wanted), 1 when the boot shape is not
 * the 360 MHz one (left as it is — never a boot failure), 2 on a lock
 * timeout (the PLL is restored to its previous setting; the caller reports
 * through fm1_clock_downclock_undo and runs at 360). */
static int fm1_clock_downclock(void)
{
#if MELODEE_SYS_CLK == 320
    uint32_t con1 = FM1_PLL_CON1;
    uint32_t fbds = con1 & 0xFFFu;
    uint32_t start;

    if (fbds < 0x100u)                             /* not the 360 MHz shape: leave it */
        return 1;
    fbds = (fbds * 240u + 135u) / 270u;            /* 270 -> 240, rounded */
    con1 = (con1 & ~0xFFFu) | fbds;

    start = fm1_ticks();
    FM1_PLL_CON1 = con1;                           /* the PLL restarts itself */
    while (!(FM1_CLK_STABLE & (1u << 14))) {       /* SDK's own lock poll */
        if ((uint32_t)(fm1_ticks() - start) > 20000u * FM1_TICKS_PER_US) {
            FM1_PLL_CON1 = (FM1_PLL_CON1 & ~0xFFFu) | (((con1 & 0xFFFu) * 270u + 119u) / 240u);
            return 2;                              /* back to 360, caller keeps going */
        }
    }
    return 0;
#else
    return 0;
#endif
}

/* True when the boot clock is the retuned one (for a splash note / editor) */
static inline int fm1_clock_is_320(void)
{
#if MELODEE_SYS_CLK == 320
    return 1;
#else
    return 0;
#endif
}
