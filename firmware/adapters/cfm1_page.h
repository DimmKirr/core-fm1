/* SPDX-License-Identifier: MIT */
/* Page adapter: the contract between UI shells and page content.
 * A page is one screen of the synthesizer UI (home, engine params,
 * sequencer, effects, etc.). The shell handles navigation, input
 * routing, and the status bar. Pages handle content. */
#ifndef CFM1_PAGE_H
#define CFM1_PAGE_H
#include <stdint.h>

/* page flags */
#define PAGE_HOME    (1u << 0)  /* the default page after boot */
#define PAGE_SYSTEM  (1u << 1)  /* system page (settings, about) */

typedef struct cfm1_page {
    const char *name;           /* short label: "HOME", "ENGINE", "SEQ" */
    const char *title;          /* display title (shell renders this) */
    uint32_t flags;

    /* lifecycle */
    void (*enter)(void);        /* page became active (draw full) */
    void (*leave)(void);        /* page lost focus (save state) */

    /* rendering */
    void (*draw)(void);         /* redraw page content to canvas */
    void (*status)(void);       /* draw status bar contribution (optional, NULL ok) */

    /* input (return 1 if handled, 0 to let shell handle) */
    int (*knob)(uint32_t id, int32_t clicks);
    int (*button)(uint32_t id, uint32_t down);
    int (*key)(uint32_t note, uint32_t vel);   /* piano key (optional, NULL ok) */
} cfm1_page_t;

/* a shell registers pages at startup; max 16 pages */
#define CFM1_MAX_PAGES 16

#endif /* CFM1_PAGE_H */
