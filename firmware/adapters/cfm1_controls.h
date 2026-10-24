/* SPDX-License-Identifier: MIT */
/* Control map adapter: maps physical buttons and encoders to UI actions.
 * Each firmware lineage provides its own table; the shell dispatches
 * through it instead of hardcoding button-to-page assignments. */
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
#define CTL_SELECT_PAGE   0   /* step through pages (core-fm1 default) */
#define CTL_SELECT_BPM    1   /* adjust tempo */

/* button action: navigate to a named page, or NULL for no action */
typedef struct cfm1_controls {
    const char *btn_tap[BTN_COUNT];     /* page name on tap (NULL = no nav) */
    const char *btn_hold[BTN_COUNT];    /* page name on hold (NULL = no nav) */
    uint32_t    hold_us;                /* hold threshold in microseconds */
    uint8_t     select_default;         /* CTL_SELECT_PAGE or CTL_SELECT_BPM */
} cfm1_controls_t;

/* --- Felucca control map --- */

#if defined(CONFIG_CONTROLS_FELUCCA) || \
    (!defined(CONFIG_CONTROLS_MELODEE) && !defined(CONFIG_CONTROLS_SLOOP))

static const cfm1_controls_t cfm1_controls = {
    .btn_tap = {
        [BTN_OCTDN] = 0,
        [BTN_OCTUP] = 0,
        [BTN_FX]    = "FX",
        [BTN_SCL]   = "SCL",
        [BTN_ENV]   = "ENV",
        [BTN_LFO]   = "LFO",
        [BTN_EDIT]  = "EDIT",
        [BTN_GLO]   =
#ifdef CONFIG_DEBUG_LOG
                       "DEBUG",
#else
                       0,
#endif
        [BTN_HOME]  = "HOME",
        [BTN_SAVE]  = 0,
        [BTN_ARP]   = "ARP",
        [BTN_SEQ]   = "SEQ",
        [BTN_PLAY]  = 0,
        [BTN_REC]   = 0,
    },
    .btn_hold = {
#ifdef CONFIG_SYS_MENU
        [BTN_HOME]  = "SYS",
#else
        [BTN_HOME]  = "ABOUT",
#endif
    },
    .hold_us = 500000u,
    .select_default = CTL_SELECT_PAGE,
};

/* --- Melodee control map --- */

#elif defined(CONFIG_CONTROLS_MELODEE)

static const cfm1_controls_t cfm1_controls = {
    .btn_tap = {
        [BTN_OCTDN] = 0,
        [BTN_OCTUP] = 0,
        [BTN_FX]    = "FX",
        [BTN_SCL]   = "SCL",
        [BTN_ENV]   = "ENV",
        [BTN_LFO]   = "LFO",
        [BTN_EDIT]  = "EDIT",
        [BTN_GLO]   = "MIXER",
        [BTN_HOME]  = "HOME",
        [BTN_SAVE]  = "SAVE",
        [BTN_ARP]   = "ARP",
        [BTN_SEQ]   = "SEQ",
        [BTN_PLAY]  = 0,
        [BTN_REC]   = 0,
    },
    .btn_hold = {
        [BTN_HOME]  = "MENU",
        [BTN_SAVE]  = 0,   /* undo, handled by subsystem */
    },
    .hold_us = 500000u,
    .select_default = CTL_SELECT_BPM,
};

/* --- SLOOP control map --- */

#elif defined(CONFIG_CONTROLS_SLOOP)

static const cfm1_controls_t cfm1_controls = {
    .btn_tap = {
        [BTN_OCTDN] = 0,
        [BTN_OCTUP] = 0,
        [BTN_FX]    = "FX",
        [BTN_SCL]   = "SCL",
        [BTN_ENV]   = "ENV",
        [BTN_LFO]   = "LFO",
        [BTN_EDIT]  = "EDIT",
        [BTN_GLO]   = "MIXER",
        [BTN_HOME]  = "HOME",
        [BTN_SAVE]  = "SAVE",
        [BTN_ARP]   = "ARP",
        [BTN_SEQ]   = "SEQ",
        [BTN_PLAY]  = 0,
        [BTN_REC]   = 0,
    },
    .btn_hold = {
        [BTN_HOME]  = "MENU",
    },
    .hold_us = 700000u,     /* SLOOP uses 0.7s hold threshold */
    .select_default = CTL_SELECT_BPM,
};

#endif

#endif /* CFM1_CONTROLS_H */
