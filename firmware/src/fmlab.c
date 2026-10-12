/* SPDX-License-Identifier: GPL-3.0-only */
/* FM Lab: a six-operator FM synth for the M-VAVE FM-1, to learn FM sound design on.
 *
 * The engine is Dexed's msfa core as sloopDX ported it to integer C (dx7_core.c): a real DX7 voice,
 * 32 algorithms, six operators with their envelopes, the pitch envelope and the LFO. This file is the
 * instrument around it. platform.c underneath is Dinghy's, untouched.
 *
 *   keys         play, 8 voices; the notes go out over USB-MIDI too
 *   OCT- OCT+    the octave (kept in flash)
 *   PRESETS      the voice
 *   MASTER       the volume (platform.c)
 *   USB-MIDI     notes in play, any channel
 *
 * Editing, which is the point of the thing. Every knob takes effect under the notes you are already
 * holding -- dx_update is Dexed's Dx7Note::update, so the timbre changes without restarting the
 * envelopes or the phases. Turn an operator's level while a chord rings and you hear exactly what
 * that operator was doing.
 *
 *   HOME         the keyboard and the voice
 *   EDIT         the selected operator: level, ratio, detune, sensitivities, keyboard scaling
 *   ENV          the selected operator's envelope: four rates, four levels
 *   LFO          speed, delay, pitch and amplitude depth, wave
 *   GLO          algorithm, feedback, key sync, transpose, the pitch envelope
 *   SELECT       which operator (1..6) the EDIT and ENV pages show
 *   ALGORITHM    the algorithm, on any page
 *   SEL          the next bank of four on the page (the knobs are KNOB1..4, left to right)
 *   FX           clear the PK mark on the load meter
 *
 * Rules of the road (the chip has no FPU and no libgcc): integers only, no float, no 64-bit division.
 * tests/host.c runs this file on a computer: the screen as a PNG, the sound as a WAV.
 *
 * Who owns what, between the main loop and the audio ISR:
 *   the ISR owns dxv[], vst[] and the render. It never touches flash or the screen.
 *   the main loop owns the panel, the screen and the patch buffers. It speaks to the ISR only through
 *   the event ring (ev_push) and the published patch index (pcur), so no large struct is ever written
 *   while the ISR is reading it.
 */
#include "platform.c"

#define DX_LG_N 6                               /* 64-sample blocks: as Dexed renders, 4 per audio ISR */
#include "dx7_core.c"
#include "dx7_voice.h"
#include "dx7_voices.h"

#define NV 8u                                   /* voices (RAM: 428 B each) */

/* ---- the patch. `edit` is the main loop's own copy and the one the knobs change; pbuf is the pair
 * the ISR reads from. A change goes: edit -> the spare pbuf -> publish -> flip -> tell the ISR. The
 * ISR never sees a half-written patch, and nothing large is ever written under it. */
static uint8_t edit[156];
static uint8_t pbuf[2][156];
static volatile uint8_t pcur;                   /* the ISR reads pbuf[pcur] */
static uint8_t modified;                        /* the voice differs from the factory one */

/* ---- main loop -> audio ISR: a single-producer ring of note events */
enum { EV_ON = 1, EV_OFF, EV_PATCH, EV_PANIC };
typedef struct { uint8_t kind, note, vel; } ev_t;
#define EVQ 32u
static ev_t evq[EVQ];
static volatile uint32_t ev_w, ev_r;

static void ev_push(uint32_t kind, uint32_t note, uint32_t vel)
{
    uint32_t w = ev_w;
    if (w - ev_r >= EVQ)
        return;                                 /* full: drop (the ISR is 5.8 ms behind at worst) */
    /* a patch event only says "read pbuf[pcur] again", so a second one behind the first is nothing:
     * a knob turned quickly would otherwise queue a dx_update of every voice per detent */
    if (kind == EV_PATCH && w != ev_r && evq[(w - 1u) % EVQ].kind == EV_PATCH)
        return;
    evq[w % EVQ].kind = (uint8_t)kind;
    evq[w % EVQ].note = (uint8_t)note;
    evq[w % EVQ].vel = (uint8_t)vel;
    RING_PUBLISH();                             /* the event is written before the ISR may see it */
    ev_w = w + 1u;
}

