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
    uint8_t playing;
    uint8_t recording;
    uint8_t sel;
} song_t;

/* --- globals (the audio core owns these) --- */

static track_t trk[NTRK];
static song_t song;

/* --- engine registry (engines.c) --- */

#ifndef NENGINES
#define NENGINES 1
#endif

#endif /* CFM1_AUDIO_H */
