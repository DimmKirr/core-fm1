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
#include "fm1_flash.h"                             /* RAMFN, FL_FAR: the switch runs from RAM */

/* Registers (SDK WL82.h JL_ANA / JL_CLOCK, addresses verified against the map;
 * live readings by @charlesvestal, PR #27): the SYS PLL is the one the SDK
 * header names PLL2 — its CON1 (0x119AC) holds the VCO multiplier as
 * FBDS[11:0] = (mult - 2): the SPL programs 268, i.e. 24 MHz / 12 (ref div in
 * PLL2_CON0) * 270 = 540 MHz VCO, and CLK_CON2 selects 2/3 of it = 360 MHz.
 * The header's PLL_CON0/1 (0x119A0/A4) are a different PLL (reads 0x026 in
 * CON1's multiplier field on the same unit).
 *
 *   PLL2_CON0 0x119A8   [0] PLL_EN, [1] PLL_REST
 *   PLL2_CON1 0x119AC   [11:0] FBDS = mult - 2 (SPL: 268 = 270, the 360 MHz
 *                       shape); higher fields untouched.
 *   CLK_CON0  0x1000C   [1] rc_clk_en, [8] sys_clk_en — JieLi's own
 *                       sys_clk_source_rc_clk_on/off moves the system onto
 *                       the RC16M while the PLL is reprogrammed.
 *   0x1EEE008 [14]      the handshake the SDK polls after a clock change
 *                       (CACHE_WAIT); polled as in sys_clk_source_rc_clk_on.
 */
#define FM1_PLL2_CON0   (*(volatile uint32_t *)0x119A8u)
#define FM1_PLL2_CON1   (*(volatile uint32_t *)0x119ACu)
#define FM1_SYS_DIV     (*(volatile uint32_t *)0x10008u)
#define FM1_CLK_CON0    (*(volatile uint32_t *)0x1000Cu)
#define FM1_CLK_STABLE  (*(volatile uint32_t *)0x1EEE008u)

#ifndef MELODEE_SYS_CLK
#define MELODEE_SYS_CLK 0              /* 0: boot at the SPL's 360, as shipped */
#endif

/* The SPL leaves the system at ~360 MHz (VCO 540: multiplier 270, CLK_CON2
 * 2/3). MELODEE_SYS_CLK=320 scales the VCO by 8/9 (540 -> 480, multiplier
 * 270 -> 240, JieLi's 320 MHz profile against M-VAVE's 360 MHz one); every
 * post-div chain scales with it. The sequence follows JieLi's own
 * sys_clk_source_rc_clk_on: move the system onto the RC16M, write the PLL,
 * poll the handshake, move back — all from .ram_text, since the system clock
 * (and with it the SFC/XIP window) wobbles while the PLL restarts. Call
 * through FL_FAR from fm1_main(), before lcd_init(): IRQs are off, CPU1 is
 * not started, no audio ISR exists. TIMER4 runs from the 24 MHz crystal and
 * keeps counting through the source switch.
 * Returns 0 on success (or no retune wanted), 1 when the boot shape is not
 * the 360 MHz one (left as it is — never a boot failure), 2 on a timeout
 * waiting for the PLL (the multiplier is restored; the caller reports
 * through fm1_clock_downclock_undo and runs at 360). */
static RAMFN int fm1_clock_downclock_ram(void)
{
#if MELODEE_SYS_CLK == 320
    uint32_t con1 = FM1_PLL2_CON1;
    uint32_t mult = (con1 & 0xFFFu) + 2u;          /* 268 -> 270, the 360 shape */
    uint32_t con0 = FM1_CLK_CON0;
    uint32_t start, i;

    if (mult != 270u)                              /* not the 360 MHz shape: leave it */
        return 1;
    con1 = (con1 & ~0xFFFu) | (240u - 2u);         /* 540 -> 480 VCO: sys 360 -> 320 */

    FM1_CLK_CON0 = (con0 | (1u << 1)) & ~(1u << 8);/* system onto the RC16M */
    __asm__ volatile("csync");
    FM1_PLL2_CON1 = con1;                          /* the PLL restarts itself */
    start = FM1_T4_CNT;                            /* (fm1_ticks() does not inline into .ram_text) */
    i = 0;
    while (!(FM1_CLK_STABLE & (1u << 14))) {       /* SDK's own handshake poll */
        if (++i > 1000000u ||
            (uint32_t)(FM1_T4_CNT - start) > 20000u * FM1_TICKS_PER_US) {
            FM1_PLL2_CON1 = (FM1_PLL2_CON1 & ~0xFFFu) | (270u - 2u);
            FM1_CLK_CON0 = con0;                   /* back to the PLL at 360 */
            __asm__ volatile("csync");
            return 2;                              /* caller keeps going at 360 */
        }
    }
    __asm__ volatile("csync");
    for (i = 0; i < 300u; i++)                     /* settle, as rc_clk_on does */
        __asm__ volatile("nop");
    FM1_CLK_CON0 = con0;                           /* back to the PLL, now 480*2/3 */
    __asm__ volatile("csync");
    return 0;
#else
    return 0;
#endif
}

/* .ram_text (0x01C0xxxx) is beyond the 23-bit call range from XIP: call it
 * through a pointer the compiler cannot fold back (fm1_flash.h's FL_FAR). */
#define fm1_clock_downclock() FL_FAR(fm1_clock_downclock_ram)()

/* True when the boot clock is the retuned one (for a splash note / editor) */
static inline int fm1_clock_is_320(void)
{
#if MELODEE_SYS_CLK == 320
    return 1;
#else
    return 0;
#endif
}
