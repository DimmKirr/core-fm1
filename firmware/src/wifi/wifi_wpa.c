/* SPDX-License-Identifier: GPL-3.0-only */
/* Clean-room WPA2-PSK from FIPS 197 (AES-128), FIPS 180-4 (SHA-1),
 * RFC 2104 (HMAC), RFC 2898 (PBKDF2), IEEE 802.11i (4-way handshake, CCMP).
 * No vendor code, IR or disassembly. */
#ifdef CONFIG_WIFI_WPA

/* ---- AES-128 (FIPS 197) ---- */

/* The S-box: the well-known 256-byte substitution table from FIPS 197 section 5.1.1,
 * derived from the multiplicative inverse in GF(2^8) followed by an affine transform */
static const uint8_t wifi_aes_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

/* Round constants for key expansion (FIPS 197 section 5.2) */
static const uint8_t wifi_aes_rcon[10] = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

/* xtime: multiply by 2 in GF(2^8) with irreducible polynomial 0x11b */
static uint8_t wifi_aes_xtime(uint8_t x)
{
    return (uint8_t)((x << 1) ^ (((x >> 7) & 1u) * 0x1bu));
}

/* Key expansion: 128-bit key -> 11 round keys (44 words, 176 bytes) */
static void wifi_aes_key_expand(const uint8_t key[16], uint8_t rk[176])
{
    uint32_t i;
    uint8_t t[4];

    for (i = 0; i < 16u; i++)
        rk[i] = key[i];

    for (i = 16; i < 176u; i += 4u) {
        t[0] = rk[i - 4u];
        t[1] = rk[i - 3u];
        t[2] = rk[i - 2u];
        t[3] = rk[i - 1u];

        if ((i & 15u) == 0) {
            uint8_t tmp = t[0];
            t[0] = wifi_aes_sbox[t[1]] ^ wifi_aes_rcon[(i >> 4) - 1u];
            t[1] = wifi_aes_sbox[t[2]];
            t[2] = wifi_aes_sbox[t[3]];
            t[3] = wifi_aes_sbox[tmp];
        }

        rk[i + 0u] = rk[i - 16u] ^ t[0];
        rk[i + 1u] = rk[i - 15u] ^ t[1];
        rk[i + 2u] = rk[i - 14u] ^ t[2];
        rk[i + 3u] = rk[i - 13u] ^ t[3];
    }
}

static void wifi_aes_add_rk(uint8_t s[16], const uint8_t *rk)
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        s[i] ^= rk[i];
}

static void wifi_aes_sub_bytes(uint8_t s[16])
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        s[i] = wifi_aes_sbox[s[i]];
}

/* ShiftRows: state is column-major s[row + 4*col].
 * Row 0: no shift. Row 1: left 1. Row 2: left 2. Row 3: left 3 (= right 1). */
static void wifi_aes_shift_rows(uint8_t s[16])
{
    uint8_t t;
    /* row 1: shift left 1 */
    t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
    /* row 2: shift left 2 */
    t = s[2]; s[2] = s[10]; s[10] = t;
    t = s[6]; s[6] = s[14]; s[14] = t;
    /* row 3: shift left 3 = right 1 */
    t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;
}

/* MixColumns: each column multiplied by the MDS matrix in GF(2^8) */
static void wifi_aes_mix_cols(uint8_t s[16])
{
    uint32_t c;
    for (c = 0; c < 4u; c++) {
        uint8_t *col = s + 4u * c;
        uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
        uint8_t x0 = wifi_aes_xtime(a0), x1 = wifi_aes_xtime(a1);
        uint8_t x2 = wifi_aes_xtime(a2), x3 = wifi_aes_xtime(a3);
        col[0] = x0 ^ x1 ^ a1 ^ a2 ^ a3;
        col[1] = a0 ^ x1 ^ x2 ^ a2 ^ a3;
        col[2] = a0 ^ a1 ^ x2 ^ x3 ^ a3;
        col[3] = x0 ^ a0 ^ a1 ^ a2 ^ x3;
    }
}

/* Encrypt one 16-byte block in place (FIPS 197 section 5.1) */
static void wifi_aes_encrypt(uint8_t state[16], const uint8_t key[16])
{
    uint8_t rk[176];
    uint32_t r;

    wifi_aes_key_expand(key, rk);
    wifi_aes_add_rk(state, rk);

    for (r = 1; r < 10u; r++) {
        wifi_aes_sub_bytes(state);
        wifi_aes_shift_rows(state);
        wifi_aes_mix_cols(state);
        wifi_aes_add_rk(state, rk + 16u * r);
    }
    /* final round: no MixColumns */
    wifi_aes_sub_bytes(state);
    wifi_aes_shift_rows(state);
    wifi_aes_add_rk(state, rk + 160u);
}

