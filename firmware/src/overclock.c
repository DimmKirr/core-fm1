/* SPDX-License-Identifier: GPL-3.0-only */
/* OVERCLOCK (HOME held, ui_menu.c): the SYS clock the FM-1 boots at (main.c oc_boot; the retune is PR #27's by
 * @johnskiller, hal/fm1_clock.h):
 *   360 MHz   the default: M-VAVE's boot clock (the SPL's); never writes the PLL (what 1.2 ran): a 320 that a soft
 *             reset kept (an update, a restart) stays until the next power-off
 *   320 MHz   JieLi's own clock table: the PLL is retuned at every start
 * The supply rails stay where the boot loader sets them.
 * Kept in favorites.factory[15][30] bits 2..3 (favorites.c; older firmware leaves them alone): 3 is 320 MHz, any other
 * value 360 (1.3.0 and 1.3.1 stored 0 for their 320 default, 1 for 360, 2 for an unreleased 396). 320 takes a restart:
 * closing the menu asks for one (oc_restart: main.c restarts once the settings are saved); 360 the next power-on.
 * On some FM-1s the retune itself fails, and two such starts entered UBOOT through the boot guard: a session at
 * 320 MHz or one that retuned and then crashed, hung or lost CPU1 is set back to 360, and the next start leaves the
 * PLL alone (main.c oc_boot, oc_poll; core.h oc_guard). A retune that rolled back, or a session that died within 30 s of one, marks the
 * unit (bit 4, PREF_OC_LOST: kept with the settings): OVERCLOCK then reads 360 ONLY and the PLL is never written. */
enum { OC_320, OC_360, OC_N };
static const char *const OC_NAME[OC_N] = {"320 MHz", "360 MHz"};
#define PREF_OC_SHIFT 2u
#define PREF_OC_MASK (3u << PREF_OC_SHIFT)
#define PREF_OC_320 3u                          /* the stored 320 MHz; 360 stores 1 (1.3.0 and 1.3.1 read it as 360) */
#define PREF_OC_LOST 0x10u                      /* the retune failed on this FM-1: 360 MHz only */
enum { OC_CPU_FAULT = 1, OC_SAID, OC_PLL_FAULT };   /* oc_reverted */
static uint8_t oc_boot_mode = OC_360;           /* the mode this session booted with (main.c oc_boot) */
static uint8_t oc_restart;                      /* the restart question answered Yes (main.c oc_poll) */
static uint8_t oc_reverted;                     /* OC_CPU_FAULT / OC_PLL_FAULT: to save and say (main.c oc_poll) */

static uint32_t oc_get(void)
{
    return (PREF_BITS & PREF_OC_MASK) >> PREF_OC_SHIFT == PREF_OC_320 ? OC_320 : OC_360;
}
static void oc_put(uint32_t m)
{
    PREF_BITS = (uint8_t)((PREF_BITS & ~PREF_OC_MASK) | (m == OC_320 ? PREF_OC_320 : 1u) << PREF_OC_SHIFT);
}
static int oc_lost(void) { return (PREF_BITS & PREF_OC_LOST) != 0; }

/* main.c oc_boot: the PLL did not take the retune (a rollback now, or lost: a session that died within 30 s of one):
 * marked for good, 360 MHz from here, saved and said */
static void oc_retune_failed(void)
{
    PREF_BITS |= PREF_OC_LOST;
    oc_put(OC_360);
    oc_boot_mode = OC_360;
    oc_reverted = OC_PLL_FAULT;
}

/* main.c oc_boot: the mode to boot with, the saved one; 360 MHz after a session at 320 MHz or one that retuned, which
 * ended without an intended reset (failed: core.h oc_guard.live), and for good once a retune failed (lost) */
static uint32_t oc_boot_choose(int failed, int lost)
{
    uint32_t mode = oc_get();
    if (lost) {
        oc_retune_failed();
        return OC_360;
    }
    if ((failed || oc_lost()) && mode != OC_360) {
        oc_put(OC_360);
        oc_reverted = OC_CPU_FAULT;
        mode = OC_360;
    }
    oc_boot_mode = (uint8_t)mode;
    return mode;
}

/* main.c oc_poll: CPU1 retired in a session at 320 MHz: 360 from the next start */
static void oc_core1_lost(void)
{
    if (oc_boot_mode != OC_360 && oc_get() != OC_360) {
        oc_put(OC_360);
        oc_reverted = OC_CPU_FAULT;
    }
}
