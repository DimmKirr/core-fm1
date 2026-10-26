/* SPDX-License-Identifier: GPL-3.0-only */
/* core-fm1 unity root: one translation unit, all functions static.
 * CONFIG_* symbols from build/gen/config.h select HAL variants. */
#include "config.h"
#include <stdint.h>

extern uint32_t _data_start[], _data_end[], _data_load[];
extern uint32_t _bss_start[], _bss_end[];
extern uint32_t _dsp_start[], _dsp_end[], _dsp_load[];
extern uint32_t _dt_start[], _dt_end[], _dt_load[];
extern uint32_t _pool_start[], _pool_end[];
extern uint32_t _rt_start[], _rt_end[], _rt_load[];
extern uint32_t _audio_start[], _audio_end[], _audio_load[];

/* --- HAL (header-only) --- */

#include "fm1_cc.h"
#include "fm1_time.h"

#if defined(CONFIG_HAL_SYS_MELODEE)
#include "fm1_sys.h"
#elif defined(CONFIG_HAL_SYS_FELUCCA)
#include "fm1_sys_felucca.h"
#elif defined(CONFIG_HAL_SYS_SLOOP)
#include "fm1_sys_sloop.h"
#endif

#if defined(CONFIG_HAL_LCD_MELODEE)
#include "fm1_lcd_hw.h"
#elif defined(CONFIG_HAL_LCD_FELUCCA)
#include "fm1_lcd_hw_felucca.h"
#endif

#if defined(CONFIG_HAL_GUARD_MELODEE)
#include "fm1_guard.h"
#elif defined(CONFIG_HAL_GUARD_FELUCCA)
#include "fm1_guard_felucca.h"
#elif defined(CONFIG_HAL_GUARD_SLOOP)
#include "fm1_guard_sloop.h"
#endif

#include "fm1_gpio.h"
#include "fm1_irq.h"

#ifdef CONFIG_HAL_MELODEE_POWER
#include "fm1_power.h"
#endif

/* peripherals included when their subsystem is configured */
#ifdef CONFIG_AUDIO
#include "fm1_audio.h"
#include "fm1_adc.h"
#endif

#ifdef CONFIG_INPUT
#if defined(CONFIG_HAL_INPUT_MELODEE)
#include "fm1_input.h"
#elif defined(CONFIG_HAL_INPUT_FELUCCA)
#include "fm1_input_felucca.h"
#elif defined(CONFIG_HAL_INPUT_SLOOP)
#include "fm1_input_sloop.h"
#endif
#endif

#ifdef CONFIG_FLASH
#if defined(CONFIG_HAL_FLASH_MELODEE)
#include "fm1_flash.h"
#elif defined(CONFIG_HAL_FLASH_FELUCCA)
#include "fm1_flash_felucca.h"
#elif defined(CONFIG_HAL_FLASH_SLOOP)
#include "fm1_flash_sloop.h"
#endif
#endif

#ifdef CONFIG_MIDI_UART
#if defined(CONFIG_HAL_UART_MELODEE)
#include "fm1_uart.h"
#elif defined(CONFIG_HAL_UART_FELUCCA)
#include "fm1_uart_felucca.h"
#endif
#endif

#ifdef CONFIG_USB
#if defined(CONFIG_HAL_USB_MELODEE)
#include "fm1_usb.h"
#elif defined(CONFIG_HAL_USB_FELUCCA)
#include "fm1_usb_felucca.h"
#endif
#endif

#ifdef CONFIG_DUAL_CORE
#if defined(CONFIG_HAL_MULTICORE_MELODEE)
#include "fm1_multicore.h"
#elif defined(CONFIG_HAL_MULTICORE_FELUCCA)
#include "fm1_multicore_felucca.h"
#elif defined(CONFIG_HAL_MULTICORE_SLOOP)
#include "fm1_multicore_sloop.h"
#endif
#endif

#ifdef CONFIG_HAL_MELODEE_CACHE
#include "fm1_cache.h"
#endif
#ifdef CONFIG_HAL_MELODEE_CLOCK
#include "fm1_clock.h"
#endif

/* --- libc (freestanding: memset, memcpy, memmove, strlen) --- */
#include "libc.c"

/* --- LCD driver (ST7789 240x240) --- */
#if defined(CONFIG_UI_FELUCCA)
#include "ui/felucca/lcd.c"
#elif defined(CONFIG_UI_MELODEE)
#include "ui/melodee/lcd.c"
#else
#include "ui/melodee/lcd.c"
#endif

