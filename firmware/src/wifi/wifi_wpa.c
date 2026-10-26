/* SPDX-License-Identifier: GPL-3.0-only */
/* WPA2-PSK: 4-way handshake, CCMP encryption/decryption.
 * AES-128: FIPS 197. SHA-1: FIPS 180-4. HMAC: RFC 2104.
 * PBKDF2: RFC 2898. PRF: IEEE 802.11i. CCMP: IEEE 802.11i. */

/* ======================================================================
 * AES-128 block cipher
 * ====================================================================== */

static const uint8_t aes_sbox[256] = {
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
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16,
};

static const uint8_t aes_rcon[10] = {
    0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36
};

struct aes_ctx {
    uint32_t rk[44]; /* round keys for AES-128: 4*(10+1) */
};

static void aes_key_expand(struct aes_ctx *ctx, const uint8_t key[16])
{
    for (int i = 0; i < 4; i++)
        ctx->rk[i] = ((uint32_t)key[4*i] << 24) | ((uint32_t)key[4*i+1] << 16) |
                      ((uint32_t)key[4*i+2] << 8) | key[4*i+3];
    for (int i = 4; i < 44; i++) {
        uint32_t t = ctx->rk[i-1];
        if ((i & 3) == 0) {
            t = ((uint32_t)aes_sbox[(t >> 16) & 0xFF] << 24) |
                ((uint32_t)aes_sbox[(t >>  8) & 0xFF] << 16) |
                ((uint32_t)aes_sbox[ t        & 0xFF] <<  8) |
                 (uint32_t)aes_sbox[(t >> 24) & 0xFF];
            t ^= (uint32_t)aes_rcon[(i >> 2) - 1] << 24;
        }
        ctx->rk[i] = ctx->rk[i-4] ^ t;
    }
}

static uint8_t aes_xtime(uint8_t x)
{
    return (uint8_t)((x << 1) ^ (((x >> 7) & 1) * 0x1B));
}

static void aes_encrypt_block(const struct aes_ctx *ctx, const uint8_t in[16], uint8_t out[16])
{
    uint8_t s[16];
    for (int i = 0; i < 16; i++) s[i] = in[i];

    /* AddRoundKey(0) */
    for (int i = 0; i < 4; i++) {
        uint32_t w = ctx->rk[i];
        s[4*i]   ^= (uint8_t)(w >> 24);
        s[4*i+1] ^= (uint8_t)(w >> 16);
        s[4*i+2] ^= (uint8_t)(w >>  8);
        s[4*i+3] ^= (uint8_t)(w);
    }

    for (int round = 1; round <= 10; round++) {
        uint8_t t[16];

        /* SubBytes */
        for (int i = 0; i < 16; i++)
            t[i] = aes_sbox[s[i]];

        /* ShiftRows */
        s[0]  = t[0];  s[1]  = t[5];  s[2]  = t[10]; s[3]  = t[15];
        s[4]  = t[4];  s[5]  = t[9];  s[6]  = t[14]; s[7]  = t[3];
        s[8]  = t[8];  s[9]  = t[13]; s[10] = t[2];  s[11] = t[7];
        s[12] = t[12]; s[13] = t[1];  s[14] = t[6];  s[15] = t[11];

        /* MixColumns (skip on final round) */
        if (round < 10) {
            for (int c = 0; c < 4; c++) {
                int j = 4 * c;
                uint8_t a0 = s[j], a1 = s[j+1], a2 = s[j+2], a3 = s[j+3];
                uint8_t x01 = aes_xtime(a0), x11 = aes_xtime(a1);
                uint8_t x21 = aes_xtime(a2), x31 = aes_xtime(a3);
                s[j]   = x01 ^ x11 ^ a1 ^ a2 ^ a3;
                s[j+1] = a0 ^ x11 ^ x21 ^ a2 ^ a3;
                s[j+2] = a0 ^ a1 ^ x21 ^ x31 ^ a3;
                s[j+3] = x01 ^ a0 ^ a1 ^ a2 ^ x31;
            }
        }

        /* AddRoundKey */
        for (int i = 0; i < 4; i++) {
            uint32_t w = ctx->rk[round * 4 + i];
            s[4*i]   ^= (uint8_t)(w >> 24);
            s[4*i+1] ^= (uint8_t)(w >> 16);
            s[4*i+2] ^= (uint8_t)(w >>  8);
            s[4*i+3] ^= (uint8_t)(w);
        }
    }

    for (int i = 0; i < 16; i++) out[i] = s[i];
}

/* ======================================================================
 * SHA-1 (FIPS 180-4)
 * ====================================================================== */

struct sha1_ctx {
    uint32_t h[5];
    uint8_t  buf[64];
    uint32_t buf_len;
    uint64_t total_len;
};

static uint32_t sha1_rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

static void sha1_init(struct sha1_ctx *c)
{
    c->h[0] = 0x67452301u;
    c->h[1] = 0xEFCDAB89u;
    c->h[2] = 0x98BADCFEu;
    c->h[3] = 0x10325476u;
    c->h[4] = 0xC3D2E1F0u;
    c->buf_len = 0;
    c->total_len = 0;
}

