/* SPDX-License-Identifier: GPL-3.0-only */
/* Minimal WebSocket server (RFC 6455) for CDP transport.
 * Single connection, text frames only, no extensions. */

#define WS_STATE_HTTP     0
#define WS_STATE_OPEN     1
#define WS_STATE_CLOSED   2

#define WS_OP_CONT   0x0
#define WS_OP_TEXT   0x1
#define WS_OP_BIN    0x2
#define WS_OP_CLOSE  0x8
#define WS_OP_PING   0x9
#define WS_OP_PONG   0xA

#define WS_MAX_FRAME  2048

static uint8_t ws_state;
static uint8_t ws_rx_buf[WS_MAX_FRAME];
static uint32_t ws_rx_len;

/* SHA-1 for WebSocket handshake: minimal implementation. */

static void sha1_block(uint32_t h[5], const uint8_t block[64])
{
    uint32_t w[80];
    uint32_t i, a, b, c, d, e, f, k, t;
    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)block[i*4] << 24) | ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) | block[i*4+3];
    for (i = 16; i < 80; i++) {
        t = w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16];
        w[i] = (t << 1) | (t >> 31);
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4];
    for (i = 0; i < 80; i++) {
        if (i < 20)      { f = (b & c) | ((~b) & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;             k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else              { f = b ^ c ^ d;             k = 0xCA62C1D6; }
        t = ((a << 5) | (a >> 27)) + f + e + k + w[i];
        e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

static void sha1(const uint8_t *msg, uint32_t len, uint8_t out[20])
{
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint8_t block[64];
    uint32_t i, blocks, pos = 0;

    while (pos + 64 <= len) {
        sha1_block(h, msg + pos);
        pos += 64;
    }
    /* final block with padding */
    uint32_t rem = len - pos;
    for (i = 0; i < rem; i++) block[i] = msg[pos + i];
    block[rem] = 0x80;
    for (i = rem + 1; i < 64; i++) block[i] = 0;
    if (rem >= 56) {
        sha1_block(h, block);
        for (i = 0; i < 64; i++) block[i] = 0;
    }
    {
        uint64_t bits = (uint64_t)len * 8;
        block[56] = (uint8_t)(bits >> 56);
        block[57] = (uint8_t)(bits >> 48);
        block[58] = (uint8_t)(bits >> 40);
        block[59] = (uint8_t)(bits >> 32);
        block[60] = (uint8_t)(bits >> 24);
        block[61] = (uint8_t)(bits >> 16);
        block[62] = (uint8_t)(bits >> 8);
        block[63] = (uint8_t)(bits);
    }
    sha1_block(h, block);
    for (i = 0; i < 5; i++) {
        out[i*4]   = (uint8_t)(h[i] >> 24);
        out[i*4+1] = (uint8_t)(h[i] >> 16);
        out[i*4+2] = (uint8_t)(h[i] >> 8);
        out[i*4+3] = (uint8_t)(h[i]);
    }
}

/* Base64 encode */
static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static uint32_t base64_encode(const uint8_t *in, uint32_t in_len, char *out)
{
    uint32_t i, j = 0;
    for (i = 0; i + 2 < in_len; i += 3) {
        out[j++] = b64_table[(in[i] >> 2) & 0x3F];
        out[j++] = b64_table[((in[i] & 0x03) << 4) | ((in[i+1] >> 4) & 0x0F)];
        out[j++] = b64_table[((in[i+1] & 0x0F) << 2) | ((in[i+2] >> 6) & 0x03)];
        out[j++] = b64_table[in[i+2] & 0x3F];
    }
    if (i < in_len) {
        out[j++] = b64_table[(in[i] >> 2) & 0x3F];
        if (i + 1 < in_len) {
            out[j++] = b64_table[((in[i] & 0x03) << 4) | ((in[i+1] >> 4) & 0x0F)];
            out[j++] = b64_table[((in[i+1] & 0x0F) << 2)];
        } else {
            out[j++] = b64_table[((in[i] & 0x03) << 4)];
            out[j++] = '=';
        }
        out[j++] = '=';
    }
    out[j] = '\0';
    return j;
}

/* Base64 decode (for CDP memory write payloads) */
static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static uint32_t base64_decode(const char *in, uint32_t in_len, uint8_t *out)
{
    uint32_t i, j = 0;
    for (i = 0; i + 3 < in_len; i += 4) {
        int a = b64_val(in[i]), b = b64_val(in[i+1]);
        int c = b64_val(in[i+2]), d = b64_val(in[i+3]);
        if (a < 0 || b < 0) break;
        out[j++] = (uint8_t)((a << 2) | (b >> 4));
        if (c >= 0) out[j++] = (uint8_t)(((b & 0xF) << 4) | (c >> 2));
        if (d >= 0) out[j++] = (uint8_t)(((c & 0x3) << 6) | d);
    }
    return j;
}

/* WebSocket handshake key extraction and accept computation */

static const char ws_magic[] = "258EAFA5-E914-47DA-95CA-5AB9CD86B611";

static int ws_find_key(const uint8_t *http, uint32_t len, char *key_out)
{
    const char *needle = "Sec-WebSocket-Key: ";
    uint32_t nlen = 19;
    uint32_t i;
    for (i = 0; i + nlen < len; i++) {
        uint32_t j, match = 1;
        for (j = 0; j < nlen; j++) {
            if (http[i + j] != (uint8_t)needle[j]) { match = 0; break; }
        }
        if (match) {
            uint32_t k = 0;
            i += nlen;
            while (i < len && http[i] != '\r' && http[i] != '\n' && k < 63) {
                key_out[k++] = (char)http[i++];
            }
            key_out[k] = '\0';
            return (int)k;
        }
    }
    return 0;
}

static int ws_handle_upgrade(const uint8_t *data, uint32_t len)
{
    char client_key[64];
    int klen = ws_find_key(data, len, client_key);
    if (klen == 0) return -1;

    /* concat key + magic GUID */
    uint8_t concat[128];
    uint32_t clen = 0;
    uint32_t i;
    for (i = 0; client_key[i] && clen < 100; i++)
        concat[clen++] = (uint8_t)client_key[i];
    for (i = 0; ws_magic[i] && clen < 127; i++)
        concat[clen++] = (uint8_t)ws_magic[i];

    uint8_t hash[20];
    sha1(concat, clen, hash);

    char accept_b64[32];
    base64_encode(hash, 20, accept_b64);

    /* build HTTP 101 response */
    char resp[256];
    uint32_t rlen = 0;
    const char *parts[] = {
        "HTTP/1.1 101 Switching Protocols\r\n",
        "Upgrade: websocket\r\n",
        "Connection: Upgrade\r\n",
        "Sec-WebSocket-Accept: ", 0,
        "\r\n\r\n", 0
    };
    for (i = 0; parts[i]; i++) {
        const char *p = parts[i];
        while (*p && rlen < 255) resp[rlen++] = *p++;
        if (i == 3) {
            /* insert accept key */
            const char *a = accept_b64;
            while (*a && rlen < 255) resp[rlen++] = *a++;
        }
    }

    tcp_write((const uint8_t *)resp, rlen);
    return 0;
}

/* WebSocket frame decode: returns payload length, -1 on error/incomplete */
static int ws_decode_frame(const uint8_t *data, uint32_t len,
                           uint8_t *opcode, uint8_t **payload, uint32_t *payload_len)
{
    if (len < 2) return -1;
    *opcode = data[0] & 0x0F;
    uint8_t masked = (data[1] >> 7) & 1;
    uint32_t plen = data[1] & 0x7F;
    uint32_t hdr = 2;

    if (plen == 126) {
        if (len < 4) return -1;
        plen = ((uint32_t)data[2] << 8) | data[3];
        hdr = 4;
    } else if (plen == 127) {
        if (len < 10) return -1;
        plen = ((uint32_t)data[6] << 24) | ((uint32_t)data[7] << 16) |
               ((uint32_t)data[8] << 8) | data[9];
        hdr = 10;
    }

    if (masked) {
        if (len < hdr + 4 + plen) return -1;
        uint8_t mask[4];
        mask[0] = data[hdr]; mask[1] = data[hdr+1];
        mask[2] = data[hdr+2]; mask[3] = data[hdr+3];
        hdr += 4;
        /* unmask in place (data is in rx_buf which we own) */
        uint8_t *p = (uint8_t *)data + hdr;
        uint32_t i;
        for (i = 0; i < plen; i++) p[i] ^= mask[i & 3];
        *payload = p;
    } else {
        if (len < hdr + plen) return -1;
        *payload = (uint8_t *)data + hdr;
    }
    *payload_len = plen;
    return (int)(hdr + (masked ? 4 : 0) + plen);
}

/* WebSocket frame encode: write to TCP. Server frames are unmasked. */
static int ws_send_text(const uint8_t *data, uint32_t len)
{
    uint8_t hdr[10];
    uint32_t hdr_len;

    hdr[0] = 0x80 | WS_OP_TEXT; /* FIN + text */
    if (len < 126) {
        hdr[1] = (uint8_t)len;
        hdr_len = 2;
    } else if (len < 65536) {
        hdr[1] = 126;
        hdr[2] = (uint8_t)(len >> 8);
        hdr[3] = (uint8_t)len;
        hdr_len = 4;
    } else {
        return -1; /* too large */
    }

    /* send header then payload (two TCP writes, or combine) */
    uint8_t frame[WS_MAX_FRAME + 10];
    if (hdr_len + len > sizeof(frame)) return -1;
    uint32_t i;
    for (i = 0; i < hdr_len; i++) frame[i] = hdr[i];
    for (i = 0; i < len; i++) frame[hdr_len + i] = data[i];
    return tcp_write(frame, hdr_len + len);
}

static void ws_send_pong(const uint8_t *data, uint32_t len)
{
    uint8_t frame[130];
    if (len > 125) return;
    frame[0] = 0x80 | WS_OP_PONG;
    frame[1] = (uint8_t)len;
    uint32_t i;
    for (i = 0; i < len; i++) frame[2 + i] = data[i];
    tcp_write(frame, 2 + len);
}

static void ws_init(void)
{
    ws_state = WS_STATE_HTTP;
    ws_rx_len = 0;
}