static void patch_publish(void)                 /* main loop: hand `edit` to the ISR */
{
    memcpy(pbuf[pcur ^ 1u], edit, 156);
    RING_PUBLISH();                             /* the buffer is filled before the ISR is told to read it */
    pcur ^= 1u;
    ev_push(EV_PATCH, 0, 0);
}

/* ---- the voices (the ISR's) */
static dxv_t dxv[NV];
static struct {
    uint8_t note, vel, held;
    uint32_t age;
} vst[NV];
static uint32_t ages;
static volatile uint32_t v_lit;                 /* bit v: voice v sounds (the main loop reads, for the LEDs) */

/* The voice's transpose, u[144], 24 = none. dx7_core.c does not read it (sloopDX did not either), so
 * it is applied here, at the one place a note reaches the engine. vst[].note stays the note that was
 * played, so the keyboard and note-off still match. */
static int tnote(uint32_t note)
{
    int n = (int)note + (int)pbuf[pcur][144] - 24;
    return n < 0 ? 0 : n > 127 ? 127 : n;
}

static void v_on(uint32_t note, uint32_t vel)
{
    uint32_t v, pick = 0, best = 0xFFFFFFFFu, score;
    for (v = 0; v < NV; v++) {
        if (vst[v].held && vst[v].note == note) score = 0u;                     /* the same key again */
        else if (!dxv[v].on)                    score = 1u;                     /* never used */
        else if (!dx_playing(&dxv[v]))          score = 2u;                     /* finished */
        else if (!vst[v].held)                  score = 0x40000000u + vst[v].age;  /* releasing: oldest */
        else                                    score = 0x80000000u + vst[v].age;  /* held: oldest */
        if (score < best) { best = score; pick = v; }
    }
    vst[pick].note = (uint8_t)note;
    vst[pick].vel = (uint8_t)vel;
    vst[pick].held = 1;
    vst[pick].age = ++ages;
    dx_init(&dxv[pick], pbuf[pcur], tnote(note), (int)vel);
}

static void v_off(uint32_t note)
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (dxv[v].on && vst[v].held && vst[v].note == note) {
            vst[v].held = 0;
            dx_keyup(&dxv[v]);
        }
}

static void ev_drain(void)                      /* audio ISR, before the render */
{
    while (ev_r != ev_w) {
        const ev_t *e = &evq[ev_r % EVQ];
        uint32_t v;
        switch (e->kind) {
        case EV_ON:  v_on(e->note, e->vel); break;
        case EV_OFF: v_off(e->note); break;
        case EV_PATCH:                          /* the voice changed under the sounding notes */
            for (v = 0; v < NV; v++)
                if (dxv[v].on)
                    dx_update(&dxv[v], pbuf[pcur], tnote(vst[v].note), (int)vst[v].vel);
            break;
        case EV_PANIC:
            for (v = 0; v < NV; v++) { dxv[v].on = 0; vst[v].held = 0; }
            break;
        default: break;
        }
        ev_r++;
    }
}

/* ---- the load. The ISR must render HALF_FRAMES before the DMA plays them: 5.80 ms at 44.1 kHz.
 * Anything under that is headroom, and this is the number that decides how many voices the FM-1 can
 * actually hold. On the computer fm1_ticks() is that computer's clock, so the figure means nothing
 * there -- read it on the device. */
#define BUDGET_US (HALF_FRAMES * 1000000u / FS)
static volatile uint32_t cpu_q8, cpu_peak;      /* Q8 of the budget; the worst block so far, in percent */
static uint8_t cpu_over;                        /* bit k: the block k blocks ago took over 85 % */

