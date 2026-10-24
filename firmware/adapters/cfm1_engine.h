/* SPDX-License-Identifier: MIT */
/* Engine adapter: the contract between audio cores and synthesis engines.
 * Derived from felucca_engine.h (MIT, Hügelton Instruments) with melodee's
 * optional fields added. An engine includes this header and nothing else
 * from core-fm1. */
#ifndef CFM1_ENGINE_H
#define CFM1_ENGINE_H
#include <stdint.h>

#ifndef NVOICE
#error "cfm1_engine.h: the host defines NVOICE before including this header"
#endif
#ifndef NPART
#error "cfm1_engine.h: the host defines NPART before including this header"
#endif

#ifndef NELEM
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))
#endif

/* --- parameters --- */

enum {
    F_INT, F_PCT, F_BIPCT, F_TIME, F_LFOHZ, F_CUTOFF, F_DB, F_SEMI,
    F_ENUM, F_BPM, F_NOTE, F_ONOFF, F_OCT, F_STEPS
};

typedef struct {
    const char *label;
    uint8_t fmt;
    int16_t min, max, def;
    const char *const *names;
    const char *unit;
} param_desc_t;

/* --- voice state --- */

typedef struct {
    uint8_t note, vel, gate, active;
    uint8_t stage;
    uint8_t side;
    int32_t env;
    int32_t env_out;
    int32_t pitch16, pitch_cur;
    int32_t gstep;
    int32_t fine;
    uint32_t ph[3];
    int32_t s[8];
    uint32_t age;
    uint8_t mvel;
    int16_t mrnd;
} voice_t;

/* --- per-voice modulation (computed by audio core each block) --- */

typedef struct {
    uint32_t inc;
    int32_t pitch16;
    int32_t amp0, amp1;
    int32_t cutoff;
    int32_t shape;
    int32_t envq15;
    int32_t fine;
} vmod_t;

/* --- presets --- */

#define FX(d, c, dl, r) .fx = {(d),(c),(dl),(r)}
#define PAT(p)           .pat = (p)

typedef struct {
    const char *name;
    int8_t e[8];
    uint8_t env[4];
    int8_t fenv;
    uint8_t mono;
    uint8_t fx[4];
    uint8_t pat;
} preset_t;

/* --- engine vtable --- */

struct track;

typedef struct {
    const char *name;
    const char *page_title[2];
    param_desc_t edit[8];
    const preset_t *presets;
    uint8_t npresets;
    uint8_t knob[4];
    uint8_t poly;
    uint8_t sampled;
    uint8_t keep;
    uint8_t oneshot;
    uint8_t ownenv;
    uint16_t mem;

    /* required */
    void (*note_on)(struct track *t, voice_t *v);
    void (*render)(struct track *t, voice_t *v, int32_t *out,
                   uint32_t n, const vmod_t *m);

    /* optional (NULL = not used) */
    int32_t (*amp)(struct track *t, voice_t *v, int32_t adsr);
    const param_desc_t *(*desc)(const struct track *t, uint32_t k);
    void (*block)(struct track *t);
    int32_t (*keys)(const struct track *t, uint32_t k);
    int (*done)(struct track *t, voice_t *v);

    /* melodee extensions (NULL/0 for felucca/sloop engines) */
    uint32_t (*alloc)(struct track *t, uint32_t note);
    void (*legato)(struct track *t, voice_t *v);
    void (*mono_key)(struct track *t, uint32_t note);
    void (*post)(struct track *t, int32_t *out, uint32_t n, uint32_t nr);
    uint32_t (*cap)(const struct track *t);
    uint32_t (*units)(const struct track *t);
    void (*preset_make)(uint32_t k, preset_t *p, char *name);
} engine_t;

#endif /* CFM1_ENGINE_H */
