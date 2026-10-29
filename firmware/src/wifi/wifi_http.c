/* SPDX-License-Identifier: GPL-3.0-only */
/* Clean-room async HTTP/1.1 GET client on lwIP raw TCP API.
 * Single concurrent request, callback-based, no malloc. */

#ifdef CONFIG_WIFI_LWIP

#include "lwip/tcp.h"
#include "lwip/dns.h"

/* ---- callback type ---------------------------------------------------- */

typedef void (*http_done_cb)(int status, const uint8_t *body,
                             uint32_t body_len, void *ctx);

/* ---- state machine ---------------------------------------------------- */

#define HTTP_IDLE          0
#define HTTP_DNS_WAIT      1
#define HTTP_CONNECTING    2
#define HTTP_RECV_HEADERS  3
#define HTTP_RECV_BODY     4
#define HTTP_DONE          5
#define HTTP_ERROR         6

#define HTTP_BUF_SIZE      2048
#define HTTP_TIMEOUT_MS    30000

static struct {
    uint8_t        state;
    struct tcp_pcb *pcb;
    http_done_cb   cb;
    void          *ctx;
    ip_addr_t      server_ip;
    char           host[64];
    char           path[192];
    uint8_t        buf[HTTP_BUF_SIZE];
    uint32_t       buf_len;
    uint32_t       body_offset;  /* offset of body within buf */
    int            status_code;
    uint32_t       content_length;
    uint8_t        have_content_length;
    uint32_t       start_ms;
} http_state;

/* ---- forward declarations --------------------------------------------- */

static err_t http_connected_cb(void *arg, struct tcp_pcb *tpcb, err_t err);
static err_t http_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static void  http_err_cb(void *arg, err_t err);
static void  http_finish(int status);

/* ---- internal helpers ------------------------------------------------- */

static void http_finish(int status)
{
    cfm1_log_int("wifi: http done status=", (int32_t)status);
    if (http_state.cb) {
        const uint8_t *body = 0;
        uint32_t body_len = 0;
        if (http_state.body_offset > 0 &&
            http_state.buf_len > http_state.body_offset) {
            body = http_state.buf + http_state.body_offset;
            body_len = http_state.buf_len - http_state.body_offset;
        }
        http_state.cb(status, body, body_len, http_state.ctx);
    }
    if (http_state.pcb) {
        tcp_arg(http_state.pcb, 0);
        tcp_recv(http_state.pcb, 0);
        tcp_err(http_state.pcb, 0);
        tcp_close(http_state.pcb);
        http_state.pcb = 0;
    }
    http_state.state = HTTP_IDLE;
}

/* ---- parse HTTP status line from buffer ------------------------------- */

static int http_parse_status(const uint8_t *data, uint32_t len)
{
    /* "HTTP/1.x NNN ..." */
    uint32_t i = 0;
    int code = 0;

    /* skip to first space */
    while (i < len && data[i] != ' ') i++;
    if (i >= len) return -1;
    i++; /* skip space */

    /* parse 3-digit status */
    if (i + 3 > len) return -1;
    code = (data[i] - '0') * 100 + (data[i+1] - '0') * 10 + (data[i+2] - '0');
    return code;
}

/* ---- find \r\n\r\n header terminator --------------------------------- */

static uint32_t http_find_body(const uint8_t *data, uint32_t len)
{
    uint32_t i;
    if (len < 4) return 0;
    for (i = 0; i <= len - 4; i++) {
        if (data[i] == '\r' && data[i+1] == '\n' &&
            data[i+2] == '\r' && data[i+3] == '\n')
            return i + 4;
    }
    return 0;
}

/* ---- parse Content-Length from headers -------------------------------- */

static uint32_t http_parse_content_length(const uint8_t *hdr, uint32_t hdr_len,
                                          uint8_t *found)
{
    /* Scan for "Content-Length:" (case-insensitive enough for servers
     * that use standard capitalization). */
    const char *needle = "Content-Length:";
    uint32_t nlen = 15;
    uint32_t i, val;

    *found = 0;
    if (hdr_len < nlen) return 0;
    for (i = 0; i <= hdr_len - nlen; i++) {
        uint32_t j;
        uint8_t match = 1;
        for (j = 0; j < nlen; j++) {
            char a = (char)hdr[i + j];
            char b = needle[j];
            /* tolower for alpha chars */
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { match = 0; break; }
        }
        if (!match) continue;
        /* skip spaces after colon */
        j = i + nlen;
        while (j < hdr_len && hdr[j] == ' ') j++;
        /* parse digits */
        val = 0;
        while (j < hdr_len && hdr[j] >= '0' && hdr[j] <= '9') {
            val = val * 10 + (hdr[j] - '0');
            j++;
        }
        *found = 1;
        return val;
    }
    return 0;
}

/* ---- DNS callback ----------------------------------------------------- */

static void http_dns_cb(const char *name, const ip_addr_t *addr, void *arg)
{
    (void)name;
    (void)arg;

    if (!addr || http_state.state != HTTP_DNS_WAIT) {
        cfm1_log("wifi: http dns failed");
        http_finish(-1);
        return;
    }

    cfm1_log("wifi: http dns resolved");
    http_state.server_ip = *addr;
    http_state.pcb = tcp_new();
    if (!http_state.pcb) {
        http_finish(-1);
        return;
    }

    tcp_arg(http_state.pcb, 0);
    tcp_recv(http_state.pcb, http_recv_cb);
    tcp_err(http_state.pcb, http_err_cb);

    http_state.state = HTTP_CONNECTING;
    if (tcp_connect(http_state.pcb, &http_state.server_ip, 80,
                    http_connected_cb) != ERR_OK) {
        http_finish(-1);
    }
}