static void sha1_process_block(struct sha1_ctx *c, const uint8_t block[64])
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)block[4*i] << 24) | ((uint32_t)block[4*i+1] << 16) |
               ((uint32_t)block[4*i+2] << 8) | block[4*i+3];
    for (int i = 16; i < 80; i++)
        w[i] = sha1_rotl(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

    uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4];

    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & cc) | (~b & d);          k = 0x5A827999u; }
        else if (i < 40) { f = b ^ cc ^ d;                   k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDCu; }
        else              { f = b ^ cc ^ d;                   k = 0xCA62C1D6u; }
        uint32_t tmp = sha1_rotl(a, 5) + f + e + k + w[i];
        e = d; d = cc; cc = sha1_rotl(b, 30); b = a; a = tmp;
    }

    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}

static void sha1_update(struct sha1_ctx *c, const uint8_t *data, uint32_t len)
{
    c->total_len += len;
    while (len > 0) {
        uint32_t space = 64 - c->buf_len;
        uint32_t copy = (len < space) ? len : space;
        for (uint32_t i = 0; i < copy; i++)
            c->buf[c->buf_len + i] = data[i];
        c->buf_len += copy;
        data += copy;
        len -= copy;
        if (c->buf_len == 64) {
            sha1_process_block(c, c->buf);
            c->buf_len = 0;
        }
    }
}

static void sha1_final(struct sha1_ctx *c, uint8_t digest[20])
{
    uint64_t bits = c->total_len * 8;
    uint8_t pad = 0x80;
    sha1_update(c, &pad, 1);
    pad = 0;
    while (c->buf_len != 56)
        sha1_update(c, &pad, 1);
    uint8_t len_be[8];
    for (int i = 7; i >= 0; i--) { len_be[i] = (uint8_t)bits; bits >>= 8; }
    sha1_update(c, len_be, 8);
    for (int i = 0; i < 5; i++) {
        digest[4*i]   = (uint8_t)(c->h[i] >> 24);
        digest[4*i+1] = (uint8_t)(c->h[i] >> 16);
        digest[4*i+2] = (uint8_t)(c->h[i] >>  8);
        digest[4*i+3] = (uint8_t)(c->h[i]);
    }
}

static void sha1_hash(const uint8_t *data, uint32_t len, uint8_t digest[20])
{
    struct sha1_ctx c;
    sha1_init(&c);
    sha1_update(&c, data, len);
    sha1_final(&c, digest);
}

/* ======================================================================
 * HMAC-SHA1 (RFC 2104)
 * ====================================================================== */

#define SHA1_BLOCK_SIZE 64
#define SHA1_DIGEST_SIZE 20

static void hmac_sha1(const uint8_t *key, uint32_t key_len,
                      const uint8_t *msg, uint32_t msg_len,
                      uint8_t out[20])
{
    uint8_t k_pad[SHA1_BLOCK_SIZE];
    uint8_t tk[SHA1_DIGEST_SIZE];

    /* if key > block size, hash it first */
    if (key_len > SHA1_BLOCK_SIZE) {
        sha1_hash(key, key_len, tk);
        key = tk;
        key_len = SHA1_DIGEST_SIZE;
    }

    /* ipad */
    for (uint32_t i = 0; i < SHA1_BLOCK_SIZE; i++)
        k_pad[i] = (i < key_len ? key[i] : 0) ^ 0x36;

    struct sha1_ctx c;
    sha1_init(&c);
    sha1_update(&c, k_pad, SHA1_BLOCK_SIZE);
    sha1_update(&c, msg, msg_len);
    uint8_t inner[SHA1_DIGEST_SIZE];
    sha1_final(&c, inner);

    /* opad */
    for (uint32_t i = 0; i < SHA1_BLOCK_SIZE; i++)
        k_pad[i] = (i < key_len ? key[i] : 0) ^ 0x5C;

    sha1_init(&c);
    sha1_update(&c, k_pad, SHA1_BLOCK_SIZE);
    sha1_update(&c, inner, SHA1_DIGEST_SIZE);
    sha1_final(&c, out);
}

/* ======================================================================
 * PBKDF2-SHA1 (RFC 2898)
 * ====================================================================== */

static void wifi_pbkdf2_sha1(const char *passphrase, const uint8_t *ssid, uint8_t ssid_len,
                              uint32_t iterations, uint8_t *psk, uint32_t psk_len)
{
    uint32_t pass_len = 0;
    while (passphrase[pass_len]) pass_len++;

    uint32_t pos = 0;
    for (uint32_t block = 1; pos < psk_len; block++) {
        /* U1 = HMAC-SHA1(password, salt || INT(block)) */
        uint8_t salt_block[36]; /* max ssid 32 + 4 bytes block index */
        for (uint32_t i = 0; i < ssid_len; i++)
            salt_block[i] = ssid[i];
        salt_block[ssid_len]     = (uint8_t)(block >> 24);
        salt_block[ssid_len + 1] = (uint8_t)(block >> 16);
        salt_block[ssid_len + 2] = (uint8_t)(block >>  8);
        salt_block[ssid_len + 3] = (uint8_t)(block);

        uint8_t u[SHA1_DIGEST_SIZE];
        uint8_t result[SHA1_DIGEST_SIZE];
        hmac_sha1((const uint8_t *)passphrase, pass_len,
                  salt_block, ssid_len + 4, u);
        for (int i = 0; i < SHA1_DIGEST_SIZE; i++)
            result[i] = u[i];

        /* U2..Uc */
        for (uint32_t iter = 1; iter < iterations; iter++) {
            uint8_t u_next[SHA1_DIGEST_SIZE];
            hmac_sha1((const uint8_t *)passphrase, pass_len,
                      u, SHA1_DIGEST_SIZE, u_next);
            for (int i = 0; i < SHA1_DIGEST_SIZE; i++) {
                u[i] = u_next[i];
                result[i] ^= u_next[i];
            }
        }

        /* copy to output */
        for (int i = 0; i < SHA1_DIGEST_SIZE && pos < psk_len; i++, pos++)
            psk[pos] = result[i];
    }
}

