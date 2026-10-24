/* SPDX-License-Identifier: MIT
 * Derived from Melodee (Kerem Kilic, Ellic Studio) OTA implementation. */
/* OTA adapter: hardware hooks for ota.c (M-UPGRADE SysEx update protocol).
 * The portable core (ota.c) calls these; core_fm1.c provides the wiring. */
#ifndef CFM1_OTA_H
#define CFM1_OTA_H
#include <stdint.h>

#ifdef CONFIG_OTA

#ifndef CFM1_LOADER_ID
#define CFM1_LOADER_ID CFM1_PACKAGE_ID
#endif

/* OTA area constants (must match ota.c, but defined here so hooks can use them) */
#ifndef OTA_AREA
#define OTA_AREA      0x000E0000u
#define OTA_AREA_LEN  0x5000u
#endif

static uint8_t cfm1_flash_ok;

/* --- hooks ota.c expects (defined before ota.c is included) --- */

static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { fm1_wdt_feed(); }

static int ota_in_area(uint32_t off, uint32_t n)
{
    return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN);
}

static int ota_erase(uint32_t off)
{
    uint32_t took;
    if (!ota_in_area(off, 0x1000u) || (off & 0xFFFu))
        return -8;
#ifdef CONFIG_AUDIO
    fm1_audio_stop();
#endif
    return fl_erase4k(off, &took);
}

static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!ota_in_area(off, n))
        return -8;
    return fl_write(off, p, n);
}

/* chunked 256-byte reads: re-enable IRQs between chunks so audio DMA keeps running */
static int ota_fread(uint32_t off, void *p, uint32_t n)
{
    uint8_t *d = (uint8_t *)p;
    while (n) {
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc)
            return rc;
        off += k;
        d += k;
        n -= k;
    }
    return 0;
}

static void ota_show(uint32_t step, int32_t code)
{
    if (step < 9u) {
        static const char *const STEP[] = {"", "PACKAGE", "CHECK", "LOADER", "CONFIRM", "RESTART"};
        cfm1_log(STEP[step < 6u ? step : 0]);
    } else if (code == 1) {
        cfm1_log("OTA: dry run OK");
    } else {
        cfm1_log_int("OTA: fail ", code);
    }
    lcd_fill(0, 0, 240, 240, 0x0000);
    {
        const char *msg = step < 9u ? "UPDATE" : (code == 1 ? "DRY RUN OK" : "OTA FAILED");
        boot_text_1x(120, msg, 0xFFFF, 0x0000);
    }
    lcd_sync();
    if (step >= 9u)
        fm1_delay_ms(1500);
}

static void ota_commit(const uint8_t *parm)
{
    cfm1_log("OTA: committing");
    /* clear crash counter so the OTA reboot isn't counted as a fault */
    fm1_crash.magic = 0;
    fm1_crash.count = 0;
#ifdef CONFIG_USB
    usb_detach();
    fm1_delay_ms(30);
#endif
    fm1_enter_update(parm);
}

#else /* !CONFIG_OTA */

static void ota_service(void) {}
static void ota_session(void) {}
static int  ota_boot_cleanup(void) { return 0; }

#endif /* CONFIG_OTA */

#endif /* CFM1_OTA_H */
