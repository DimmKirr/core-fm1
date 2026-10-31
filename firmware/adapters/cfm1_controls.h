/* SPDX-License-Identifier: MIT */
/* Control map adapter: maps physical buttons and encoders to UI views.
 * Each firmware upstream provides its own table; the shell dispatches
 * through it instead of hardcoding button-to-view assignments. */
#ifndef CFM1_CONTROLS_H
#define CFM1_CONTROLS_H
#include <stdint.h>

/* button IDs (matches hardware matrix after calibration) */
#define BTN_OCTDN   0
#define BTN_OCTUP   1
#define BTN_FX      2
#define BTN_SCL     3
#define BTN_ENV     4
#define BTN_LFO     5
#define BTN_EDIT    6
#define BTN_GLO     7
#define BTN_HOME    8
#define BTN_SAVE    9
#define BTN_ARP    10
#define BTN_SEQ    11
#define BTN_PLAY   12
#define BTN_REC    13
#define BTN_COUNT  14

/* encoder IDs */
#define ENC_SELECT   0
#define ENC_ALGO     1
#define ENC_K1       2
#define ENC_K2       3
#define ENC_K3       4
#define ENC_K4       5
#define ENC_PRESET   6

/* what SELECT encoder does when no page claims it */
#define CTL_SELECT_PAGE   0   /* cycle pages within current view */
#define CTL_SELECT_BPM    1   /* adjust tempo */

/* button action: VIEW_* id on tap/hold, VIEW_NONE for no nav action */
typedef struct cfm1_controls {
    uint8_t     btn_tap[BTN_COUNT];     /* VIEW_* on tap */
    uint8_t     btn_hold[BTN_COUNT];    /* VIEW_* on hold */
    uint32_t    hold_us;                /* hold threshold in microseconds */
    uint8_t     select_default;         /* CTL_SELECT_PAGE or CTL_SELECT_BPM */
} cfm1_controls_t;

/* --- Felucca control map --- */

#if defined(CONFIG_CONTROLS_FELUCCA) || \
    (!defined(CONFIG_CONTROLS_MELODEE) && !defined(CONFIG_CONTROLS_SLOOP))

static const cfm1_controls_t cfm1_controls = {
    .btn_tap = {
        [BTN_OCTDN] = VIEW_NONE,
        [BTN_OCTUP] = VIEW_NONE,
        [BTN_FX]    = VIEW_FX,
        [BTN_SCL]   = VIEW_SCL,
        [BTN_ENV]   = VIEW_ENV,
        [BTN_LFO]   = VIEW_LFO,
        [BTN_EDIT]  = VIEW_EDIT,
        [BTN_GLO]   = VIEW_GLO,
        [BTN_HOME]  = VIEW_HOME,
        [BTN_SAVE]  = VIEW_SAVE,
        [BTN_ARP]   = VIEW_ARP,
        [BTN_SEQ]   = VIEW_SEQ,
        [BTN_PLAY]  = VIEW_NONE,
        [BTN_REC]   = VIEW_NONE,
    },
    .btn_hold = {
        [BTN_OCTDN] = VIEW_NONE,
        [BTN_OCTUP] = VIEW_NONE,
        [BTN_FX]    = VIEW_DEMO,
        [BTN_SCL]   = VIEW_NONE,
        [BTN_ENV]   = VIEW_NONE,
        [BTN_LFO]   = VIEW_NONE,
        [BTN_EDIT]  = VIEW_NONE,
        [BTN_GLO]   = VIEW_CONN,
        [BTN_HOME]  = VIEW_SETTINGS,
        [BTN_SAVE]  = VIEW_NONE,
        [BTN_ARP]   = VIEW_NONE,
        [BTN_SEQ]   = VIEW_NONE,
        [BTN_PLAY]  = VIEW_NONE,
        [BTN_REC]   = VIEW_NONE,
    },
    .hold_us = 500000u,
    .select_default = CTL_SELECT_PAGE,
};

