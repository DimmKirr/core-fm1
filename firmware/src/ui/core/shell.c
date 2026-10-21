/* SPDX-License-Identifier: GPL-3.0-only */
/* core-fm1 UI shell: page navigation, input routing, status bar.
 * Lineage shells (Felucca, Melodee) extend this with their visual
 * language. The core shell is a minimal fallback: bitmap font,
 * no theming, page name in the header, left/right to switch. */
#include "adapters/cfm1_page.h"

/* ---------------------------------------------------------------- state --- */

static const cfm1_page_t *shell_pages[CFM1_MAX_PAGES];
static uint32_t shell_npage;
static uint32_t shell_cur;          /* active page index */
static uint32_t shell_dirty;        /* 1: full redraw needed */

/* ------------------------------------------------------------ registry --- */

static void shell_register(const cfm1_page_t *page)
{
    if (shell_npage < CFM1_MAX_PAGES)
        shell_pages[shell_npage++] = page;
}

static void shell_set_page(uint32_t idx)
{
    if (idx >= shell_npage)
        return;
    if (shell_cur < shell_npage && shell_pages[shell_cur]->leave)
        shell_pages[shell_cur]->leave();
    shell_cur = idx;
    shell_dirty = 1;
    if (shell_pages[shell_cur]->enter)
        shell_pages[shell_cur]->enter();
}

/* ------------------------------------------------------- navigation --- */

static void shell_next(void)
{
    if (shell_npage > 1)
        shell_set_page((shell_cur + 1) % shell_npage);
}

static void shell_prev(void)
{
    if (shell_npage > 1)
        shell_set_page((shell_cur + shell_npage - 1) % shell_npage);
}

static const cfm1_page_t *shell_active(void)
{
    return shell_npage ? shell_pages[shell_cur] : 0;
}

/* -------------------------------------------------------- status bar --- */
/* 240 x 16 strip at the top: page name left, track/voice info right.
 * Uses the boot font (font5x7, boot_char_1x) already in core_fm1.c. */

static void shell_draw_status(void)
{
    const cfm1_page_t *p = shell_active();
    if (!p)
        return;

    lcd_fill(0, 0, 240, 16, 0x0000);

    /* page name, left */
    boot_text_1x(2, p->title ? p->title : p->name, 0x8410, 0x0000);

    /* page position indicator: "2/5" right-aligned */
    if (shell_npage > 1) {
        char buf[8];
        uint32_t i = 0;
        buf[i++] = '0' + (char)((shell_cur + 1) / 10);
        if (buf[0] == '0') i = 0;
        buf[i++] = '0' + (char)((shell_cur + 1) % 10);
        buf[i++] = '/';
        buf[i++] = '0' + (char)(shell_npage / 10);
        if (buf[i - 1] == '0') i--;
        buf[i++] = '0' + (char)(shell_npage % 10);
        buf[i] = 0;
        uint32_t w = boot_text_width_1x(buf);
        boot_text_1x(2 + 240 - 4 - w, buf, 0x8410, 0x0000);
    }

    /* let the page add its own status (optional) */
    if (p->status)
        p->status();
}

/* ----------------------------------------------------------- drawing --- */

static void shell_draw(void)
{
    const cfm1_page_t *p = shell_active();
    if (!p)
        return;

    if (shell_dirty) {
        lcd_fill(0, 0, 240, 240, 0x0000);
        shell_draw_status();
        shell_dirty = 0;
    }

    if (p->draw)
        p->draw();
}

/* ------------------------------------------------------------- input --- */

static void shell_knob(uint32_t id, int32_t clicks)
{
    const cfm1_page_t *p = shell_active();
    if (p && p->knob && p->knob(id, clicks))
        return;
    /* unhandled: nav encoder (id 0) switches pages */
    if (id == 0) {
        if (clicks > 0) shell_next();
        else if (clicks < 0) shell_prev();
    }
}

static void shell_button(uint32_t id, uint32_t down)
{
    const cfm1_page_t *p = shell_active();
    if (p && p->button && p->button(id, down))
        return;
    /* unhandled buttons: shell could handle HOME, etc. */
}

static void shell_key(uint32_t note, uint32_t vel)
{
    const cfm1_page_t *p = shell_active();
    if (p && p->key)
        p->key(note, vel);
}

/* -------------------------------------------------------------- init --- */

static void shell_init(void)
{
    shell_npage = 0;
    shell_cur = 0;
    shell_dirty = 1;
}

static void shell_boot(void)
{
    uint32_t i;
    /* find the HOME page */
    for (i = 0; i < shell_npage; i++) {
        if (shell_pages[i]->flags & PAGE_HOME) {
            shell_set_page(i);
            return;
        }
    }
    /* no HOME flag: start on page 0 */
    if (shell_npage)
        shell_set_page(0);
}
