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
 *   HOME         the algorithm, drawn, with each operator lit by how hard it is working
 *   EDIT         the selected operator: level, ratio, detune, sensitivities, keyboard scaling
 *   ENV          the selected operator's envelope, drawn to scale, a segment at a time
 *   LFO          speed, delay, pitch and amplitude depth, wave
 *   GLO          algorithm, feedback, key sync, transpose, the pitch envelope
 *   SELECT       which operator (1..6) the EDIT and ENV pages show, and which box HOME outlines
 *   ALGORITHM    the algorithm, on any page
 *   SEL          the next bank on the page (the knobs are KNOB1.., left to right)
 *   SAVE         write this sound into one of your 32 slots: press, pick the slot, press again
 *   FX           clear the PK mark on the load meter
 *
 * Keeping things. PRESETS runs over the 20 built-in voices and then 32 slots of your own in flash.
 * An unused slot holds INIT VOICE -- a single operator at full level on algorithm 1, which is to
 * say a sine wave, and the honest place to start building something from.
 *
 * Patches go in and out in the DX7's own .syx format, so thirty years of other people's voices are
 * a MIDI cable away and reading them is most of how anyone learns this. A 32-voice bulk dump
 * replaces the whole bank; a single-voice dump lands in the editor, under whatever you are playing,
 * and you decide whether to keep it. The host can ask for either back (tools/syx.py).
 *
 * Listening, which is the other half of it. You cannot hear what an operator does by reading its
 * numbers; you hear it by taking it away and putting it back.
 *
 *   ARP          switch the selected operator off, and on again
 *   SEQ          solo it: every other operator off, and again for all six back on
 *   PLAY         the other side of the A/B compare
 *   REC          copy this side onto the other one, so there is something to come back to
 *
 * The switches are not part of the voice -- a DX7's were front-panel switches, not voice data -- so
 * they are applied on the way to the ISR and `edit` never carries them. Soloing a modulator is
 * silent, because a modulator is never heard, only felt: the output bus in the diagram turns red to
 * say so. That is the whole carrier-and-modulator distinction in one button.
 *
 * The two drawings are read out of the engine rather than kept alongside it: the diagram comes from
 * DX_ALG, the table dx_compute_ext obeys, and the envelope from the same arithmetic dx_env_advance
 * and dx_env_sample use. Neither can drift out of agreement with what you are hearing.
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
#include "spectrum.h"

#define NV 8u                                   /* voices (RAM: 428 B each) */

/* ---- the patch. `edit` is the main loop's own copy and the one the knobs change; pbuf is the pair
 * the ISR reads from. A change goes: edit -> the spare pbuf -> publish -> flip -> tell the ISR. The
 * ISR never sees a half-written patch, and nothing large is ever written under it. */
static uint8_t edit[156];
static uint8_t pbuf[2][156];
static volatile uint8_t pcur;                   /* the ISR reads pbuf[pcur] */
static uint8_t modified;                        /* the voice differs from the factory one */

/* The operator switches: bit op (0 = OP6 .. 5 = OP1) means that operator is switched off. They are
 * a way of listening, not part of the sound -- a DX7's were front-panel switches, not voice data --
 * so they live outside `edit`, are not saved, and survive an A/B switch. */
static uint8_t opmute;

/* A/B: two patches, one of them in `edit` and the other stashed here, with the switch swapping
 * them. Land on a sound, copy it across, change one thing, and flip back and forth: hearing the
 * same parameter two ways in succession is how you find out what it does. */
static uint8_t other[156];
static uint8_t ab;                              /* which side `edit` is: 0 = A, 1 = B */
static uint8_t other_mod, other_voice;

/* ---- the bank: 32 slots of your own in flash, kept in the DX7's packed 128-byte form, which is
 * exactly what a .syx carries. The bank in RAM, the bank in flash and a bulk dump on the wire are
 * then all the same 4096 bytes, and bankmsg holds them with the SysEx wrapper already around them,
 * so a dump out costs no copy and no second buffer.
 *
 * An unwritten slot holds INIT VOICE rather than being marked empty, which is both less state and
 * truer to the instrument: a slot always holds a voice, and until you put something in it that
 * voice is a sine wave.
 *
 * The voice knob runs over the 20 built-in voices and then your 32: 0..19 factory, 20..51 slots. */
#define NUSER 32u
#define NVOICE (DX_NVOICES + NUSER)

static uint8_t bankmsg[6u + NUSER * 128u + 2u];   /* F0 43 00 09 20 00 <4096> sum F7 */
static uint8_t *const bankv = bankmsg + 6u;       /* the 32 packed voices inside it */

/* what the voice line says instead of the voice, while saving or just after something arrived */
static uint8_t saving, save_slot;
static const char *flash_msg;
static uint32_t flash_at, msg_gen;

static void say(const char *m)                  /* a word on the voice line for a second and a half */
{
    flash_msg = m;
    flash_at = fm1_ms;
    msg_gen++;
}

static void bank_init(void)
{
    uint8_t u[156];
    uint32_t i;
    bankmsg[0] = 0xF0; bankmsg[1] = 0x43; bankmsg[2] = 0x00;
    bankmsg[3] = 0x09; bankmsg[4] = 0x20; bankmsg[5] = 0x00;   /* 32-voice bulk dump, 4096 bytes */
    bankmsg[sizeof bankmsg - 1u] = 0xF7;
    if (bank_read(bankv, NUSER * 128u) == 0)
        return;
    dx_init_voice(u);                           /* no bank yet, or a cut write: 32 sine waves */
    for (i = 0; i < NUSER; i++)
        dx_pack(u, bankv + i * 128u);
}

static int bank_store(uint32_t slot, const uint8_t *u)   /* 0: written */
{
    dx_pack(u, bankv + slot * 128u);
    return bank_write(bankv, NUSER * 128u);
}

/* the ten characters a voice calls itself, printable and without the spaces a DX7 pads with */
static void voice_name(uint32_t vi, char *out)
{
    uint32_t i, n = 0;
    if (vi < DX_NVOICES) {
        const char *s = DX_VOICE_NAME[vi];
        while (n < 10u && s[n]) { out[n] = s[n]; n++; }
    } else {
        const uint8_t *b = bankv + (vi - DX_NVOICES) * 128u + 118u;
        for (i = 0; i < 10u; i++)
            out[i] = b[i] >= 32u && b[i] < 127u ? (char)b[i] : ' ';
        n = 10u;
    }
    while (n && out[n - 1u] == ' ')
        n--;
    out[n] = 0;
}

/* ---- naming a sound
 *
 * A sound you saved under someone else's name is a sound you will not find again, so SAVE asks for
 * a name as well as a slot. The two happen at once rather than one after the other: PRESETS picks
 * the slot, SELECT picks the letter you are on, ALGORITHM picks what that letter is, and SAVE
 * writes. Nothing is a new page and nothing is an extra press.
 *
 * The DX7 allows any printable character in a name; this knob offers a smaller alphabet, because
 * forty-one detents end to end is a name you can type and ninety-five is not. Space comes first,
 * so one detent back from A is a blank -- which is how a name shorter than ten gets its tail.
 *
 * The name being typed lives here and not in `edit`, because any button other than SAVE backs out
 * of the save, and backing out must leave the voice as it was. It is copied into the patch at the
 * moment the slot is written, so the voice line then agrees with the slot. */
