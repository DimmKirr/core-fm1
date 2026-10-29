/* SPDX-License-Identifier: GPL-3.0-only */
/* Cooperative context switch for pi32v2.
 * Based on fm1-base ctx.S (proven on AC791N hardware).
 * Saves rets + r4-r15, exchanges sp, restores through pc. */

    .section .text,"ax",@progbits

/* void sched_switch(uint32_t **old_sp, uint32_t *new_sp)
 * r0 = &old_task->saved_sp
 * r1 = new_task->saved_sp
 *
 * Saves callee-saved registers + return address onto current stack.
 * Stores current sp into *old_sp.
 * Loads new sp from new_sp.
 * Restores registers and returns to new task's saved pc. */
    .globl sched_switch
sched_switch:
    [--sp] = {rets}
    [--sp] = {r15-r4}
    r2 = sp
    [r0] = r2
    sp = r1
    {r15-r4} = [sp++]
    {rets} = [sp++]
    rts