/* ======================================================================
 * PRF-384 (IEEE 802.11i) for PTK derivation
 * ====================================================================== */

static void wifi_prf384(const uint8_t *key, uint32_t key_len,
                        const char *label,
                        const uint8_t *data, uint32_t data_len,
                        uint8_t *output, uint32_t output_len)
{
    uint32_t label_len = 0;
    while (label[label_len]) label_len++;

    /* msg = label || 0x00 || data || counter */
    /* build in parts using hmac_sha1 directly to avoid large temp buffer */
    uint32_t pos = 0;
    for (uint8_t counter = 0; pos < output_len; counter++) {
        /* full message: label \0 data counter */
        uint8_t msg[128];
        uint32_t mlen = 0;
        for (uint32_t i = 0; i < label_len; i++)
            msg[mlen++] = (uint8_t)label[i];
        msg[mlen++] = 0;
        for (uint32_t i = 0; i < data_len && mlen < sizeof(msg) - 1; i++)
            msg[mlen++] = data[i];
        msg[mlen++] = counter;

        uint8_t hash[SHA1_DIGEST_SIZE];
        hmac_sha1(key, key_len, msg, mlen, hash);

        for (int i = 0; i < SHA1_DIGEST_SIZE && pos < output_len; i++, pos++)
            output[pos] = hash[i];
    }
}

/* ======================================================================
 * AES Key Wrap (RFC 3394) for GTK unwrap
 * ====================================================================== */

/* AES-128 decrypt block (for key unwrap) */

static const uint8_t aes_inv_sbox[256] = {
    0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
    0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
    0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
    0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
    0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
    0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
    0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
    0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
    0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
    0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
    0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
    0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
    0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
    0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
    0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
    0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d,
};

static uint8_t aes_mul(uint8_t a, uint8_t b)
{
    uint8_t p = 0;
    for (int i = 0; i < 8; i++) {
        if (b & 1) p ^= a;
        uint8_t hi = a & 0x80;
        a <<= 1;
        if (hi) a ^= 0x1B;
        b >>= 1;
    }
    return p;
}

static void aes_decrypt_block(const struct aes_ctx *ctx, const uint8_t in[16], uint8_t out[16])
{
    uint8_t s[16];
    for (int i = 0; i < 16; i++) s[i] = in[i];

    /* AddRoundKey(10) */
    for (int i = 0; i < 4; i++) {
        uint32_t w = ctx->rk[40 + i];
        s[4*i]   ^= (uint8_t)(w >> 24);
        s[4*i+1] ^= (uint8_t)(w >> 16);
        s[4*i+2] ^= (uint8_t)(w >>  8);
        s[4*i+3] ^= (uint8_t)(w);
    }

    for (int round = 9; round >= 0; round--) {
        /* InvShiftRows */
        uint8_t t[16];
        t[0]  = s[0];  t[1]  = s[13]; t[2]  = s[10]; t[3]  = s[7];
        t[4]  = s[4];  t[5]  = s[1];  t[6]  = s[14]; t[7]  = s[11];
        t[8]  = s[8];  t[9]  = s[5];  t[10] = s[2];  t[11] = s[15];
        t[12] = s[12]; t[13] = s[9];  t[14] = s[6];  t[15] = s[3];

        /* InvSubBytes */
        for (int i = 0; i < 16; i++)
            s[i] = aes_inv_sbox[t[i]];

        /* AddRoundKey */
        for (int i = 0; i < 4; i++) {
            uint32_t w = ctx->rk[round * 4 + i];
            s[4*i]   ^= (uint8_t)(w >> 24);
            s[4*i+1] ^= (uint8_t)(w >> 16);
            s[4*i+2] ^= (uint8_t)(w >>  8);
            s[4*i+3] ^= (uint8_t)(w);
        }

        /* InvMixColumns (skip on round 0) */
        if (round > 0) {
            for (int c = 0; c < 4; c++) {
                int j = 4 * c;
                uint8_t a0 = s[j], a1 = s[j+1], a2 = s[j+2], a3 = s[j+3];
                s[j]   = aes_mul(a0,0x0E) ^ aes_mul(a1,0x0B) ^ aes_mul(a2,0x0D) ^ aes_mul(a3,0x09);
                s[j+1] = aes_mul(a0,0x09) ^ aes_mul(a1,0x0E) ^ aes_mul(a2,0x0B) ^ aes_mul(a3,0x0D);
                s[j+2] = aes_mul(a0,0x0D) ^ aes_mul(a1,0x09) ^ aes_mul(a2,0x0E) ^ aes_mul(a3,0x0B);
                s[j+3] = aes_mul(a0,0x0B) ^ aes_mul(a1,0x0D) ^ aes_mul(a2,0x09) ^ aes_mul(a3,0x0E);
            }
        }
    }

    for (int i = 0; i < 16; i++) out[i] = s[i];
}