/* ---- SHA-1 (FIPS 180-4) ---- */

typedef struct {
    uint32_t h[5];
    uint64_t len;           /* total bits processed */
    uint8_t buf[64];
    uint32_t buf_len;
} wifi_sha1_t;

static uint32_t wifi_sha1_rotl(uint32_t x, uint32_t n)
{
    return (x << n) | (x >> (32u - n));
}

static void wifi_sha1_init(wifi_sha1_t *ctx)
{
    ctx->h[0] = 0x67452301u;
    ctx->h[1] = 0xEFCDAB89u;
    ctx->h[2] = 0x98BADCFEu;
    ctx->h[3] = 0x10325476u;
    ctx->h[4] = 0xC3D2E1F0u;
    ctx->len = 0;
    ctx->buf_len = 0;
}

/* Process one 512-bit (64-byte) block */
static void wifi_sha1_block(wifi_sha1_t *ctx, const uint8_t blk[64])
{
    uint32_t w[80], a, b, c, d, e, f, k, t2;
    uint32_t i;

    for (i = 0; i < 16u; i++)
        w[i] = (uint32_t)blk[4u * i] << 24 | (uint32_t)blk[4u * i + 1u] << 16 |
               (uint32_t)blk[4u * i + 2u] << 8 | blk[4u * i + 3u];
    for (i = 16; i < 80u; i++)
        w[i] = wifi_sha1_rotl(w[i - 3u] ^ w[i - 8u] ^ w[i - 14u] ^ w[i - 16u], 1);

    a = ctx->h[0]; b = ctx->h[1]; c = ctx->h[2]; d = ctx->h[3]; e = ctx->h[4];

    for (i = 0; i < 80u; i++) {
        if (i < 20u)       { f = (b & c) | ((~b) & d);         k = 0x5A827999u; }
        else if (i < 40u)  { f = b ^ c ^ d;                    k = 0x6ED9EBA1u; }
        else if (i < 60u)  { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDCu; }
        else               { f = b ^ c ^ d;                    k = 0xCA62C1D6u; }

        t2 = wifi_sha1_rotl(a, 5) + f + e + k + w[i];
        e = d; d = c; c = wifi_sha1_rotl(b, 30); b = a; a = t2;
    }

    ctx->h[0] += a; ctx->h[1] += b; ctx->h[2] += c; ctx->h[3] += d; ctx->h[4] += e;
}

static void wifi_sha1_update(wifi_sha1_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++) {
        ctx->buf[ctx->buf_len++] = data[i];
        if (ctx->buf_len == 64u) {
            wifi_sha1_block(ctx, ctx->buf);
            ctx->len += 512u;
            ctx->buf_len = 0;
        }
    }
}

static void wifi_sha1_final(wifi_sha1_t *ctx, uint8_t digest[20])
{
    uint64_t bits;
    uint32_t i;

    ctx->len += (uint64_t)ctx->buf_len * 8u;
    bits = ctx->len;

    /* padding: 0x80 byte, then zeros, then 64-bit length big-endian */
    ctx->buf[ctx->buf_len++] = 0x80u;
    if (ctx->buf_len > 56u) {
        while (ctx->buf_len < 64u)
            ctx->buf[ctx->buf_len++] = 0;
        wifi_sha1_block(ctx, ctx->buf);
        ctx->buf_len = 0;
    }
    while (ctx->buf_len < 56u)
        ctx->buf[ctx->buf_len++] = 0;

    for (i = 0; i < 8u; i++)
        ctx->buf[56u + i] = (uint8_t)(bits >> (56u - 8u * i));
    wifi_sha1_block(ctx, ctx->buf);

    for (i = 0; i < 5u; i++) {
        digest[4u * i + 0u] = (uint8_t)(ctx->h[i] >> 24);
        digest[4u * i + 1u] = (uint8_t)(ctx->h[i] >> 16);
        digest[4u * i + 2u] = (uint8_t)(ctx->h[i] >> 8);
        digest[4u * i + 3u] = (uint8_t)(ctx->h[i]);
    }
}

static void wifi_sha1(const uint8_t *data, uint32_t len, uint8_t digest[20])
{
    wifi_sha1_t ctx;
    wifi_sha1_init(&ctx);
    wifi_sha1_update(&ctx, data, len);
    wifi_sha1_final(&ctx, digest);
}

/* ---- HMAC-SHA1 (RFC 2104) ---- */

#define WIFI_SHA1_BLOCK 64u
#define WIFI_SHA1_DIGEST 20u

