/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* HOME's light (Stage and the wheel page): each track in use lights the screen from its edges as it plays, in its
 * colour, behind everything drawn there, the header too; how the light moves is the kind of sound (browse.c's
 * category): a BASS swells from the floor, a LEAD streaks from the floor's middle round the corners, up the sides and
 * along the top, a PAD breathes up them, a PLUCK ripples out from its row, KEYS stack a block a note of the chord up
 * the sides, FX sparkle, an ORGAN glows while held, STRINGS swell brighter higher up, BRASS rises with the note, WIND
 * drifts up, a BELL rings out twice, anything else glows once. A DRUM kit lights by lane: the kick from the floor, the
 * snare and the clap all round and paler, the hats high on the sides and along the top (open: longer), toms and the
 * rest half-way up.
 * The light is a path round the screen (p: -119 the floor's middle .. 0 a bottom corner .. 239 a top corner .. 359 the
 * top's middle, both sides alike), worked out every frame on HOME before anything is drawn (home_light, ui_view.c), 2 px
 * a step (glow_light: a colour a step, on ui_draw_frame's stack); a pixel of the background takes the steps of the
 * edges near it, faded by its distance from each (glow_paint), so Stage's margins, the gaps between its rows, its band
 * and the header (the floor's and the top's light alone: they then send only the rows lit), the wheel page's sides and
 * keys all show their part of one light. A drawn frame whose light is the last one's sends nothing for it (glow.sig
 * against glow.drawn; the header: glow.top). The tracks' note-ons: voice.c glow_hit (the ISR's); a kit's lanes:
 * eng_drum.c drum_flash. Included by ui_draw.c */
#define GL_NS 120                                       /* 120 steps up the sides (rows 239 .. 0) */
#define GL_NF 60                                        /* 60 along the floor, corner to middle */
#define GL_NT 60                                        /* 60 along the top, corner to middle */
#define GL_N (GL_NS + GL_NF + GL_NT)
#define GL_GAIN 240                                     /* the light at its brightest: 94 % of the track's colour */
#define GL_STALE 32u                                    /* 8 ms units: hits older than a poll this late are not shown */
#define GL_DT 9                                         /* the top's light reaches 9 rows down (above the header's words) */

enum { GS_GLOW, GS_FLOOR, GS_STREAK, GS_BREATHE, GS_RIPPLE, GS_STACK, GS_KIT, GS_SPARKLE, GS_HOLD, GS_SWELL, GS_BLAZE,
       GS_DRIFT, GS_BELL, GS_N };
/* a category's motion (CAT_NONE .. CAT_BELL) */
static const uint8_t GL_STYLE[CAT_N] = {GS_GLOW, GS_FLOOR, GS_STREAK, GS_BREATHE, GS_RIPPLE, GS_STACK, GS_KIT, GS_SPARKLE,
                                        GS_GLOW, GS_HOLD, GS_SWELL, GS_BLAZE, GS_DRIFT, GS_BELL};
/* the held motions: their level's rise while a note is held and fall after (of 255, per 8 ms); 0: a hit's */
static const uint8_t GL_ATK[GS_N] = {[GS_BREATHE] = 4, [GS_HOLD] = 40, [GS_SWELL] = 2, [GS_BLAZE] = 16, [GS_DRIFT] = 5};
static const uint8_t GL_REL[GS_N] = {[GS_BREATHE] = 1, [GS_HOLD] = 8, [GS_SWELL] = 1, [GS_BLAZE] = 4, [GS_DRIFT] = 2};
/* a kit's lanes (eng_drum.c drum_lane_abbr) in four groups: BD; SD, CP; CH, OH; TM, RS, CB */
static const uint8_t GL_KIT[4] = {0x01u, 0x06u, 0x18u, 0xE0u};