/* ---- TCP connected callback ------------------------------------------- */

static err_t http_connected_cb(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    char req[320];
    uint32_t rlen = 0;
    const char *s;

    (void)arg;

    if (err != ERR_OK || http_state.state != HTTP_CONNECTING) {
        cfm1_log("wifi: http connect failed");
        http_finish(-1);
        return ERR_OK;
    }

    cfm1_log("wifi: http connected, sending GET");
    /* build GET request */
    s = "GET ";
    while (*s) req[rlen++] = *s++;
    s = http_state.path;
    while (*s && rlen < sizeof(req) - 64) req[rlen++] = *s++;
    s = " HTTP/1.1\r\nHost: ";
    while (*s) req[rlen++] = *s++;
    s = http_state.host;
    while (*s && rlen < sizeof(req) - 32) req[rlen++] = *s++;
    s = "\r\nConnection: close\r\n\r\n";
    while (*s) req[rlen++] = *s++;

    http_state.state = HTTP_RECV_HEADERS;
    http_state.buf_len = 0;
    http_state.body_offset = 0;
    http_state.status_code = 0;
    http_state.content_length = 0;
    http_state.have_content_length = 0;

    tcp_write(tpcb, req, (uint16_t)rlen, TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);

    return ERR_OK;
}

/* ---- TCP receive callback --------------------------------------------- */

static err_t http_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p,
                          err_t err)
{
    struct pbuf *q;
    (void)arg;
    (void)err;

    if (!p) {
        /* server closed connection -- finish with what we have */
        int code = http_state.status_code > 0 ? http_state.status_code : -1;
        http_finish(code);
        return ERR_OK;
    }

    /* copy incoming data into buffer */
    for (q = p; q != 0; q = q->next) {
        uint32_t space = HTTP_BUF_SIZE - http_state.buf_len;
        uint32_t chunk = (uint32_t)q->len;
        if (chunk > space) chunk = space;
        if (chunk > 0) {
            memcpy(http_state.buf + http_state.buf_len,
                        (const uint8_t *)q->payload, chunk);
            http_state.buf_len += chunk;
        }
    }

    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);

    /* parse headers if we haven't found body yet */
    if (http_state.state == HTTP_RECV_HEADERS) {
        uint32_t body_off = http_find_body(http_state.buf, http_state.buf_len);
        if (body_off > 0) {
            http_state.body_offset = body_off;
            http_state.status_code = http_parse_status(http_state.buf, body_off);
            http_state.content_length = http_parse_content_length(
                http_state.buf, body_off, &http_state.have_content_length);
            http_state.state = HTTP_RECV_BODY;
            cfm1_log_int("wifi: http status=", (int32_t)http_state.status_code);
        }
    }

    /* check if body is complete */
    if (http_state.state == HTTP_RECV_BODY && http_state.have_content_length) {
        uint32_t body_len = http_state.buf_len - http_state.body_offset;
        if (body_len >= http_state.content_length) {
            http_finish(http_state.status_code);
        }
    }

    return ERR_OK;
}

/* ---- TCP error callback ----------------------------------------------- */

static void http_err_cb(void *arg, err_t err)
{
    (void)arg;
    (void)err;
    /* pcb is already freed by lwIP when err_cb fires */
    cfm1_log("wifi: http tcp error");
    http_state.pcb = 0;
    http_finish(-1);
}

/* ---- public API ------------------------------------------------------- */

static int http_get(const char *host, const char *path,
                    http_done_cb cb, void *ctx)
{
    err_t dns_err;
    uint32_t i;

    if (http_state.state != HTTP_IDLE)
        return -1;

    cfm1_log("wifi: http GET");
    /* copy host */
    for (i = 0; i < sizeof(http_state.host) - 1 && host[i]; i++)
        http_state.host[i] = host[i];
    http_state.host[i] = 0;

    /* copy path */
    for (i = 0; i < sizeof(http_state.path) - 1 && path[i]; i++)
        http_state.path[i] = path[i];
    http_state.path[i] = 0;

    http_state.cb = cb;
    http_state.ctx = ctx;
    http_state.buf_len = 0;
    http_state.start_ms = fm1_ms;

    http_state.state = HTTP_DNS_WAIT;
    dns_err = dns_gethostbyname(host, &http_state.server_ip, http_dns_cb, 0);

    if (dns_err == ERR_OK) {
        /* already cached -- go straight to connect */
        http_dns_cb(host, &http_state.server_ip, 0);
    } else if (dns_err != ERR_INPROGRESS) {
        http_state.state = HTTP_IDLE;
        return -1;
    }

    return 0;
}

static int http_busy(void)
{
    return http_state.state != HTTP_IDLE;
}

static void http_poll(void)
{
    if (http_state.state == HTTP_IDLE)
        return;
    /* timeout check */
    if (fm1_ms - http_state.start_ms > HTTP_TIMEOUT_MS) {
        cfm1_log("wifi: http timeout");
        http_finish(-1);
    }
}

#else /* !CONFIG_WIFI_LWIP */

typedef void (*http_done_cb)(int status, const uint8_t *body,
                             uint32_t body_len, void *ctx);
static int  http_get(const char *host, const char *path,
                     http_done_cb cb, void *ctx)
{ (void)host; (void)path; (void)cb; (void)ctx; return -1; }
static int  http_busy(void) { return 0; }
static void http_poll(void) {}

#endif /* CONFIG_WIFI_LWIP */