static void wifi_hmac_sha1(const uint8_t *key, uint32_t key_len,
                           const uint8_t *data, uint32_t data_len,
                           uint8_t out[20])
{
    wifi_sha1_t ctx;
    uint8_t k_pad[WIFI_SHA1_BLOCK];
    uint8_t tk[WIFI_SHA1_DIGEST];
    uint32_t i;

    if (key_len > WIFI_SHA1_BLOCK) {
        wifi_sha1(key, key_len, tk);
        key = tk;
        key_len = WIFI_SHA1_DIGEST;
    }

    /* inner: SHA1(K ^ ipad || data) */
    for (i = 0; i < WIFI_SHA1_BLOCK; i++)
        k_pad[i] = (i < key_len ? key[i] : 0u) ^ 0x36u;

    wifi_sha1_init(&ctx);
    wifi_sha1_update(&ctx, k_pad, WIFI_SHA1_BLOCK);
    wifi_sha1_update(&ctx, data, data_len);
    wifi_sha1_final(&ctx, tk);

    /* outer: SHA1(K ^ opad || inner_hash) */
    for (i = 0; i < WIFI_SHA1_BLOCK; i++)
        k_pad[i] = (i < key_len ? key[i] : 0u) ^ 0x5Cu;

    wifi_sha1_init(&ctx);
    wifi_sha1_update(&ctx, k_pad, WIFI_SHA1_BLOCK);
    wifi_sha1_update(&ctx, tk, WIFI_SHA1_DIGEST);
    wifi_sha1_final(&ctx, out);
}

/* HMAC-SHA1 with two data segments (avoids concatenation buffer) */
static void wifi_hmac_sha1_2(const uint8_t *key, uint32_t key_len,
                             const uint8_t *d1, uint32_t d1_len,
                             const uint8_t *d2, uint32_t d2_len,
                             uint8_t out[20])
{
    wifi_sha1_t ctx;
    uint8_t k_pad[WIFI_SHA1_BLOCK];
    uint8_t tk[WIFI_SHA1_DIGEST];
    uint32_t i;

    if (key_len > WIFI_SHA1_BLOCK) {
        wifi_sha1(key, key_len, tk);
        key = tk;
        key_len = WIFI_SHA1_DIGEST;
    }

    for (i = 0; i < WIFI_SHA1_BLOCK; i++)
        k_pad[i] = (i < key_len ? key[i] : 0u) ^ 0x36u;

    wifi_sha1_init(&ctx);
    wifi_sha1_update(&ctx, k_pad, WIFI_SHA1_BLOCK);
    wifi_sha1_update(&ctx, d1, d1_len);
    wifi_sha1_update(&ctx, d2, d2_len);
    wifi_sha1_final(&ctx, tk);

    for (i = 0; i < WIFI_SHA1_BLOCK; i++)
        k_pad[i] = (i < key_len ? key[i] : 0u) ^ 0x5Cu;

    wifi_sha1_init(&ctx);
    wifi_sha1_update(&ctx, k_pad, WIFI_SHA1_BLOCK);
    wifi_sha1_update(&ctx, tk, WIFI_SHA1_DIGEST);
    wifi_sha1_final(&ctx, out);
}

/* ---- PBKDF2-SHA1 (RFC 2898) ---- */

static void wifi_pbkdf2_sha1(const uint8_t *pass, uint32_t pass_len,
                             const uint8_t *salt, uint32_t salt_len,
                             uint32_t iterations, uint32_t dk_len,
                             uint8_t *out)
{
    uint32_t block = 1, pos = 0;

    while (pos < dk_len) {
        uint8_t u[WIFI_SHA1_DIGEST], t[WIFI_SHA1_DIGEST];
        uint8_t be[4];
        uint32_t j, k, copy;

        /* U1 = HMAC(pass, salt || INT(block)), INT is 4 bytes big-endian */
        be[0] = (uint8_t)(block >> 24);
        be[1] = (uint8_t)(block >> 16);
        be[2] = (uint8_t)(block >> 8);
        be[3] = (uint8_t)(block);

        wifi_hmac_sha1_2(pass, pass_len, salt, salt_len, be, 4, u);
        for (k = 0; k < WIFI_SHA1_DIGEST; k++)
            t[k] = u[k];

        for (j = 1; j < iterations; j++) {
            wifi_hmac_sha1(pass, pass_len, u, WIFI_SHA1_DIGEST, u);
            for (k = 0; k < WIFI_SHA1_DIGEST; k++)
                t[k] ^= u[k];
        }

        copy = dk_len - pos;
        if (copy > WIFI_SHA1_DIGEST)
            copy = WIFI_SHA1_DIGEST;
        for (k = 0; k < copy; k++)
            out[pos + k] = t[k];
        pos += copy;
        block++;
    }
}