/* --- minimal 5x7 bitmap font for boot screen --- */
static const uint8_t font5x7[] = {
    0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x5F,0x00,0x00,
    0x00,0x07,0x00,0x07,0x00,
    0x14,0x7F,0x14,0x7F,0x14,
    0x24,0x2A,0x7F,0x2A,0x12,
    0x23,0x13,0x08,0x64,0x62,
    0x36,0x49,0x55,0x22,0x50,
    0x00,0x05,0x03,0x00,0x00,
    0x00,0x1C,0x22,0x41,0x00,
    0x00,0x41,0x22,0x1C,0x00,
    0x08,0x2A,0x1C,0x2A,0x08,
    0x08,0x08,0x3E,0x08,0x08,
    0x00,0x50,0x30,0x00,0x00,
    0x08,0x08,0x08,0x08,0x08,
    0x00,0x60,0x60,0x00,0x00,
    0x20,0x10,0x08,0x04,0x02,
    0x3E,0x51,0x49,0x45,0x3E,
    0x00,0x42,0x7F,0x40,0x00,
    0x42,0x61,0x51,0x49,0x46,
    0x21,0x41,0x45,0x4B,0x31,
    0x18,0x14,0x12,0x7F,0x10,
    0x27,0x45,0x45,0x45,0x39,
    0x3C,0x4A,0x49,0x49,0x30,
    0x01,0x71,0x09,0x05,0x03,
    0x36,0x49,0x49,0x49,0x36,
    0x06,0x49,0x49,0x29,0x1E,
    0x00,0x36,0x36,0x00,0x00,
    0x00,0x56,0x36,0x00,0x00,
    0x00,0x08,0x14,0x22,0x41,
    0x14,0x14,0x14,0x14,0x14,
    0x41,0x22,0x14,0x08,0x00,
    0x02,0x01,0x51,0x09,0x06,
    0x32,0x49,0x79,0x41,0x3E,
    0x7E,0x11,0x11,0x11,0x7E,
    0x7F,0x49,0x49,0x49,0x36,
    0x3E,0x41,0x41,0x41,0x22,
    0x7F,0x41,0x41,0x22,0x1C,
    0x7F,0x49,0x49,0x49,0x41,
    0x7F,0x09,0x09,0x01,0x01,
    0x3E,0x41,0x41,0x51,0x32,
    0x7F,0x08,0x08,0x08,0x7F,
    0x00,0x41,0x7F,0x41,0x00,
    0x20,0x40,0x41,0x3F,0x01,
    0x7F,0x08,0x14,0x22,0x41,
    0x7F,0x40,0x40,0x40,0x40,
    0x7F,0x02,0x04,0x02,0x7F,
    0x7F,0x04,0x08,0x10,0x7F,
    0x3E,0x41,0x41,0x41,0x3E,
    0x7F,0x09,0x09,0x09,0x06,
    0x3E,0x41,0x51,0x21,0x5E,
    0x7F,0x09,0x19,0x29,0x46,
    0x46,0x49,0x49,0x49,0x31,
    0x01,0x01,0x7F,0x01,0x01,
    0x3F,0x40,0x40,0x40,0x3F,
    0x1F,0x20,0x40,0x20,0x1F,
    0x7F,0x20,0x18,0x20,0x7F,
    0x63,0x14,0x08,0x14,0x63,
    0x03,0x04,0x78,0x04,0x03,
    0x61,0x51,0x49,0x45,0x43,
    0x00,0x00,0x7F,0x41,0x41,
    0x02,0x04,0x08,0x10,0x20,
    0x41,0x41,0x7F,0x00,0x00,
    0x04,0x02,0x01,0x02,0x04,
    0x40,0x40,0x40,0x40,0x40,
    0x00,0x01,0x02,0x04,0x00,
    0x20,0x54,0x54,0x54,0x78,
    0x7F,0x48,0x44,0x44,0x38,
    0x38,0x44,0x44,0x44,0x20,
    0x38,0x44,0x44,0x48,0x7F,
    0x38,0x54,0x54,0x54,0x18,
    0x08,0x7E,0x09,0x01,0x02,
    0x08,0x14,0x54,0x54,0x3C,
    0x7F,0x08,0x04,0x04,0x78,
    0x00,0x44,0x7D,0x40,0x00,
    0x20,0x40,0x44,0x3D,0x00,
    0x00,0x7F,0x10,0x28,0x44,
    0x00,0x41,0x7F,0x40,0x00,
    0x7C,0x04,0x18,0x04,0x78,
    0x7C,0x08,0x04,0x04,0x78,
    0x38,0x44,0x44,0x44,0x38,
    0x7C,0x14,0x14,0x14,0x08,
    0x08,0x14,0x14,0x18,0x7C,
    0x7C,0x08,0x04,0x04,0x08,
    0x48,0x54,0x54,0x54,0x20,
    0x04,0x3F,0x44,0x40,0x20,
    0x3C,0x40,0x40,0x20,0x7C,
    0x1C,0x20,0x40,0x20,0x1C,
    0x3C,0x40,0x30,0x40,0x3C,
    0x44,0x28,0x10,0x28,0x44,
    0x0C,0x50,0x50,0x50,0x3C,
    0x44,0x64,0x54,0x4C,0x44,
    0x00,0x08,0x36,0x41,0x00,
    0x00,0x00,0x7F,0x00,0x00,
    0x00,0x41,0x36,0x08,0x00,
    0x08,0x08,0x2A,0x1C,0x08,
};

static uint16_t boot_px[8];

static void boot_char(uint32_t cx, uint32_t cy, char ch, uint16_t fg, uint16_t bg)
{
    uint32_t col, row;
    if (ch < ' ' || ch > '~')
        ch = ' ';
    const uint8_t *g = &font5x7[(ch - ' ') * 5];
    for (col = 0; col < 5; col++) {
        uint8_t bits = g[col];
        for (row = 0; row < 7; row++) {
            uint16_t c = (bits & (1u << row)) ? fg : bg;
            lcd_sync(); /* the previous DMA still owns boot_px */
            for (uint32_t i = 0; i < 4; i++)
                boot_px[i] = (uint16_t)((c >> 8) | (c << 8));
            lcd_blit(cx + col * 2, cy + row * 2, 2, 2, boot_px);
        }
    }
}

static void boot_text(uint32_t x, uint32_t y, const char *s, uint16_t fg, uint16_t bg)
{
    while (*s) {
        boot_char(x, y, *s, fg, bg);
        x += 12;
        s++;
    }
}

static void boot_char_1x(uint32_t cx, uint32_t cy, char ch, uint16_t fg, uint16_t bg)
{
    uint32_t col, row;
    if (ch < ' ' || ch > '~')
        ch = ' ';
    const uint8_t *g = &font5x7[(ch - ' ') * 5];
    for (col = 0; col < 5; col++) {
        uint8_t bits = g[col];
        for (row = 0; row < 7; row++) {
            uint16_t c = (bits & (1u << row)) ? fg : bg;
            lcd_sync();
            boot_px[0] = (uint16_t)((c >> 8) | (c << 8));
            lcd_blit(cx + col, cy + row, 1, 1, boot_px);
        }
    }
}

static uint32_t boot_text_width_1x(const char *s)
{
    uint32_t n = 0;
    while (*s++) n++;
    return n ? n * 6 - 1 : 0;
}