static void v_shed(void)                        /* overloaded: lose a voice rather than glitch every voice */
{
    uint32_t v, pick = NV, worst = 0, score;
    for (v = 0; v < NV; v++) {
        if (!dxv[v].on)
            continue;
        score = (vst[v].held ? 0u : 0x80000000u) + (ages - vst[v].age);   /* releasing first, then oldest */
        if (score >= worst) { worst = score; pick = v; }
    }
    if (pick < NV)
        dxv[pick].on = 0;
}

/* ---- the sound. dx_compute adds DX_N mono samples; one carrier at full level is buf >> 12 = 8192,
 * so OUT_SHIFT 13 would let eight of them reach full scale together -- but eight real voices have more
 * than one carrier each, so 14 is where a full chord stops clipping. The DC blocker is on the sum: FM
 * with feedback is not symmetric, and the offsets of the voices add. */
#define OUT_SHIFT 14

static void app_audio(int32_t *out, uint32_t n)
{
    static int32_t mono[DX_N];
    static int32_t dc_x, dc_y;
    uint32_t b, v, i, lit = 0, us, t0 = fm1_ticks();

    ev_drain();

    for (b = 0; b < n; b += DX_N) {
        for (i = 0; i < DX_N; i++)
            mono[i] = 0;
        for (v = 0; v < NV; v++) {
            if (!dxv[v].on)
                continue;
            if (!dx_playing(&dxv[v])) {         /* the carriers are done: the voice is free again */
                dxv[v].on = 0;
                continue;
            }
            dx_compute(&dxv[v], mono, 0);
            lit |= 1u << v;
        }
        for (i = 0; i < DX_N; i++) {
            int32_t s = mono[i] >> OUT_SHIFT;
            /* a ~34 Hz high pass. The feedback truncates towards zero: >> would floor it, and a
             * negative state then feeds itself exactly back and the offset sits there for ever. */
            int32_t fb = dc_y * 32610;          /* |dc_y| <= 32767, so this stays inside an int32 */
            int32_t y = s - dc_x + ((fb + (fb < 0 ? 32767 : 0)) >> 15);
            dc_x = s;
            y = y > 32767 ? 32767 : y < -32767 ? -32767 : y;
            dc_y = y;
            out[2u * (b + i)] += y;
            out[2u * (b + i) + 1u] += y;
        }
    }
    v_lit = lit;

    us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
    if (us * 100u / BUDGET_US > cpu_peak)
        cpu_peak = us * 100u / BUDGET_US;
    cpu_q8 = (cpu_q8 * 15u + us * 256u / BUDGET_US) / 16u;
    cpu_over = (uint8_t)(cpu_over << 1 | (us * 100u > BUDGET_US * 85u));
    if ((cpu_over & 3u) == 3u)                  /* two blocks in a row over 85 %: we are not keeping up */
        v_shed();
}

/* ---- the parameters under the knobs.
 * A page is a flat list; the knobs show four at a time and SEL steps to the next four. `op` entries
 * are an offset inside the selected operator's 21 bytes, `g` entries an index into the 156.
 * Operators are stored OP6 first, so operator n lives at (6 - n) * 21. */
enum { P_G = 0, P_OP = 1 };
enum { K_NUM = 0, K_RATIO, K_DET, K_MODE, K_ONOFF, K_CURVE, K_WAVE, K_ALG, K_BRK, K_TRSP };
typedef struct {
    const char *name;
    uint8_t where, idx, max, kind;
} param_t;

