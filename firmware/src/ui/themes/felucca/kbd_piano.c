/* SPDX-License-Identifier: GPL-3.0-only */
/* Piano direct-map keyboard modal: one key per character.
 * 27 piano keys = 26 letters + space across 4 layers (abc/ABC/123/!@#).
 * ALGO rotates layers, K1 moves cursor, K2 scrolls charset, FX=backspace,
 * OCT+=confirm, OCT-=cancel. SELECT toggles password mask. */

#define KBD_MAX_LEN   63
#define KBD_N_LAYERS   4

static const char KBD_L_ABC[27] = {
    'a','b','c','d','e','f','g','h','i','j','k','l','m',
    'n','o','p','q','r','s','t','u','v','w','x','y','z',' '
};
static const char KBD_L_CAP[27] = {
    'A','B','C','D','E','F','G','H','I','J','K','L','M',
    'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',' '
};
static const char KBD_L_123[27] = {
    '1','?','2','#','3','$','4',
    '5','%','6','&','7',
    '8','*','9','+','0','=',
    '-','.',',','_','/','@',
    '!','(',' '
};
static const char KBD_L_SYM[27] = {
    '~','^','`','!','|','@','\\',
    ';','#',':','$','\'',
    '"','%','<','&','>','*',
    '{','}','=','[','(',']',
    ')','?',' '
};

static const char * const KBD_MAPS[KBD_N_LAYERS] = {
    KBD_L_ABC, KBD_L_CAP, KBD_L_123, KBD_L_SYM
};
static const char * const KBD_NAMES[KBD_N_LAYERS] = {
    "abc", "ABC", "123", "!@#"
};

/* black key x positions on the 240px strip (15px white keys, 10px black) */
static const uint8_t KBD_BLK_X[11] = {
    10, 25, 40, 70, 85, 115, 130, 145, 175, 190, 220
};

static struct {
    char     buf[KBD_MAX_LEN + 1];
    uint8_t  len;
    uint8_t  cur;
    uint8_t  layer;
    uint8_t  max_len;
    uint8_t  hide;
    uint8_t  done;
    uint8_t  flash_pos;
    uint8_t  flash_n;
    const char *title;
    void (*on_done)(const char *, uint8_t);
} kbd;

static int kbd_is_black(uint32_t pos)
{
    return (int)((0x54Au >> ((pos + 5u) % 12u)) & 1u);
}

static void kbd_insert(char ch)
{
    uint8_t i;
    if (kbd.len >= kbd.max_len) return;
    for (i = kbd.len; i > kbd.cur; i--)
        kbd.buf[i] = kbd.buf[i - 1];
    kbd.buf[kbd.cur] = ch;
    kbd.len++;
    kbd.cur++;
    kbd.buf[kbd.len] = 0;
}

static void kbd_backspace(void)
{
    uint8_t i;
    if (kbd.cur == 0) return;
    kbd.cur--;
    for (i = kbd.cur; i < kbd.len - 1u; i++)
        kbd.buf[i] = kbd.buf[i + 1];
    kbd.len--;
    kbd.buf[kbd.len] = 0;
}

/* --------------------------------------------------------- drawing --- */

static void kbd_draw_piano(int32_t y0)
{
    uint32_t pos, wi = 0, bi = 0;
    int32_t wh = 52, bh = 32, bw = 10;

    for (pos = 0; pos < 27u; pos++) {
        if (kbd_is_black(pos)) continue;
        int32_t x = (int32_t)(wi * 15u);
        uint8_t lit = (kbd.flash_n > 0 && kbd.flash_pos == (uint8_t)pos);
        uint16_t bg = lit ? T_THEME : T_RAISE;
        uint16_t fg = lit ? T_INK : T_TEXT;
        cv_rrect(x + 1, y0, 13, wh, 2, bg, T_BG);
        { char c[2]; c[0] = KBD_MAPS[kbd.layer][pos]; c[1] = 0;
          cv_text_in(x + 1, y0 + wh - 12, 13, &AF_S, c, fg, bg); }
        wi++;
    }

    cv_rect(104, y0, 1, wh, T_LINE);

    for (pos = 0; pos < 27u; pos++) {
        if (!kbd_is_black(pos)) continue;
        int32_t x = (int32_t)KBD_BLK_X[bi];
        uint8_t lit = (kbd.flash_n > 0 && kbd.flash_pos == (uint8_t)pos);
        uint16_t bg = lit ? T_THEME : 0x0821u;
        uint16_t fg = lit ? T_INK : T_MID;
        cv_rect(x, y0, bw, bh, bg);
        { char c[2]; c[0] = KBD_MAPS[kbd.layer][pos]; c[1] = 0;
          cv_text_in(x, y0 + bh - 12, bw, &AF_S, c, fg, bg); }
        bi++;
    }
}