static void boot_text_1x(uint32_t y, const char *s, uint16_t fg, uint16_t bg)
{
    uint32_t w = boot_text_width_1x(s);
    uint32_t x = (240 - w) / 2;
    while (*s) {
        boot_char_1x(x, y, *s, fg, bg);
        x += 6;
        s++;
    }
}

/* --- fault handler (required by fm1_irq.h) --- */

static void fm1_fault(const fm1_crash_t *c)
{
    (void)c;
    lcd_fill(0, 0, 240, 240, 0xF800);
    boot_text(60, 100, "FAULT", 0xFFFF, 0xF800);
    lcd_sync();
    for (;;)
        fm1_wdt_feed();
}

/* --- boot logo (RLE-compressed RGB565, generated by tools/logo_convert.py) --- */

#include "ui/core/boot_logo.c"

/* --- component manifest (survives in binary for verification) --- */
#include "manifest.h"
volatile const char *cfm1_manifest_anchor = cfm1_manifest;

/* --- adapters (stub APIs for subsystems not yet integrated) ---
 *
 * Wired (real lineage .c exists in firmware/src/):
 *   cfm1_audio.h      - audio core types + voice interface
 *   cfm1_ble.h        - BLE stack (Optimist)
 *   cfm1_controls.h   - encoder/button names
 *   cfm1_log.h        - debug log ring buffer
 *   cfm1_ota.h        - flash ops for ota.c
 *   cfm1_page.h       - UI page vtable
 *   cfm1_settings.h   - sys menu settings
 *   cfm1_udisk.h      - USB mass storage
 *
 * Stub (placeholder, no real .c yet):
 *   cfm1_console.h    - CDC serial diagnostics
 *   cfm1_editor.h     - SysEx web editor
 *   cfm1_fx.h         - effects chain
 *   cfm1_midi.h       - MIDI routing + clock
 *   cfm1_mix.h        - per-track mixer
 *   cfm1_mod.h        - modulation matrix
 *   cfm1_scales.h     - scale/tuning tables
 *   cfm1_seq.h        - step sequencer
 *   cfm1_storage.h    - flash project storage
 *   cfm1_usb_audio.h  - USB audio class
 */

#ifdef CONFIG_USB
#define CFM1_USB_MIDI 1
#endif

#ifdef CONFIG_BLE_MIDI
#define CFM1_BLE_MIDI 1
#endif

#include "cfm1_ble.h"
#include "cfm1_audio.h"
#include "cfm1_seq.h"
#include "cfm1_fx.h"
#include "cfm1_midi.h"
#include "cfm1_storage.h"
#include "cfm1_mod.h"
#include "cfm1_mix.h"
#include "cfm1_scales.h"
#include "cfm1_udisk.h"
#include "cfm1_log.h"
#include "cfm1_controls.h"
#include "cfm1_usb_audio.h"
#include "cfm1_editor.h"
#include "cfm1_console.h"

#ifdef CONFIG_SYS_MENU
#include "cfm1_settings.h"
#include "cfm1_sys_menu.h"
#endif

/* --- USB host + mass storage (UDisk) --- */

#ifdef CONFIG_USB_HOST
#include "fm1_usb_musb.h"

/* Stubs for cpu.a RTOS dependencies */
void P33_SYSTEM_RESET(void) { fm1_enter_uboot(); }
const int config_asser = 0;
const char *os_current_task_rom(void) { return "fm1"; }
volatile uint32_t jiffies;
int printf(const char *fmt, ...) { (void)fmt; return 0; }
void cpu_assert_debug(void) { for (;;) fm1_wdt_feed(); }
void sys_local_irq_disable(void) { __asm__ volatile("cli" ::: "memory"); }
void sys_local_irq_enable(void) { __asm__ volatile("csync\n\tsti" ::: "memory"); }
void local_irq_disable(void) { __asm__ volatile("cli" ::: "memory"); }
void local_irq_enable(void) { __asm__ volatile("csync\n\tsti" ::: "memory"); }
void __local_irq_disable(void) { __asm__ volatile("cli" ::: "memory"); }
void __local_irq_enable(void) { __asm__ volatile("csync\n\tsti" ::: "memory"); }
int __local_irq_lock_cnt(void) { return 0; }
void udelay(uint32_t us) { (void)us; }
void delay(unsigned int n) { (void)n; }
void delay_us(unsigned int us) { (void)us; }
void hw_udelay(unsigned int us) { (void)us; }
void os_time_dly(uint32_t ticks) { fm1_delay_ms(ticks * 10); }
void wdt_clear(void) { fm1_wdt_feed(); }
void log_print(const char *fmt, ...) { (void)fmt; }
int putchar(int c) { (void)c; return c; }
int gpio_set_die(unsigned int gpio, int v) { (void)gpio; (void)v; return 0; }
int gpio_set_dieh(unsigned int gpio, int v) { (void)gpio; (void)v; return 0; }
int gpio_set_pull_up(unsigned int gpio, int v) { (void)gpio; (void)v; return 0; }
int gpio_set_pull_down(unsigned int gpio, int v) { (void)gpio; (void)v; return 0; }
int gpio_set_hd(unsigned int gpio, int v) { (void)gpio; (void)v; return 0; }
int gpio_set_hd1(unsigned int gpio, int v) { (void)gpio; (void)v; return 0; }
int gpio_set_direction(unsigned int gpio, unsigned int dir) { (void)gpio; (void)dir; return 0; }
int gpio_set_output_clk(unsigned int gpio, int clk) { (void)gpio; (void)clk; return 0; }
void gpio_set_output_channle(unsigned int gpio, unsigned int ch) { (void)gpio; (void)ch; }
uint32_t get_jiffies(void) { return jiffies; }
uint32_t time_lapse(uint32_t *r, uint32_t us) { uint32_t now = jiffies; uint32_t elapsed = now - *r; *r = now; return elapsed >= us; }
void usb_mdelay(unsigned int ms) { fm1_delay_ms(ms); }
int usb_host_timeout(unsigned int ms) { (void)ms; return 0; }

