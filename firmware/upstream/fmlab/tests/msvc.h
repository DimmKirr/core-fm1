/* SPDX-License-Identifier: GPL-3.0-only */
/* Host build on MSVC: cl is not gcc. Force-included (/FI) ahead of everything.
 * Only the host half of platform.c compiles here, so the one GNU-ism that reaches the compiler is the
 * section attribute on gfx.c's canvas, which means nothing off the device. */
#ifndef FMLAB_MSVC_H
#define FMLAB_MSVC_H
#ifdef _MSC_VER
#define __attribute__(x)
#pragma warning(disable : 4996)                 /* fopen, sscanf: "unsafe" */
#pragma warning(disable : 4127)                 /* conditional expression is constant */
#endif
#endif