/* Now fix aes_unwrap to use aes_decrypt_block properly */
static int aes_key_unwrap(const uint8_t kek[16], const uint8_t *cipher,
                          uint32_t n_blocks, uint8_t *plain)
{
    struct aes_ctx aes;
    aes_key_expand(&aes, kek);

    uint8_t a[8];
    for (int i = 0; i < 8; i++) a[i] = cipher[i];
    for (uint32_t i = 0; i < n_blocks; i++)
        for (int j = 0; j < 8; j++)
            plain[8*i + j] = cipher[8*(i+1) + j];

    for (int j = 5; j >= 0; j--) {
        for (int i = (int)n_blocks - 1; i >= 0; i--) {
            uint32_t t_val = (uint32_t)(n_blocks * (uint32_t)j + (uint32_t)i + 1);
            uint8_t blk[16], dec[16];
            for (int k = 0; k < 8; k++) blk[k] = a[k];
            blk[7] ^= (uint8_t)(t_val);
            blk[6] ^= (uint8_t)(t_val >> 8);
            blk[5] ^= (uint8_t)(t_val >> 16);
            blk[4] ^= (uint8_t)(t_val >> 24);
            for (int k = 0; k < 8; k++) blk[8+k] = plain[8*i + k];

            aes_decrypt_block(&aes, blk, dec);

            for (int k = 0; k < 8; k++) a[k] = dec[k];
            for (int k = 0; k < 8; k++) plain[8*i + k] = dec[8+k];
        }
    }

    for (int i = 0; i < 8; i++)
        if (a[i] != 0xA6) return -1;
    return 0;
}

/* ======================================================================
 * CCMP (AES-CCM, IEEE 802.11i)
 * ====================================================================== */

static uint64_t wifi_ccmp_pn; /* packet number (replay counter) */

/* Build CCM nonce: priority(1) + addr2(6) + PN(6) = 13 bytes */
static void ccmp_build_nonce(uint8_t nonce[13], uint8_t priority,
                             const uint8_t *addr2, uint64_t pn)
{
    nonce[0] = priority;
    for (int i = 0; i < 6; i++) nonce[1+i] = addr2[i];
    nonce[7]  = (uint8_t)(pn >> 40);
    nonce[8]  = (uint8_t)(pn >> 32);
    nonce[9]  = (uint8_t)(pn >> 24);
    nonce[10] = (uint8_t)(pn >> 16);
    nonce[11] = (uint8_t)(pn >>  8);
    nonce[12] = (uint8_t)(pn);
}

/* Build AAD from 802.11 header for CCMP MIC computation */
static uint32_t ccmp_build_aad(const uint8_t *hdr, uint32_t hdr_len, uint8_t *aad)
{
    /* AAD: FC(2, masked) + A1(6) + A2(6) + A3(6) + SC(2, seq masked) [+ A4(6)] */
    uint16_t fc = (uint16_t)hdr[0] | ((uint16_t)hdr[1] << 8);
    /* mask: clear subtype bits 4-6, retry, PM, more data, order */
    fc &= 0xC78F;

    uint32_t pos = 0;
    /* length placeholder (filled at end) */
    aad[pos++] = 0; aad[pos++] = 0;
    aad[pos++] = (uint8_t)(fc); aad[pos++] = (uint8_t)(fc >> 8);
    /* A1, A2, A3 */
    for (int i = 0; i < 18; i++) aad[pos++] = hdr[4 + i];
    /* SC with fragment number only (zero seq) */
    aad[pos++] = hdr[22] & 0x0F; aad[pos++] = 0;

    int has_a4 = (fc & 0x0300) == 0x0300; /* toDS and fromDS both set */
    if (has_a4 && hdr_len >= 30) {
        for (int i = 0; i < 6; i++) aad[pos++] = hdr[24 + i];
    }

    /* fill length */
    uint16_t aad_len = (uint16_t)(pos - 2);
    aad[0] = (uint8_t)(aad_len >> 8);
    aad[1] = (uint8_t)(aad_len);
    return pos;
}

/* XOR 16 bytes: dst ^= src */
static void xor_block(uint8_t *dst, const uint8_t *src)
{
    for (int i = 0; i < 16; i++) dst[i] ^= src[i];
}

