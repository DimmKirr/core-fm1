/* SPDX-License-Identifier: GPL-3.0-only */
/* Universal view-based navigation: page registry, view memory, cycling.
 * Included in the unity build before the shell. All functions static. */

static const cfm1_page_t *nav_pages[CFM1_MAX_PAGES];
static uint32_t nav_npage;
static uint32_t nav_cur;
static uint8_t  nav_view;
static uint32_t nav_dirty;

#define NAV_NO_PAGE 0xFFu
static uint8_t nav_view_last[VIEW_COUNT];

static void nav_init(void)
{
    uint32_t i;
    nav_npage = 0;
    nav_cur = 0;
    nav_view = VIEW_HOME;
    nav_dirty = 1;
    for (i = 0; i < VIEW_COUNT; i++)
        nav_view_last[i] = NAV_NO_PAGE;
}

static void nav_register(const cfm1_page_t *page)
{
    if (nav_npage < CFM1_MAX_PAGES)
        nav_pages[nav_npage++] = page;
}

static const cfm1_page_t *nav_active(void)
{
    return nav_npage ? nav_pages[nav_cur] : 0;
}

static void nav_set_page(uint32_t idx)
{
    if (idx >= nav_npage)
        return;
    if (nav_cur < nav_npage && nav_pages[nav_cur]->leave)
        nav_pages[nav_cur]->leave();
    nav_cur = idx;
    nav_view = nav_pages[idx]->view;
    if (nav_view < VIEW_COUNT)
        nav_view_last[nav_view] = (uint8_t)idx;
    nav_dirty = 1;
    if (nav_pages[nav_cur]->enter)
        nav_pages[nav_cur]->enter();
}

static uint32_t nav_find_in_view(uint8_t view, uint32_t from, int32_t dir)
{
    uint32_t i, pos = from;
    for (i = 0; i < nav_npage; i++) {
        if (dir > 0)
            pos = (pos + 1) % nav_npage;
        else
            pos = (pos + nav_npage - 1) % nav_npage;
        if (nav_pages[pos]->view != view)
            continue;
        if (nav_pages[pos]->visible && !nav_pages[pos]->visible())
            continue;
        return pos;
    }
    return from;
}

static void nav_open_view(uint8_t view)
{
    uint32_t i;
    uint8_t idx;

    if (view >= VIEW_COUNT)
        return;

    if (nav_view == view) {
        uint32_t next = nav_find_in_view(view, nav_cur, 1);
        if (next != nav_cur)
            nav_set_page(next);
        return;
    }

    idx = nav_view_last[view];
    if (idx != NAV_NO_PAGE && idx < nav_npage &&
        nav_pages[idx]->view == view &&
        (!nav_pages[idx]->visible || nav_pages[idx]->visible())) {
        nav_set_page(idx);
        return;
    }

    for (i = 0; i < nav_npage; i++) {
        if (nav_pages[i]->view == view &&
            (!nav_pages[i]->visible || nav_pages[i]->visible())) {
            nav_set_page(i);
            return;
        }
    }
}

static void nav_cycle(int32_t dir)
{
    uint32_t next = nav_find_in_view(nav_view, nav_cur, dir);
    if (next != nav_cur)
        nav_set_page(next);
}

static void nav_build_views(void)
{
    uint32_t i;
    for (i = 0; i < nav_npage; i++) {
        uint8_t v = nav_pages[i]->view;
        if (v < VIEW_COUNT && nav_view_last[v] == NAV_NO_PAGE) {
            if (!nav_pages[i]->visible || nav_pages[i]->visible())
                nav_view_last[v] = (uint8_t)i;
        }
    }
}

static void nav_boot(void)
{
    nav_build_views();
    nav_open_view(VIEW_HOME);
}