static struct {
    uint8_t seen[NTRK], vel[NTRK], cat[NTRK], env[NTRK], nn[NTRK], lanes[NTRK];
    uint8_t age[NTRK][4];                               /* 8 ms units since the last hit (255: none lately); a kit:
                                                         * by lane group */
    uint32_t ms, sig, drawn, top;                       /* the last poll; this frame's light, the one the strips last
                                                         * drew (0: draw them), the top's as the header last took it */
    uint8_t lit;                                        /* this frame's light is not all dark */
    uint8_t ds, df;                                     /* how far it reaches in: from the sides, from the floor */
} glow;
static const uint16_t *glow_cur;                        /* this frame's light (glow_light), while HOME draws */

/* every frame on HOME's pages: the hits since the last poll (a kit's lanes into *lanes, for Stage's band), the held
 * motions' levels */
static void glow_poll(uint8_t *lanes)
{
    uint8_t h[NTRK], v[NTRK];
    uint32_t k, g, i, dt = (fm1_ms - glow.ms) >> 3, fresh = dt < GL_STALE;
    fm1_irq_off();                                      /* (one coherent ISR snapshot) */
    for (k = 0; k < NTRK; k++) {
        h[k] = glow_hit[k];
        v[k] = glow_vel[k];
        lanes[k] = drum_flash[k];
        drum_flash[k] = 0;
    }
    fm1_irq_on();
    glow.ms += dt << 3;
    if (dt > 255u)
        dt = 255u;
    for (k = 0; k < NTRK; k++) {
        uint32_t st = GL_STYLE[glow.cat[k] % CAT_N], held = 0, e = glow.env[k];
        for (g = 0; g < 4u; g++)
            glow.age[k][g] = (uint8_t)(glow.age[k][g] + dt > 255u ? 255u : glow.age[k][g] + dt);
        if (h[k] != glow.seen[k] && fresh) {
            glow.vel[k] = v[k];
            if (st == GS_KIT) {
                uint32_t ln = lanes[k] ? lanes[k] : 0x02u;      /* (a drum sound on a synth: as a snare) */
                for (g = 0; g < 4u; g++)
                    if (ln & GL_KIT[g]) {
                        glow.age[k][g] = 0;
                        glow.lanes[k] = (uint8_t)((glow.lanes[k] & ~GL_KIT[g]) | (ln & GL_KIT[g]));
                    }
            } else {
                glow.age[k][0] = 0;
                for (i = 0, glow.nn[k] = 0; i < NVOICE; i++)   /* (KEYS: the chord's notes) */
                    glow.nn[k] = (uint8_t)(glow.nn[k] + (trk[k].v[i].active && trk[k].v[i].gate));
            }
        }
        glow.seen[k] = h[k];
        if (GL_ATK[st]) {
            for (i = 0; i < NVOICE && !held; i++)
                held = trk[k].v[i].active && trk[k].v[i].gate;
            e = held ? e + GL_ATK[st] * dt : e > GL_REL[st] * dt ? e - GL_REL[st] * dt : 0u;
            glow.env[k] = (uint8_t)(e > 255u ? 255u : e);
        }
    }
}

/* track k's sound's category (the loaded one: browse.c cur_entry reads the selected track); a DRUM track is a kit */
static uint32_t glow_cat(uint32_t k)
{
    uint32_t src, kk;
    uint8_t s = song.sel;
    if (drum_track(&trk[k]))
        return CAT_DRUM;
    song.sel = (uint8_t)k;
    cur_entry(&src, &kk);
    song.sel = s;
    return entry_cat(src, kk) % CAT_N;
}

static int32_t gl_lin(int32_t a, int32_t dur) { return a < 0 || a >= dur ? 0 : 256 - a * 256 / dur; }   /* 256 .. 0 */
static int32_t gl_dec(int32_t a, int32_t dur) { int32_t x = gl_lin(a, dur); return x * x >> 8; }   /* .. squared */
static int32_t gl_bump(int32_t d, int32_t w) { return gl_lin(d < 0 ? -d : d, w); }              /* a peak of width 2w */
static int32_t gl_ramp(int32_t x, int32_t w) { return x <= 0 ? 0 : x >= w ? 256 : x * 256 / w; }