static void kbd_draw(void)
{
    uint32_t gh = Y_FOOT - Y_GRAPH;

    /* --- cards --- */
    {
        char cc[2] = { '_', 0 };
        char ps[4], ln[8];
        uint8_t li = 0;
        if (kbd.cur > 0 && kbd.cur <= kbd.len)
            cc[0] = kbd.hide ? '*' : kbd.buf[kbd.cur - 1];
        fui_fmt_val(ps, (int16_t)kbd.cur);
        if (kbd.len >= 10) ln[li++] = '0' + kbd.len / 10;
        ln[li++] = '0' + kbd.len % 10;
        ln[li++] = '/';
        if (kbd.max_len >= 10) ln[li++] = '0' + kbd.max_len / 10;
        ln[li++] = '0' + kbd.max_len % 10;
        ln[li] = 0;
        ui_card(0, "CHAR", cc, "", T_THEME, -1, ICON_GENERIC);
        ui_card(1, "POS", ps, "",
                T_MID, (int32_t)kbd.cur * 1000 / (kbd.max_len ? kbd.max_len : 1),
                ICON_GENERIC);
        ui_card(2, "MODE", KBD_NAMES[kbd.layer], "", T_ACCENT, -1, ICON_GENERIC);
        ui_card(3, "LEN", ln, "", T_DIM, -1, ICON_GENERIC);
    }

    /* --- graph area --- */
    cv_begin(240, gh, T_BG);

    cv_text_on(6, 2, &AF_S, kbd.title ? kbd.title : "KEYBOARD", T_TEXT, T_BG);
    {
        char ctr[8]; uint8_t ci = 0;
        if (kbd.len >= 10) ctr[ci++] = '0' + kbd.len / 10;
        ctr[ci++] = '0' + kbd.len % 10;
        ctr[ci++] = '/';
        if (kbd.max_len >= 10) ctr[ci++] = '0' + kbd.max_len / 10;
        ctr[ci++] = '0' + kbd.max_len % 10;
        ctr[ci] = 0;
        { int32_t cw = text_w(&AF_S, ctr);
          cv_text_on(234 - cw, 2, &AF_S, ctr, T_DIM, T_BG); }
    }

    /* text field */
    cv_rrect(4, 16, 232, 20, 3, T_SURF, T_BG);
    {
        int32_t tx = 8;
        uint8_t vis = 28, off = 0, i;
        if (kbd.cur > vis - 2)
            off = kbd.cur - vis + 2;
        for (i = off; i < kbd.len && (uint8_t)(i - off) < vis; i++) {
            char c[2]; c[0] = kbd.hide ? '*' : kbd.buf[i]; c[1] = 0;
            int32_t cw = text_w(&AF_S, c);
            if (i == kbd.cur) {
                cv_rect(tx, 18, cw + 2, 14, T_THEME);
                cv_text_on(tx + 1, 19, &AF_S, c, T_INK, T_THEME);
            } else {
                cv_text_on(tx, 19, &AF_S, c, T_TEXT, T_SURF);
            }
            tx += cw + 1;
        }
        if (kbd.cur >= kbd.len)
            cv_rect(tx, 18, 2, 14, T_THEME);
    }

    /* mode bar */
    {
        int32_t mx = 6;
        uint8_t li;
        for (li = 0; li < KBD_N_LAYERS; li++) {
            int32_t tw = text_w(&AF_S, KBD_NAMES[li]);
            int32_t pw = tw + 8;
            uint16_t pbg = (li == kbd.layer) ? T_THEME : T_SURF;
            uint16_t pfg = (li == kbd.layer) ? T_INK   : T_DIM;
            cv_rrect(mx, 40, pw, 12, 3, pbg, T_BG);
            cv_text_on(mx + 4, 42, &AF_S, KBD_NAMES[li], pfg, pbg);
            mx += pw + 4;
        }
        if (kbd.hide) {
            cv_rrect(180, 40, 36, 12, 3, T_ACCENT, T_BG);
            cv_text_on(184, 42, &AF_S, "HIDE", T_INK, T_ACCENT);
        }
    }

    kbd_draw_piano(56);
    cv_blit(0, Y_GRAPH);

    ui_footer("OCT-", "KEYBOARD", "OCT+");

    if (kbd.flash_n > 0) kbd.flash_n--;
    if (kbd.flash_n > 0) nav_dirty = 1;
}