/* AES-CCM encrypt (802.11i CCMP): in-place, appends 8-byte MIC */
static int wifi_ccmp_encrypt(const uint8_t *tk, const uint8_t *hdr, uint32_t hdr_len,
                             uint8_t *data, uint32_t data_len)
{
    struct aes_ctx aes;
    aes_key_expand(&aes, tk);

    uint64_t pn = ++wifi_ccmp_pn;
    uint8_t nonce[13];
    ccmp_build_nonce(nonce, 0, hdr + 10, pn); /* addr2 at offset 10 in 802.11 hdr */

    uint8_t aad[32];
    uint32_t aad_len = ccmp_build_aad(hdr, hdr_len, aad);

    /* --- CBC-MAC for MIC --- */
    /* B0: flags(1) + nonce(13) + length(2) */
    uint8_t b[16];
    b[0] = 0x59; /* Adata=1, M=8 => (M-2)/2=3 => 0x40|0x08|0x01, L=2 => L-1=1 */
    for (int i = 0; i < 13; i++) b[1+i] = nonce[i];
    b[14] = (uint8_t)(data_len >> 8);
    b[15] = (uint8_t)(data_len);

    uint8_t x[16];
    aes_encrypt_block(&aes, b, x);

    /* process AAD */
    uint8_t aad_blk[16];
    for (int i = 0; i < 16; i++) aad_blk[i] = 0;
    for (uint32_t i = 0; i < aad_len && i < 16; i++) aad_blk[i] = aad[i];
    xor_block(x, aad_blk);
    aes_encrypt_block(&aes, x, x);
    if (aad_len > 16) {
        for (int i = 0; i < 16; i++) aad_blk[i] = 0;
        for (uint32_t i = 16; i < aad_len && (i - 16) < 16; i++) aad_blk[i-16] = aad[i];
        xor_block(x, aad_blk);
        aes_encrypt_block(&aes, x, x);
    }

    /* process payload */
    for (uint32_t off = 0; off < data_len; off += 16) {
        uint8_t blk[16];
        for (int i = 0; i < 16; i++) blk[i] = 0;
        uint32_t rem = data_len - off;
        if (rem > 16) rem = 16;
        for (uint32_t i = 0; i < rem; i++) blk[i] = data[off + i];
        xor_block(x, blk);
        aes_encrypt_block(&aes, x, x);
    }

    uint8_t mic[8];
    for (int i = 0; i < 8; i++) mic[i] = x[i]; /* T = first 8 bytes of X */

    /* --- CTR mode encryption --- */
    /* A0: flags(1) + nonce(13) + counter(2) */
    uint8_t a0[16];
    a0[0] = 0x01; /* L=2 => L-1=1 */
    for (int i = 0; i < 13; i++) a0[1+i] = nonce[i];
    a0[14] = 0; a0[15] = 0;

    /* encrypt MIC with counter=0 */
    uint8_t s0[16];
    aes_encrypt_block(&aes, a0, s0);
    for (int i = 0; i < 8; i++) mic[i] ^= s0[i];

    /* encrypt payload with counter=1,2,... */
    for (uint32_t off = 0; off < data_len; off += 16) {
        uint32_t ctr = (off >> 4) + 1;
        a0[14] = (uint8_t)(ctr >> 8);
        a0[15] = (uint8_t)(ctr);
        uint8_t si[16];
        aes_encrypt_block(&aes, a0, si);
        uint32_t rem = data_len - off;
        if (rem > 16) rem = 16;
        for (uint32_t i = 0; i < rem; i++)
            data[off + i] ^= si[i];
    }

    /* append MIC after encrypted data */
    for (int i = 0; i < 8; i++)
        data[data_len + i] = mic[i];

    return 0;
}

/* AES-CCM decrypt (802.11i CCMP): in-place, verifies 8-byte MIC at end */
static int wifi_ccmp_decrypt(const uint8_t *tk, const uint8_t *hdr, uint32_t hdr_len,
                             uint8_t *data, uint32_t data_len)
{
    /* data_len includes the 8-byte MIC */
    if (data_len < 8) return -1;
    uint32_t payload_len = data_len - 8;

    struct aes_ctx aes;
    aes_key_expand(&aes, tk);

    /* extract PN from CCMP header (bytes 0,1,4,5,6,7 after 802.11 hdr) */
    /* caller must provide the PN separately or we derive from the CCMP header.
     * For now, use the global PN. In real use, parse from the CCMP IV. */
    uint8_t nonce[13];
    ccmp_build_nonce(nonce, 0, hdr + 10, wifi_ccmp_pn);

    /* --- CTR mode decryption --- */
    uint8_t a0[16];
    a0[0] = 0x01;
    for (int i = 0; i < 13; i++) a0[1+i] = nonce[i];
    a0[14] = 0; a0[15] = 0;

    /* decrypt MIC with counter=0 */
    uint8_t s0[16];
    aes_encrypt_block(&aes, a0, s0);
    uint8_t mic[8];
    for (int i = 0; i < 8; i++) mic[i] = data[payload_len + i] ^ s0[i];

    /* decrypt payload with counter=1,2,... */
    for (uint32_t off = 0; off < payload_len; off += 16) {
        uint32_t ctr = (off >> 4) + 1;
        a0[14] = (uint8_t)(ctr >> 8);
        a0[15] = (uint8_t)(ctr);
        uint8_t si[16];
        aes_encrypt_block(&aes, a0, si);
        uint32_t rem = payload_len - off;
        if (rem > 16) rem = 16;
        for (uint32_t i = 0; i < rem; i++)
            data[off + i] ^= si[i];
    }

    /* --- CBC-MAC verification --- */
    uint8_t b[16];
    b[0] = 0x59;
    for (int i = 0; i < 13; i++) b[1+i] = nonce[i];
    b[14] = (uint8_t)(payload_len >> 8);
    b[15] = (uint8_t)(payload_len);

    uint8_t x[16];
    aes_encrypt_block(&aes, b, x);

    uint8_t aad[32];
    uint32_t aad_len = ccmp_build_aad(hdr, hdr_len, aad);

    uint8_t aad_blk[16];
    for (int i = 0; i < 16; i++) aad_blk[i] = 0;
    for (uint32_t i = 0; i < aad_len && i < 16; i++) aad_blk[i] = aad[i];
    xor_block(x, aad_blk);
    aes_encrypt_block(&aes, x, x);
    if (aad_len > 16) {
        for (int i = 0; i < 16; i++) aad_blk[i] = 0;
        for (uint32_t i = 16; i < aad_len && (i - 16) < 16; i++) aad_blk[i-16] = aad[i];
        xor_block(x, aad_blk);
        aes_encrypt_block(&aes, x, x);
    }

    for (uint32_t off = 0; off < payload_len; off += 16) {
        uint8_t blk[16];
        for (int i = 0; i < 16; i++) blk[i] = 0;
        uint32_t rem = payload_len - off;
        if (rem > 16) rem = 16;
        for (uint32_t i = 0; i < rem; i++) blk[i] = data[off + i];
        xor_block(x, blk);
        aes_encrypt_block(&aes, x, x);
    }

    /* verify MIC */
    for (int i = 0; i < 8; i++)
        if (mic[i] != x[i]) return -1;

    return 0;
}

