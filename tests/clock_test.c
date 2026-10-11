/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual clock-switch routine with modeled MMIO and watchdog recovery. */
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
static uint32_t pll, source, ticks, programmed_at, writes, mode, step;
static jmp_buf failed;
static uint32_t read_word(uint32_t a)
{
    if (a == 0x119ACu) return pll;
    if (a == 0x1000Cu) return source;
    if (a == 0x10804u) { ticks += step; return ticks; }
    if (a == 0x40200u) return mode == 3 ? 1u << 31 : 0;
    assert(a == 0x1EEE008u);
    if ((mode == 2 && writes) || (mode == 1 && writes == 1)) return 0;
    return 1u << 14;
}
static void write_word(uint32_t a, uint32_t v)
{
    if (a == 0x119ACu) {
        assert(source == 0x10003u); /* RC16M throughout both programming attempts */
        pll = v; programmed_at = ticks; writes++;
    } else {
        assert(a == 0x1000Cu);
        if (v == 0x10101u) assert(ticks - programmed_at >= 20000u * 24u);
        source = v;
    }
}
#define FM1_CLOCK_HOST_TEST 1
#define MELODEE_SYS_CLK 320
#define FCLK_READ(a) read_word(a)
#define FCLK_WRITE(a,v) write_word(a,v)
#define FCLK_SYNC() ((void)0)
#define FCLK_FAILED() longjmp(failed, 1)
#include "../firmware/hal/fm1_clock.h"
static void reset(uint32_t m)
{
    pll = 0x0750310Cu; source = 0x10101u;
    ticks = UINT32_MAX - 24000u; programmed_at = writes = 0; mode = m; step = 24000u;
}
int main(void)
{
    reset(0); assert(fm1_clock_downclock() == 0);
    assert(pll == 0x075030EEu && source == 0x10101u && writes == 1);
    reset(0); pll = 0x07503026u; assert(fm1_clock_downclock() == 1);
    assert(pll == 0x07503026u && source == 0x10101u && writes == 0);
    reset(0); source &= ~(1u << 8); assert(fm1_clock_downclock() == 1 && !writes);
    reset(3); assert(fm1_clock_downclock() == 1 && !writes); /* busy SFC: no source switch */
    reset(1); assert(fm1_clock_downclock() == 2);
    assert(pll == 0x0750310Cu && source == 0x10101u && writes == 2);
    reset(2);
    if (!setjmp(failed)) { fm1_clock_downclock(); assert(!"rollback must not return"); }
    assert(source == 0x10003u && pll == 0x0750310Cu && writes == 2);
    reset(0); step = 0;
    if (!setjmp(failed)) { fm1_clock_downclock(); assert(!"stopped timer must not return"); }
    assert(source == 0x10003u && writes == 2);
    puts("clock: retune, wrapping timer, ready-bit settling, unfamiliar boot, rollback wait, failed rollback and stopped timer passed");
}