/* ---- PRF for PTK derivation (IEEE 802.11i section 8.5.1.1) ---- */

/* PRF(K, A, B) = HMAC-SHA1(K, A || 0x00 || B || i) for i = 0, 1, ... */
static void wifi_prf(const uint8_t *key, uint32_t key_len,
                     const uint8_t *label, uint32_t label_len,
                     const uint8_t *data, uint32_t data_len,
                     uint8_t *out, uint32_t out_bits)
{
    uint32_t out_len = (out_bits + 7u) / 8u;
    uint32_t pos = 0, i = 0;
    uint8_t r[128]; /* label (~25) + 1 + data (76) + 1 */
    uint32_t r_len = 0;
    uint8_t digest[WIFI_SHA1_DIGEST];
    uint32_t q;

    if (label_len + 1u + data_len + 1u > sizeof(r))
        return;

    for (q = 0; q < label_len; q++)
        r[r_len++] = label[q];
    r[r_len++] = 0;
    for (q = 0; q < data_len; q++)
        r[r_len++] = data[q];
    r[r_len] = 0; /* counter byte, updated per iteration */

    while (pos < out_len) {
        uint32_t copy;
        r[r_len] = (uint8_t)i;
        wifi_hmac_sha1(key, key_len, r, r_len + 1u, digest);
        copy = out_len - pos;
        if (copy > WIFI_SHA1_DIGEST)
            copy = WIFI_SHA1_DIGEST;
        for (q = 0; q < copy; q++)
            out[pos + q] = digest[q];
        pos += WIFI_SHA1_DIGEST;
        i++;
    }
}

/* ---- WPA2 state and key derivation ---- */

#define WPA_PMK_LEN 32u
#define WPA_NONCE_LEN 32u
#define WPA_PTK_LEN 48u      /* KCK(16) + KEK(16) + TK(16) for CCMP */
#define WPA_KCK_LEN 16u
#define WPA_KEK_LEN 16u
#define WPA_TK_LEN 16u
#define WPA_MIC_LEN 16u
#define WPA_REPLAY_LEN 8u

typedef struct {
    uint8_t pmk[WPA_PMK_LEN];
    uint8_t ptk[WPA_PTK_LEN];        /* KCK || KEK || TK */
    uint8_t snonce[WPA_NONCE_LEN];
    uint8_t anonce[WPA_NONCE_LEN];
    uint8_t aa[6];                    /* AP MAC (authenticator) */
    uint8_t spa[6];                   /* our MAC (supplicant) */
    uint8_t replay[WPA_REPLAY_LEN];
    uint64_t tx_pn;                   /* CCMP TX packet number */
    uint64_t rx_pn;                   /* CCMP RX packet number */
    uint8_t tk_valid;
    uint8_t step;                     /* handshake step (0=idle, 1..4) */
} wifi_wpa_t;

static wifi_wpa_t wifi_wpa;

/* Simple PRNG for SNonce: xorshift32 seeded from the timer free-run counter.
 * Not cryptographically secure; adequate for a synthesizer's debug WiFi. */
static uint32_t wifi_wpa_prng_state;

static uint32_t wifi_wpa_rand(void)
{
    uint32_t x = wifi_wpa_prng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    wifi_wpa_prng_state = x;
    return x;
}

static void wifi_wpa_gen_nonce(uint8_t nonce[32])
{
    uint32_t i;
    wifi_wpa_prng_state ^= fm1_ticks();
    for (i = 0; i < 8u; i++) {
        uint32_t r = wifi_wpa_rand();
        nonce[4u * i + 0u] = (uint8_t)(r);
        nonce[4u * i + 1u] = (uint8_t)(r >> 8);
        nonce[4u * i + 2u] = (uint8_t)(r >> 16);
        nonce[4u * i + 3u] = (uint8_t)(r >> 24);
    }
}

