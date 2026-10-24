/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Derived from Melodee (Kerem Kilic, Ellic Studio) loader. */
/* Core FM1 update loader entry. The SPL decompresses the loader to its load
 * address (0x01C0A800) and calls its first byte. Interrupts off,
 * our own stacks, clear .bss, then C. Never returns. */
	.section .text.crt0,"ax",@progbits
	.globl _start
_start:
	goto 1f
	.ascii "COREFM1-LOADER-1"	/* 16 bytes: core-fm1 recognises its loader in a package by this */
	.balign 2
1:	cli
	sp = _stack_top
	ssp = _sstack_top
	call ldr_cstart
	goto .