typedef struct {
    uint8_t st, nn, lanes;
    int16_t o;                                          /* where its row is (p) */
    int32_t v, e, a[4];                                 /* the hit's strength (128 .. 255), the held level, ms since */
    uint32_t seed;
    uint16_t c, pale;
} gl_trk_t;

/* a track's light at p: in its colour (*m) and paler (*q), 0 .. 256 */
static void gl_at(const gl_trk_t *t, int32_t p, uint32_t ms, int32_t *m, int32_t *q)
{
    int32_t a = t->a[0], v = t->v, i = 0, j = 0, d, s = p > 239 ? 239 : p;   /* (s: the height, the top's the sides' top) */
    uint32_t g;
    switch (t->st) {
    case GS_GLOW:
        i = v * gl_dec(a, 300) * 150 >> 16;
        break;
    case GS_FLOOR:                                      /* the floor, and the sides' lower half */
        i = (v * gl_dec(a, 480) >> 8) * (p <= 0 ? 256 : 256 - gl_ramp(p, 130)) >> 8;
        break;
    case GS_STREAK:                                     /* the floor's middle to the top's in 340 ms, an 80 px tail */
        d = -119 + a * 478 / 340 - p;
        if (d >= 0 && d < 80) {
            i = (v * (a < 340 ? 256 : gl_lin(a - 340, 150)) >> 8) * (256 - d * 256 / 80) >> 8;
            j = d < 6 ? i / 2 : 0;                      /* (its head paler) */
        }
        break;
    case GS_RIPPLE:                                     /* a ring out from its row, both ways */
        d = p - t->o;
        i = (v * gl_lin(a, 380) >> 8) * gl_bump((d < 0 ? -d : d) - a * 9 / 16, 16) >> 8;
        break;
    case GS_BELL:                                       /* two rings, slower, the second paler */
        d = p - t->o;
        d = d < 0 ? -d : d;
        i = (v * gl_lin(a, 900) >> 8) * gl_bump(d - a * 6 / 16, 12) >> 8;
        if (a >= 160)
            j = (v * gl_lin(a - 160, 740) >> 8) * gl_bump(d - (a - 160) * 6 / 16, 12) >> 8;
        break;
    case GS_STACK:                                      /* a block a note up the sides (the top: the fourth), 45 ms apart */
        if (p < 0) {
            i = (v * gl_dec(a, 420) >> 8) * 150 >> 8;
        } else if ((p > 239 || p % 60 < 52) && s / 60 < t->nn) {
            i = v * gl_dec(a - 45 * (s / 60), 420) >> 8;
        }
        break;
    case GS_SPARKLE:                                    /* eight sparks along the path, 40 ms apart */
        for (g = 0; g < 8u; g++) {
            int32_t at = a - (int32_t)g * 40, x;
            if (at < 0 || at >= 180)
                continue;
            x = (v * gl_lin(at, 180) >> 8) *
                gl_bump(p - ((int32_t)(((t->seed + g) * 2654435761u >> 12) % 479u) - 119), 14) >> 8;
            if (g & 1u) j += x; else i += x;
        }
        break;
    case GS_KIT:
        i = (v * gl_dec(t->a[0], 240) >> 8) * (p <= 0 ? 256 : 256 - gl_ramp(p, 90)) >> 8;    /* BD */
        j = (v * gl_dec(t->a[1], 170) >> 8) * 200 >> 8;                                    /* SD, CP */
        i += (v * gl_dec(t->a[2], (t->lanes & 0x10u) ? 320 : 90) >> 8) * gl_ramp(p - 150, 60) >> 8;   /* CH, OH */
        i += (v * gl_dec(t->a[3], 200) >> 8) * gl_bump(p - 120, 50) >> 8;                 /* TM, RS, CB */
        break;
    case GS_BREATHE: {                                  /* a slow wave up the sides and along the top (2.4 s), the floor
                                                         * calm */
        int32_t x = (p * 256 / 160 - (int32_t)((ms / 75u) & 31u) * 8) & 255;
        i = (v * t->e >> 8) * (p < 0 ? 100 : (150 + ((x < 128 ? 128 - x : x - 128) * 2) * 106 / 256) * 150 >> 8) >> 8;
        break;
    }
    case GS_DRIFT:                                      /* bands drifting up and over, 260 px apart */
        d = p - ((int32_t)((ms >> 3) % 520u) - 119);
        i = (v * t->e >> 8) * (gl_bump(d, 50) + gl_bump(d + 260, 50) + gl_bump(d - 260, 50)) * 170 >> 16;
        break;
    case GS_HOLD:
        i = (v * t->e >> 8) * (p <= 0 ? 90 : 130) >> 8;
        break;
    case GS_SWELL:
        i = (v * t->e >> 8) * (p <= 0 ? 70 : 80 + s * 100 / 239) >> 8;
        break;
    case GS_BLAZE:                                      /* the floor's middle up to as far as the note has swelled */
        i = (v * gl_ramp(-119 + t->e * 514 / 256 - p, 36) >> 8) * 170 >> 8;
        break;
    }
    *m = i > 256 ? 256 : i;
    *q = j > 256 ? 256 : j;
}