/* --- Melodee control map --- */

#elif defined(CONFIG_CONTROLS_MELODEE)

static const cfm1_controls_t cfm1_controls = {
    .btn_tap = {
        [BTN_OCTDN] = VIEW_NONE,
        [BTN_OCTUP] = VIEW_NONE,
        [BTN_FX]    = VIEW_FX,
        [BTN_SCL]   = VIEW_SCL,
        [BTN_ENV]   = VIEW_ENV,
        [BTN_LFO]   = VIEW_LFO,
        [BTN_EDIT]  = VIEW_EDIT,
        [BTN_GLO]   = VIEW_GLO,
        [BTN_HOME]  = VIEW_HOME,
        [BTN_SAVE]  = VIEW_SAVE,
        [BTN_ARP]   = VIEW_ARP,
        [BTN_SEQ]   = VIEW_SEQ,
        [BTN_PLAY]  = VIEW_NONE,
        [BTN_REC]   = VIEW_NONE,
    },
    .btn_hold = {
        [BTN_OCTDN] = VIEW_NONE,
        [BTN_OCTUP] = VIEW_NONE,
        [BTN_FX]    = VIEW_DEMO,
        [BTN_SCL]   = VIEW_NONE,
        [BTN_ENV]   = VIEW_NONE,
        [BTN_LFO]   = VIEW_NONE,
        [BTN_EDIT]  = VIEW_NONE,
        [BTN_GLO]   = VIEW_CONN,
        [BTN_HOME]  = VIEW_SETTINGS,
        [BTN_SAVE]  = VIEW_NONE,
        [BTN_ARP]   = VIEW_NONE,
        [BTN_SEQ]   = VIEW_NONE,
        [BTN_PLAY]  = VIEW_NONE,
        [BTN_REC]   = VIEW_NONE,
    },
    .hold_us = 500000u,
    .select_default = CTL_SELECT_BPM,
};

/* --- SLOOP control map --- */

#elif defined(CONFIG_CONTROLS_SLOOP)

static const cfm1_controls_t cfm1_controls = {
    .btn_tap = {
        [BTN_OCTDN] = VIEW_NONE,
        [BTN_OCTUP] = VIEW_NONE,
        [BTN_FX]    = VIEW_FX,
        [BTN_SCL]   = VIEW_SCL,
        [BTN_ENV]   = VIEW_ENV,
        [BTN_LFO]   = VIEW_LFO,
        [BTN_EDIT]  = VIEW_EDIT,
        [BTN_GLO]   = VIEW_GLO,
        [BTN_HOME]  = VIEW_HOME,
        [BTN_SAVE]  = VIEW_SAVE,
        [BTN_ARP]   = VIEW_ARP,
        [BTN_SEQ]   = VIEW_SEQ,
        [BTN_PLAY]  = VIEW_NONE,
        [BTN_REC]   = VIEW_NONE,
    },
    .btn_hold = {
        [BTN_OCTDN] = VIEW_NONE,
        [BTN_OCTUP] = VIEW_NONE,
        [BTN_FX]    = VIEW_DEMO,
        [BTN_SCL]   = VIEW_NONE,
        [BTN_ENV]   = VIEW_NONE,
        [BTN_LFO]   = VIEW_NONE,
        [BTN_EDIT]  = VIEW_NONE,
        [BTN_GLO]   = VIEW_CONN,
        [BTN_HOME]  = VIEW_SETTINGS,
        [BTN_SAVE]  = VIEW_NONE,
        [BTN_ARP]   = VIEW_NONE,
        [BTN_SEQ]   = VIEW_NONE,
        [BTN_PLAY]  = VIEW_NONE,
        [BTN_REC]   = VIEW_NONE,
    },
    .hold_us = 700000u,
    .select_default = CTL_SELECT_BPM,
};

#endif

#endif /* CFM1_CONTROLS_H */
