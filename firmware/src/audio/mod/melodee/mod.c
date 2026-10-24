/* SPDX-License-Identifier: GPL-3.0-only */
/* Melodee modulation matrix stub: 4 slots per track, SRC -> DST by AMT.
 * TODO: port from upstream melodee mod.c. */

enum { MS_OFF, MS_LFO, MS_ENV, MS_VEL, MS_KEY, MS_RAND, MS_MODW, MS_AT, MS_EXPR, MS_SH, MS_SLEW, MS_N };
enum { MD_OFF, MD_PITCH, MD_CUT, MD_SHP, MD_AMP, MD_PAN, MD_RESERVED6, MD_RESERVED7, MD_RESERVED8, MD_RESERVED9, MD_RATE, MD_VIB, MD_E1,
       MD_DEPTH = MD_E1 + 8, MD_N };

#define NMSLOT 4u

static const char *N_MSRC[] = {
    "OFF", "LFO", "ENV", "VEL", "KEY", "RAND", "MODW", "AT", "EXPR", "S&H", "SLEW"
};
static const char *N_MDST[] = {
    "OFF", "PITCH", "CUT", "SHP", "AMP", "PAN", "-", "-", "-", "-", "RATE", "VIB",
    "E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8", "DEPTH"
};

static const char *mod_src_name(uint32_t src)
{
    return src < MS_N ? N_MSRC[src] : "---";
}

static const char *mod_dst_name(uint32_t dst)
{
    return dst < MD_N ? N_MDST[dst] : "---";
}

static const char *mod_src_icon(uint32_t src) { (void)src; return ""; }
static const char *mod_dst_icon(uint32_t dst) { (void)dst; return ""; }

static void mod_begin(track_t *t) { (void)t; }
static void mod_end(track_t *t) { (void)t; }
static void mod_voice(voice_t *v) { (void)v; }
static void mod_init(void) {}