static const param_t PG_OP[] = {
    {"LEVEL",  P_OP, 16, 99, K_NUM},            /* how loud -- or, on a modulator, how much it modulates */
    {"COARSE", P_OP, 18, 31, K_RATIO},
    {"FINE",   P_OP, 19, 99, K_NUM},
    {"DETUNE", P_OP, 20, 14, K_DET},
    {"MODE",   P_OP, 17, 1,  K_MODE},
    {"VEL SENS", P_OP, 15, 7, K_NUM},
    {"AMP MOD",  P_OP, 14, 3, K_NUM},
    {"RATE SCL", P_OP, 13, 7, K_NUM},
    {"BREAK PT", P_OP, 8,  99, K_BRK},
    {"L DEPTH", P_OP, 9,  99, K_NUM},
    {"R DEPTH", P_OP, 10, 99, K_NUM},
    {"L CURVE", P_OP, 11, 3,  K_CURVE},
    {"R CURVE", P_OP, 12, 3,  K_CURVE},
};
static const param_t PG_EG[] = {
    {"RATE 1",  P_OP, 0, 99, K_NUM},
    {"RATE 2",  P_OP, 1, 99, K_NUM},
    {"RATE 3",  P_OP, 2, 99, K_NUM},
    {"RATE 4",  P_OP, 3, 99, K_NUM},
    {"LEVEL 1", P_OP, 4, 99, K_NUM},
    {"LEVEL 2", P_OP, 5, 99, K_NUM},
    {"LEVEL 3", P_OP, 6, 99, K_NUM},            /* where it sits while the key is held */
    {"LEVEL 4", P_OP, 7, 99, K_NUM},            /* where it goes when you let go */
};
static const param_t PG_LFO[] = {
    {"SPEED",   P_G, 137, 99, K_NUM},
    {"DELAY",   P_G, 138, 99, K_NUM},
    {"PITCH MOD", P_G, 139, 99, K_NUM},
    {"AMP MOD", P_G, 140, 99, K_NUM},
    {"WAVE",    P_G, 142, 5,  K_WAVE},
    {"KEY SYNC", P_G, 141, 1, K_ONOFF},
    {"P M SENS", P_G, 143, 7, K_NUM},
};
static const param_t PG_GLO[] = {
    {"ALGORITHM", P_G, 134, 31, K_ALG},
    {"FEEDBACK",  P_G, 135, 7,  K_NUM},
    {"OSC SYNC",  P_G, 136, 1,  K_ONOFF},
    {"TRANSPOSE", P_G, 144, 48, K_TRSP},
    {"P RATE 1", P_G, 126, 99, K_NUM},
    {"P RATE 2", P_G, 127, 99, K_NUM},
    {"P RATE 3", P_G, 128, 99, K_NUM},
    {"P RATE 4", P_G, 129, 99, K_NUM},
    {"P LEVEL 1", P_G, 130, 99, K_NUM},
    {"P LEVEL 2", P_G, 131, 99, K_NUM},
    {"P LEVEL 3", P_G, 132, 99, K_NUM},
    {"P LEVEL 4", P_G, 133, 99, K_NUM},
};

enum { PAGE_HOME = 0, PAGE_OP, PAGE_EG, PAGE_LFO, PAGE_GLO };
static const struct { const char *name; const param_t *p; uint8_t n, perop; } PAGE[] = {
    {"FM LAB",    0,      0,                              0},
    {"OPERATOR",  PG_OP,  sizeof PG_OP / sizeof PG_OP[0],   1},
    {"ENVELOPE",  PG_EG,  sizeof PG_EG / sizeof PG_EG[0],   1},
    {"LFO",       PG_LFO, sizeof PG_LFO / sizeof PG_LFO[0], 0},
    {"GLOBAL",    PG_GLO, sizeof PG_GLO / sizeof PG_GLO[0], 0},
};

static uint8_t page, bank, opsel = 5;           /* opsel indexes the 156 bytes: 0 = OP6 .. 5 = OP1 */

static uint32_t p_at(const param_t *p)          /* where this parameter lives in the 156 */
{
    return p->where == P_OP ? (uint32_t)(5u - opsel) * 21u + p->idx : p->idx;
}

