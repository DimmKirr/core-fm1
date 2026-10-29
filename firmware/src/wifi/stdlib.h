/* SPDX-License-Identifier: GPL-3.0-only */
/* Shim stdlib.h for lwIP on bare-metal AC791N.
 * Only provides atoi (used by netif.c for interface numbering). */
#ifndef CFM1_STDLIB_H
#define CFM1_STDLIB_H

#ifndef NULL
#define NULL ((void *)0)
#endif

static inline int atoi(const char *s)
{
    int n = 0, neg = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9')
        n = n * 10 + (*s++ - '0');
    return neg ? -n : n;
}

#endif /* CFM1_STDLIB_H */