/* ----------------------------------------------------------- input --- */

static int kbd_knob(uint32_t id, int32_t clicks)
{
    switch (id) {
    case ENC_ALGO: {
        int32_t l = (int32_t)kbd.layer + clicks;
        while (l < 0) l += KBD_N_LAYERS;
        kbd.layer = (uint8_t)(l % KBD_N_LAYERS);
        break;
    }
    case ENC_K1: {
        int32_t c = (int32_t)kbd.cur + clicks;
        if (c < 0) c = 0;
        if (c > (int32_t)kbd.len) c = (int32_t)kbd.len;
        kbd.cur = (uint8_t)c;
        break;
    }
    case ENC_K2:
        if (kbd.cur > 0) {
            int32_t ch = (int32_t)(kbd.buf[kbd.cur - 1] - ' ') + clicks;
            while (ch < 0) ch += 95;
            ch %= 95;
            kbd.buf[kbd.cur - 1] = (char)(' ' + ch);
        }
        break;
    case ENC_K3:
        kbd.cur = (clicks < 0) ? 0 : kbd.len;
        break;
    case ENC_K4:
        if (clicks < 0) {
            uint8_t n = kbd.cur > 5u ? 5u : kbd.cur;
            while (n-- > 0) kbd_backspace();
        } else {
            kbd.buf[kbd.cur] = 0;
            kbd.len = kbd.cur;
        }
        break;
    case ENC_SELECT:
        kbd.hide = !kbd.hide;
        break;
    default:
        break;
    }
    nav_dirty = 1;
    return 1;
}

static int kbd_button(uint32_t id, uint32_t down)
{
    if (down) return 1;
    switch (id) {
    case BTN_OCTUP:
        kbd.done = 1;
        if (kbd.on_done) kbd.on_done(kbd.buf, 1);
        nav_modal_close();
        return 1;
    case BTN_OCTDN:
        kbd.done = 2;
        if (kbd.on_done) kbd.on_done(kbd.buf, 0);
        nav_modal_close();
        return 1;
    case BTN_FX:
        kbd_backspace();
        break;
    case BTN_PLAY:
        kbd_insert(' ');
        break;
    case BTN_HOME:
        kbd.len = 0; kbd.cur = 0; kbd.buf[0] = 0;
        break;
    case BTN_REC:
        kbd.hide = !kbd.hide;
        break;
    default:
        break;
    }
    nav_dirty = 1;
    return 1;
}

static int kbd_key(uint32_t note, uint32_t vel)
{
    if (!vel) return 1;
    if (note < 53u || note > 79u) return 1;
    kbd_insert(KBD_MAPS[kbd.layer][note - 53u]);
    kbd.flash_pos = (uint8_t)(note - 53u);
    kbd.flash_n = 6;
    nav_dirty = 1;
    return 1;
}

/* --------------------------------------------------------- public --- */

static const cfm1_modal_t kbd_piano_modal = {
    .draw   = kbd_draw,
    .knob   = kbd_knob,
    .button = kbd_button,
    .key    = kbd_key,
};

static void kbd_piano_start(const char *title, const char *prefill,
                            uint8_t max_len, uint8_t hide,
                            void (*on_done)(const char *, uint8_t))
{
    kbd.len = 0;
    if (prefill) {
        while (prefill[kbd.len] && kbd.len < max_len) {
            kbd.buf[kbd.len] = prefill[kbd.len];
            kbd.len++;
        }
    }
    kbd.buf[kbd.len] = 0;
    kbd.cur = kbd.len;
    kbd.layer = 0;
    kbd.max_len = max_len > KBD_MAX_LEN ? (uint8_t)KBD_MAX_LEN : max_len;
    kbd.hide = hide;
    kbd.done = 0;
    kbd.flash_pos = 0xFFu;
    kbd.flash_n = 0;
    kbd.title = title;
    kbd.on_done = on_done;
    nav_modal_open(&kbd_piano_modal);
}