static int wifi_wpa_cmp(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

/* Derive PTK: PRF-384(PMK, "Pairwise key expansion",
 *   min(AA,SPA) || max(AA,SPA) || min(ANonce,SNonce) || max(ANonce,SNonce)) */
static void wifi_wpa_derive_ptk(void)
{
    static const uint8_t label[] = "Pairwise key expansion";
    uint8_t data[76]; /* 6+6+32+32 */
    const uint8_t *lo_addr, *hi_addr, *lo_nonce, *hi_nonce;
    uint32_t i;

    if (wifi_wpa_cmp(wifi_wpa.aa, wifi_wpa.spa, 6) < 0) {
        lo_addr = wifi_wpa.aa;  hi_addr = wifi_wpa.spa;
    } else {
        lo_addr = wifi_wpa.spa; hi_addr = wifi_wpa.aa;
    }
    if (wifi_wpa_cmp(wifi_wpa.anonce, wifi_wpa.snonce, 32) < 0) {
        lo_nonce = wifi_wpa.anonce; hi_nonce = wifi_wpa.snonce;
    } else {
        lo_nonce = wifi_wpa.snonce; hi_nonce = wifi_wpa.anonce;
    }

    for (i = 0; i < 6u; i++)  data[i] = lo_addr[i];
    for (i = 0; i < 6u; i++)  data[6u + i] = hi_addr[i];
    for (i = 0; i < 32u; i++) data[12u + i] = lo_nonce[i];
    for (i = 0; i < 32u; i++) data[44u + i] = hi_nonce[i];

    wifi_prf(wifi_wpa.pmk, WPA_PMK_LEN,
             label, sizeof(label) - 1u,
             data, 76,
             wifi_wpa.ptk, WPA_PTK_LEN * 8u);
}

/* ---- EAPOL-Key frame handling (IEEE 802.11i) ---- */

#define EAPOL_VER_OFF  0
#define EAPOL_TYPE_OFF 1   /* type: 3 = EAPOL-Key */
#define EAPOL_LEN_OFF  2   /* body length (big-endian 16) */
#define EAPOL_KEY_TYPE 4   /* key descriptor: 2 = RSN */
#define EAPOL_KEY_INFO 5   /* key information (big-endian 16) */
#define EAPOL_KEY_LEN  7   /* key length (big-endian 16) */
#define EAPOL_REPLAY   9   /* replay counter (8 bytes) */
#define EAPOL_NONCE    17  /* nonce (32 bytes) */
#define EAPOL_IV       49  /* key IV (16 bytes) */
#define EAPOL_RSC      65  /* RSC (8 bytes) */
#define EAPOL_RESERVED 73  /* reserved (8 bytes) */
#define EAPOL_MIC      81  /* MIC (16 bytes) */
#define EAPOL_DATA_LEN 97  /* key data length (big-endian 16) */
#define EAPOL_DATA     99  /* key data */
#define EAPOL_HDR_LEN  99  /* minimum EAPOL-Key frame */

/* Key info bits (IEEE 802.11i Table 62) */
#define KI_VER_HMAC_SHA1 0x0002u   /* WPA2: HMAC-SHA1 MIC, AES key wrap */
#define KI_PAIRWISE  0x0008u
#define KI_INSTALL   0x0040u
#define KI_ACK       0x0080u
#define KI_MIC       0x0100u
#define KI_SECURE    0x0200u
#define KI_ENCRYPT   0x1000u

/* Compute MIC: HMAC-SHA1 truncated to 128 bits (WPA2) */
static void wifi_wpa_compute_mic(const uint8_t *eapol, uint32_t len, uint8_t mic[16])
{
    uint8_t hmac[20];
    uint32_t i;
    wifi_hmac_sha1(wifi_wpa.ptk, WPA_KCK_LEN, eapol, len, hmac);
    for (i = 0; i < 16u; i++)
        mic[i] = hmac[i];
}

/* Verify MIC: zero MIC field, compute, compare */
static int wifi_wpa_verify_mic(uint8_t *eapol, uint32_t len)
{
    uint8_t saved[16], computed[16];
    uint32_t i;
    int ok;

    if (len < EAPOL_HDR_LEN)
        return 0;

    for (i = 0; i < 16u; i++) {
        saved[i] = eapol[EAPOL_MIC + i];
        eapol[EAPOL_MIC + i] = 0;
    }
    wifi_wpa_compute_mic(eapol, len, computed);
    for (i = 0; i < 16u; i++)
        eapol[EAPOL_MIC + i] = saved[i];

    ok = 1;
    for (i = 0; i < 16u; i++)
        if (saved[i] != computed[i]) ok = 0;
    return ok;
}

/* Build an EAPOL-Key response frame into buf[]. Returns total frame length. */
static uint32_t wifi_wpa_build_msg(uint8_t *buf, uint32_t buf_size,
                                   uint16_t key_info, uint16_t key_len,
                                   const uint8_t *nonce,
                                   const uint8_t *key_data, uint32_t key_data_len)
{
    uint32_t body_len, total, i;

    body_len = 95u + key_data_len;
    total = 4u + body_len;
    if (total > buf_size)
        return 0;

    for (i = 0; i < total; i++)
        buf[i] = 0;

    buf[EAPOL_VER_OFF] = 2;       /* 802.1X-2004 */
    buf[EAPOL_TYPE_OFF] = 3;      /* EAPOL-Key */
    buf[EAPOL_LEN_OFF] = (uint8_t)(body_len >> 8);
    buf[EAPOL_LEN_OFF + 1u] = (uint8_t)body_len;
    buf[EAPOL_KEY_TYPE] = 2;      /* RSN */
    buf[EAPOL_KEY_INFO] = (uint8_t)(key_info >> 8);
    buf[EAPOL_KEY_INFO + 1u] = (uint8_t)key_info;
    buf[EAPOL_KEY_LEN] = (uint8_t)(key_len >> 8);
    buf[EAPOL_KEY_LEN + 1u] = (uint8_t)key_len;

    for (i = 0; i < WPA_REPLAY_LEN; i++)
        buf[EAPOL_REPLAY + i] = wifi_wpa.replay[i];

    if (nonce)
        for (i = 0; i < WPA_NONCE_LEN; i++)
            buf[EAPOL_NONCE + i] = nonce[i];

    buf[EAPOL_DATA_LEN] = (uint8_t)(key_data_len >> 8);
    buf[EAPOL_DATA_LEN + 1u] = (uint8_t)key_data_len;
    if (key_data && key_data_len)
        for (i = 0; i < key_data_len; i++)
            buf[EAPOL_DATA + i] = key_data[i];

    if (key_info & KI_MIC) {
        uint8_t mic[16];
        wifi_wpa_compute_mic(buf, total, mic);
        for (i = 0; i < 16u; i++)
            buf[EAPOL_MIC + i] = mic[i];
    }

    return total;
}

/* Message 1: AP -> STA (ANonce, ACK, no MIC).
 * We generate SNonce, derive PTK, reply with message 2. */
static int wifi_wpa_rx_msg1(uint8_t *eapol, uint32_t len,
                            uint8_t *tx_buf, uint32_t tx_size, uint32_t *tx_len)
{
    uint32_t i;
    uint16_t ki;

    if (len < EAPOL_HDR_LEN)
        return -1;

    ki = (uint16_t)eapol[EAPOL_KEY_INFO] << 8 | eapol[EAPOL_KEY_INFO + 1u];
    if (!(ki & KI_ACK) || (ki & KI_MIC))
        return -1;

    for (i = 0; i < WPA_NONCE_LEN; i++)
        wifi_wpa.anonce[i] = eapol[EAPOL_NONCE + i];
    for (i = 0; i < WPA_REPLAY_LEN; i++)
        wifi_wpa.replay[i] = eapol[EAPOL_REPLAY + i];

    wifi_wpa_gen_nonce(wifi_wpa.snonce);
    wifi_wpa_derive_ptk();

    *tx_len = wifi_wpa_build_msg(tx_buf, tx_size,
                                 KI_VER_HMAC_SHA1 | KI_PAIRWISE | KI_MIC,
                                 16, wifi_wpa.snonce, NULL, 0);
    wifi_wpa.step = 2;
    return *tx_len ? 0 : -1;
}

/* Message 3: AP -> STA (ANonce, MIC, Install+Secure+ACK).
 * Verify MIC, install TK, reply with message 4. */
static int wifi_wpa_rx_msg3(uint8_t *eapol, uint32_t len,
                            uint8_t *tx_buf, uint32_t tx_size, uint32_t *tx_len)
{
    uint32_t i;
    uint16_t ki;

    if (len < EAPOL_HDR_LEN)
        return -1;

    ki = (uint16_t)eapol[EAPOL_KEY_INFO] << 8 | eapol[EAPOL_KEY_INFO + 1u];
    if (!(ki & KI_ACK) || !(ki & KI_MIC) || !(ki & KI_INSTALL) || !(ki & KI_SECURE))
        return -1;

    if (!wifi_wpa_verify_mic(eapol, len))
        return -1;

    for (i = 0; i < WPA_REPLAY_LEN; i++)
        wifi_wpa.replay[i] = eapol[EAPOL_REPLAY + i];

    wifi_wpa.tk_valid = 1;
    wifi_wpa.tx_pn = 0;
    wifi_wpa.rx_pn = 0;

    *tx_len = wifi_wpa_build_msg(tx_buf, tx_size,
                                 KI_VER_HMAC_SHA1 | KI_PAIRWISE | KI_MIC | KI_SECURE,
                                 16, NULL, NULL, 0);
    wifi_wpa.step = 4;
    return *tx_len ? 0 : -1;
}

/* Receive EAPOL-Key, dispatch to msg1 or msg3. After successful msg3,
 * wifi_wpa.tk_valid == 1 and the TK is at ptk+32. Returns 0 on success. */
static int wifi_wpa_rx_eapol(uint8_t *eapol, uint32_t len,
                             uint8_t *tx_buf, uint32_t tx_size, uint32_t *tx_len)
{
    uint16_t ki;

    *tx_len = 0;
    if (len < EAPOL_HDR_LEN)
        return -1;
    if (eapol[EAPOL_TYPE_OFF] != 3 || eapol[EAPOL_KEY_TYPE] != 2)
        return -1;

    ki = (uint16_t)eapol[EAPOL_KEY_INFO] << 8 | eapol[EAPOL_KEY_INFO + 1u];

    if ((ki & KI_ACK) && !(ki & KI_MIC))
        return wifi_wpa_rx_msg1(eapol, len, tx_buf, tx_size, tx_len);
    if ((ki & KI_ACK) && (ki & KI_MIC) && (ki & KI_INSTALL))
        return wifi_wpa_rx_msg3(eapol, len, tx_buf, tx_size, tx_len);

    return -1;
}

/* Init: derive PMK from passphrase + SSID, set MAC addresses */
static void wifi_wpa_init(const uint8_t *pass, uint32_t pass_len,
                          const uint8_t *ssid, uint32_t ssid_len,
                          const uint8_t our_mac[6], const uint8_t ap_mac[6])
{
    uint32_t i;

    wifi_wpa.step = 0;
    wifi_wpa.tk_valid = 0;
    wifi_wpa.tx_pn = 0;
    wifi_wpa.rx_pn = 0;

    for (i = 0; i < 6u; i++) {
        wifi_wpa.spa[i] = our_mac[i];
        wifi_wpa.aa[i] = ap_mac[i];
    }

    wifi_pbkdf2_sha1(pass, pass_len, ssid, ssid_len,
                     4096u, WPA_PMK_LEN, wifi_wpa.pmk);
}

static void wifi_wpa_reset(void)
{
    uint32_t i;
    for (i = 0; i < WPA_PMK_LEN; i++) wifi_wpa.pmk[i] = 0;
    for (i = 0; i < WPA_PTK_LEN; i++) wifi_wpa.ptk[i] = 0;
    wifi_wpa.tk_valid = 0;
    wifi_wpa.step = 0;
    wifi_wpa.tx_pn = 0;
    wifi_wpa.rx_pn = 0;
}

/* ---- CCMP (AES-CCM, IEEE 802.11i section 8.3.3) ---- */

static void wifi_ccmp_aes_block(const uint8_t key[16],
                                const uint8_t in[16],
                                uint8_t out[16])
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        out[i] = in[i];
    wifi_aes_encrypt(out, key);
}