#include "fm1_usb_host.h"
#include "udisk/usb_host_xfer.c"
#include "udisk/usb_host_enum.c"
#include "udisk/usb_msc.c"
#ifdef CONFIG_UDISK
#include "vendor/fatfs/ff.c"
#include "udisk/diskio.c"
#include "udisk/udisk.c"
#endif
#endif

/* --- engine plugins (compile independently of the audio core) --- */

#ifdef CONFIG_ENGINES
#include "fe_tables.h"
#include "audio/engines/dsp.c"
#include "audio/engines/engines.c"
#endif

/* --- USB MIDI + SysEx (must compile even without the full audio core) --- */

#ifdef CONFIG_USB
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#ifndef MELODEE_CDC
#define MELODEE_CDC 0
#endif
#ifndef MELODEE_USB_AUDIO
#define MELODEE_USB_AUDIO 0
#endif
#ifdef CONFIG_OTA
#define MELODEE_OTA 1
#else
#define MELODEE_OTA 0
#endif
#include "midi/usb.c"
#ifdef CONFIG_USB_AUDIO
#include "midi/melodee/usb_audio.c"
#endif
#ifdef CONFIG_OTA
#include "../adapters/cfm1_ota.h"
#include "boot/ota.c"
#endif
#ifdef CONFIG_EDITOR
#include "editor/melodee/editor.c"
#endif
#ifdef CONFIG_CONSOLE
#include "console/console.c"
#endif
#endif

/* --- OTG dual-role switching (needs both device and host code above) --- */

#if defined(CONFIG_USB) && defined(CONFIG_USB_HOST)
#include "cfm1_otg.h"
#endif

/* --- BLE subsystem (Optimist stack, firmware/upstream/optimist/) --- */

#ifdef CONFIG_BLE
#define BLE_API static
#define BLE_DEVICE_NAME CONFIG_FIRMWARE_NAME
#ifdef CONFIG_BLE_CENTRAL
#define BLE_CENTRAL 1
#else
#define BLE_CENTRAL 0
#endif
#ifdef CONFIG_BLE_BOND
#define BLE_LL_ENC 1
#define BLE_SMP_LEGACY 1
#endif
#ifdef CONFIG_BLE_DIAG
#define BLE_DIAG 1
#else
#define BLE_DIAG 0
#endif
#ifdef CONFIG_BLE_HID
#define CFM1_BLE_HID 1
#define BLE_CF_NO_HID 100
#endif
#ifdef CONFIG_BLE_FILE_TRANSFER
#define CFM1_BLE_FT 1
#endif
#ifdef CONFIG_BLE_STUB
#define CFM1_BLE_STUB 1
#endif

/* forward declarations the app callbacks need */
#include "../upstream/optimist/firmware/src/ble/ble.h"

/* app callbacks (the firmware's side of the BLE stack: defined before the
 * modules that call them so the compiler sees the bodies, not just the
 * static declarations in ble.h) */
static int ble_app_midi_peek(uint32_t *pkt, uint32_t *t_ms) {
    (void)pkt; (void)t_ms; return 0;
}
static void ble_app_midi_pop(void) {}
static void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last_ts) {
    (void)pkt; (void)ts; (void)last_ts;
}
static void ble_app_state(void) {
    int conn = ble_connected();
    int midi = ble_midi_ready();
    cfm1_log(conn ? "ble: CONNECTED" : "ble: DISCONNECTED");
    cfm1_log_int("ble: midi_ready=", midi);
}
#if BLE_SMP_LEGACY
static void ble_app_bond(const uint8_t rand[8], uint16_t ediv, const uint8_t ltk[16]) {
    (void)rand; (void)ltk;
    cfm1_log("ble: BOND received");
    cfm1_log_int("ble: bond ediv=", (int32_t)ediv);
}
static void ble_app_peer_id(const uint8_t irk[16], const uint8_t addr[6], uint8_t addr_rand) {
    (void)irk;
    cfm1_log("ble: peer identity");
    cfm1_log_hex("ble: peer ", addr, 6);
    cfm1_log_int("ble: peer rnd=", addr_rand);
}
#if BLE_CENTRAL
static void ble_app_central_keys(const struct ble_keys *k) { (void)k; }
#endif
#endif

/* upstream modules */
#include "../upstream/optimist/firmware/src/ble/ble_prim.c"
#if BLE_LL_ENC
#include "../upstream/optimist/firmware/src/ble/ble_aes.c"
#endif
#include "../upstream/optimist/firmware/src/ble/ble_midi.c"
#include "../upstream/optimist/firmware/src/ble/ble_ll.c"
#include "../upstream/optimist/firmware/src/ble/ble_smp.c"
#include "../upstream/optimist/firmware/src/ble/ble_host.c"
#ifdef CFM1_BLE_FT
#include "ble/ble_ft.c"
#include "ble/ble_att_ft.c"
#else
#include "../upstream/optimist/firmware/src/ble/ble_att.c"
#endif
#include "../upstream/optimist/firmware/src/ble/ble_store.c"
#if BLE_CENTRAL
#include "ble/ble_scan.c"
#include "../upstream/optimist/firmware/src/ble/ble_gattc.c"
#include "../upstream/optimist/firmware/src/ble/ble_central.c"
#ifdef CFM1_BLE_HID
#include "ble/ble_hid.c"
#endif
static struct ble_scan_tab ble_scan_tab;
#endif
/* baseband driver */
#ifdef CFM1_BLE_STUB
#include "ble/ble_hw_stub_sim.c"
#else
#define BLE_HW_WL82 1
#include "../upstream/optimist/firmware/src/ble/ble_vm.c"
#include "../upstream/optimist/firmware/src/ble/ble_hw_wl82.c"
#endif

