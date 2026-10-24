/* SPDX-License-Identifier: GPL-3.0-only */
/* Audio adapter: types and constants shared between the audio core,
 * engines, effects, sequencer, and UI. */
#ifndef CFM1_AUDIO_H
#define CFM1_AUDIO_H
#include <stdint.h>

/* --- core constants --- */

#define NVOICE 8
#define NPART  4
#define NTRK   NPART
#define NSTEP  64
#define CTL    32          /* samples per control tick */
#define BLOCK  128         /* DMA half-buffer: 128 stereo samples @ 44.1kHz */

/* --- audio pipeline precision --- */

#ifdef CONFIG_AUDIO_24BIT
#define AUDIO_Q       23
#define AUDIO_AMP_ONE 8388607           /* Q23 full scale */
#define AUDIO_SAT_MAX 8388607
#define AUDIO_SAT_MIN (-8388607)
#ifndef CONFIG_AUDIO_24BIT_HEADROOM
#define CONFIG_AUDIO_24BIT_HEADROOM 1
#endif
#define AUDIO_OUT_SHIFT CONFIG_AUDIO_24BIT_HEADROOM
static inline int32_t audio_sat(int32_t x)
{
    return x > AUDIO_SAT_MAX ? AUDIO_SAT_MAX
         : x < AUDIO_SAT_MIN ? AUDIO_SAT_MIN : x;
}
#else
#define AUDIO_Q       15
#define AUDIO_AMP_ONE 32767             /* Q15 full scale */
#define AUDIO_OUT_SHIFT 7               /* Q15 << 7 = Q22 in 24-bit DAC word */
static inline int32_t audio_sat(int32_t x)
{
    return x > 32767 ? 32767 : x < -32767 ? -32767 : x;
}
#endif

/* engine params are always the last 8 in the P_* enum */
#include "cfm1_engine.h"

/* --- parameter IDs (superset of all lineages) --- */

enum {
    P_LEVEL, P_PAN, P_TUNE, P_FINE,
    P_ATK, P_DEC, P_SUS, P_REL,
    P_FLT, P_RES, P_FENV, P_SHP,
    P_LFO, P_LFRT, P_LFSH, P_LFDL,
    P_GLI, P_UNI, P_UDTN, P_USPRD,
    P_ARP, P_ARTM, P_ARNG, P_AGATE,
    P_OCT, P_ROOT, P_SCALE, P_QUANT, P_TRANS,
    P_MONO, P_HOLD, P_VELO,
    /* modulation matrix */
    P_M1SRC, P_M1DST, P_M1AMT,
    P_M2SRC, P_M2DST, P_M2AMT,
    P_M3SRC, P_M3DST, P_M3AMT,
    P_M4SRC, P_M4DST, P_M4AMT,
    /* FX sends (common across lineages) */
    P_DIST, P_CHOR, P_DLY, P_REV,
    /* per-engine track params (Felucca 1.5: ANALOG filter type) */
    P_FTYPE,
    /* engine params (always last 8) */
    P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7,
    P_COUNT
};

/* --- step (sequencer pattern data) --- */

typedef struct {
    uint8_t note[4];
    uint8_t vel;
    uint8_t flags;
    uint8_t len;
    uint8_t hit;
} step_t;

/* --- track --- */

typedef struct track {
    int16_t p[P_COUNT];
    uint8_t engine, preset;
    uint8_t eng_req;
    uint8_t user;
    voice_t v[NVOICE];
    /* LFO */
    uint32_t lfo_ph;
    int32_t lfo_val;
    int32_t lfo_fade;
    uint32_t lfo_rnd;
    int16_t lfo_prev;
    /* keyboard input */
    uint8_t held[16];
    uint8_t nheld;
    uint8_t arp_phys;
    uint8_t latched;
    /* sequencer state */
    step_t step[NSTEP];
    uint32_t seq_pos;
    uint16_t seq_idx;
} track_t;

/* --- song (transport) --- */

typedef struct {
    uint32_t bpm;
    uint32_t tick;
    uint32_t len;
    uint32_t master_q12;
    uint8_t playing;
    uint8_t recording;
    uint8_t rec;
    uint8_t sel;
} song_t;

/* --- globals (the audio core owns these) --- */

static track_t trk[NTRK];
static song_t song;

/* --- engine registry --- */
/* When CONFIG_AUDIO is enabled, engines.c defines ENGINES[] and NENGINES.
 * When audio is off, provide a minimal stub so UI pages compile. */

#ifndef CONFIG_AUDIO

/* When real engines are compiled (CONFIG_ENGINES), engines.c provides
 * ENGINES[] and NENGINES. Otherwise, provide a mock for the UI. */
#ifndef CONFIG_ENGINES
static const engine_t _eng_mock = {
    .name       = "ANALOG",
    .page_title = {"SYNTH 1", "SYNTH 2"},
    .edit       = {
        { "CUT",  F_CUTOFF, 20, 999, 355, 0, "Hz" },
        { "RES",  F_PCT,     0, 100,  79, 0, "%"  },
        { "ATK",  F_TIME,    0, 999,  10, 0, "ms" },
        { "REL",  F_TIME,    0, 999,  88, 0, "ms" },
        { "SHP",  F_PCT,     0, 100,  50, 0, "%"  },
        { "FLT",  F_PCT,     0, 100,  64, 0, "%"  },
        { "DRV",  F_PCT,     0, 100,   0, 0, "%"  },
        { "MIX",  F_PCT,     0, 100, 100, 0, "%"  },
    },
    .knob       = {0, 1, 2, 3},
    .note_on    = 0,
    .render     = 0,
};
static const engine_t *const ENGINES[] = { &_eng_mock };
#undef NENGINES
#define NENGINES 1
#endif /* !CONFIG_ENGINES */

static void cfm1_mock_init(void)
{
    uint32_t k;
    song.bpm = 120;
    song.playing = 0;
    song.sel = 0;
    for (k = 0; k < NTRK; k++) {
        uint32_t e;
        trk[k].eng_req = 0;
        for (e = 0; e < 8; e++)
            trk[k].p[P_E0 + e] = 0;
        trk[k].p[P_ATK]   = 10;
        trk[k].p[P_DEC]   = 40;
        trk[k].p[P_SUS]   = 80;
        trk[k].p[P_REL]   = 88;
        trk[k].p[P_LEVEL] = 100;
        trk[k].p[P_LFO]   = 50;
        trk[k].p[P_LFRT]  = 30;
        trk[k].p[P_DIST]  = 20;
        trk[k].p[P_DLY]   = 35;
        trk[k].p[P_REV]   = 45;
        trk[k].p[P_ROOT]  = 0;
        trk[k].p[P_SCALE] = 0;
        trk[k].p[P_OCT]   = 0;
        trk[k].p[P_ARP]   = 0;
        trk[k].p[P_MONO]  = 0;
        trk[k].p[P_HOLD]  = 0;
        trk[k].p[P_GLI]   = 0;
        trk[k].p[P_UNI]   = 0;
    }
    trk[0].step[0].note[0] = 60;
    trk[0].step[0].vel = 100;
    trk[0].step[2].note[0] = 64;
    trk[0].step[2].vel = 80;
    trk[0].step[4].note[0] = 67;
    trk[0].step[4].vel = 90;
    trk[0].step[6].note[0] = 72;
    trk[0].step[6].vel = 110;
}

#else /* CONFIG_AUDIO */
#ifndef NENGINES
#define NENGINES 1
#endif
#endif /* CONFIG_AUDIO */

#endif /* CFM1_AUDIO_H */