/* ======================================================================
 * EAPOL-Key frame
 * ====================================================================== */

struct eapol_key {
    uint8_t  version;
    uint8_t  type;
    uint16_t length;
    uint8_t  descriptor_type;
    uint16_t key_info;
    uint16_t key_length;
    uint8_t  replay_counter[8];
    uint8_t  nonce[32];
    uint8_t  iv[16];
    uint8_t  rsc[8];
    uint8_t  id[8];
    uint8_t  mic[16];
    uint16_t key_data_length;
};

#define EAPOL_KEY_HDR_SIZE 95 /* sizeof struct above without padding */

/* ======================================================================
 * WPA state and helpers
 * ====================================================================== */

static uint8_t wifi_ptk[48]; /* KCK(16) + KEK(16) + TK(16) */
static uint8_t wifi_gtk[32];
static uint8_t wifi_pmk[32];
static uint8_t wifi_anonce[32];
static uint8_t wifi_snonce[32];
static uint8_t wifi_wpa_step; /* 0=idle, 1=got msg1, 2=sent msg2, 3=got msg3, 4=done */
static uint8_t wifi_replay_counter[8];
static uint32_t wifi_snonce_counter; /* simple counter for SNonce generation */

/* our MAC address (defined before this file in wifi_rtos.c globals) */
/* wifi_sta_bssid is defined in wifi_sta.c */

/* read big-endian uint16 from wire */
static uint16_t wpa_be16(const uint8_t *p) { return ((uint16_t)p[0] << 8) | p[1]; }
static void wpa_put_be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

/* memcmp replacement (no libc) */
static int wpa_memcmp(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

static void wpa_memset(uint8_t *p, uint8_t v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) p[i] = v;
}

static void wpa_memcpy(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
}

/* our MAC: 6-byte placeholder read at init time */
static uint8_t wifi_wpa_our_mac[6];

/* derive PTK from PMK, addresses, nonces */
static void wifi_wpa_derive_ptk(void)
{
    /* data = min(AA,SPA) || max(AA,SPA) || min(ANonce,SNonce) || max(ANonce,SNonce) */
    uint8_t data[76]; /* 6+6+32+32 */
    const uint8_t *aa = wifi_sta_bssid;
    const uint8_t *spa = wifi_wpa_our_mac;

    if (wpa_memcmp(aa, spa, 6) < 0) {
        wpa_memcpy(data, aa, 6);
        wpa_memcpy(data + 6, spa, 6);
    } else {
        wpa_memcpy(data, spa, 6);
        wpa_memcpy(data + 6, aa, 6);
    }

    if (wpa_memcmp(wifi_anonce, wifi_snonce, 32) < 0) {
        wpa_memcpy(data + 12, wifi_anonce, 32);
        wpa_memcpy(data + 44, wifi_snonce, 32);
    } else {
        wpa_memcpy(data + 12, wifi_snonce, 32);
        wpa_memcpy(data + 44, wifi_anonce, 32);
    }

    wifi_prf384(wifi_pmk, 32, "Pairwise key expansion", data, 76, wifi_ptk, 48);
}

/* compute EAPOL MIC using KCK (first 16 bytes of PTK) */
static void wifi_wpa_compute_mic(const uint8_t *eapol_frame, uint32_t frame_len,
                                 uint8_t mic_out[16])
{
    /* WPA2 uses HMAC-SHA1-128 (truncated to 16 bytes) */
    uint8_t hmac_out[SHA1_DIGEST_SIZE];
    hmac_sha1(wifi_ptk, 16, eapol_frame, frame_len, hmac_out);
    for (int i = 0; i < 16; i++) mic_out[i] = hmac_out[i];
}