static const char *const WAVE[] = {"TRIANGLE", "SAW DOWN", "SAW UP", "SQUARE", "SINE", "SAMP/HOLD"};
static const char *const CURVE[] = {"-LIN", "-EXP", "+EXP", "+LIN"};
static const char NOTE_NAME[12][3] = {"C ", "C#", "D ", "D#", "E ", "F ", "F#", "G ", "G#", "A ", "A#", "B "};
/* ---- the screen */
#define C_ACC RGB(180, 140, 255)                /* lilac, as the logo */
#define C_KEY RGB(210, 210, 210)
#define C_OFF RGB(90, 90, 90)
#define C_LBL RGB(150, 150, 150)                /* parameter names: present, but not the thing you read */
#define KEY0 53u                                /* key 0 = F3 */
static int black(uint32_t note) { return (0x54Au >> (note % 12u)) & 1u; }

static uint32_t put(char *s, uint32_t i, const char *t) { while (*t) s[i++] = *t++; return i; }
static uint32_t putn(char *s, uint32_t i, uint32_t v, uint32_t w)   /* right-aligned in w, space-padded */
{
    char d[10];
    uint32_t n = 0;
    do { d[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (w-- > n) s[i++] = ' ';
    while (n) s[i++] = d[--n];
    return i;
}

/* a parameter's value as the DX7 means it, not as the byte happens to be stored */
static uint32_t putval(char *s, uint32_t i, const param_t *p, uint32_t v)
{
    switch (p->kind) {
    case K_RATIO:                               /* coarse 0 is half speed, then 1, 2, 3 ... */
        s[i++] = 'x';
        if (!v) return put(s, i, "0.5");
        return putn(s, i, v, 1);
    case K_DET:  s[i++] = v < 7u ? '-' : '+'; return putn(s, i, v < 7u ? 7u - v : v - 7u, 1);
    case K_MODE: return put(s, i, v ? "FIXED" : "RATIO");
    case K_ONOFF: return put(s, i, v ? "ON" : "OFF");
    case K_CURVE: return put(s, i, CURVE[v & 3u]);
    case K_WAVE: return put(s, i, WAVE[v < 6u ? v : 0]);
    case K_ALG:  return putn(s, i, v + 1u, 1);
    case K_BRK:                                 /* 0..99 is A-1 upwards */
        i = put(s, i, NOTE_NAME[(v + 21u) % 12u]);
        return putn(s, i, (v + 21u) / 12u, 1);
    case K_TRSP: s[i++] = v < 24u ? '-' : '+'; return putn(s, i, v < 24u ? 24u - v : v - 24u, 1);
    default: return putn(s, i, v, 1);
    }
}

static void draw_voice(uint32_t vi, uint32_t y)
{
    char s[28];
    uint32_t i = putn(s, 0, vi + 1u, 2);
    s[i++] = ' ';
    i = put(s, i, DX_VOICE_NAME[vi]);
    if (modified) { s[i++] = ' '; s[i++] = '*'; }
    s[i] = 0;
    draw_text_box(0, y, 240, &FONT_S, s, modified ? C_ACC : C_WHITE, 1);
}

static uint32_t carriers(uint32_t alg)          /* bit op: operator at index op feeds the output */
{
    uint32_t op, m = 0;
    for (op = 0; op < 6u; op++)
        if ((DX_ALG[alg & 31u][op] & 3u) == 0u)
            m |= 1u << op;
    return m;
}

/* One parameter row: name on the left, value on the right, and a bar underneath so the amount is
 * visible at a glance. The knob that moves it is KNOB1..4, top to bottom. */
static void draw_row(uint32_t slot, const param_t *p, uint32_t v)
{
    char s[32];
    uint32_t y = 56u + slot * 32u, i;
    cv_begin(240, 30, C_BLACK);
    if (p) {
        uint32_t w = p->max ? (uint32_t)(228u * v) / p->max : 0u;
        cv_rect(6, 24, 228, 4, RGB(45, 45, 45));
        if (w) cv_rect(6, 24, (int32_t)w, 4, C_ACC);
    }
    cv_blit(0, y);
    if (!p) {
        draw_text_box(0, y, 240, &FONT_S, "", C_OFF, 1);
        return;
    }
    i = put(s, 0, p->name);
    s[i] = 0;
    draw_text_box(8, y, 150, &FONT_S, s, C_LBL, 0);
    i = putval(s, 0, p, v);
    s[i] = 0;
    draw_text_box(150, y, 84, &FONT_S, s, C_WHITE, 2);
}

static void draw_head(void)
{
    char s[32];
    uint32_t i = put(s, 0, PAGE[page].name), nb;
    if (PAGE[page].perop) {
        i = put(s, i, "  OP");
        i = putn(s, i, 6u - opsel, 1);
        /* a carrier is heard; a modulator is only felt. Which one an operator is depends on the
         * algorithm, so it has to be read out of the table rather than remembered. */
        i = put(s, i, (carriers(edit[134]) >> opsel & 1u) ? " CAR" : " MOD");
    }
    nb = ((uint32_t)PAGE[page].n + 3u) / 4u;
    if (nb > 1u) {
        i = put(s, i, "  ");
        i = putn(s, i, bank + 1u, 1);
        s[i++] = '/';
        i = putn(s, i, nb, 1);
    }
    s[i] = 0;
    draw_text_box(0, 24, 240, &FONT_S, s, C_ACC, 1);
}

static void draw_keys(uint32_t lit)             /* bit k: key k sounds */
{
    uint32_t k, w = 0;
    cv_begin(240, 56, C_BLACK);
    for (k = 0; k < 27u; k++)                   /* 16 white keys of 14 px from x = 8 */
        if (!black(KEY0 + k)) {
            cv_rect(8 + (int32_t)w * 14, 0, 13, 56, lit >> k & 1u ? C_ACC : C_KEY);
            w++;
        }
    for (k = 0, w = 0; k < 27u; k++)            /* the black ones on the line between two white */
        if (black(KEY0 + k))
            cv_rect(8 + (int32_t)w * 14 - 5, 0, 9, 34, lit >> k & 1u ? C_ACC : C_BLACK);
        else
            w++;
    cv_blit(0, 140);
}


static void draw_status(int32_t oct, uint32_t nv, uint32_t cpu, uint32_t pk)
{
    char s[32];
    uint32_t i = put(s, 0, "OCT");
    s[i++] = oct < 0 ? '-' : '+';
    s[i++] = (char)('0' + (oct < 0 ? -oct : oct));
    i = put(s, i, " VOX");
    i = putn(s, i, nv, 1);
    i = put(s, i, " CPU");
    i = putn(s, i, cpu > 999u ? 999u : cpu, 3);
    s[i++] = '%';
    i = put(s, i, " PK");
    i = putn(s, i, pk > 999u ? 999u : pk, 3);
    s[i++] = '%';
    s[i] = 0;
    draw_text_box(0, 212, 240, &FONT_S, s, pk > 85u ? RGB(255, 110, 110) : C_OFF, 1);
}

/* ---- the main loop */
static void load_voice(uint32_t vi)
{
    memcpy(edit, DX_VOICES[vi], 156);
    dx_sanitize(edit);
    modified = 0;
    patch_publish();
}

static void app_main(void)
{
    struct { int8_t oct; uint8_t voice; } set = {0, 0};
    uint32_t prev = 0, k, t_change = 0, dirty = 0, t_meter = 0, repaint = 1;
    uint32_t lit_drawn = ~0u, nv_drawn = ~0u, cpu_drawn = ~0u, pk_drawn = ~0u;
    int32_t oct_drawn = 99;
    uint8_t held[27] = {0}, shown[4] = {255, 255, 255, 255};
    uint8_t page_drawn = 255, bank_drawn = 255, op_drawn = 255, voice_drawn = 255, mod_drawn = 255;

    settings_load(&set, sizeof set);
    if (set.oct < -2 || set.oct > 2) set.oct = 0;
    if (set.voice >= DX_NVOICES) set.voice = 0;
    load_voice(set.voice);
    memcpy(pbuf[pcur ^ 1u], edit, 156);         /* both halves valid before the ISR ever reads one */

    lcd_fill(0, 0, 240, 240, C_BLACK);

    for (;;) {
        uint32_t keys, down, up, rel, b, lit = 0, nv = 0, e;
        uint32_t base = (uint32_t)(KEY0 + 12 * set.oct);
        int32_t enc;
        uint8_t st, d1, d2;
        platform_poll();

        keys = fm1_in.notes;                    /* bit k: key k (0 = F3 .. 26 = G5) */
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        for (k = 0; k < 27u; k++) {             /* a key stops the note it started: the octave may change */
            if (down >> k & 1u) {
                held[k] = (uint8_t)(base + k);
                ev_push(EV_ON, held[k], 100);
                midi_send(0x90, held[k], 100);
            }
            if (up >> k & 1u) {
                ev_push(EV_OFF, held[k], 0);
                midi_send(0x80, held[k], 0);
            }
        }

        b = fm1_input_edges(&rel);
        if (b & (1u << BTN_OCT_DOWN) && set.oct > -2) { set.oct--; dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_OCT_UP) && set.oct < 2) { set.oct++; dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_FX)) cpu_peak = 0;              /* clear the worst-block mark */
        if (b & (1u << BTN_HOME)) { page = PAGE_HOME; bank = 0; }
        if (b & (1u << BTN_EDIT)) { page = PAGE_OP;  bank = 0; }
        if (b & (1u << BTN_ENV))  { page = PAGE_EG;  bank = 0; }
        if (b & (1u << BTN_LFO))  { page = PAGE_LFO; bank = 0; }
        if (b & (1u << BTN_GLO))  { page = PAGE_GLO; bank = 0; }
        if (b & (1u << BTN_SEL) && PAGE[page].n)           /* the next four parameters */
            bank = (uint8_t)((bank + 1u) % (((uint32_t)PAGE[page].n + 3u) / 4u));
        if (b & (1u << BTN_SAVE)) load_voice(set.voice);   /* back to the factory voice */

        if ((enc = fm1_enc_take(ENC_PRESETS)) != 0) {      /* the voice, under the sounding notes */
            int32_t vi = (int32_t)set.voice + enc;
            while (vi < 0) vi += DX_NVOICES;
            set.voice = (uint8_t)(vi % DX_NVOICES);
            load_voice(set.voice);
            dirty = 1;
            t_change = fm1_ms;
        }
        if ((enc = fm1_enc_take(ENC_SELECT)) != 0) {       /* which operator the OP / ENV pages show */
            int32_t o = (int32_t)opsel - enc;              /* clockwise counts OP1 -> OP6 */
            while (o < 0) o += 6;
            opsel = (uint8_t)(o % 6);
        }
        if ((enc = fm1_enc_take(ENC_ALGORITHM)) != 0) {    /* its own knob, on any page */
            int32_t a = (int32_t)edit[134] + enc;
            edit[134] = (uint8_t)(a < 0 ? 0 : a > 31 ? 31 : a);
            modified = 1;
            patch_publish();
        }
        for (e = 0; e < 4u; e++) {                         /* KNOB1..4: the four parameters shown */
            uint32_t pi = (uint32_t)bank * 4u + e;
            const param_t *p;
            int32_t v;
            if ((enc = fm1_enc_take(ENC_KNOB1 + e)) == 0 || pi >= PAGE[page].n)
                continue;
            p = &PAGE[page].p[pi];
            v = (int32_t)edit[p_at(p)] + enc;
            edit[p_at(p)] = (uint8_t)(v < 0 ? 0 : v > (int32_t)p->max ? (int32_t)p->max : v);
            modified = 1;
            patch_publish();
        }

        while (midi_get(&st, &d1, &d2)) {       /* USB-MIDI in, any channel */
            if ((st & 0xF0u) == 0x90u && d2) ev_push(EV_ON, d1, d2);
            else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u) ev_push(EV_OFF, d1, 0);
            else if ((st & 0xF0u) == 0xB0u && d1 == 123u) ev_push(EV_PANIC, 0, 0);
        }

        base = (uint32_t)(KEY0 + 12 * set.oct);
        for (k = 0; k < NV; k++)                /* what sounds, on the keys of this octave */
            if (v_lit >> k & 1u) {
                nv++;
                if (vst[k].note >= base && vst[k].note < base + 27u)
                    lit |= 1u << (vst[k].note - base);
            }
        for (k = 0; k < 27u; k++)
            fm1_led_key(KEY_LED(k), lit >> k & 1u);
        fm1_led_key(BTN_OCT_DOWN, set.oct < 0);
        fm1_led_key(BTN_OCT_UP, set.oct > 0);
        fm1_led_key(BTN_HOME, page == PAGE_HOME);
        fm1_led_key(BTN_EDIT, page == PAGE_OP);
        fm1_led_key(BTN_ENV, page == PAGE_EG);
        fm1_led_key(BTN_LFO, page == PAGE_LFO);
        fm1_led_key(BTN_GLO, page == PAGE_GLO);

        if (page != page_drawn) {               /* a new page: clear once, then only what changes */
            page_drawn = page;
            bank_drawn = op_drawn = 255;
            lcd_fill(0, 0, 240, 208, C_BLACK);
            if (page == PAGE_HOME)
                draw_text_box(0, 18, 240, &FONT_L, APP_NAME, C_ACC, 1);
            repaint = 1;
        }
        if (page != PAGE_HOME && (bank != bank_drawn || opsel != op_drawn || repaint)) {
            bank_drawn = bank;
            op_drawn = opsel;
            draw_head();
            shown[0] = shown[1] = shown[2] = shown[3] = 255;
        }
        if (set.voice != voice_drawn || modified != mod_drawn || repaint) {
            voice_drawn = (uint8_t)set.voice;
            mod_drawn = modified;
            draw_voice(set.voice, page == PAGE_HOME ? 64u : 188u);
            if (page != PAGE_HOME) { bank_drawn = 255; }   /* the header may name a changed algorithm */
        }
        repaint = 0;

        if (page == PAGE_HOME) {
            if (lit != lit_drawn) { lit_drawn = lit; draw_keys(lit); }
        } else {
            for (e = 0; e < 4u; e++) {
                uint32_t pi = (uint32_t)bank * 4u + e;
                const param_t *p = pi < PAGE[page].n ? &PAGE[page].p[pi] : 0;
                uint8_t v = p ? edit[p_at(p)] : (uint8_t)254u;   /* 254 marks "this row is empty" */
                if (shown[e] != v) {
                    shown[e] = v;
                    draw_row(e, p, p ? v : 0u);
                }
            }
            lit_drawn = ~0u;
        }

        if (fm1_ms - t_meter > 250u) {          /* the meter moves every block: redraw it 4 times a second */
            uint32_t cpu = cpu_q8 * 100u / 256u, pk = cpu_peak;
            t_meter = fm1_ms;
            if (set.oct != oct_drawn || nv != nv_drawn || cpu != cpu_drawn || pk != pk_drawn) {
                oct_drawn = set.oct;
                nv_drawn = nv;
                cpu_drawn = cpu;
                pk_drawn = pk;
                draw_status(set.oct, nv, cpu, pk);
            }
        }

        if (dirty && !v_lit && fm1_ms - t_change > 2000u) {   /* an erase stalls the sound: save when silent */
            settings_save(&set, sizeof set);
            dirty = 0;
        }
    }
}