/* RF trim reader and radio startup (only with real baseband) */
#if BLE_HW_WL82
static struct ble_rf_trims cfm1_ble_trims;
static struct ble_vm_info  cfm1_ble_vm_info;
static uint8_t cfm1_ble_rf_src;
static uint8_t cfm1_ble_up;

static int cfm1_ble_vm_rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)
{
    (void)ctx;
#ifdef CONFIG_FLASH
    if (!cfm1_flash_ok)
        return -1;
    while (n) {
        uint32_t k = n > 256u ? 256u : n;
        uint32_t f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, dst, k);
        irq_restore(f);
        if (rc) return rc;
        off += k; dst += k; n -= k;
    }
    return 0;
#else
    (void)off; (void)dst; (void)n;
    return -1;
#endif
}

static int cfm1_ble_radio_start(void)
{
    struct ble_rf_trims vm;
    int complete, save;

    cfm1_log("ble: scanning VM trims...");
    complete = ble_vm_scan(cfm1_ble_vm_rd, 0, &cfm1_ble_vm_info, &vm, 0, 0);
    cfm1_log_int("ble: vm complete=", complete);
    cfm1_log_int("ble: vm area=", (int32_t)cfm1_ble_vm_info.area);
    cfm1_log_int("ble: vm nrec=", (int32_t)cfm1_ble_vm_info.nrec);
    cfm1_log_int("ble: vm have=", cfm1_ble_vm_info.have);
    cfm1_log_int("ble: vm ok187=", cfm1_ble_vm_info.ok187);
    if (cfm1_ble_vm_info.read_err)
        cfm1_log("ble: vm read error!");

    {
        uint8_t copy[BLE_RF_COPY_SIZE];
        uint32_t i;
        for (i = 0; i < BLE_RF_COPY_SIZE; i++)
            copy[i] = 0;
        cfm1_ble_rf_src = (uint8_t)ble_rf_choose(complete, &vm,
            cfm1_ble_vm_info.area, copy, &cfm1_ble_trims, &save);
    }

    cfm1_log_int("ble: rf_src=", cfm1_ble_rf_src);
    if (cfm1_ble_rf_src == BLE_RF_NONE) {
        cfm1_log("ble: NO TRIMS - radio OFF");
        return 0;
    }
    cfm1_log("ble: attaching IRQs");
    ble_hw_wl82_attach();
    cfm1_log("ble: starting radio...");
    cfm1_log_hex("ble: x106 ", cfm1_ble_trims.x106, 2);
    cfm1_log_hex("ble: x107 ", cfm1_ble_trims.x107, 7);
    ble_hw_wl82_start(&cfm1_ble_trims);
    cfm1_ble_up = 1;
    cfm1_log("ble: radio started OK");
    return 1;
}
#endif /* BLE_HW_WL82 */

#ifdef CONFIG_SYS_MENU_BLE
static uint8_t cfm1_ble_prev_on;
static uint8_t cfm1_ble_prev_disc;
static uint32_t cfm1_ble_sync_fires;
static uint32_t cfm1_ble_hb_ms;

static void cfm1_ble_sync(void)
{
    uint8_t want = sys_settings.ble_on && sys_settings.ble_discoverable;
    if (want != cfm1_ble_prev_disc || sys_settings.ble_on != cfm1_ble_prev_on) {
        cfm1_ble_sync_fires++;
        cfm1_log_int("ble:sync #", (int32_t)cfm1_ble_sync_fires);
        cfm1_log_int("ble: on=", sys_settings.ble_on);
        cfm1_log_int("ble: prev_on=", cfm1_ble_prev_on);
        cfm1_log_int("ble: disc=", sys_settings.ble_discoverable);
        cfm1_log_int("ble: want=", want);
        cfm1_log_int("ble: prev_disc=", cfm1_ble_prev_disc);
        cfm1_log_hex("ble: raw ", (const uint8_t *)&sys_settings, sizeof sys_settings);
        cfm1_ble_prev_on = sys_settings.ble_on;
        cfm1_ble_prev_disc = want;
        cfm1_log(want ? "ble: enabling adv" : "ble: disabling adv");
        ble_enable(want);
    }
#ifdef CFM1_BLE_STUB
    ble_hw_stub_tick();
#endif
    if (cfm1_log_ms - cfm1_ble_hb_ms > 5000u) {
        cfm1_ble_hb_ms = cfm1_log_ms;
        cfm1_log_int("ble:hb conn=", ble_connected());
        cfm1_log_int("ble:hb midi=", ble_midi_ready());
        cfm1_log_int("ble:hb fires=", (int32_t)cfm1_ble_sync_fires);
    }
}
#else
static void cfm1_ble_sync(void) {}
#endif

#endif /* CONFIG_BLE */

/* --- WiFi subsystem (GPL-3.0 clean-room driver) --- */

#ifdef CONFIG_WIFI
#include "../adapters/cfm1_wifi.h"
#include "wifi/wifi_rtos.c"
#include "wifi/wifi_rf.c"
#include "wifi/wifi_sdio.c"
#include "wifi/wifi_mac.c"
#include "wifi/wifi_sta.c"
#include "wifi/wifi_data.c"
#include "wifi/wifi_wpa.c"
#include "wifi/wifi_lwip.c"
#include "wifi/wifi_api.c"
#ifdef CONFIG_CDP
#include "wifi/wifi_tcp.c"
#include "cdp/cdp_ws.c"
#include "cdp/cdp_json.c"
#include "cdp/cdp_fm1.c"
#include "cdp/cdp_server.c"
#endif
#endif /* CONFIG_WIFI */

/* --- audio subsystem --- */

/* ISR timing: written by assembly in fm1_isr.S, non-static */
uint32_t fm1_audio_irq_start_ticks;
uint32_t fm1_audio_irq_elapsed_ticks;

static int32_t master_iir;

#ifdef CONFIG_AUDIO
#include "audio/params.c"
#ifndef CONFIG_ENGINES
#include "audio/engines/engines.c"
#endif