/* this frame's light into lt (GL_N steps: the sides, the floor, the top), the tracks' rows at org (p); dark: none
 * (the knobs out); ds, df: how far it reaches in from the sides and the floor. glow.sig: its signature; glow.top: the
 * top's, as the header takes it (upd: a drawn frame; between them the header keeps the last) */
static void glow_light(uint16_t *lt, const int16_t *org, int dark, uint32_t ds, uint32_t df, int upd)
{
    gl_trk_t g[NTRK];
    uint32_t n = 0, k, i, sig = 2166136261u, top = 2166136261u;
    glow_cur = lt;
    glow.ds = (uint8_t)ds;
    glow.df = (uint8_t)df;
    for (k = 0; k < trk_n() && !dark; k++) {
        gl_trk_t *t = &g[n];
        uint32_t a;
        glow.cat[k] = (uint8_t)glow_cat(k);
        t->st = GL_STYLE[glow.cat[k]];
        for (a = 0, i = 0; i < 4u; i++) {
            t->a[i] = glow.age[k][i] * 8;
            a |= glow.age[k][i] < 125u;                 /* (a hit within the last second) */
        }
        if (trk[k].p[P_MUTE] || (!a && !glow.env[k]))
            continue;
        t->nn = glow.nn[k] ? glow.nn[k] : 1u;
        t->lanes = glow.lanes[k];
        t->o = org[k];
        t->v = 128 + (glow.vel[k] & 127);
        t->e = glow.env[k];
        t->seed = glow.seen[k] * 37u + k;
        t->c = T_TRK(k);
        t->pale = mix565(t->c, T_TEXT, 16u);
        n++;
    }
    glow.lit = 0;
    for (i = 0; i < GL_N; i++) {
        int32_t p = i < GL_NS ? 2 * (int32_t)i : i < GL_NS + GL_NF ? -2 * (int32_t)(i - GL_NS)
                                                                   : 240 + 2 * (int32_t)(i - GL_NS - GL_NF), m, q;
        uint16_t c = T_BG;
        for (k = 0; k < n; k++) {
            gl_at(&g[k], p, fm1_ms, &m, &q);
            if (m) c = mix565(c, g[k].c, (uint32_t)(m * GL_GAIN >> 11));
            if (q) c = mix565(c, g[k].pale, (uint32_t)(q * GL_GAIN >> 11));
        }
        lt[i] = c;
        glow.lit |= c != T_BG;
        sig = (sig ^ c) * 16777619u;
        if (i >= GL_NS + GL_NF)
            top = (top ^ c) * 16777619u;
    }
    sig += ds * 7u + df * 131u + ux.gen * 977u;
    glow.sig = sig + !sig;
    if (upd)
        glow.top = top + ux.gen * 977u;
}