/* CCMP nonce: Priority(1) || A2(6) || PN(6) = 13 bytes */
static void wifi_ccmp_nonce(uint8_t nonce[13],
                            uint8_t priority,
                            const uint8_t a2[6],
                            uint64_t pn)
{
    nonce[0] = priority;
    nonce[1] = a2[0]; nonce[2] = a2[1]; nonce[3] = a2[2];
    nonce[4] = a2[3]; nonce[5] = a2[4]; nonce[6] = a2[5];
    nonce[7]  = (uint8_t)(pn >> 40);
    nonce[8]  = (uint8_t)(pn >> 32);
    nonce[9]  = (uint8_t)(pn >> 24);
    nonce[10] = (uint8_t)(pn >> 16);
    nonce[11] = (uint8_t)(pn >> 8);
    nonce[12] = (uint8_t)(pn);
}

/* CBC-MAC: compute 8-byte MIC over AAD + plaintext.
 * B0 flags = 0x59: Adata=1, M=8 (M-2)/2=3, L=2 (L-1=1). */
static void wifi_ccmp_cbc_mac(const uint8_t tk[16],
                              const uint8_t nonce[13],
                              const uint8_t *aad, uint32_t aad_len,
                              const uint8_t *payload, uint32_t payload_len,
                              uint8_t mic[8])
{
    uint8_t b[16], x[16];
    uint32_t i, j;

    /* B0 */
    b[0] = 0x59u;
    for (i = 0; i < 13u; i++)
        b[1u + i] = nonce[i];
    b[14] = (uint8_t)(payload_len >> 8);
    b[15] = (uint8_t)(payload_len);
    wifi_ccmp_aes_block(tk, b, x);

    /* AAD: 2-byte length prefix then AAD bytes, padded to 16-byte blocks */
    b[0] = (uint8_t)(aad_len >> 8);
    b[1] = (uint8_t)(aad_len);
    j = 2;
    i = 0;
    while (j < 16u) {
        b[j] = (i < aad_len) ? aad[i++] : 0;
        j++;
    }
    for (j = 0; j < 16u; j++)
        x[j] ^= b[j];
    wifi_ccmp_aes_block(tk, x, x);

    while (i < aad_len) {
        for (j = 0; j < 16u; j++)
            b[j] = (i < aad_len) ? aad[i++] : 0;
        for (j = 0; j < 16u; j++)
            x[j] ^= b[j];
        wifi_ccmp_aes_block(tk, x, x);
    }

    /* payload blocks */
    i = 0;
    while (i < payload_len) {
        for (j = 0; j < 16u; j++)
            b[j] = (i + j < payload_len) ? payload[i + j] : 0;
        for (j = 0; j < 16u; j++)
            x[j] ^= b[j];
        wifi_ccmp_aes_block(tk, x, x);
        i += 16u;
    }

    for (i = 0; i < 8u; i++)
        mic[i] = x[i];
}