/* --- modulation matrix --- */
#ifdef CONFIG_MODULATION
#if defined(CONFIG_MOD_MELODEE)
#define CFM1_HAS_MOD 1
#include "audio/mod/melodee/mod.c"
#endif
#endif

#include "audio/voice.c"

/* --- stereo mixer --- */
#ifdef CONFIG_MIX
#define CFM1_HAS_MIX 1
#include "audio/mix/mix.c"
#endif

#include "audio/core/audio.c"

/* --- effects chain --- */
#ifdef CONFIG_EFFECTS_FELUCCA
#include "audio/effects/felucca/fx.c"
#elif defined(CONFIG_EFFECTS_MELODEE)
#include "audio/effects/melodee/fx.c"
#endif
#endif

/* --- scales --- */
#if defined(CONFIG_SCALES_MELODEE)
#define CFM1_HAS_SCALES 1
#include "scales/melodee/scales.c"
#elif defined(CONFIG_SCALES_FELUCCA)
#define CFM1_HAS_SCALES 1
#include "scales/scales.c"
#endif

/* --- MIDI routing --- */
#if defined(CONFIG_MIDI_MELODEE)
#include "midi/melodee/midi_routing.c"
#elif defined(CONFIG_MIDI_FELUCCA)
#include "midi/midi_routing.c"
#endif

/* --- sequencer --- */
#if defined(CONFIG_SEQ_MELODEE)
#include "seq/melodee/seq.c"
#elif defined(CONFIG_SEQ_FELUCCA)
#include "seq/seq.c"
#endif

/* --- storage --- */
#if defined(CONFIG_STORAGE_MELODEE)
#include "storage/melodee/storage.c"
#elif defined(CONFIG_STORAGE_FELUCCA)
#include "storage/storage.c"
#endif

/* --- GFX renderer --- */
#if defined(CONFIG_UI_FELUCCA)
#include "ui/felucca/gfx.c"
#elif defined(CONFIG_UI_MELODEE)
#include "ui/melodee/gfx.c"
#endif

/* --- UI shell --- */
#include "cfm1_page.h"

#if defined(CONFIG_UI_FELUCCA)
#include "ui/felucca/ui.c"
#elif defined(CONFIG_UI_MELODEE)
#include "ui/melodee/shell.c"
#else
/* core-fm1 default shell (bitmap font, minimal navigation) */
#include "ui/core/shell.c"
#include "ui/core/pages.c"
#endif

/* --- main loop --- */

/* TIMER5 ISR: 10 kHz system tick (input scan, USB poll, ms counter).
 * Non-static: called from isr_timer5 assembly wrapper in fm1_isr.S.
 * Priority 4 (above ALNK0 at 3): input scan keeps pace during audio render. */
void fm1_timer5_irq(void)
{
    uint32_t t0 = fm1_ticks();
    *(volatile uint32_t *)0x10900u |= 0x4000u;  /* fm1_timer5_ack */
#ifdef CONFIG_INPUT
    fm1_input_tick();
#endif
#ifdef CONFIG_USB
    {
        static uint8_t usb_div;
        if (++usb_div >= 5u) {       /* 10 kHz / 5 = 2 kHz, matching Melodee/Felucca/SLOOP */
            usb_div = 0;
            usb_poll();
        }
    }
#endif
    {
        static uint32_t last, acc;
        acc += t0 - last;
        last = t0;
        while (acc >= 1000u * FM1_TICKS_PER_US) {
            acc -= 1000u * FM1_TICKS_PER_US;
            fm1_ms++;
            cfm1_log_ms = fm1_ms;
        }
    }
}
extern void isr_timer5(void);

#ifdef CONFIG_INPUT
static void ui_input(void)
{
    {
        uint32_t np = fm1_input_note_edges();
        uint32_t i;
        for (i = 0; i < 27u; i++) {
            if (np & (1u << i)) {
#if defined(CONFIG_UI_FELUCCA)
                fsh_key(53u + i, 127);
#elif defined(CONFIG_UI_CORE)
                shell_key(53u + i, 127);
#elif defined(CONFIG_AUDIO)
                trk_note_on(&trk[song.sel], 53u + i, 127);
#endif
            }
        }
    }
    {
        static uint32_t prev_notes;
        uint32_t released = prev_notes & ~fm1_in.notes;
        uint32_t i;
        prev_notes = fm1_in.notes;
        for (i = 0; i < 27u; i++) {
            if (released & (1u << i)) {
#if defined(CONFIG_UI_FELUCCA)
                fsh_key(53u + i, 0);
#elif defined(CONFIG_UI_CORE)
                shell_key(53u + i, 0);
#elif defined(CONFIG_AUDIO)
                trk_note_off(&trk[song.sel], 53u + i);
#endif
            }
        }
    }
    {
        uint32_t bp, br;
        bp = fm1_input_edges(&br);
        if (bp || br) {
            uint32_t i;
            for (i = 0; i < 14u; i++) {
                if (bp & (1u << i)) {
#if defined(CONFIG_UI_FELUCCA)
                    fsh_button(i, 1);
#elif defined(CONFIG_UI_CORE)
                    shell_button(i, 1);
#endif
                }
                if (br & (1u << i)) {
#if defined(CONFIG_UI_FELUCCA)
                    fsh_button(i, 0);
#elif defined(CONFIG_UI_CORE)
                    shell_button(i, 0);
#endif
                }
            }
        }
    }
    {
        uint32_t e;
        for (e = 0; e < FM1_NENC; e++) {
            int32_t s = fm1_enc_take(e);
            if (s) {
#if defined(CONFIG_UI_FELUCCA)
                fsh_knob(e, s);
#elif defined(CONFIG_UI_CORE)
                shell_knob(e, s);
#endif
            }
        }
    }
}
#endif /* CONFIG_INPUT */

