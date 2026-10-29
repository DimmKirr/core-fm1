/* SPDX-License-Identifier: MIT */
/* Debug DAP adapter: NOINIT byte ring buffer + SysEx debug protocol.
 * Ported from fm1-dock (dock.c) for USB-based log reading and diagnostics.
 * The host side uses tools/hbdeploy.py (D_HELLO, D_LOG commands). */
#ifndef CFM1_DEBUG_H
#define CFM1_DEBUG_H
#include <stdint.h>

#ifdef CONFIG_DEBUG_DAP

/* ---- NOINIT structure: survives resets ---- */
#define CFM1_DBG_MAGIC    0x564E4246u      /* "FBNV" */
#define CFM1_DBG_LOG_SIZE 2048u
#define CFM1_DBG_CRASH_MAGIC 0x43525348u   /* "CRSH" */

typedef struct {
    uint32_t magic;
    uint32_t log_w;                        /* bytes written: ring position is log_w % LOG_SIZE */
    char     log[CFM1_DBG_LOG_SIZE];
    struct { uint32_t magic, vec, pc, rets, ms; } crash;
    struct { uint32_t worst, overruns; } stats;
} cfm1_dbg_nv_t;

/* non-static: the compiler must not assume zero-init for .noinit objects */
cfm1_dbg_nv_t cfm1_dbg_nv __attribute__((section(".noinit")));

static void cfm1_debug_init(void)
{
    if (cfm1_dbg_nv.magic == CFM1_DBG_MAGIC)
        return;
    memset(&cfm1_dbg_nv, 0, sizeof cfm1_dbg_nv);
    cfm1_dbg_nv.magic = CFM1_DBG_MAGIC;
}

static void cfm1_debug_put(const char *s)
{
    while (*s)
        cfm1_dbg_nv.log[cfm1_dbg_nv.log_w++ % CFM1_DBG_LOG_SIZE] = *s++;
}

/* ---- CRC-32 (zlib-compatible, bitwise) ---- */
static uint32_t cfm1_dbg_crc32(uint32_t c, const uint8_t *p, uint32_t n)
{
    uint32_t k;
    c = ~c;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}

/* ---- SysEx frame collection (filled by sysex_byte in usb.c) ---- */
static uint8_t  cfm1_dbg_sx[64];          /* incoming host request (small: D_HELLO, D_LOG) */
static uint32_t cfm1_dbg_sx_pos;
static volatile uint8_t cfm1_dbg_sx_ready;
static volatile uint8_t cfm1_dbg_sx_on;

/* ---- byte helpers (self-contained: ota_rd32/ota_wr32 are not yet visible) ---- */
static uint32_t dbg_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void dbg_wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

#else /* !CONFIG_DEBUG_DAP */

static void cfm1_debug_init(void) {}
static void cfm1_debug_put(const char *s) { (void)s; }

#endif /* CONFIG_DEBUG_DAP */

#endif /* CFM1_DEBUG_H */
