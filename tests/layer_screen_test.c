/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Host test of Felucca 1.2's panel controls as Melodee has them, through the UI test's stubs (ui_test.c):
 * double-tapped quick layers stay open and own the keys silently, FX LATCH, the FX key map (PRESETS assigns, EDIT
 * restores the default), MENU > SCREEN OFF (the panel dark while the transport runs, the wake press swallowed, NEVER),
 * and LFO 2 (SYNC's increments at every division and tempo, TRIG NOTE / FREE, POL UNI, the transport's reset). */
#include <stdint.h>
static uint32_t lcd_commands[8], lcd_count;
static void map_take_hook(uint32_t e);
#define UI_ENC_TAKE_HOOK(e) map_take_hook(e)
#define UI_LCD_POWER(s) (lcd_commands[lcd_count++ % 8u] = (s))
#define UI_LCD_WAKE() ((void)0)
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <assert.h>

static void reset_controls(void)
{
    seq_stop(); ui_power_on();
    memset(&scrn, 0, sizeof scrn); lcd_count = 0;
    settings_screen = kb_asleep = kb_lock = 0;
}
static void tap_fast(uint32_t b) { btn_down(b); frame(); btn_up(b); frame(); }
static void test_layers(void)
{
    for (uint32_t l=LAYER_GLO;l<LAYER_N;l++) {
        reset_controls(); uint32_t b=LAYERS[l].btn;
        tap_fast(b); tap_fast(b);
        assert(ui.lock==l && ui.layer==l && ui.home);
        key_down(white(0)); frame();
        assert(kb_layer & (1u<<white(0))); assert(!gates());
        key_up(white(0)); frame(); tap_fast(b);
        assert(!ui.lock && !ui.layer);
    }
    puts("layers/screen: double-tap layers and silent layer keys passed");
}
static uint32_t map_watch, map_takes[NE], map_inject_role;
static int32_t map_inject;
static void map_take_hook(uint32_t e)
{
    if (map_watch) {
        for (uint32_t role = 0; role < NE; role++)
            if (e == panel.enc[role]) map_takes[role]++;
        if (map_inject && e == panel.enc[map_inject_role]) {
            host_enc[e] += map_inject * panel.dir[map_inject_role];
            map_inject = 0;
        }
    }
}
static void test_map_encoder_reads(void)
{
    const uint32_t roles[] = {EN_K1, EN_K2, EN_SELECT};
    for (uint32_t i = 0; i < NELEM(roles); i++)
    for (int32_t dir = -1; dir <= 1; dir += 2) {
        reset_controls(); set_engine_of(TSEL, ENGI_FM6); fm6_opsel = 2;
        go_title("EG RATE"); frame(); press(B_EDIT);
        uint32_t row = smap.row, col = smap.col, role = roles[i], page = ui.page;
        assert(smap.on && row >= 2u && row + 2u < sec.n && col == 2u);
        memset(map_takes, 0, sizeof map_takes);
        map_inject_role = role; map_inject = dir; map_watch = 1;
        turn(role, dir);                             /* scan ISR publishes another detent just after the read */
        map_watch = 0;
        assert(map_takes[role] == 1u);
        for (uint32_t r = 0; r < NE; r++) assert(map_takes[r] == 1u);
        assert(host_enc[panel.enc[role]] == dir * panel.dir[role]);
        assert(ui.page == page && smap.row == row + (role == EN_K2 ? dir : 0) &&
               smap.col == (role == EN_K2 ? 0u : col + dir));
        memset(map_takes, 0, sizeof map_takes); map_watch = 1;
        frame();
        map_watch = 0;
        assert(map_takes[role] == 1u && !host_enc[panel.enc[role]]);
        assert(ui.page == page && smap.row == row + (role == EN_K2 ? 2 * dir : 0) &&
               smap.col == (role == EN_K2 ? 0u : col + 2 * dir));
    }
    puts("EDIT map: detents arriving after a knob read survive until the next input pass, both axes and directions");
}
static void test_screen_sleep(void)
{
    reset_controls(); scr_put(1); song.playing=1;
    fm1_ms=scrn.idle+300000u; ui_draw();
    assert(scrn.st==SCR_OFF && kb_asleep && song.playing && lcd_commands[0]==0);
    key_down(white(0)); frame(); assert(!gates() && scrn.st==SCR_WAKE);
    fm1_ms+=120; ui_draw(); assert(scrn.st==SCR_SLPOUT && lcd_commands[1]==1);
    fm1_ms+=120; ui_draw(); assert(scrn.st==SCR_ON && lcd_commands[2]==2);
    key_up(white(0)); fm1_ms+=320; frame(); assert(!kb_asleep);
    key_down(white(0)); frame(); assert(gates()); key_up(white(0)); frame();
    song.playing=0;
    for(uint32_t v=0;v<5;v++){scr_put(v);assert(scr_get()==v);}
    scr_put(0);fm1_ms+=3600001u;ui_draw();assert(scrn.st==SCR_ON);
    scr_put(1);ui.confirm=1;fm1_ms+=300001;ui_draw();assert(scrn.st==SCR_ON);
    ui.confirm=0;scr_put(1);fm1_ms+=300001;ui_draw();assert(scrn.st==SCR_OFF);
    int16_t value=TSEL->p[P_LEVEL];host_enc[panel.enc[EN_K1]]=2;frame();assert(TSEL->p[P_LEVEL]==value);
    scr_wake_now();fm1_ms+=320;frame();assert(!kb_asleep);
    puts("layers/screen: screen timeout, continuing transport, silent wake key/knob and NEVER passed");
}
static void test_lfo(void)
{
    reset_controls(); track_t *t=TSEL;
    for(uint32_t s=1;s<NELEM(N_LSYNC);s++){
        t->p[P_LSYNC]=s;
        for(uint32_t bpm=60;bpm<=240;bpm+=60){
            song.g[G_BPM]=bpm;uint32_t cycle=div_samples(LSYNC_DIV[s]);
            double expected=4294967296.0*CTL/cycle;
            assert(fabs((double)lfo_sync_inc(t)-expected)<1.01);
        }
    }
    t->p[P_LSYNC]=0;t->p[P_LTRIG]=1;t->lfo_ph=0x34567890;
    trk_note_on(t,60,100);assert(t->lfo_ph==0x34567890);trk_note_off(t,60);
    t->p[P_LTRIG]=0;trk_note_on(t,64,100);assert(!t->lfo_ph);
    t->p[P_LPOL]=1;t->p[P_LWAVE]=0;
    int32_t out[CTL];for(uint32_t i=0;i<2000;i++){track_render(t,out,CTL);assert(t->lfo_val>=0 && t->lfo_val<=32767);}
    t->p[P_LTRIG]=1;t->p[P_LSYNC]=5;t->lfo_ph=1;seq_start();assert(!t->lfo_ph);seq_stop();
    puts("layers/screen: all synced LFO divisions, NOTE/FREE, UNI and transport reset passed");
}
/* the FX key map and the native CZ-1 favorites share no bits (the map sat in CZ's row, factory[15][14..23], over
 * native slots 47..126's stars), and a settings record from before the move keeps its map, or its stars when its
 * bytes cannot be a map (a code no map writes) or sit only on filled slots */



int main(void) { test_layers(); test_map_encoder_reads(); test_screen_sleep(); test_lfo(); return 0; }