/* generate SNonce using a simple counter (real impl would use hardware RNG) */
static void wifi_wpa_gen_snonce(void)
{
    wifi_snonce_counter++;
    uint8_t seed[20];
    /* hash counter + our MAC for randomness */
    uint8_t cnt_buf[10];
    cnt_buf[0] = (uint8_t)(wifi_snonce_counter >> 24);
    cnt_buf[1] = (uint8_t)(wifi_snonce_counter >> 16);
    cnt_buf[2] = (uint8_t)(wifi_snonce_counter >>  8);
    cnt_buf[3] = (uint8_t)(wifi_snonce_counter);
    for (int i = 0; i < 6; i++) cnt_buf[4+i] = wifi_wpa_our_mac[i];
    sha1_hash(cnt_buf, 10, seed);
    /* stretch to 32 bytes */
    for (int i = 0; i < 20; i++) wifi_snonce[i] = seed[i];
    cnt_buf[0] ^= 0xFF;
    sha1_hash(cnt_buf, 10, seed);
    for (int i = 0; i < 12; i++) wifi_snonce[20+i] = seed[i];
}

/* ======================================================================
 * EAPOL-Key frame building and sending
 * ====================================================================== */

/* build and send EAPOL-Key msg2 (STA -> AP) */
static int wifi_wpa_send_msg2(const uint8_t *replay_counter)
{
    /* EAPOL frame: version(1) + type(1) + length(2) + descriptor_type(1) +
     * key_info(2) + key_length(2) + replay_counter(8) + nonce(32) +
     * IV(16) + RSC(8) + ID(8) + MIC(16) + key_data_length(2) = 97 bytes */
    uint8_t frame[4 + EAPOL_KEY_HDR_SIZE]; /* EAPOL header(4) + key descriptor */
    wpa_memset(frame, 0, sizeof(frame));

    /* EAPOL header */
    frame[0] = 2;    /* version: 802.1X-2004 */
    frame[1] = 3;    /* type: EAPOL-Key */
    wpa_put_be16(frame + 2, EAPOL_KEY_HDR_SIZE);

    /* key descriptor */
    uint8_t *kd = frame + 4;
    kd[0] = 2; /* descriptor type: RSN */

    /* key info: pairwise(0x0008) | MIC(0x0100) | version=2/HMAC-SHA1(0x0002) */
    uint16_t key_info = 0x010A; /* pairwise | MIC | version 2 */
    wpa_put_be16(kd + 1, key_info);

    /* key length */
    wpa_put_be16(kd + 3, 16); /* CCMP: 16 */

    /* replay counter from msg1 */
    wpa_memcpy(kd + 5, replay_counter, 8);

    /* our nonce */
    wpa_memcpy(kd + 13, wifi_snonce, 32);

    /* IV, RSC, ID: zero */
    /* MIC: computed below, at offset 77 (kd+73) */
    /* key data length: 0 for msg2 */
    wpa_put_be16(kd + 91, 0);

    /* compute MIC over the entire EAPOL frame (with MIC field zeroed) */
    uint8_t mic[16];
    wifi_wpa_compute_mic(frame, sizeof(frame), mic);
    wpa_memcpy(kd + 73, mic, 16); /* write MIC into frame */

    /* send via data path as EAPOL */
    struct sk_buff *skb = alloc_skb(14 + sizeof(frame), 0);
    if (!skb) return -1;
    uint8_t *p = skb_put(skb, 14 + sizeof(frame));

    /* ethernet header: dst=BSSID, src=our MAC, type=0x888E (EAPOL) */
    wpa_memcpy(p, wifi_sta_bssid, 6);
    wpa_memcpy(p + 6, wifi_wpa_our_mac, 6);
    p[12] = 0x88; p[13] = 0x8E;
    wpa_memcpy(p + 14, frame, sizeof(frame));

    return wifi_mac_tx(skb);
}

/* build and send EAPOL-Key msg4 (STA -> AP) */
static int wifi_wpa_send_msg4(const uint8_t *replay_counter)
{
    uint8_t frame[4 + EAPOL_KEY_HDR_SIZE];
    wpa_memset(frame, 0, sizeof(frame));

    frame[0] = 2;    /* version */
    frame[1] = 3;    /* type: EAPOL-Key */
    wpa_put_be16(frame + 2, EAPOL_KEY_HDR_SIZE);

    uint8_t *kd = frame + 4;
    kd[0] = 2; /* RSN */

    /* key info: pairwise(0x0008) | MIC(0x0100) | secure(0x0200) | version=2(0x0002) */
    uint16_t key_info = 0x030A; /* pairwise | MIC | secure | version 2 */
    wpa_put_be16(kd + 1, key_info);
    wpa_put_be16(kd + 3, 16);
    wpa_memcpy(kd + 5, replay_counter, 8);
    /* nonce, IV, RSC, ID: zero for msg4 */
    wpa_put_be16(kd + 91, 0);

    uint8_t mic[16];
    wifi_wpa_compute_mic(frame, sizeof(frame), mic);
    wpa_memcpy(kd + 73, mic, 16);

    struct sk_buff *skb = alloc_skb(14 + sizeof(frame), 0);
    if (!skb) return -1;
    uint8_t *p = skb_put(skb, 14 + sizeof(frame));
    wpa_memcpy(p, wifi_sta_bssid, 6);
    wpa_memcpy(p + 6, wifi_wpa_our_mac, 6);
    p[12] = 0x88; p[13] = 0x8E;
    wpa_memcpy(p + 14, frame, sizeof(frame));

    return wifi_mac_tx(skb);
}