static const char NAME_CH[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.#+";
#define NAME_NCH (sizeof NAME_CH - 1u)

static char save_name[11];
static uint8_t save_cur;

static uint32_t name_ix(char c)                 /* where a character sits on the knob; else a space */
{
    uint32_t i;
    for (i = 0; i < NAME_NCH; i++)
        if (NAME_CH[i] == c)
            return i;
    return 0;
}

static void name_begin(void)                    /* the name to edit is the one the patch already has */
{
    uint32_t i;
    for (i = 0; i < 10u; i++) {
        uint8_t c = edit[145 + i];
        save_name[i] = NAME_CH[name_ix(c >= 32u && c < 127u ? (char)c : ' ')];
    }
    save_name[10] = 0;
    save_cur = 0;
}

static int slot_free(uint32_t slot)             /* nothing of yours in it yet: still the INIT VOICE */
{
    static const char INIT[10] = {'I', 'N', 'I', 'T', ' ', 'V', 'O', 'I', 'C', 'E'};
    const uint8_t *b = bankv + slot * 128u + 118u;
    uint32_t i;
    for (i = 0; i < 10u; i++)
        if (b[i] != (uint8_t)INIT[i])
            return 0;
    return 1;
}

static uint32_t name_sum(void)                  /* enough of the name to notice a letter changing */
{
    uint32_t i, s = 0;
    for (i = 0; i < 10u; i++)
        s += (uint32_t)save_name[i] * (i + 1u);
    return s & 127u;
}

/* ---- main loop -> audio ISR: a single-producer ring of note events */
enum { EV_ON = 1, EV_OFF, EV_PATCH, EV_MONO, EV_PANIC };
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
    uint8_t *d = pbuf[pcur ^ 1u];
    uint32_t op;
    memcpy(d, edit, 156);
    /* The switches are applied on the way out, so `edit` -- what is drawn, compared and reverted --
     * never carries them. An operator goes off by having its output level taken to zero, which is
     * what the DX7's switch did. Its keyboard level scaling goes with it: a + curve adds to the
     * output level before the level is used, so leaving it in would let a "switched off" operator
     * creep back at the top or bottom of the keyboard. */
    for (op = 0; op < 6u; op++)
        if (opmute >> op & 1u) {
            d[op * 21u + 16u] = 0;              /* output level */
            d[op * 21u + 9u] = 0;               /* L depth */
            d[op * 21u + 10u] = 0;              /* R depth */
        }
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
static volatile uint8_t v_last;                 /* the newest note: the one the displays follow */

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
    v_last = (uint8_t)pick;                     /* the envelope and the diagram follow the newest note */
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

/* ---- mono mode (FM Lab's; the DX7 has nothing to say about it, which is why the switch lives in
 * settings and not in the voice -- a bank saved here is still a true DX7 dump).
 *
 * One voice, last-note priority, with fall-back: the newest key down is the one you hear, and
 * letting it go drops you back to the key still under your finger rather than to silence. The
 * envelopes start again only when nothing was already held, so a slur keeps the attack it began
 * with and a separated note gets its own. On an FM patch that is a bigger difference than it sounds
 * -- the attack is where most of the bright modulation lives, so legato playing is a different
 * timbre, not just a different articulation. There is no portamento: the pitch steps. */
#define MSTK 16                                 /* more fingers than this and the oldest is forgotten */
static volatile uint8_t monomode;               /* the ISR's; the main loop asks for it with EV_MONO */
static uint8_t mnote[MSTK], mvel[MSTK], mheld;  /* the keys down, oldest first; the top is what sounds */

static uint32_t m_drop(uint32_t note)           /* take a key off the stack; 1 if it was there */
{
    uint32_t i;
    for (i = 0; i < mheld; i++)
        if (mnote[i] == note)
            break;
    if (i >= mheld)
        return 0;
    for (; i + 1u < mheld; i++) { mnote[i] = mnote[i + 1u]; mvel[i] = mvel[i + 1u]; }
    mheld--;
    return 1;
}

static void m_follow(uint32_t note, uint32_t vel, int again)
{
    vst[0].note = (uint8_t)note;
    vst[0].vel = (uint8_t)vel;
    vst[0].age = ++ages;
    if (again)
        dx_init(&dxv[0], pbuf[pcur], tnote(note), (int)vel);
    else
        dx_retune(&dxv[0], pbuf[pcur], tnote(note), (int)vel);
    v_last = 0;
}

static void m_on(uint32_t note, uint32_t vel)
{
    uint32_t i, slur = mheld && dx_playing(&dxv[0]);
    m_drop(note);                               /* the same key twice: it is only ever on once */
    if (mheld == MSTK) {
        for (i = 0; i + 1u < MSTK; i++) { mnote[i] = mnote[i + 1u]; mvel[i] = mvel[i + 1u]; }
        mheld--;
    }
    mnote[mheld] = (uint8_t)note;
    mvel[mheld] = (uint8_t)vel;
    mheld++;
    vst[0].held = 1;
    m_follow(note, vel, !slur);
}

static void m_off(uint32_t note)
{
    if (!m_drop(note))
        return;
    if (!mheld) {
        vst[0].held = 0;
        dx_keyup(&dxv[0]);
        return;
    }
    m_follow(mnote[mheld - 1u], mvel[mheld - 1u], 0);
}

static void ev_drain(void)                      /* audio ISR, before the render */
{
    while (ev_r != ev_w) {
        const ev_t *e = &evq[ev_r % EVQ];
        uint32_t v;
        switch (e->kind) {
        case EV_ON:  if (monomode) m_on(e->note, e->vel); else v_on(e->note, e->vel); break;
        case EV_OFF: if (monomode) m_off(e->note); else v_off(e->note); break;
        case EV_PATCH:                          /* the voice changed under the sounding notes */
            for (v = 0; v < NV; v++)
                if (dxv[v].on)
                    dx_update(&dxv[v], pbuf[pcur], tnote(vst[v].note), (int)vst[v].vel);
            break;
        case EV_MONO:                           /* the switch itself: start the new mode from silence */
            monomode = (uint8_t)e->note;
            for (v = 0; v < NV; v++) { dxv[v].on = 0; vst[v].held = 0; }
            mheld = 0;
            break;
        case EV_PANIC:
            for (v = 0; v < NV; v++) { dxv[v].on = 0; vst[v].held = 0; }
            mheld = 0;
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
            if (sp_want)
                sp_feed(y);                     /* the analyser's tap, on the finished sample */
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
    const char *knob;                           /* six characters for the legend, or 0 for name */
    uint8_t where, idx, max, kind;
} param_t;

static const param_t PG_OP[] = {
    {"LEVEL",  0, P_OP, 16, 99, K_NUM},         /* how loud -- or, on a modulator, how much it modulates */
    {"COARSE", 0, P_OP, 18, 31, K_RATIO},
    {"FINE",   0, P_OP, 19, 99, K_NUM},
    {"DETUNE", 0, P_OP, 20, 14, K_DET},
    {"MODE",   0, P_OP, 17, 1,  K_MODE},
    {"VEL SENS", "V SENS", P_OP, 15, 7, K_NUM},
    {"AMP MOD",  "AMPMOD", P_OP, 14, 3, K_NUM},
    {"RATE SCL", "RT SCL", P_OP, 13, 7, K_NUM},
    {"BREAK PT", "BRK PT", P_OP, 8,  99, K_BRK},
    {"L DEPTH", "L DEP", P_OP, 9,  99, K_NUM},
    {"R DEPTH", "R DEP", P_OP, 10, 99, K_NUM},
    {"L CURVE", "L CRV", P_OP, 11, 3,  K_CURVE},
    {"R CURVE", "R CRV", P_OP, 12, 3,  K_CURVE},
};
static const param_t PG_EG[] = {                /* a rate and the level it is heading for, paired */
    {"RATE 1",  0, P_OP, 0, 99, K_NUM},
    {"LEVEL 1", "LVL 1", P_OP, 4, 99, K_NUM},
    {"RATE 2",  0, P_OP, 1, 99, K_NUM},
    {"LEVEL 2", "LVL 2", P_OP, 5, 99, K_NUM},
    {"RATE 3",  0, P_OP, 2, 99, K_NUM},
    {"LEVEL 3", "LVL 3", P_OP, 6, 99, K_NUM},         /* where it sits while the key is held */
    {"RATE 4",  0, P_OP, 3, 99, K_NUM},
    {"LEVEL 4", "LVL 4", P_OP, 7, 99, K_NUM},         /* where it goes when you let go */
};
static const param_t PG_LFO[] = {
    {"SPEED",   0, P_G, 137, 99, K_NUM},
    {"DELAY",   0, P_G, 138, 99, K_NUM},
    {"PITCH MOD", "P MOD", P_G, 139, 99, K_NUM},
    {"AMP MOD", "AMPMOD", P_G, 140, 99, K_NUM},
    {"WAVE",    0, P_G, 142, 5,  K_WAVE},
    {"KEY SYNC", "K SYNC", P_G, 141, 1, K_ONOFF},
    {"P M SENS", "PMSENS", P_G, 143, 7, K_NUM},
};
static const param_t PG_GLO[] = {
    {"ALGORITHM", "ALGO", P_G, 134, 31, K_ALG},
    {"FEEDBACK",  "FEEDBK", P_G, 135, 7,  K_NUM},
    {"OSC SYNC",  "O SYNC", P_G, 136, 1,  K_ONOFF},
    {"TRANSPOSE", "TRANSP", P_G, 144, 48, K_TRSP},
    {"P RATE 1", "PRATE1", P_G, 126, 99, K_NUM},
    {"P RATE 2", "PRATE2", P_G, 127, 99, K_NUM},
    {"P RATE 3", "PRATE3", P_G, 128, 99, K_NUM},
    {"P RATE 4", "PRATE4", P_G, 129, 99, K_NUM},
    {"P LEVEL 1", "P LVL1", P_G, 130, 99, K_NUM},
    {"P LEVEL 2", "P LVL2", P_G, 131, 99, K_NUM},
    {"P LEVEL 3", "P LVL3", P_G, 132, 99, K_NUM},
    {"P LEVEL 4", "P LVL4", P_G, 133, 99, K_NUM},
};

enum { PAGE_HOME = 0, PAGE_OP, PAGE_EG, PAGE_LFO, PAGE_GLO };
/* rows: how many parameters a bank shows at once, and y0 where the first one sits. The envelope
 * page shows only two, because the graph above them is worth more than two more numbers -- and a
 * rate with the level it is heading for is the pair that actually means something together. */
static const struct { const char *name; const param_t *p; uint8_t n, perop, rows, y0; } PAGE[] = {
    {"FM LAB",    0,      0,                                0, 4, 56},
    {"OPERATOR",  PG_OP,  sizeof PG_OP / sizeof PG_OP[0],   1, 4, 56},
    {"ENVELOPE",  PG_EG,  sizeof PG_EG / sizeof PG_EG[0],   1, 2, 124},
    {"LFO",       PG_LFO, sizeof PG_LFO / sizeof PG_LFO[0], 0, 4, 56},
    {"GLOBAL",    PG_GLO, sizeof PG_GLO / sizeof PG_GLO[0], 0, 4, 56},
};
#define PAGE_ROWS ((uint32_t)PAGE[page].rows)
#define PAGE_NBANK (((uint32_t)PAGE[page].n + PAGE_ROWS - 1u) / PAGE_ROWS)
#define HOME_NBANK 2u                           /* HOME's two screens: the diagram, and the spectrum */

static uint8_t page, bank, opsel = 5;           /* opsel is the engine's operator index: 0 = OP6 .. 5 = OP1 */

/* Go to a page; pressing the button for the page you are already on walks its banks and wraps. */
static void page_to(uint8_t p)
{
    if (page != p) {
        page = p;
        bank = 0;
    } else if (page == PAGE_HOME) {
        bank = (uint8_t)((bank + 1u) % HOME_NBANK);
    } else if (PAGE[page].n) {
        bank = (uint8_t)((bank + 1u) % PAGE_NBANK);
    }
}

/* The panel prints OP1..OP6 under the low six black keys, and the stock firmware uses them that
 * way. On the two pages whose whole subject is one operator -- OPERATOR and ENVELOPE -- so do we:
 * the key picks the operator instead of sounding, because there you are pointing at one rather
 * than playing it. On every other page they are ordinary keys. Returns the engine's operator
 * index (0 = OP6 .. 5 = OP1, as opsel counts) or -1 for "this is a note". */
static const uint8_t OP_KEY[6] = {1, 3, 5, 8, 10, 13};   /* F#3 G#3 A#3 C#4 D#4 F#4 = OP1..OP6 */
static int op_key(uint32_t k)
{
    uint32_t i;
    if (page != PAGE_OP && page != PAGE_EG)
        return -1;
    for (i = 0; i < 6u; i++)
        if (OP_KEY[i] == k)
            return (int)(5u - i);
    return -1;
}

/* The two black keys at the top of the keyboard, which the panel prints MONO and POLY under. They
 * do the same on OPERATOR, where you are already deciding how the voice should behave. Mono is a
 * setting of the instrument and not of the voice -- the DX7 format has no room for it, and we would
 * rather a bank saved here stay a true DX7 dump than smuggle a byte into one. */
#define MONO_KEY 20u                            /* C#5 */
#define POLY_KEY 22u                            /* D#5 */

/* The 156 bytes hold the operators in the DX7's own order, OP6 first, and dx7_core.c indexes
 * DX_ALG with the same number -- so the block index and the engine's operator index are the one
 * thing, and opsel is both. (Proven by muting each block in turn under algorithm 16, which has a
 * single carrier: block 5 is the one that silences it, and carriers() puts that carrier at index 5.) */
static uint32_t p_at(const param_t *p)          /* where this parameter lives in the 156 */
{
    return p->where == P_OP ? (uint32_t)opsel * 21u + p->idx : p->idx;
}

static const char *const WAVE[] = {"TRIANGLE", "SAW DOWN", "SAW UP", "SQUARE", "SINE", "SAMP/HOLD"};
static const char *const CURVE[] = {"-LIN", "-EXP", "+EXP", "+LIN"};
static const char NOTE_NAME[12][3] = {"C ", "C#", "D ", "D#", "E ", "F ", "F#", "G ", "G#", "A ", "A#", "B "};
/* ---- the screen */
#define C_ACC RGB(180, 140, 255)                /* lilac, as the logo */
#define C_OFF RGB(90, 90, 90)
#define C_LBL RGB(150, 150, 150)                /* parameter names: present, but not the thing you read */
#define KEY0 53u                                /* key 0 = F3 */

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
    char s[32], nm[11];
    uint32_t i = 0;
    if (flash_msg) {                            /* something arrived, or was written: say so here */
        draw_text_box(0, y, 240, &FONT_S, flash_msg, C_ACC, 1);
        return;
    }
    if (saving) {                               /* the slot to write to, and the name to write */
        int32_t x, cw;
        uint32_t k;
        i = put(s, i, "SAVE U");
        s[i++] = (char)('0' + (save_slot + 1u) / 10u);   /* zero-padded: the line must not shift
                                                          * sideways as the slot crosses ten */
        s[i++] = (char)('0' + (save_slot + 1u) % 10u);
        s[i++] = ' ';
        s[i] = 0;
        cw = FONT_S.adv[glyph(&FONT_S, ' ')];   /* the font is fixed width: one cell per character */
        cv_begin(240, FONT_S.h, C_BLACK);
        x = ((int32_t)240 - (text_w(&FONT_S, s) + 10 * cw)) / 2;
        /* the slot number is accented when that slot already holds a sound of yours, so the one
         * thing you cannot undo -- writing over something -- is visible without costing a word */
        x = cv_text(x, 0, &FONT_S, s, slot_free(save_slot) ? C_LBL : C_ACC);
        for (k = 0; k < 10u; k++) {
            char c1[2];
            c1[0] = save_name[k]; c1[1] = 0;
            if (k == save_cur) {                /* the letter the knob is on: accented, and ruled
                                                 * underneath so that it shows on a blank too */
                cv_rect(x, (int32_t)FONT_S.h - 2, cw, 2, C_ACC);
                cv_text(x, 0, &FONT_S, c1, C_ACC);
            } else
                cv_text(x, 0, &FONT_S, c1, C_WHITE);
            x += cw;
        }
        cv_blit(0, y);
        return;
    }
    s[i++] = (char)('A' + ab);                  /* which side of the compare you are listening to */
    s[i++] = ' ';
    if (vi < DX_NVOICES) {
        s[i++] = ' ';
        i = putn(s, i, vi + 1u, 2);
    } else {
        s[i++] = 'U';                           /* one of yours */
        i = putn(s, i, vi - DX_NVOICES + 1u, 2);
    }
    s[i++] = ' ';
    voice_name(vi, nm);
    i = put(s, i, nm);
    if (modified) { s[i++] = ' '; s[i++] = '*'; }
    s[i] = 0;
    draw_text_box(0, y, 240, &FONT_S, s, modified ? C_ACC : C_WHITE, 1);
}

/* ---- the algorithm, drawn from the engine's own routing table
 *
 * DX_ALG[alg][op] is what dx_compute_ext actually obeys, so a diagram derived from it cannot
 * disagree with what you hear. Per operator: bits 0-1 the bus it writes (0 = the output, so a
 * carrier), bit 2 add rather than replace, bits 4-5 the bus it reads, bit 6 feedback in, bit 7
 * feedback out. Operators run in index order, which is OP6 first.
 *
 * So the modulators of an operator are whichever operators wrote its input bus since that bus was
 * last overwritten. A bus is not cleared when it is read -- dx_compute_ext leaves it in place --
 * which is how one operator comes to modulate several carriers, as in algorithms 19 to 25. */
static uint32_t carriers(uint32_t alg)          /* bit op: operator at index op feeds the output */
{
    uint32_t op, m = 0;
    for (op = 0; op < 6u; op++)
        if ((DX_ALG[alg & 31u][op] & 3u) == 0u)
            m |= 1u << op;
    return m;
}

#define ALG_UNSET 255u
static uint8_t a_mods[6];                       /* bit m: operator m modulates this one */
static uint8_t a_row[6], a_col[6];              /* 0 = the bottom row, where the carriers are */
static uint8_t a_ncol, a_nrow, a_fb;            /* a_fb: bit op, operator op is in the feedback loop */

static void alg_mods(uint32_t alg)
{
    uint8_t pend[3] = {0, 0, 0};                /* who has written bus 1 and 2 and not been read yet */
    uint32_t op;
    a_fb = 0;
    for (op = 0; op < 6u; op++) {
        uint32_t fl = DX_ALG[alg & 31u][op], inb = (fl >> 4) & 3u, outb = fl & 3u;
        a_mods[op] = inb ? pend[inb] : 0u;
        if (outb) pend[outb] = (uint8_t)((fl & 4u) ? pend[outb] | (1u << op) : (1u << op));
        if (fl & 0xC0u) a_fb |= (uint8_t)(1u << op);
    }
}

/* Place the tree the way the DX7 manual draws it: carriers along the bottom, a modulator one row
 * above the highest thing it feeds, and a shared modulator centred over everything it feeds.
 * Walked with an explicit stack -- the audio ISR borrows this stack, and RULES.md says not to
 * spend it. Checked against the printed algorithm chart for all 32. */
static void alg_layout(uint32_t alg)
{
    uint8_t stack[8], took[8];
    uint32_t sp, op, k, col = 0, maxcol = 0, maxrow = 0;
    alg_mods(alg);
    for (op = 0; op < 6u; op++) { a_row[op] = (uint8_t)ALG_UNSET; a_col[op] = (uint8_t)ALG_UNSET; }

    /* Rows. a_mods[op] only ever names operators of lower index, because the bus has to be written
     * before it is read and the engine runs the operators in index order. So one descending pass
     * settles every row: by the time it reaches an operator, everything it feeds is already done. */
    for (op = 0; op < 6u; op++)
        if ((DX_ALG[alg & 31u][op] & 3u) == 0u) a_row[op] = 0;
    for (op = 6u; op-- > 0u; ) {
        if (a_row[op] == (uint8_t)ALG_UNSET) continue;
        for (k = 0; k < op; k++)
            if ((a_mods[op] >> k & 1u) &&
                (a_row[k] == (uint8_t)ALG_UNSET || a_row[k] < a_row[op] + 1u))
                a_row[k] = (uint8_t)(a_row[op] + 1u);
    }

    /* Columns, depth first from each carrier in DX7 order. An operator whose modulators this walk
     * had to place sits centred over them; one that brought nothing new takes the next free
     * column. That is what keeps the carriers of algorithm 24 side by side instead of stacked. */
    for (op = 6u; op-- > 0u; ) {
        if (a_row[op] != 0u || a_col[op] != (uint8_t)ALG_UNSET) continue;
        sp = 0; stack[0] = (uint8_t)op; took[0] = 0;
        for (;;) {
            uint32_t cur = stack[sp], kid = 6u;
            for (k = 6u; k-- > 0u; )            /* modulators in display order, OP1 first */
                if ((a_mods[cur] >> k & 1u) && a_col[k] == (uint8_t)ALG_UNSET) { kid = k; break; }
            if (kid < 6u && sp < 7u) {
                took[sp] = 1; stack[++sp] = (uint8_t)kid; took[sp] = 0; continue;
            }
            if (took[sp]) {
                uint32_t lo = 255u, hi = 0;
                for (k = 0; k < 6u; k++)
                    if ((a_mods[cur] >> k & 1u) && a_col[k] != (uint8_t)ALG_UNSET) {
                        if (a_col[k] < lo) lo = a_col[k];
                        if (a_col[k] > hi) hi = a_col[k];
                    }
                a_col[cur] = (uint8_t)((lo + hi) / 2u);
            } else {
                a_col[cur] = (uint8_t)col++;
            }
            if (!sp) break;
            sp--;
        }
    }
    for (op = 0; op < 6u; op++)                 /* an operator nothing reaches still has to go somewhere */
        if (a_col[op] == (uint8_t)ALG_UNSET) { a_row[op] = 0; a_col[op] = (uint8_t)col++; }

    /* A modulator feeding more than one operator gets centred over all of them. Done bottom up, so
     * the operators it feeds have their final columns by the time it is moved. */
    for (k = 1u; k < 6u; k++)
        for (op = 0; op < 6u; op++) {
            uint32_t n = 0, lo = 255u, hi = 0, c;
            if (a_row[op] != k) continue;
            for (c = 0; c < 6u; c++)
                if (a_mods[c] >> op & 1u) {
                    n++;
                    if (a_col[c] < lo) lo = a_col[c];
                    if (a_col[c] > hi) hi = a_col[c];
                }
            if (n > 1u) a_col[op] = (uint8_t)((lo + hi) / 2u);
        }

    for (op = 0; op < 6u; op++) {
        if (a_col[op] > maxcol) maxcol = a_col[op];
        if (a_row[op] > maxrow) maxrow = a_row[op];
    }
    a_ncol = (uint8_t)(maxcol + 1u);
    a_nrow = (uint8_t)(maxrow + 1u);
}

/* where the diagram lives, and how big a box is */
#define ALG_Y 64
#define ALG_H 136
/* cv_begin clamps a canvas that will not fit instead of failing, which shows up as a stale strip at
 * the bottom of the screen rather than as an error. Catch the mismatch at compile time instead. */
typedef char fmlab_canvas_fits[(240u * ALG_H <= CV_MAX) ? 1 : -1];
static int32_t a_bw, a_bh, a_x0, a_dx, a_dy, a_ybase;

static void alg_metrics(void)
{
    int32_t used;
    a_dx = 236 / (int32_t)a_ncol;
    a_bw = a_dx - 6;
    if (a_bw > 40) a_bw = 40;
    if (a_bw < 14) a_bw = 14;
    a_dy = (ALG_H - 10) / (int32_t)a_nrow;
    if (a_dy > 44) a_dy = 44;                   /* two rows should not sprawl over the whole screen */
    a_bh = a_dy - 6;
    if (a_bh > 26) a_bh = 26;
    if (a_bh < 10) a_bh = 10;
    a_x0 = (240 - a_dx * (int32_t)a_ncol) / 2;
    used = a_dy * (int32_t)a_nrow;              /* centre the stack, output bus and all */
    a_ybase = ALG_H - 6 - (ALG_H - 10 - used) / 2;
}
static int32_t a_bx(uint32_t op) { return a_x0 + a_dx * (int32_t)a_col[op] + (a_dx - a_bw) / 2; }
static int32_t a_by(uint32_t op) { return a_ybase - a_dy * ((int32_t)a_row[op] + 1) + (a_dy - a_bh) / 2; }

/* How loud an operator is right now, 0..255. level_in is what the engine feeds to exp2 to get the
 * operator's gain, so it is already logarithmic -- which is the useful thing to see, and is how a
 * DX7 envelope is specified in the first place. A 32-bit aligned read can race the ISR; the worst
 * that does is draw one frame from a value a block old. */
static uint32_t op_now(uint32_t op)
{
    uint32_t v = v_last;
    int32_t l;
    if (v >= NV || !dxv[v].on || !(v_lit >> v & 1u))
        return 0;
    l = dxv[v].level_in[op];
    if (l <= 0) return 0;
    l >>= 16;                                   /* 0 .. 16 << 8 */
    return l >= 4080 ? 255u : (uint32_t)l * 255u / 4080u;
}

static uint16_t op_shade(uint32_t op, uint32_t lev)
{
    uint32_t t = 40u + lev * 215u / 255u;       /* never black: an idle operator is still there */
    if (!(a_fb >> op & 1u))
        return RGB((t * 180u) / 255u, (t * 140u) / 255u, t);
    return RGB(t, (t * 150u) / 255u, (t * 90u) / 255u);   /* feedback: warmer, so it stands out */
}

static void alg_box(int32_t x, int32_t y, uint32_t op, int carrier, int sel)
{
    uint32_t off = (opmute >> op) & 1u;
    uint16_t c = off ? RGB(70, 70, 70) : op_shade(op, op_now(op));
    char s[2];
    if (carrier) {                              /* filled: you hear this one */
        cv_rect(x, y, a_bw, a_bh, c);
    } else {
        cv_rect(x, y, a_bw, 2, c);
        cv_rect(x, y + a_bh - 2, a_bw, 2, c);
        cv_rect(x, y, 2, a_bh, c);
        cv_rect(x + a_bw - 2, y, 2, a_bh, c);
    }
    if (sel) {                                  /* the operator the OP and ENV pages are editing */
        cv_rect(x - 3, y - 3, a_bw + 6, 1, C_WHITE);
        cv_rect(x - 3, y + a_bh + 2, a_bw + 6, 1, C_WHITE);
        cv_rect(x - 3, y - 3, 1, a_bh + 6, C_WHITE);
        cv_rect(x + a_bw + 2, y - 3, 1, a_bh + 6, C_WHITE);
    }
    s[0] = (char)('0' + (6u - op));
    s[1] = 0;
    if (a_bh >= 14)
        cv_text(x + (a_bw - 5) / 2, y + (a_bh - 13) / 2, &FONT_S, s, carrier ? C_BLACK : c);
    if (off)                                    /* struck through: switched off */
        cv_line(x + 1, y + a_bh - 2, x + a_bw - 2, y + 1, RGB(190, 85, 85));
}

static void draw_alg(void)
{
    uint32_t op, m, k, cs;
    alg_layout(edit[134]);
    alg_metrics();
    cs = carriers(edit[134]);
    cv_begin(240, ALG_H, C_BLACK);
    for (op = 0; op < 6u; op++) {               /* the wires first, so the boxes sit on top of them */
        int32_t cx = a_bx(op) + a_bw / 2, ty = a_by(op) + a_bh;
        m = a_mods[op];
        for (k = 0; k < 6u; k++) {
            /* a wire out of a switched-off operator carries nothing: draw it as nearly gone, so
             * the picture shows what is reaching the output and what is not */
            uint16_t wc = (opmute >> k & 1u) ? RGB(38, 38, 38) : C_OFF;
            if (!(m >> k & 1u)) continue;
            cv_line(a_bx(k) + a_bw / 2, a_by(k) + a_bh, a_bx(k) + a_bw / 2, ty + 3, wc);
            cv_line(a_bx(k) + a_bw / 2, ty + 3, cx, ty + 3, wc);
        }
        if (m) cv_line(cx, ty + 3, cx, ty, C_OFF);
        if (a_fb >> op & 1u) {                  /* the feedback loop, round the right-hand side */
            int32_t x = a_bx(op) + a_bw, y = a_by(op);
            uint16_t fc = (opmute >> op & 1u) ? RGB(60, 45, 35) : RGB(200, 150, 90);
            cv_line(x, y + 3, x + 4, y + 3, fc);
            cv_line(x + 4, y + 3, x + 4, y + a_bh - 3, fc);
            cv_line(x + 4, y + a_bh - 3, x, y + a_bh - 3, fc);
        }
    }
    for (op = 0; op < 6u; op++)                 /* a carrier reaches the output: show where it goes */
        if (cs >> op & 1u)
            cv_line(a_bx(op) + a_bw / 2, a_by(op) + a_bh, a_bx(op) + a_bw / 2, a_ybase + 3,
                    (opmute >> op & 1u) ? RGB(38, 38, 38) : C_OFF);
    /* The output bus goes red when every carrier is switched off. Solo a modulator and you hear
     * nothing -- which is the point, because a modulator is never heard, only felt -- and this is
     * the line that says so rather than leaving you wondering what broke. */
    cv_rect(a_x0, a_ybase + 3, a_dx * (int32_t)a_ncol, 2, (cs & ~(uint32_t)opmute) ? C_OFF : RGB(130, 50, 50));
    for (op = 0; op < 6u; op++)
        alg_box(a_bx(op), a_by(op), op, (cs >> op & 1u) != 0, op == opsel);
    cv_blit(0, ALG_Y);
}

/* Only the boxes, for the live levels: redrawing the whole diagram several times a second would
 * cost more of the CPU budget than the thing it is drawing. */
static void draw_alg_levels(void)
{
    uint32_t op, cs = carriers(edit[134]);
    for (op = 0; op < 6u; op++) {
        int32_t x = a_bx(op) - 3, y = a_by(op) - 3;
        cv_begin((uint32_t)(a_bw + 6), (uint32_t)(a_bh + 6), C_BLACK);
        alg_box(3, 3, op, (cs >> op & 1u) != 0, op == opsel);
        cv_blit((uint32_t)x, (uint32_t)(ALG_Y + y));
    }
}

/* ---- the spectrum, the other half of HOME
 *
 * What the instrument is putting out, 1024 points of it, on a linear frequency axis. Linear is the
 * whole point: FM builds its harmonics as sidebands at the carrier plus and minus whole multiples
 * of the modulator, so on a linear axis they are a row of evenly spaced lines, and raising a
 * modulator's LEVEL makes that row visibly spread outward. That is the one picture worth having.
 *
 * It analyses the output and not a single operator, which sounds like the lesser thing but is not:
 * ARP and SEQ already switch an operator off and solo one, so soloing a carrier gives you exactly
 * that branch of the algorithm, with nothing here having to make a second claim about what the
 * engine is doing.
 *
 * The faint lines are whole multiples of the note being played. Partials that sit on them are
 * harmonic and partials that sit between them are not, which is the difference between a COARSE of
 * 2 and a COARSE of 2 with DETUNE, or between a ratio of 3 and a ratio of 3.5, and it is far easier
 * to see here than to hear. */
#define SP_Y ALG_Y
typedef char fmlab_spectrum_fits[(240u * SP_H <= CV_MAX) ? 1 : -1];

static const uint16_t SP_SPAN[5] = {1000, 2000, 5000, 10000, 20000};
#define SP_NSPAN (sizeof SP_SPAN / sizeof SP_SPAN[0])
static uint8_t spspan = 2, sp_note;             /* the span on the knob, and the note to mark up */
static uint8_t sp_pk[240];                      /* a peak that falls back slowly, so a pluck reads */

/* middle A is 440: one octave of equal temperament in Q8 Hz, starting at MIDI note 0 */
static const uint16_t SP_NOTE_HZ[12] = {2093, 2218, 2349, 2489, 2637, 2794,
                                        2960, 3136, 3322, 3520, 3729, 3951};
static uint32_t sp_hz(uint32_t note)
{
    if (note > 127u)
        note = 127u;
    return ((uint32_t)SP_NOTE_HZ[note % 12u] << (note / 12u)) >> 8;
}

static void draw_spectrum(void)
{
    uint32_t span = SP_SPAN[spspan], c, k, grid, f0 = 0;
    if (v_lit) {                                /* something is sounding: mark its harmonics */
        int32_t n = (int32_t)sp_note + (int32_t)edit[144] - 24;   /* the patch's own transpose */
        f0 = sp_hz((uint32_t)(n < 0 ? 0 : n));
    }
    cv_begin(240, SP_H, C_BLACK);
    /* Nothing crosses the plot: a ruler along the top for the harmonics and a scale along the
     * floor for the kilohertz, so the only tall things on the screen are the partials. */
    cv_rect(0, SP_H / 3u, 240, 1, RGB(22, 22, 22));       /* -24 dB */
    cv_rect(0, SP_H * 2u / 3u, 240, 1, RGB(22, 22, 22));  /* -48 dB */
    cv_rect(0, SP_H - 1, 240, 1, RGB(48, 48, 48));        /* and the floor the bars stand on */
    grid = span <= 2000u ? 500u : span <= 5000u ? 1000u : span <= 10000u ? 2000u : 5000u;
    for (k = grid; k < span; k += grid)
        cv_rect((int32_t)(k * 240u / span), SP_H - 5, 1, 4, RGB(70, 70, 70));
    /* The harmonics, but only the first sixteen and only while they are far enough apart to be
     * read as a comb: past that they stop being a ruler and turn into a hedge. */
    if (f0 * 240u / span >= 4u)
        for (k = 1u; f0 && k <= 16u && k * f0 < span; k++)
            cv_rect((int32_t)(k * f0 * 240u / span), 0, 1, k == 1u ? 9 : 5,
                    k == 1u ? RGB(150, 118, 210) : RGB(96, 76, 136));
    for (c = 0; c < 240u; c++) {
        uint32_t b0 = (c * span / 240u) * SP_N / FS;
        uint32_t b1 = ((c + 1u) * span / 240u) * SP_N / FS;
        uint32_t h = 0;
        if (b1 <= b0)
            b1 = b0 + 1u;                       /* a column narrower than a bin still shows one */
        if (b1 > SP_BINS)
            b1 = SP_BINS;
        for (k = b0; k < b1; k++)
            if (sp_bin[k] > h)
                h = sp_bin[k];
        sp_pk[c] = (uint8_t)(h > sp_pk[c] ? h : sp_pk[c] > 2u ? sp_pk[c] - 2u : 0u);
        if (h) {
            cv_rect((int32_t)c, (int32_t)(SP_H - h), 1, (int32_t)h, RGB(52, 62, 120));
            cv_rect((int32_t)c, (int32_t)(SP_H - h), 1, 1, C_WHITE);
        }
        if (sp_pk[c] > h)
            cv_rect((int32_t)c, (int32_t)(SP_H - sp_pk[c]), 1, 1, C_ACC);
    }
    /* The two things you have to know to read it, written on it: the note the ruler belongs to,
     * and how far across the screen reaches. */
    {
        char s[16];
        uint32_t i = 0;
        if (v_lit) {
            i = put(s, 0, NOTE_NAME[sp_note % 12u]);
            if (i && s[i - 1u] == ' ')
                i--;
            i = putn(s, i, sp_note / 12u >= 1u ? sp_note / 12u - 1u : 0u, 1);   /* middle C is C4 */
            s[i] = 0;
            cv_text(3, 14, &FONT_S, s, RGB(120, 100, 160));
        }
        i = put(s, 0, "0-");
        i = putn(s, i, span / 1000u, 1);
        i = put(s, i, "K");
        s[i] = 0;
        cv_text((int32_t)(237u - text_w(&FONT_S, s)), 14, &FONT_S, s, RGB(80, 80, 80));
    }
    cv_blit(0, SP_Y);
}

/* ---- the envelope, drawn by stepping it the way the engine does
 *
 * A DX7 envelope is four (rate, level) pairs. The level is where the segment is heading and the
 * rate is how fast it gets there, so the same rate gives a different *time* depending on how far
 * it has to travel -- which is the single most confusing thing about programming a DX7, and the
 * reason this is drawn to scale rather than as four equal slopes.
 *
 * The maths below is dx_env_advance and dx_env_sample, solved for the segment length instead of
 * run one block at a time: falling is linear, and rising is linear within each octave of level,
 * so a handful of exact steps covers it. */
#define EG_Y 44
#define EG_H 72
#define EG_FULL (16 << 24)                      /* the level the engine saturates at */

static int32_t eg_target(const dx_env_t *e, uint32_t i)
{
    int32_t a = (int32_t)(dx_scaleoutlevel(e->levels[i]) >> 1);
    a = (a << 6) + e->outlevel - 4256;
    return (a < 16 ? 16 : a) << 16;
}

static int32_t eg_inc(const dx_env_t *e, uint32_t i)
{
    int32_t q = ((int32_t)e->rates[i] * 41) >> 6;
    q += e->rate_scaling;
    if (q > 63) q = 63;
    return (4 + (q & 3)) << (2 + DX_LG_N + (q >> 2));
}

/* how many blocks dx_env_sample would need to walk from `from` to `to` */
static uint32_t eg_blocks(int32_t from, int32_t to, int32_t inc)
{
    uint32_t n = 0;
    if (inc <= 0) return 1u;
    if (to <= from) {                           /* falling is linear, and the last step overshoots */
        uint32_t d = (uint32_t)((from - to + inc - 1) / inc);
        return d ? d : 1u;
    }
    if (from < (1716 << 16)) from = 1716 << 16; /* the jump a rising segment starts with */
    while (from < to && n < 1000000u) {
        int32_t band = (((from >> 24) + 1) << 24);
        int32_t step = (((17 << 24) - from) >> 24) * inc;   /* the shift floors: this is the factor dx_env_sample uses */
        int32_t lim = to < band ? to : band;
        uint32_t k;
        if (step <= 0) break;
        k = (uint32_t)((lim - from + step - 1) / step);
        if (!k) k = 1u;
        n += k;
        from += (int32_t)k * step;
    }
    return n ? n : 1u;            /* the block that jumps to 1716 also takes a step */
}

static int32_t eg_y(int32_t level)              /* level is already logarithmic: 1 unit = 6 dB */
{
    if (level < 0) level = 0;
    if (level > EG_FULL) level = EG_FULL;
    return (EG_H - 3) - (int32_t)(((int64_t)level * (EG_H - 4)) / EG_FULL);
}

/* The four segments to scale, a held-sustain gap between 3 and 4, and a marker where the envelope
 * of the operator on screen has actually got to. `seg` is the segment being edited. */
static void draw_eg(const dx_env_t *e, const dx_env_t *live, uint32_t seg)
{
    int32_t t[4], x[5], lvl[5];
    uint32_t b[4], i, tot = 0;
    const int32_t x0 = 4, x1 = 236, hold = 22;
    int32_t span, acc;

    lvl[0] = 0;
    for (i = 0; i < 4u; i++) {
        t[i] = eg_target(e, i);
        b[i] = eg_blocks(lvl[i], t[i], eg_inc(e, i));
        if (b[i] > 60000u) b[i] = 60000u;       /* a 90 s segment must not squash the other three */
        lvl[i + 1] = t[i];
        tot += b[i];
    }
    if (!tot) tot = 1u;

    cv_begin(240, EG_H, C_BLACK);
    cv_rect(0, EG_H - 2, 240, 1, RGB(40, 40, 40));

    span = x1 - x0 - hold;
    acc = 0;
    x[0] = x0;
    for (i = 0; i < 4u; i++) {                  /* widths in proportion to real time */
        acc += (int32_t)b[i];
        x[i + 1] = x0 + (int32_t)(((int64_t)acc * span) / (int32_t)tot);
        if (x[i + 1] < x[i] + 3) x[i + 1] = x[i] + 3;
        if (i == 2) { x[3] += hold; }           /* the key is held here, for as long as you hold it */
    }
    if (x[4] > x1) x[4] = x1;

    for (i = 0; i < 4u; i++) {
        uint16_t c = (i == seg) ? C_ACC : RGB(95, 100, 135);
        int32_t xa = (i == 3) ? x[3] : x[i], ya = eg_y(lvl[i]), yb = eg_y(lvl[i + 1]);
        if (i == 3) {                           /* the sustain, flat, before the release */
            cv_line(x[2] + 1, eg_y(lvl[3]), x[3], eg_y(lvl[3]), RGB(90, 95, 120));
            ya = eg_y(lvl[3]);
        }
        cv_line(xa, ya, x[i + 1], yb, c);
        if (i == seg) {                         /* the segment the two knobs are moving */
            cv_line(xa, EG_H - 2, xa, EG_H - 6, C_ACC);
            cv_line(x[i + 1], EG_H - 2, x[i + 1], EG_H - 6, C_ACC);
        }
    }

    if (live) {                                 /* where this operator's envelope is right now */
        uint32_t ix = live->ix < 4u ? live->ix : 3u;
        int32_t lo = lvl[ix], hi = lvl[ix + 1], l = live->level, xa, xb, px;
        xa = (ix == 3u) ? x[3] : x[ix];
        xb = x[ix + 1];
        px = (hi == lo) ? xb : xa + (int32_t)(((int64_t)(l - lo) * (xb - xa)) / (hi - lo));
        if (px < x0) px = x0;
        if (px > x1) px = x1;
        cv_rect(px - 1, eg_y(l) - 1, 3, 3, C_WHITE);
    }
    cv_blit(0, EG_Y);
}

/* The envelope to draw: the live one if this operator is sounding, otherwise one built the way a
 * note at middle C would build it, so the page is never blank. The shape always comes from `edit`,
 * so a switched-off operator still shows the envelope it would have -- but it gets no marker,
 * because the one the ISR is running has been taken to the floor and would sit there. */
static void draw_eg_now(uint32_t seg)
{
    dx_env_t scratch;
    uint32_t v = v_last;
    const uint8_t *o = &edit[opsel * 21u];
    int rs = 0, ol = dx_op_level(o, 60, 100, &rs);
    dx_env_init(&scratch, o, o + 4, ol, rs);
    if (v < NV && dxv[v].on && (v_lit >> v & 1u) && !(opmute >> opsel & 1u))
        draw_eg(&scratch, &dxv[v].env[opsel], seg);
    else
        draw_eg(&scratch, 0, seg);
}

/* One parameter row: name on the left, value on the right, and a bar underneath so the amount is
 * visible at a glance. The knob that moves it is KNOB1..4, top to bottom. */
static void draw_row(uint32_t slot, const param_t *p, uint32_t v)
{
    char s[32];
    uint32_t y = PAGE[page].y0 + slot * 32u, i;
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
        if (opmute >> opsel & 1u)
            i = put(s, i, " OFF");
    }
    nb = PAGE_NBANK;
    if (nb > 1u) {
        i = put(s, i, "  ");
        i = putn(s, i, bank + 1u, 1);
        s[i++] = '/';
        i = putn(s, i, nb, 1);
    }
    s[i] = 0;
    draw_text_box(0, 24, 240, &FONT_S, s, C_ACC, 1);
}

/* ---- what the four knobs do, under the four knobs
 *
 * The knobs are a row of four under the screen; the parameters they move are a column of four on
 * it. Everyone works that mapping out in a second or two and then works it out again every time
 * they come back to the instrument. This strip says it instead: four cells, left to right, in the
 * order your hand finds them. On the pages that already name each parameter beside its value it
 * is saying the same thing twice, which is the price of its being in the same place on every
 * screen -- somewhere you can glance without first deciding whether it is worth glancing at. */
#define LEGEND_Y 208u
#define STATUS_Y 224u

static void draw_legend(void)
{
    uint32_t e;
    cv_begin(240, 16, C_BLACK);
    for (e = 1u; e < 4u; e++)   /* without these, two seven-letter names run into one word */
        cv_rect((int32_t)(e * 60u), 4, 1, 8, RGB(44, 44, 44));
    for (e = 0; e < 4u; e++) {
        const char *s = 0;
        if (page == PAGE_HOME) {
            s = (bank == 1u && e == 0u) ? "SPAN" : 0;   /* the diagram has no knobs at all */
        } else {
            uint32_t pi = (uint32_t)bank * PAGE_ROWS + e;
            if (e < PAGE_ROWS && pi < PAGE[page].n)
                s = PAGE[page].p[pi].knob ? PAGE[page].p[pi].knob : PAGE[page].p[pi].name;
        }
        if (s) {
            uint32_t w = text_w(&FONT_S, s), pad = w + 6u < 60u ? (60u - w) / 2u : 3u;
            cv_text((int32_t)(e * 60u + pad), 0, &FONT_S, s, C_OFF);
        }
    }
    cv_blit(0, LEGEND_Y);
}

static void draw_status(int32_t oct, uint32_t nv, uint32_t cpu, uint32_t pk)
{
    char s[32];
    uint32_t i = put(s, 0, "OCT");
    s[i++] = oct < 0 ? '-' : '+';
    s[i++] = (char)('0' + (oct < 0 ? -oct : oct));
    if (monomode) {                             /* the voice count says nothing when there is one */
        i = put(s, i, " MONO");
    } else {
        i = put(s, i, " VOX");
        i = putn(s, i, nv, 1);
    }
    i = put(s, i, " CPU");
    i = putn(s, i, cpu > 999u ? 999u : cpu, 3);
    s[i++] = '%';
    i = put(s, i, " PK");
    i = putn(s, i, pk > 999u ? 999u : pk, 3);
    s[i++] = '%';
    s[i] = 0;
    draw_text_box(0, STATUS_Y, 240, &FONT_S, s, pk > 85u ? RGB(255, 110, 110) : C_OFF, 1);
}

/* ---- the main loop */
static void load_voice(uint32_t vi)
{
    if (vi < DX_NVOICES)
        memcpy(edit, DX_VOICES[vi], 156);
    else
        dx_unpack(bankv + (vi - DX_NVOICES) * 128u, edit);
    dx_sanitize(edit);
    modified = 0;
    opmute = 0;                                 /* a new voice arrives with all six operators on */
    patch_publish();
}

/* The operator switches. ON/OFF is the primitive and SOLO is everything-else-off, which is the
 * pair that gets used: ON/OFF to ask what one operator was contributing, SOLO to hear it alone.
 * Soloing a modulator is silent, and is meant to be -- see the output bus in draw_alg. */
static void op_toggle(uint32_t op)
{
    opmute ^= (uint8_t)(1u << op);
    patch_publish();
}

static void op_solo(uint32_t op)
{
    uint8_t only = (uint8_t)(0x3Fu & ~(1u << op));
    opmute = (opmute == only) ? 0u : only;      /* solo the same one twice: everything back on */
    patch_publish();
}

/* A/B. The switch swaps the two patches outright, so every page keeps reading `edit` and nothing
 * else in the file has to know the compare exists. The voice number and the edited mark travel
 * with the patch, or the name under the diagram would belong to the other side. */
static void ab_swap(uint8_t *voice)
{
    uint32_t i;
    uint8_t t;
    for (i = 0; i < 156u; i++) { t = edit[i]; edit[i] = other[i]; other[i] = t; }
    t = modified;   modified = other_mod;   other_mod = t;
    t = *voice;     *voice = other_voice;   other_voice = t;
    ab ^= 1u;
    patch_publish();
}

static void ab_copy(uint8_t voice)              /* this side onto the other: a place to come back to */
{
    memcpy(other, edit, 156);
    other_mod = modified;
    other_voice = voice;
}

/* ---- .syx in and out
 *
 * Thirty years of DX7 patches are a MIDI cable away, and reading other people's voices is most of
 * how anyone learns this. The FM-1 speaks the DX7's own dump format, so a .syx file off the net
 * goes straight in and a voice you built here comes straight back out.
 *
 *   F0 43 0n 09 20 00 <4096> sum F7   32 voices: the whole bank, written to flash
 *   F0 43 0n 00 01 1B  <155> sum F7   one voice: into the editor, as a DX7 does -- it lands in what
 *                                     you are playing and you choose whether to keep it
 *   F0 43 2n 09 F7 / F0 43 2n 00 F7   the host asking for either of those back
 *
 * The channel nibble is ignored on the way in and sent as 0. A bad checksum is still our message:
 * saying so is more use than handing it to the updater, which would only drop it.
 *
 * This runs in the main loop, from platform_poll, so a bank write stops the sound for the ~100 ms
 * of the two erases. That is the only time FM Lab deliberately interrupts itself. */
static int app_sysex(const uint8_t *p, uint32_t n)
{
    uint8_t u[156];
    uint32_t sub;
    if (n < 3u || p[0] != 0x43u)
        return 0;                               /* not Yamaha's: let the updater look at it */
    sub = p[1] >> 4;
    if (sub == 2u && n == 3u) {                 /* a request for a dump */
        if (p[2] == 0x09u) {
            bankv[NUSER * 128u] = dx_syx_sum(bankv, NUSER * 128u);
            say(sysex_send(bankmsg, sizeof bankmsg) ? "BANK SEND FAILED" : "BANK SENT");
        } else if (p[2] == 0x00u) {
            uint8_t m[163];
            m[0] = 0xF0; m[1] = 0x43; m[2] = 0x00; m[3] = 0x00; m[4] = 0x01; m[5] = 0x1B;
            memcpy(m + 6, edit, 155);           /* the editor, not the stored voice: what you hear */
            m[161] = dx_syx_sum(m + 6, 155);
            m[162] = 0xF7;
            say(sysex_send(m, sizeof m) ? "VOICE SEND FAILED" : "VOICE SENT");
        } else
            return 0;
        return 1;
    }
    if (sub != 0u || n < 5u)
        return 0;
    if (p[2] == 0x09u && p[3] == 0x20u && p[4] == 0x00u && n == 5u + NUSER * 128u + 1u) {
        if (dx_syx_sum(p + 5, NUSER * 128u) != p[n - 1u])
            say("BANK BAD CHECKSUM");
        else {
            memcpy(bankv, p + 5, NUSER * 128u);
            say(bank_write(bankv, NUSER * 128u) ? "BANK WRITE FAILED" : "BANK RECEIVED");
        }
        return 1;
    }
    if (p[2] == 0x00u && p[3] == 0x01u && p[4] == 0x1Bu && n == 5u + 155u + 1u) {
        if (dx_syx_sum(p + 5, 155u) != p[n - 1u])
            say("VOICE BAD CHECKSUM");
        else {
            memcpy(u, p + 5, 155);
            u[155] = 0;
            dx_sanitize(u);
            memcpy(edit, u, 156);
            opmute = 0;
            modified = 1;                       /* it is not any stored voice: it is an edit */
            patch_publish();
            say("VOICE RECEIVED");
        }
        return 1;
    }
    return 0;
}

static void app_main(void)
{
    /* Only ever append to this: settings_load takes a short store and leaves the rest at default,
     * so a new field costs nothing, but moving one would hand the old bytes to the wrong name. */
    struct { int8_t oct; uint8_t voice; uint8_t slot; uint8_t mono; uint8_t span; } set = {0, 0, 0, 0, 2};
    uint32_t prev = 0, k, t_change = 0, dirty = 0, t_meter = 0, repaint = 1, repaint_meter = 1;
    uint32_t sounding = 0;                      /* keys that really started a note: a page change must not strand one */
    uint32_t t_live = 0, eg_dirty = 1, eg_live = 0;
    uint32_t nv_drawn = ~0u, cpu_drawn = ~0u, pk_drawn = ~0u, vkey_drawn = ~0u;
    int32_t oct_drawn = 99;
    uint8_t held[27] = {0}, shown[4] = {255, 255, 255, 255};
    uint8_t page_drawn = 255, bank_drawn = 255, bank_drawn2 = 255, op_drawn = 255, mono_drawn = 255;
    uint8_t alg_drawn = 255, alg_op_drawn = 255, mute_drawn = 255;

    bank_init();                                /* before the first load_voice: a slot may be the voice */
    settings_load(&set, sizeof set);
    if (set.oct < -2 || set.oct > 2) set.oct = 0;
    if (set.voice >= NVOICE) set.voice = 0;
    if (set.slot >= NUSER) set.slot = 0;
    if (set.span >= SP_NSPAN) set.span = 2;
    spspan = set.span;
    sp_init();
    set.mono = set.mono ? 1u : 0u;
    ev_push(EV_MONO, set.mono, 0);              /* the ISR owns the mode: tell it what was stored */
    save_slot = set.slot;
    load_voice(set.voice);
    ab_copy(set.voice);                         /* both sides start as the same sound */
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
                int fn = op_key(k);
                if (fn >= 0) {
                    opsel = (uint8_t)fn;        /* pointing at an operator, not playing one */
                } else if (page == PAGE_OP && (k == MONO_KEY || k == POLY_KEY)) {
                    set.mono = k == MONO_KEY;
                    ev_push(EV_MONO, set.mono, 0);
                    say(set.mono ? "MONO" : "POLY");
                    t_change = fm1_ms;
                    dirty = 1;
                } else {
                    held[k] = (uint8_t)(base + k);
                    sounding |= 1u << k;
                    sp_note = held[k];          /* the analyser marks the harmonics of the last note */
                    ev_push(EV_ON, held[k], 100);
                    midi_send(0x90, held[k], 100);
                }
            }
            if ((up >> k & 1u) && (sounding >> k & 1u)) {
                sounding &= ~(1u << k);
                ev_push(EV_OFF, held[k], 0);
                midi_send(0x80, held[k], 0);
            }
        }

        b = fm1_input_edges(&rel);
        if (b && !(b & (1u << BTN_SAVE)))
            saving = 0;                                    /* anything else backs out of the save */
        if (b & (1u << BTN_OCT_DOWN) && set.oct > -2) { set.oct--; dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_OCT_UP) && set.oct < 2) { set.oct++; dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_FX)) cpu_peak = 0;              /* clear the worst-block mark */
        /* A page button moves to its page; pressing it again walks that page's banks of parameters,
         * so the button that got you there is also the one that shows you the rest. SEL still does
         * it too -- it is the shorter reach when you are already turning knobs. */
        if (b & (1u << BTN_HOME)) page_to(PAGE_HOME);
        if (b & (1u << BTN_EDIT)) page_to(PAGE_OP);
        if (b & (1u << BTN_ENV))  page_to(PAGE_EG);
        if (b & (1u << BTN_LFO))  page_to(PAGE_LFO);
        if (b & (1u << BTN_GLO))  page_to(PAGE_GLO);
        if (b & (1u << BTN_SEL))                           /* the next bank of parameters, or screen */
            page_to(page);
        if (b & (1u << BTN_SAVE)) {                        /* press to pick a slot, press again to write */
            if (!saving) {
                saving = 1;
                save_slot = set.slot;
                name_begin();
            } else {
                saving = 0;
                memcpy(edit + 145, save_name, 10);     /* the name goes in with the sound */
                if (bank_store(save_slot, edit) == 0) {
                    set.voice = (uint8_t)(DX_NVOICES + save_slot);
                    set.slot = save_slot;
                    modified = 0;                          /* the slot now holds exactly this */
                    say("SAVED");
                    dirty = 1;
                    t_change = fm1_ms;
                } else
                    say("SAVE FAILED");
            }
        }
        if (b & (1u << BTN_ARP)) op_toggle(opsel);         /* the selected operator, off and on */
        if (b & (1u << BTN_SEQ)) op_solo(opsel);           /* only the selected operator */
        if (b & (1u << BTN_PLAY)) {                        /* the other side of the compare */
            ab_swap(&set.voice);
            dirty = 1;
            t_change = fm1_ms;
        }
        if (b & (1u << BTN_REC)) ab_copy(set.voice);       /* put this side on the other one too */

        if ((enc = fm1_enc_take(ENC_PRESETS)) != 0) {      /* the voice, or the slot to save to */
            if (saving) {
                int32_t s2 = (int32_t)save_slot + enc;
                while (s2 < 0) s2 += (int32_t)NUSER;
                save_slot = (uint8_t)(s2 % (int32_t)NUSER);
            } else {
                int32_t vi = (int32_t)set.voice + enc;
                while (vi < 0) vi += NVOICE;
                set.voice = (uint8_t)(vi % NVOICE);
                load_voice(set.voice);
                dirty = 1;
                t_change = fm1_ms;
            }
        }
        if ((enc = fm1_enc_take(ENC_SELECT)) != 0) {       /* which operator the OP / ENV pages show */
            if (saving) {                                  /* or, while saving, which letter */
                int32_t c2 = (int32_t)save_cur + enc;
                while (c2 < 0) c2 += 10;
                save_cur = (uint8_t)(c2 % 10);
            } else {
                int32_t o = (int32_t)opsel - enc;          /* clockwise counts OP1 -> OP6 */
                while (o < 0) o += 6;
                opsel = (uint8_t)(o % 6);
            }
        }
        if ((enc = fm1_enc_take(ENC_ALGORITHM)) != 0) {    /* its own knob, on any page */
            if (saving) {                                  /* or, while saving, what that letter is */
                int32_t c2 = (int32_t)name_ix(save_name[save_cur]) + enc;
                while (c2 < 0) c2 += (int32_t)NAME_NCH;
                save_name[save_cur] = NAME_CH[c2 % (int32_t)NAME_NCH];
            } else {
                int32_t a = (int32_t)edit[134] + enc;
                edit[134] = (uint8_t)(a < 0 ? 0 : a > 31 ? 31 : a);
                modified = 1;
                patch_publish();
            }
        }
        if (page == PAGE_HOME && bank == 1u && (enc = fm1_enc_take(ENC_KNOB1)) != 0) {
            int32_t s = (int32_t)spspan + enc;  /* HOME has no parameters, so KNOB1 is the span */
            spspan = (uint8_t)(s < 0 ? 0 : s >= (int32_t)SP_NSPAN ? (int32_t)SP_NSPAN - 1 : s);
            set.span = spspan;
            for (k = 0; k < 240u; k++)
                sp_pk[k] = 0;                   /* the held peaks belonged to the old axis */
            dirty = 1;
            t_change = fm1_ms;
        }
        for (e = 0; e < PAGE_ROWS; e++) {                  /* KNOB1..n: the parameters on screen */
            uint32_t pi = (uint32_t)bank * PAGE_ROWS + e;
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
            if ((st & 0xF0u) == 0x90u && d2) { sp_note = d1; ev_push(EV_ON, d1, d2); }
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
        {                                       /* the OP1..OP6 keys: all six glow where they work,
                                                 * and the one you are editing is lit outright */
            int opk = page == PAGE_OP || page == PAGE_EG;
            for (k = 0; k < 6u; k++)
                fm1_led_key_glow(KEY_LED(OP_KEY[k]), opk);
            if (opk)
                fm1_led_key(KEY_LED(OP_KEY[5u - opsel]), 1);
            fm1_led_key_glow(KEY_LED(MONO_KEY), page == PAGE_OP);
            fm1_led_key_glow(KEY_LED(POLY_KEY), page == PAGE_OP);
            if (page == PAGE_OP)                /* and the mode you are in is the one that is lit */
                fm1_led_key(KEY_LED(set.mono ? MONO_KEY : POLY_KEY), 1);
        }
        /* The octave buttons: lit for a shift that way, and pulsing at the end of the travel. The
         * pulse rides the glow layer, so the LED dips rather than going dark -- a limit, not an
         * alarm. ~1.1 Hz, lit for two thirds of it. */
        {
            uint32_t ph = (fm1_ms % 900u) < 600u;
            int lo = set.oct < 0, hi = set.oct > 0;
            fm1_led_key(BTN_OCT_DOWN, lo && (set.oct > -2 || ph));
            fm1_led_key(BTN_OCT_UP, hi && (set.oct < 2 || ph));
            fm1_led_key_glow(BTN_OCT_DOWN, set.oct == -2);
            fm1_led_key_glow(BTN_OCT_UP, set.oct == 2);
        }
        fm1_led_key(BTN_HOME, page == PAGE_HOME);
        fm1_led_key(BTN_EDIT, page == PAGE_OP);
        fm1_led_key(BTN_ENV, page == PAGE_EG);
        fm1_led_key(BTN_LFO, page == PAGE_LFO);
        fm1_led_key(BTN_GLO, page == PAGE_GLO);
        fm1_led_key(BTN_ARP, (opmute >> opsel) & 1u);      /* this operator is off */
        fm1_led_key(BTN_SEQ, opmute != 0u);                /* something is off: you are not hearing it all */
        fm1_led_key(BTN_PLAY, ab != 0u);                   /* lit on side B */
        fm1_led_key(BTN_SAVE, saving);                     /* lit while it is asking which slot */

        if (flash_msg && fm1_ms - flash_at > 1500u)
            flash_msg = 0;

        if (page != page_drawn || bank != bank_drawn2) {   /* a new screen: clear once, then only
                                                            * what changes */
            page_drawn = page;
            bank_drawn2 = bank;
            bank_drawn = op_drawn = 255;
            alg_drawn = alg_op_drawn = 255;
            eg_dirty = 1;
            repaint_meter = 1;
            for (k = 0; k < 240u; k++)
                sp_pk[k] = 0;
            lcd_fill(0, 0, 240, 240, C_BLACK);  /* all of it: the meter is only on one page now */
            draw_legend();
            if (page == PAGE_HOME)
                draw_text_box(0, 18, 240, &FONT_L, APP_NAME, C_ACC, 1);
            repaint = 1;
        }
        if (page != PAGE_HOME && (bank != bank_drawn || opsel != op_drawn || opmute != mute_drawn || repaint)) {
            bank_drawn = bank;
            op_drawn = opsel;
            draw_head();
            shown[0] = shown[1] = shown[2] = shown[3] = 255;
            eg_dirty = 1;
        }
        {                                       /* the voice line: the name, or what it is asking / saying */
            uint32_t vkey = (uint32_t)set.voice | (uint32_t)modified << 8 | (uint32_t)ab << 9 |
                            (uint32_t)saving << 10 | (uint32_t)save_slot << 11 |
                            (flash_msg ? 1u << 17 : 0u) | (msg_gen & 7u) << 18 |
                            (uint32_t)save_cur << 21 | (uint32_t)name_sum() << 25;
            if (vkey != vkey_drawn || repaint) {
                vkey_drawn = vkey;
                draw_voice(set.voice, page == PAGE_HOME ? 44u : 188u);
                if (page != PAGE_HOME) { bank_drawn = 255; }   /* the header may name a changed algorithm */
            }
        }
        repaint = 0;

        if (page == PAGE_HOME) {
            /* the whole diagram only when the shape of it changes: the live brightness is six
             * small blits on the tick below, which is a great deal cheaper than 240 x 136 */
            if (bank == 0u && (edit[134] != alg_drawn || opsel != alg_op_drawn || opmute != mute_drawn)) {
                alg_drawn = edit[134];
                alg_op_drawn = opsel;
                draw_alg();
            }
        } else {
            for (e = 0; e < PAGE_ROWS; e++) {
                uint32_t pi = (uint32_t)bank * PAGE_ROWS + e;
                const param_t *p = pi < PAGE[page].n ? &PAGE[page].p[pi] : 0;
                uint8_t v = p ? edit[p_at(p)] : (uint8_t)254u;   /* 254 marks "this row is empty" */
                if (shown[e] != v) {
                    shown[e] = v;
                    draw_row(e, p, p ? v : 0u);
                    eg_dirty = 1;
                }
            }
        }
        mute_drawn = opmute;
        sp_want = (page == PAGE_HOME && bank == 1u);
        if (fm1_ms - t_live > 60u) {            /* the displays that follow the sound, ~16 times a second */
            t_live = fm1_ms;
            if (page == PAGE_HOME) {
                if (bank == 0u) {
                    draw_alg_levels();
                } else {
                    sp_run();
                    draw_spectrum();
                }
            } else if (page == PAGE_EG) {
                uint32_t snd = v_lit ? 1u : 0u; /* when it goes quiet, one last pass clears the marker */
                if (snd || snd != eg_live || eg_dirty) {
                    eg_live = snd;
                    eg_dirty = 0;
                    draw_eg_now(bank);
                }
            }
        }

        if (page == PAGE_GLO && fm1_ms - t_meter > 250u) {   /* the meter moves every block: redraw it
                                                              * 4 times a second, and only where it is */
            uint32_t cpu = cpu_q8 * 100u / 256u, pk = cpu_peak;
            t_meter = fm1_ms;
            if (set.oct != oct_drawn || nv != nv_drawn || cpu != cpu_drawn || pk != pk_drawn
                || monomode != mono_drawn || repaint_meter) {
                oct_drawn = set.oct;
                nv_drawn = nv;
                cpu_drawn = cpu;
                pk_drawn = pk;
                mono_drawn = monomode;
                repaint_meter = 0;
                draw_status(set.oct, nv, cpu, pk);
            }
        }

        if (dirty && !v_lit && fm1_ms - t_change > 2000u) {   /* an erase stalls the sound: save when silent */
            settings_save(&set, sizeof set);
            dirty = 0;
        }
    }
}