/* Counter-mode encrypt/decrypt. flags = 0x01 (L=2). Counter 1+ for data, 0 for MIC. */
static void wifi_ccmp_ctr(const uint8_t tk[16],
                          const uint8_t nonce[13],
                          uint8_t *data, uint32_t data_len,
                          uint16_t start_ctr)
{
    uint8_t a[16], s[16];
    uint32_t i, j, blk_len;
    uint16_t ctr = start_ctr;

    a[0] = 0x01u;
    for (i = 0; i < 13u; i++)
        a[1u + i] = nonce[i];

    i = 0;
    while (i < data_len) {
        a[14] = (uint8_t)(ctr >> 8);
        a[15] = (uint8_t)(ctr);
        wifi_ccmp_aes_block(tk, a, s);

        blk_len = data_len - i;
        if (blk_len > 16u) blk_len = 16u;
        for (j = 0; j < blk_len; j++)
            data[i + j] ^= s[j];

        i += blk_len;
        ctr++;
    }
}

/* Encrypt: plaintext in payload[], 8-byte MIC appended (caller provides room).
 * hdr = 802.11 header (AAD), A2 at hdr+10. Returns payload_len + 8. */
static uint32_t wifi_ccmp_encrypt(const uint8_t *hdr, uint32_t hdr_len,
                                  uint8_t *payload, uint32_t payload_len,
                                  const uint8_t tk[16], uint64_t pn)
{
    uint8_t nonce[13], mic[8], a0[16], s0[16];
    uint32_t i;

    wifi_ccmp_nonce(nonce, 0, hdr + 10u, pn);

    wifi_ccmp_cbc_mac(tk, nonce, hdr, hdr_len, payload, payload_len, mic);

    wifi_ccmp_ctr(tk, nonce, payload, payload_len, 1);

    /* encrypt MIC with counter 0 */
    a0[0] = 0x01u;
    for (i = 0; i < 13u; i++)
        a0[1u + i] = nonce[i];
    a0[14] = 0; a0[15] = 0;
    wifi_ccmp_aes_block(tk, a0, s0);
    for (i = 0; i < 8u; i++)
        mic[i] ^= s0[i];

    for (i = 0; i < 8u; i++)
        payload[payload_len + i] = mic[i];

    return payload_len + 8u;
}