/* the light on the canvas's rows y0 .. y0+h-1, columns x0 .. x0+w-1, the canvas's top left at (sx, sy) on the screen:
 * each pixel faded towards the light of the edges near it (the sides' step at its height, the floor's below it, the
 * top's above it) by how near; drawn over what is there (the background). sides 0: the floor's and the top's alone */
static void glow_paint_at(int32_t sx, int32_t sy, int32_t x0, int32_t y0, int32_t w, int32_t h, int sides)
{
    const uint16_t *lt = glow_cur;
    int32_t ds = sides ? glow.ds : 0, df = glow.df, x, y;
    uint8_t fs[96], ff[64], ft[GL_DT];                  /* the fade by distance (of 32) */
    if (!lt || !glow.lit)
        return;
    ds = ds > 96 ? 96 : ds;
    df = df > 64 ? 64 : df;
    for (x = 0; x < ds; x++)
        fs[x] = (uint8_t)((ds - x) * (ds - x) * 32 / (ds * ds));
    for (y = 0; y < df; y++)
        ff[y] = (uint8_t)((df - y) * (df - y) * 32 / (df * df));
    for (y = 0; y < GL_DT; y++)
        ft[y] = (uint8_t)((GL_DT - y) * (GL_DT - y) * 32 / (GL_DT * GL_DT));
    for (y = y0 < 0 ? 0 : y0; y < y0 + h && y < (int32_t)cv_h; y++) {
        int32_t Y = sy + y, dy = 239 - Y, fb = dy >= 0 && dy < df ? ff[dy] : 0, fu = Y >= 0 && Y < GL_DT ? ft[Y] : 0;
        uint16_t *row = cv_px + (uint32_t)y * cv_w, cs;
        if (Y < 0 || Y > 239)
            continue;
        cs = lt[dy >> 1];
        for (x = x0 < 0 ? 0 : x0; x < x0 + w && x < (int32_t)cv_w; x++) {
            int32_t X = sx + x, dx = X < 239 - X ? X : 239 - X, f = dx >= 0 && dx < ds ? fs[dx] : 0;
            uint16_t c;
            if (!f && !fb && !fu) {                     /* (the middle, out of reach: on to the right side's) */
                if (X < 120 && 240 - ds - sx - 1 > x)
                    x = 240 - ds - sx - 1;
                continue;
            }
            c = swap16(row[x]);
            if (f)
                c = mix565(c, cs, (uint32_t)f);
            if (fb)
                c = mix565(c, lt[GL_NS + (dx >> 1)], (uint32_t)fb);
            if (fu)
                c = mix565(c, lt[GL_NS + GL_NF + (dx >> 1)], (uint32_t)fu);
            row[x] = swap16(c);
        }
    }
}
static void glow_paint_in(int32_t sx, int32_t sy, int32_t x0, int32_t y0, int32_t w, int32_t h)
{
    glow_paint_at(sx, sy, x0, y0, w, h, 1);
}
static void glow_paint(int32_t sx, int32_t sy) { glow_paint_at(sx, sy, 0, 0, (int32_t)cv_w, (int32_t)cv_h, 1); }
/* .. the floor's and the top's light alone (Stage's band, the header: the margins beside them take the sides') */
static void glow_paint_ends(int32_t sx, int32_t sy) { glow_paint_at(sx, sy, 0, 0, (int32_t)cv_w, (int32_t)cv_h, 0); }
/* the background at (x, y, w, h) on the screen, lit, sent */
static void glow_strip(int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (w <= 0 || h <= 0)
        return;
    cv_begin((uint32_t)w, (uint32_t)h, T_BG);
    glow_paint(x, y);
    cv_blit((uint32_t)x, (uint32_t)y);
}
