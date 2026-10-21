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
#include "ui/lcd.c"

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

#include "boot_logo.h"

static uint16_t logo_row[240];

static const uint8_t *boot_logo_decode_row(const uint8_t *p)
{
    uint32_t x = 0;
    while (*p) {
        uint32_t run = *p++;
        uint16_t px = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
        p += 2;
        while (run-- && x < 240u)
            logo_row[x++] = px;
    }
    p++;
    while (x < 240u)
        logo_row[x++] = 0;
    return p;
}

static void boot_logo_reveal(void)
{
    const uint8_t *p = boot_logo_rle;
    uint32_t y;
    for (y = 0; y < BOOT_LOGO_H; y++) {
        p = boot_logo_decode_row(p);
        lcd_sync();
        lcd_blit(0, y, 240, 1, logo_row);
    }
}

/* --- audio subsystem --- */

#ifdef CONFIG_AUDIO
#include "adapters/cfm1_audio.h"
#include "audio/params.c"
#include "engines/engines.c"
#include "audio/voice.c"
#include "audio/audio.c"
#endif

/* --- main loop --- */

static void fm1_main(void)
{
    lcd_init();
    lcd_fill(0, 0, 240, 240, 0x0000);
    boot_logo_reveal();
    boot_text_1x(228, CONFIG_FIRMWARE_VERSION, 0x8410, 0x0000);
    lcd_sync();

    for (;;) {
        fm1_wdt_feed();
        fm1_delay_ms(100);
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
