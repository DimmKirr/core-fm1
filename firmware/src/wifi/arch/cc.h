/* SPDX-License-Identifier: BSD-3-Clause */
/* lwIP architecture port for AC791N (pi32v2, 32-bit bare-metal).
 * Defines compiler/platform abstractions required by lwip/arch.h. */

#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

/* --- standard integer types -------------------------------------------- */
/* The JieLi toolchain provides <stdint.h>. */
#include <stdint.h>

typedef uint8_t   u8_t;
typedef int8_t    s8_t;
typedef uint16_t  u16_t;
typedef int16_t   s16_t;
typedef uint32_t  u32_t;
typedef int32_t   s32_t;
typedef uintptr_t mem_ptr_t;

/* --- byte order -------------------------------------------------------- */
#define BYTE_ORDER LITTLE_ENDIAN

/* --- compiler hints ---------------------------------------------------- */
#define LWIP_PLATFORM_DIAG(x)   do { (void)0; } while (0)
#define LWIP_PLATFORM_ASSERT(x) do { (void)0; } while (0)

/* Pack structs using GCC/Clang attribute */
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x

/* --- type limits (replaces <limits.h> when LWIP_NO_LIMITS_H=1) ---------- */
#ifndef INT_MAX
#define INT_MAX  0x7FFFFFFF
#endif

/* --- printf format strings (unused: LWIP_STATS=0, diagnostics are no-ops) */
#define X8_F   "02x"
#define U16_F  "u"
#define S16_F  "d"
#define X16_F  "x"
#define U32_F  "u"
#define S32_F  "d"
#define X32_F  "x"
#define SZT_F  "u"

/* --- sys_now: millisecond tick for lwIP timers ------------------------- */
/* Implemented in wifi_lwip.c; returns the global fm1_ms counter. */
u32_t sys_now(void);

#endif /* LWIP_ARCH_CC_H */