/* Decrypt: ciphertext + 8-byte MIC in payload[]. enc_len includes MIC.
 * Returns 0 on success (MIC ok), -1 on failure. */
static int wifi_ccmp_decrypt(const uint8_t *hdr, uint32_t hdr_len,
                             uint8_t *payload, uint32_t enc_len,
                             const uint8_t tk[16], uint64_t pn)
{
    uint8_t nonce[13], mic_enc[8], mic_calc[8], a0[16], s0[16];
    uint32_t data_len, i;

    if (enc_len < 8u)
        return -1;
    data_len = enc_len - 8u;

    wifi_ccmp_nonce(nonce, 0, hdr + 10u, pn);

    for (i = 0; i < 8u; i++)
        mic_enc[i] = payload[data_len + i];

    /* decrypt MIC with counter 0 */
    a0[0] = 0x01u;
    for (i = 0; i < 13u; i++)
        a0[1u + i] = nonce[i];
    a0[14] = 0; a0[15] = 0;
    wifi_ccmp_aes_block(tk, a0, s0);
    for (i = 0; i < 8u; i++)
        mic_enc[i] ^= s0[i];

    /* decrypt payload with counter 1+ */
    wifi_ccmp_ctr(tk, nonce, payload, data_len, 1);

    /* verify MIC over decrypted plaintext */
    wifi_ccmp_cbc_mac(tk, nonce, hdr, hdr_len, payload, data_len, mic_calc);

    for (i = 0; i < 8u; i++)
        if (mic_enc[i] != mic_calc[i])
            return -1;
    return 0;
}

static const uint8_t *wifi_wpa_get_tk(void)
{
    if (!wifi_wpa.tk_valid)
        return (const uint8_t *)0;
    return wifi_wpa.ptk + 32u;
}

static uint64_t wifi_wpa_next_pn(void)
{
    return wifi_wpa.tx_pn++;
}

#endif /* CONFIG_WIFI_WPA */