/* ======================================================================
 * 4-way handshake
 * ====================================================================== */

static void wifi_wpa_derive_pmk(const char *passphrase, const char *ssid)
{
    uint8_t ssid_len = 0;
    while (ssid[ssid_len]) ssid_len++;
    wifi_pbkdf2_sha1(passphrase, (const uint8_t *)ssid, ssid_len,
                     4096, wifi_pmk, 32);
}

static void wifi_wpa_rx_eapol(const uint8_t *data, uint32_t len)
{
    /* EAPOL frame starts after ethernet header (14 bytes) */
    if (len < 4 + EAPOL_KEY_HDR_SIZE) return;

    uint8_t version = data[0];
    uint8_t type = data[1];
    (void)version;
    if (type != 3) return; /* not EAPOL-Key */

    const uint8_t *kd = data + 4;
    uint16_t key_info = wpa_be16(kd + 1);

    int is_pairwise = (key_info & 0x0008) != 0;
    int is_install  = (key_info & 0x0040) != 0;
    int is_ack      = (key_info & 0x0080) != 0;
    int is_mic      = (key_info & 0x0100) != 0;
    int is_secure   = (key_info & 0x0200) != 0;
    int is_encrypted = (key_info & 0x1000) != 0;
    (void)is_secure;

    if (is_pairwise && is_ack && !is_mic) {
        /* ---- MSG 1: AP -> STA (ANonce) ---- */
        wpa_memcpy(wifi_anonce, kd + 13, 32);
        wpa_memcpy(wifi_replay_counter, kd + 5, 8);

        /* generate our SNonce */
        wifi_wpa_gen_snonce();

        /* derive PTK */
        wifi_wpa_derive_ptk();

        /* send msg2 */
        wifi_wpa_send_msg2(wifi_replay_counter);
        wifi_wpa_step = 2;

    } else if (is_pairwise && is_ack && is_mic && is_install) {
        /* ---- MSG 3: AP -> STA (GTK, install PTK) ---- */

        /* verify MIC: zero the MIC field in a copy, compute, compare */
        uint8_t frame_copy[256];
        uint32_t copy_len = len;
        if (copy_len > sizeof(frame_copy)) copy_len = sizeof(frame_copy);
        wpa_memcpy(frame_copy, data, copy_len);
        wpa_memset(frame_copy + 4 + 73, 0, 16); /* zero MIC in copy */

        uint8_t computed_mic[16];
        wifi_wpa_compute_mic(frame_copy, copy_len, computed_mic);

        if (wpa_memcmp(computed_mic, kd + 73, 16) != 0) {
            /* MIC mismatch: authentication failure */
            wifi_sta_on_wpa_done(0);
            return;
        }

        /* extract and decrypt GTK from key data */
        uint16_t kd_len = wpa_be16(kd + 91);
        if (kd_len > 0 && is_encrypted) {
            const uint8_t *kek = wifi_ptk + 16; /* KEK is bytes 16-31 of PTK */
            const uint8_t *enc_data = kd + 93;
            if (kd_len >= 24) { /* minimum wrapped key size */
                uint32_t n_blocks = (kd_len / 8) - 1;
                uint8_t unwrapped[64];
                if (n_blocks <= 8) {
                    if (aes_key_unwrap(kek, enc_data, n_blocks, unwrapped) == 0) {
                        /* parse GTK KDE: type(1) + length(1) + OUI(3) + data_type(1) + keyID(1) + reserved(1) + GTK */
                        /* look for OUI 00-0F-AC type 1 */
                        uint32_t unwrap_len = n_blocks * 8;
                        uint32_t off = 0;
                        while (off + 2 <= unwrap_len) {
                            uint8_t kde_type = unwrapped[off];
                            uint8_t kde_len_val = unwrapped[off + 1];
                            if (kde_type == 0xDD && kde_len_val >= 6 + 16 &&
                                unwrapped[off+2] == 0x00 && unwrapped[off+3] == 0x0F &&
                                unwrapped[off+4] == 0xAC && unwrapped[off+5] == 0x01) {
                                /* GTK: skip KDE header (6 bytes) + key ID (1) + TX (1) */
                                wpa_memcpy(wifi_gtk, unwrapped + off + 8, 16);
                                break;
                            }
                            off += 2 + kde_len_val;
                            if (kde_len_val == 0) break;
                        }
                    }
                }
            }
        }

        /* save replay counter */
        wpa_memcpy(wifi_replay_counter, kd + 5, 8);

        /* send msg4 */
        wifi_wpa_send_msg4(wifi_replay_counter);

        /* install keys and signal completion */
        wifi_wpa_step = 4;
        wifi_sta_on_wpa_done(1);
    }
}

static void wifi_wpa_init(const char *passphrase, const char *ssid)
{
    wifi_wpa_derive_pmk(passphrase, ssid);
    wifi_wpa_step = 0;
    wifi_ccmp_pn = 0;
    wifi_snonce_counter = 0;
    wpa_memset(wifi_ptk, 0, sizeof(wifi_ptk));
    wpa_memset(wifi_gtk, 0, sizeof(wifi_gtk));
}