static void fm1_main(void)
{
    lcd_init();
    lcd_fill(0, 0, 240, 240, 0x0000);
    logo_draw(0, 0);
    boot_text_1x(228, CONFIG_FIRMWARE_VERSION, 0x8410, 0x0000);
    lcd_sync();

    cfm1_log_init();
    cfm1_log("core-fm1 boot");
    cfm1_log("lcd: init OK");
    cfm1_log_int("hal: sys variant=",
#if defined(CONFIG_HAL_SYS_MELODEE)
        2
#elif defined(CONFIG_HAL_SYS_FELUCCA)
        1
#else
        0
#endif
    );
    cfm1_log("log: ring buf ready");

#ifdef CONFIG_USB
    usb_start();
    cfm1_log("usb: started");
#endif

#ifdef CONFIG_FLASH
    cfm1_flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;
    if (cfm1_flash_ok) {
        fl_plain_window_init();
        cfm1_log("flash: OK");
    } else {
        cfm1_log("flash: no chip");
    }
#endif

#ifdef CONFIG_OTA
    if (cfm1_flash_ok)
        ota_boot_cleanup();
    cfm1_log("ota: ready");
#endif

#ifdef CONFIG_SYS_MENU
    sys_settings_init();
    cfm1_log("settings: loaded");
    cfm1_log_hex("settings: raw ", (const uint8_t *)&sys_settings, sizeof sys_settings);
    cfm1_log_int("settings: size=", (int32_t)sizeof sys_settings);
#ifdef CONFIG_SYS_MENU_USB_MODE
    cfm1_log_int("settings: usb_mode=", sys_settings.usb_mode);
#endif
#ifdef CONFIG_SYS_MENU_MIDI_CH
    cfm1_log_int("settings: midi_ch=", sys_settings.midi_ch);
#endif
#ifdef CONFIG_SYS_MENU_BLE
    cfm1_log_int("settings: ble_on=", sys_settings.ble_on);
    cfm1_log_int("settings: ble_disc=", sys_settings.ble_discoverable);
#endif
#endif

#ifdef CONFIG_BLE
    {
        uint8_t addr[6];
        uint8_t addr_random = ble_hw_addr(addr);
        if (addr_random && sys_settings.ble_addr[6] == 0xA5u &&
            (sys_settings.ble_addr[5] >> 6) == 3u) {
            uint32_t ai;
            for (ai = 0; ai < 6u; ai++)
                addr[ai] = sys_settings.ble_addr[ai];
            cfm1_log("ble: addr from flash");
        } else if (addr_random) {
            uint32_t ai;
            for (ai = 0; ai < 6u; ai++)
                sys_settings.ble_addr[ai] = addr[ai];
            sys_settings.ble_addr[6] = 0xA5u;
            sys_settings_dirty = 1;
            cfm1_log("ble: addr NEW, saving");
        }
        ble_init(addr, addr_random);
        cfm1_log(addr_random ? "ble: addr random" : "ble: addr public");
        cfm1_log_hex("ble: mac ", addr, 6);
        {
            const uint8_t *np;
            uint8_t nl = ble_gap_name(&np);
            char nbuf[CFM1_LOG_LINE_LEN];
            uint32_t ni = 0;
            const char *pfx = "ble: name=";
            while (*pfx && ni < CFM1_LOG_LINE_LEN - 1)
                nbuf[ni++] = *pfx++;
            for (uint32_t k = 0; k < nl && ni < CFM1_LOG_LINE_LEN - 1; k++)
                nbuf[ni++] = (char)np[k];
            nbuf[ni] = 0;
            cfm1_log(nbuf);
        }
        cfm1_log("ble: svc BLE-MIDI");
#ifdef CFM1_BLE_FT
        cfm1_log("ble: svc FileTransfer");
#endif
#if BLE_CENTRAL
        cfm1_log("ble: role central+periph");
#else
        cfm1_log("ble: role peripheral");
#endif
#if BLE_LL_ENC
        cfm1_log("ble: encryption ON");
#else
        cfm1_log("ble: encryption OFF");
#endif
#ifdef CFM1_BLE_HID
        cfm1_log("ble: HID gamepad ON");
#endif
#if BLE_HW_WL82
        {
            int radio_ok = cfm1_ble_radio_start();
            if (!radio_ok)
                cfm1_log("ble: radio FAILED");
        }
#endif
#ifdef CONFIG_SYS_MENU_BLE
        uint8_t adv = sys_settings.ble_on && sys_settings.ble_discoverable;
        cfm1_ble_prev_on = sys_settings.ble_on;
        cfm1_ble_prev_disc = adv;
        cfm1_log_int("ble: setting on=", sys_settings.ble_on);
        cfm1_log_int("ble: setting disc=", sys_settings.ble_discoverable);
        if (adv) {
            ble_enable(1);
#if BLE_HW_WL82
            if (cfm1_ble_up) {
                cfm1_log("ble: unmasking IRQs");
                fm1_ble_irqs_hold(0);
            }
#endif
        }
        cfm1_log(adv ? "ble: advertising" : "ble: off");
#else
        ble_enable(1);
#if BLE_HW_WL82
        if (cfm1_ble_up) {
            cfm1_log("ble: unmasking IRQs");
            fm1_ble_irqs_hold(0);
        }
#endif
        cfm1_log("ble: advertising");
#endif
    }
#endif

#ifdef CONFIG_USB_HOST
    otg_init();
    cfm1_log("OTG: device mode (boot)");
#endif

#ifdef CONFIG_UDISK
    udisk_init();
    cfm1_log("udisk: init");
#endif

#if defined(CONFIG_STORAGE_MELODEE) || defined(CONFIG_STORAGE_FELUCCA)
    storage_init();
    settings_load();
    cfm1_log("storage: init");
#endif

    scales_init();
    midi_routing_init();
    midi_clock_init();
    seq_init();
    mod_init();

#ifdef CONFIG_EDITOR
    editor_init();
    cfm1_log("editor: ready");
#endif
#ifdef CONFIG_CONSOLE
    console_init();
#endif

#ifdef CONFIG_INPUT
    fm1_input_init();
    cfm1_log("input: scan OK");
#endif

#if defined(CONFIG_UI_FELUCCA)
    fsh_init();
    fsh_register_pages();
    cfm1_log_int("ui: felucca, pages=", (int32_t)fsh_npage);
    fsh_boot();
    fsh_splash();
#elif defined(CONFIG_UI_CORE)
    shell_init();
    shell_register_pages();
    shell_boot();
#endif

#ifdef CONFIG_AUDIO
    fm1_adc_init();
    {
        /* Read physical pot before audio starts so initial volume matches knob. */
        fm1_adc_read(FM1_ADC_MASTER);          /* drop first (unreliable) */
        int32_t a = fm1_adc_read(FM1_ADC_MASTER);
        master_iir = a;
        song.master_q12 = (uint32_t)((a * a) >> 8);
    }
    audio_init();
    cfm1_log("audio: init OK");
    cfm1_log("audio: 44100Hz stereo");
    cfm1_log("audio: DMA half-buf 128");
#endif

#ifdef CONFIG_INPUT
    {
        *(volatile uint32_t *)0x10900u = 0x4000u;  /* T5 CON: clear */
        *(volatile uint32_t *)0x10904u = 0;         /* T5 CNT */
        *(volatile uint32_t *)0x10908u = 600u;      /* T5 PRD: 6MHz/600 = 10kHz */
        fm1_irq_attach(FM1_IRQ_TIMER5, isr_timer5, 4);
        *(volatile uint32_t *)0x10900u = 0x4019u;   /* T5 CON: enable */
    }
    cfm1_log("input: timer5 10kHz");
#endif
#ifdef CONFIG_WIFI
    wifi_init();
    cfm1_log("wifi: init");
#ifdef CONFIG_CDP
    cdp_init();
    cfm1_log("cdp: listening on port 9222");
#endif
#endif

    fm1_irq_enable_all();
    cfm1_log("irq: enabled");
    cfm1_log("boot complete");

    for (;;) {
        uint32_t m = fm1_ms;
        fm1_wdt_feed();
#ifdef CONFIG_USB
        usb_retry(fm1_ms);
#ifdef CONFIG_USB_HOST
        otg_poll(fm1_ms);
#endif
#ifdef CONFIG_UDISK
        udisk_poll();
#endif
#ifdef CONFIG_OTA
        ota_service();
        if (usb.ota_req) {
            usb.ota_req = 0;
#ifdef CONFIG_AUDIO
            fm1_audio_stop();
#endif
            if (cfm1_flash_ok)
                ota_session();
            lcd_fill(0, 0, 240, 240, 0x0000);
#if defined(CONFIG_UI_FELUCCA)
            fsh_dirty = 1;
#endif
        }
#endif
        if (usb.uboot_req) {
            usb_detach();
            fm1_delay_ms(30);
            fm1_enter_uboot();
        }
#endif
#ifdef CONFIG_INPUT
        {
            static uint32_t uboot_t0;
            uint32_t both = (1u << 0) | (1u << 1);
            if ((fm1_in.buttons & both) != both) {
                uboot_t0 = fm1_ms;
            } else if (fm1_ms - uboot_t0 > 5000u) {
#ifdef CONFIG_AUDIO
                fm1_audio_stop();
#endif
                lcd_fill(0, 0, 240, 240, 0x0000);
#ifdef CONFIG_USB
                usb_detach();
                fm1_delay_ms(30);
#endif
                fm1_enter_uboot();
            }
        }
#endif
#ifdef CONFIG_AUDIO
        {
            int32_t a = fm1_adc_read(FM1_ADC_MASTER);
            master_iir += (a - master_iir) >> 3;
            song.master_q12 = (uint32_t)((master_iir * master_iir) >> 8);
        }
#endif
#ifdef CONFIG_EDITOR
        editor_poll();
#endif
#if defined(CONFIG_DEBUG_LOG) && defined(CONFIG_USB)
        if (log_dump_req) {
            log_dump_req = 0;
            log_dump_reply();
        }
#endif
#if defined(CONFIG_SYS_MENU) && defined(CONFIG_USB)
        if (settings_reset_req) {
            settings_reset_req = 0;
            cfm1_log("settings: RESET requested");
            sys_settings_defaults();
            sys_settings_dirty = 1;
            sys_settings_flush();
            cfm1_log("settings: defaults written, rebooting");
            fm1_delay_ms(200);
            fm1_reboot();
        }
#endif
#ifdef CONFIG_CONSOLE
        console_poll();
#endif
#ifdef CONFIG_INPUT
        ui_input();
#endif
#ifdef CONFIG_SYS_MENU
        sys_settings_flush();
#endif
#ifdef CONFIG_BLE
        cfm1_ble_sync();
#ifdef CFM1_BLE_STUB
        if (bsim_ui_dirty) {
            bsim_ui_dirty = 0;
            fsh_dirty = 1;
        }
#endif
#endif
#ifdef CONFIG_WIFI
        wifi_poll();
#ifdef CONFIG_CDP
        cdp_poll();
#endif
#endif
#if defined(CONFIG_UI_FELUCCA)
        fsh_draw();
#elif defined(CONFIG_UI_CORE)
        shell_draw();
#endif
        while (fm1_ms - m < 15u) {
#ifdef CONFIG_INPUT
            ui_input();
#endif
        }
    }
}

/* --- C entry point (called from crt0.S, must not be static) --- */

void fm1_cstart(void)
{
    uint32_t *s, *d;
    fm1_time_init();
    fm1_wdt_arm(0x0D);
    fm1_core1_stop();
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;
    for (s = _dsp_load, d = _dsp_start; d < _dsp_end; s++, d++)
        *d = *s;
    for (s = _dt_load, d = _dt_start; d < _dt_end; s++, d++)
        *d = *s;
    for (s = _audio_load, d = _audio_start; d < _audio_end; s++, d++)
        *d = *s;
    fm1_main();
}
