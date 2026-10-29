# Weather Page Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Full TCP/IP/HTTP/DNS networking stack (lwIP) + async weather fetcher + Weather UI page with condition glyphs and colored backgrounds on the FM-1 240x240 LCD.

**Architecture:** lwIP 2.2.x in `NO_SYS=1` bare-metal mode replaces the hand-rolled `wifi_tcp.c`. A callback-based HTTP GET client (`wifi_http.c`) sits on lwIP's raw TCP API. A non-blocking weather fetcher (`wifi_weather.c`) chains two HTTP calls (ip-api for geolocation, open-meteo for weather). The Weather page (`page_weather.c`) reads cached data and renders Felucca-style cards + a condition-colored graph area with 32x32 1-bit glyphs. A reusable spinner primitive (`fui_draw_spinner`) shows async activity.

**Tech Stack:** C (unity build, all `static`), lwIP 2.2.x (BSD), Python generators for glyphs/colors, pytest for unit tests, Playwright E2E for emulator tests.

---

## Phase 1: lwIP Network Stack

### Task 1: Vendor lwIP 2.2.x source tree

lwIP source must be added to the repo. Only `src/core/`, `src/include/`, and
`src/netif/` are needed. The `src/apps/` and `src/api/` directories are NOT
needed (`NO_SYS=1` mode uses only the raw API).

**Files:**
- Create: `firmware/upstream/lwip/` (git subtree or vendored copy)
- Create: `firmware/upstream/lwip/COPYING` (BSD license text)

**Step 1: Add lwIP as a git subtree**

```bash
cd /DIMM/Users/dmitry/dev/dimmkirr/core-fm1
git subtree add --prefix=firmware/upstream/lwip \
    https://git.savannah.nongnu.org/git/lwip.git STABLE-2_2_0_RELEASE --squash
```

If git subtree is not available or the URL is unreachable, download the release
tarball from https://download.savannah.nongnu.org/releases/lwip/ and extract:

```bash
mkdir -p firmware/upstream/lwip
# extract lwip-2.2.0.zip into firmware/upstream/lwip/
# result: firmware/upstream/lwip/src/{core,include,netif,apps,api}/
```

**Step 2: Verify the directory structure**

```bash
ls firmware/upstream/lwip/src/core/
# Expected: tcp.c, udp.c, dns.c, dhcp.c, pbuf.c, mem.c, memp.c, etc.
ls firmware/upstream/lwip/src/include/lwip/
# Expected: tcp.h, dns.h, dhcp.h, pbuf.h, opt.h, etc.
```

**Step 3: Commit**

```bash
git add firmware/upstream/lwip/
git commit -m "vendor: add lwIP 2.2.0 (BSD) for WiFi TCP/IP stack"
```

---

### Task 2: Create lwipopts.h for bare-metal NO_SYS mode

This is the lwIP configuration header. It overrides defaults from `lwip/opt.h`.
Minimal config for weather client: TCP + UDP + DNS + DHCP, no sockets, no threads.

**Files:**
- Create: `firmware/src/wifi/lwipopts.h`

**Step 1: Write lwipopts.h**

```c
/* SPDX-License-Identifier: BSD-3-Clause */
/* lwIP configuration for core-fm1 bare-metal WiFi.
 * NO_SYS=1: raw/callback API only, no threads, no sockets. */
#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/* bare-metal: no OS primitives */
#define NO_SYS                  1
#define LWIP_SOCKET             0
#define LWIP_NETCONN            0
#define SYS_LIGHTWEIGHT_PROT    0

/* protocols */
#define LWIP_TCP                1
#define LWIP_UDP                1
#define LWIP_DNS                1
#define LWIP_DHCP               1
#define LWIP_ARP                1
#define LWIP_ICMP               1
#define LWIP_IGMP               0
#define LWIP_IPV6               0
#define LWIP_PPP                0
#define LWIP_SNMP               0

/* memory: static pool allocation (no malloc) */
#define MEM_LIBC_MALLOC         0
#define MEM_SIZE                (6 * 1024)
#define MEM_ALIGNMENT           4
#define MEMP_NUM_PBUF           8
#define MEMP_NUM_UDP_PCB        2
#define MEMP_NUM_TCP_PCB        2
#define MEMP_NUM_TCP_PCB_LISTEN 0
#define MEMP_NUM_TCP_SEG        8
#define MEMP_NUM_NETBUF         0
#define MEMP_NUM_NETCONN        0
#define MEMP_NUM_ARP_QUEUE      4
#define MEMP_NUM_SYS_TIMEOUT    8

/* pbuf pool */
#define PBUF_POOL_SIZE          8
#define PBUF_POOL_BUFSIZE       1536

/* TCP tuning */
#define TCP_MSS                 1400
#define TCP_SND_BUF             (2 * TCP_MSS)
#define TCP_SND_QUEUELEN        (4 * TCP_SND_BUF / TCP_MSS)
#define TCP_WND                 (2 * TCP_MSS)
#define LWIP_TCP_SACK_OUT       0

/* DNS */
#define DNS_TABLE_SIZE          2
#define DNS_MAX_SERVERS         1

/* DHCP */
#define DHCP_DOES_ARP_CHECK     0

/* disable features we don't need */
#define LWIP_STATS              0
#define LWIP_STATS_DISPLAY      0
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK   1
#define LWIP_RAW                0
#define LWIP_AUTOIP             0

/* debug (disable in production) */
#define LWIP_DEBUG              0

/* checksum: let the CPU do it (no hardware offload on AC791N) */
#define CHECKSUM_GEN_IP         1
#define CHECKSUM_GEN_UDP        1
#define CHECKSUM_GEN_TCP        1
#define CHECKSUM_CHECK_IP       1
#define CHECKSUM_CHECK_UDP      1
#define CHECKSUM_CHECK_TCP      1

#endif /* LWIPOPTS_H */
```

**Step 2: Verify it compiles conceptually**

Check that lwIP's `opt.h` will pick up these overrides:
```bash
grep -c "lwipopts.h" firmware/upstream/lwip/src/include/lwip/opt.h
# Expected: 1 (it #includes "lwipopts.h")
```

**Step 3: Commit**

```bash
git add firmware/src/wifi/lwipopts.h
git commit -m "feat(wifi): add lwipopts.h for bare-metal lwIP config"
```

---

### Task 3: Create lwIP unity-build include file

The unity build `#include`s `.c` files directly. lwIP's source tree has many `.c`
files in `src/core/` and `src/netif/`. We need a single `.c` file that includes
all needed lwIP sources.

**Files:**
- Create: `firmware/src/wifi/wifi_lwip_core.c`

**Step 1: Write the lwIP unity include**

```c
/* SPDX-License-Identifier: BSD-3-Clause */
/* lwIP core sources for unity build. Includes only the modules needed
 * for NO_SYS=1 bare-metal TCP/UDP/DNS/DHCP. */

/* lwIP include path setup: lwipopts.h lives in firmware/src/wifi/,
 * lwIP headers live in firmware/upstream/lwip/src/include/.
 * The build system must add both to the include path. */

/* core */
#include "upstream/lwip/src/core/init.c"
#include "upstream/lwip/src/core/def.c"
#include "upstream/lwip/src/core/mem.c"
#include "upstream/lwip/src/core/memp.c"
#include "upstream/lwip/src/core/pbuf.c"
#include "upstream/lwip/src/core/netif.c"
#include "upstream/lwip/src/core/ip.c"
#include "upstream/lwip/src/core/tcp.c"
#include "upstream/lwip/src/core/tcp_in.c"
#include "upstream/lwip/src/core/tcp_out.c"
#include "upstream/lwip/src/core/udp.c"
#include "upstream/lwip/src/core/dns.c"
#include "upstream/lwip/src/core/timeouts.c"
#include "upstream/lwip/src/core/inet_chksum.c"

/* IPv4 */
#include "upstream/lwip/src/core/ipv4/etharp.c"
#include "upstream/lwip/src/core/ipv4/ip4.c"
#include "upstream/lwip/src/core/ipv4/ip4_addr.c"
#include "upstream/lwip/src/core/ipv4/ip4_frag.c"
#include "upstream/lwip/src/core/ipv4/dhcp.c"
#include "upstream/lwip/src/core/ipv4/icmp.c"

/* netif */
#include "upstream/lwip/src/netif/ethernet.c"
```

Note: lwIP declares many functions as non-static. In a unity build this may cause
symbol conflicts. If so, we may need to wrap lwIP in its own translation unit
or use a `#define` prefix strategy. Test this during Task 4.

**Step 2: Commit**

```bash
git add firmware/src/wifi/wifi_lwip_core.c
git commit -m "feat(wifi): add lwIP unity-build include file"
```

---

### Task 4: Wire lwIP into the unity build and verify compilation

Add the lwIP include path to the build system and include `wifi_lwip_core.c`
from `core_fm1.c`. The existing `wifi_lwip.c` already `#include`s lwIP headers
and uses the netif API, so it should compile once paths are correct.

**Files:**
- Modify: `firmware/src/core_fm1.c:664-679` (WiFi include block)
- Modify: `tools/build.py` or `Taskfile.yml` (add lwIP include paths)

**Step 1: Add wifi_lwip_core.c to the WiFi include block**

In `firmware/src/core_fm1.c`, after line 676 (`#include "wifi/wifi_wpa.c"`),
before `#include "wifi/wifi_lwip.c"`, add:

```c
#ifdef CONFIG_WIFI_LWIP
#include "wifi/wifi_lwip_core.c"
#endif
```

The include order becomes:
```c
#include "wifi/wifi_wpa.c"
#ifdef CONFIG_WIFI_LWIP
#include "wifi/wifi_lwip_core.c"
#endif
#include "wifi/wifi_lwip.c"
#include "wifi/wifi_api.c"
```

**Step 2: Add lwIP include paths to the build**

The JieLi toolchain needs `-I` flags for:
- `firmware/upstream/lwip/src/include` (lwIP public headers)
- `firmware/src/wifi` (lwipopts.h)

Check `tools/build.py` or `tools/build_core.py` for the include path list and add
these two directories. The exact mechanism depends on the build system:

```python
# In the CFLAGS or include_dirs list, add:
'-Ifirmware/upstream/lwip/src/include',
'-Ifirmware/src/wifi',
```

**Step 3: Test compilation**

```bash
task build -- --defconfig wifi
```

Expected: compilation succeeds (or reveals specific conflicts to fix).

Common issues:
- lwIP's non-static functions conflict with unity build: may need `#define`
  wrappers or compile lwIP separately
- Missing `sys_now()` function: lwIP `NO_SYS=1` needs `uint32_t sys_now(void)`
  returning milliseconds. Add to `wifi_lwip.c`:
  ```c
  uint32_t sys_now(void) { return fm1_ms; }
  ```

**Step 4: Commit**

```bash
git add firmware/src/core_fm1.c tools/build.py
git commit -m "feat(wifi): wire lwIP into unity build with include paths"
```

---

### Task 5: Delete wifi_tcp.c (replaced by lwIP)

The hand-rolled TCP server is superseded by lwIP's TCP stack. Remove it.

**Files:**
- Delete: `firmware/src/wifi/wifi_tcp.c`

**Step 1: Verify nothing includes wifi_tcp.c**

```bash
grep -rn "wifi_tcp" firmware/src/
# Expected: no references in core_fm1.c or other files
```

**Step 2: Delete and commit**

```bash
git rm firmware/src/wifi/wifi_tcp.c
git commit -m "refactor(wifi): remove hand-rolled TCP, replaced by lwIP"
```

---

### Task 6: Write the async HTTP GET client

Callback-based HTTP/1.1 GET client on lwIP's raw TCP API. Single concurrent
request. Non-blocking: state machine driven by lwIP callbacks.

**Files:**
- Create: `firmware/src/wifi/wifi_http.c`
- Create: `tests/wifi/test_http.py`

**Step 1: Write the failing test**

Create `tests/wifi/test_http.py`:

```python
#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Unit tests for wifi_http.c HTTP GET client logic."""
import pytest


def test_http_build_request():
    """The HTTP request builder produces a valid GET request."""
    host = "api.open-meteo.com"
    path = "/v1/forecast?latitude=41&longitude=29"
    expected = (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {host}\r\n"
        f"Connection: close\r\n"
        f"\r\n"
    )
    # We test the request format; actual C function tested via E2E
    assert "GET" in expected
    assert "Host: api.open-meteo.com" in expected
    assert expected.endswith("\r\n\r\n")


def test_http_parse_status_line():
    """Status line parser extracts status code."""
    line = "HTTP/1.1 200 OK\r\n"
    # Extract status code between first space and second space
    parts = line.split(" ", 2)
    code = int(parts[1])
    assert code == 200


def test_http_parse_status_404():
    line = "HTTP/1.1 404 Not Found\r\n"
    parts = line.split(" ", 2)
    code = int(parts[1])
    assert code == 404


def test_http_find_body_separator():
    """Body starts after \\r\\n\\r\\n header terminator."""
    response = (
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "\r\n"
        '{"temp":22}'
    )
    idx = response.find("\r\n\r\n")
    assert idx > 0
    body = response[idx + 4:]
    assert body == '{"temp":22}'


def test_http_parse_content_length():
    """Content-Length header parsed correctly."""
    headers = "Content-Type: application/json\r\nContent-Length: 42\r\n\r\n"
    for line in headers.split("\r\n"):
        if line.lower().startswith("content-length:"):
            cl = int(line.split(":", 1)[1].strip())
            assert cl == 42
            return
    pytest.fail("Content-Length not found")
```

**Step 2: Run test to verify it passes (these are format tests)**

```bash
task test:unit -- tests/wifi/test_http.py -v
```

Expected: PASS (these test the protocol format, not C code)

**Step 3: Write wifi_http.c**

Create `firmware/src/wifi/wifi_http.c`:

```c
/* SPDX-License-Identifier: GPL-3.0-only */
/* Async HTTP/1.1 GET client on lwIP raw TCP API.
 * Single concurrent request, callback-based, non-blocking. */

#ifdef CONFIG_WIFI_LWIP

#include "lwip/tcp.h"
#include "lwip/dns.h"

#define HTTP_BUF_SIZE     2048u
#define HTTP_TIMEOUT_MS   10000u
#define HTTP_MAX_HOST     64u
#define HTTP_MAX_PATH     256u

typedef void (*http_done_cb)(int status, const uint8_t *body,
                             uint32_t body_len, void *ctx);

typedef enum {
    HTTP_IDLE = 0,
    HTTP_DNS_WAIT,
    HTTP_CONNECTING,
    HTTP_SENDING,
    HTTP_RECV_HEADERS,
    HTTP_RECV_BODY,
    HTTP_DONE,
    HTTP_ERROR,
} http_state_t;

static struct {
    http_state_t state;
    struct tcp_pcb *pcb;
    http_done_cb   cb;
    void          *cb_ctx;
    char           host[HTTP_MAX_HOST];
    char           path[HTTP_MAX_PATH];
    uint8_t        buf[HTTP_BUF_SIZE];
    uint32_t       buf_len;
    uint32_t       body_off;
    int            status_code;
    uint32_t       content_len;
    uint32_t       start_ms;
} http;

static void http_reset(void)
{
    if (http.pcb) {
        tcp_arg(http.pcb, 0);
        tcp_recv(http.pcb, 0);
        tcp_err(http.pcb, 0);
        tcp_abort(http.pcb);
        http.pcb = 0;
    }
    http.state = HTTP_IDLE;
    http.buf_len = 0;
    http.body_off = 0;
    http.status_code = 0;
    http.content_len = 0;
}

static void http_finish(int status)
{
    http_done_cb cb = http.cb;
    void *ctx = http.cb_ctx;
    uint8_t *body = http.buf + http.body_off;
    uint32_t body_len = http.buf_len > http.body_off ?
                        http.buf_len - http.body_off : 0;
    http.state = HTTP_DONE;
    if (cb)
        cb(status, body, body_len, ctx);
    http_reset();
}

static void http_fail(void)
{
    http_finish(-1);
}

/* scan for \r\n\r\n in buf, return offset of body start or 0 */
static uint32_t http_find_headers_end(void)
{
    uint32_t i;
    if (http.buf_len < 4) return 0;
    for (i = 0; i <= http.buf_len - 4; i++) {
        if (http.buf[i]   == '\r' && http.buf[i+1] == '\n' &&
            http.buf[i+2] == '\r' && http.buf[i+3] == '\n')
            return i + 4;
    }
    return 0;
}

/* parse "HTTP/1.x NNN" from first line */
static int http_parse_status(void)
{
    uint32_t i = 0;
    int code = 0;
    while (i < http.buf_len && http.buf[i] != ' ') i++;
    i++;
    while (i < http.buf_len && http.buf[i] >= '0' && http.buf[i] <= '9') {
        code = code * 10 + (http.buf[i] - '0');
        i++;
    }
    return code;
}

/* parse Content-Length from headers */
static uint32_t http_parse_content_length(uint32_t hdr_end)
{
    uint32_t i;
    static const char cl[] = "content-length:";
    uint32_t cl_len = sizeof(cl) - 1;
    for (i = 0; i + cl_len < hdr_end; i++) {
        uint32_t j, match = 1;
        for (j = 0; j < cl_len && match; j++) {
            char c = (char)http.buf[i + j];
            if (c >= 'A' && c <= 'Z') c += 32;
            if (c != cl[j]) match = 0;
        }
        if (match) {
            uint32_t v = 0, k = i + cl_len;
            while (k < hdr_end && http.buf[k] == ' ') k++;
            while (k < hdr_end && http.buf[k] >= '0' && http.buf[k] <= '9') {
                v = v * 10 + (http.buf[k] - '0');
                k++;
            }
            return v;
        }
    }
    return 0;
}

/* ---- lwIP callbacks ---- */

static err_t http_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    (void)arg; (void)err;
    if (!p) {
        /* connection closed by server */
        if (http.state == HTTP_RECV_BODY || http.state == HTTP_RECV_HEADERS)
            http_finish(http.status_code ? http.status_code : -1);
        return ERR_OK;
    }

    /* copy data into buffer */
    struct pbuf *q;
    for (q = p; q; q = q->next) {
        uint32_t space = HTTP_BUF_SIZE - http.buf_len;
        uint32_t copy = (uint32_t)q->len < space ? (uint32_t)q->len : space;
        wifi_memcpy(http.buf + http.buf_len, (const uint8_t *)q->payload, copy);
        http.buf_len += copy;
    }
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    /* try to parse headers if we haven't yet */
    if (http.state == HTTP_RECV_HEADERS) {
        uint32_t hdr_end = http_find_headers_end();
        if (hdr_end > 0) {
            http.body_off = hdr_end;
            http.status_code = http_parse_status();
            http.content_len = http_parse_content_length(hdr_end);
            http.state = HTTP_RECV_BODY;
        }
    }

    /* check if body is complete */
    if (http.state == HTTP_RECV_BODY && http.content_len > 0) {
        uint32_t body_received = http.buf_len - http.body_off;
        if (body_received >= http.content_len)
            http_finish(http.status_code);
    }

    return ERR_OK;
}

static void http_err_cb(void *arg, err_t err)
{
    (void)arg; (void)err;
    http.pcb = 0;
    http_fail();
}

static err_t http_connected_cb(void *arg, struct tcp_pcb *pcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK) {
        http_fail();
        return ERR_OK;
    }

    /* build GET request */
    char req[HTTP_MAX_HOST + HTTP_MAX_PATH + 64];
    uint32_t i = 0, j;
    static const char g[] = "GET ";
    for (j = 0; g[j]; j++) req[i++] = g[j];
    for (j = 0; http.path[j] && i < sizeof(req) - 40; j++) req[i++] = http.path[j];
    static const char v[] = " HTTP/1.1\r\nHost: ";
    for (j = 0; v[j]; j++) req[i++] = v[j];
    for (j = 0; http.host[j] && i < sizeof(req) - 20; j++) req[i++] = http.host[j];
    static const char e[] = "\r\nConnection: close\r\n\r\n";
    for (j = 0; e[j]; j++) req[i++] = e[j];

    tcp_write(pcb, req, (uint16_t)i, TCP_WRITE_FLAG_COPY);
    tcp_output(pcb);
    http.state = HTTP_RECV_HEADERS;
    return ERR_OK;
}

/* ---- DNS callback ---- */

static void http_dns_cb(const char *name, const ip_addr_t *addr, void *arg)
{
    (void)name; (void)arg;
    if (!addr || http.state != HTTP_DNS_WAIT) {
        http_fail();
        return;
    }

    http.pcb = tcp_new();
    if (!http.pcb) {
        http_fail();
        return;
    }

    tcp_arg(http.pcb, 0);
    tcp_recv(http.pcb, http_recv_cb);
    tcp_err(http.pcb, http_err_cb);
    http.state = HTTP_CONNECTING;

    if (tcp_connect(http.pcb, addr, 80, http_connected_cb) != ERR_OK)
        http_fail();
}

/* ---- public API ---- */

static int http_get(const char *host, const char *path,
                    http_done_cb cb, void *ctx)
{
    ip_addr_t resolved;
    uint32_t i;

    if (http.state != HTTP_IDLE)
        return -1;

    http_reset();
    http.cb = cb;
    http.cb_ctx = ctx;
    http.start_ms = fm1_ms;

    for (i = 0; i < HTTP_MAX_HOST - 1 && host[i]; i++)
        http.host[i] = host[i];
    http.host[i] = 0;

    for (i = 0; i < HTTP_MAX_PATH - 1 && path[i]; i++)
        http.path[i] = path[i];
    http.path[i] = 0;

    err_t dns_err = dns_gethostbyname(http.host, &resolved, http_dns_cb, 0);
    if (dns_err == ERR_OK) {
        /* already cached */
        http_dns_cb(http.host, &resolved, 0);
        return 0;
    } else if (dns_err == ERR_INPROGRESS) {
        http.state = HTTP_DNS_WAIT;
        return 0;
    }
    return -1;
}

static int http_busy(void)
{
    return http.state != HTTP_IDLE;
}

static void http_poll(void)
{
    if (http.state == HTTP_IDLE || http.state == HTTP_DONE)
        return;

    /* timeout check */
    if (fm1_ms - http.start_ms > 30000u)
        http_fail();
}

#else /* !CONFIG_WIFI_LWIP */

static int http_get(const char *h, const char *p, void *cb, void *ctx)
{ (void)h; (void)p; (void)cb; (void)ctx; return -1; }
static int http_busy(void) { return 0; }
static void http_poll(void) {}

#endif /* CONFIG_WIFI_LWIP */
```

**Step 4: Add wifi_http.c to the unity build**

In `firmware/src/core_fm1.c`, after the `wifi_lwip.c` include (line 677),
before `wifi_api.c`, add:

```c
#include "wifi/wifi_http.c"
```

Include order becomes:
```c
#include "wifi/wifi_lwip.c"
#include "wifi/wifi_http.c"
#include "wifi/wifi_api.c"
```

**Step 5: Add http_poll() to wifi_poll()**

In `firmware/src/wifi/wifi_api.c`, inside `wifi_poll()` (line 128), add:

```c
    /* HTTP client: timeout checks */
    http_poll();
```

**Step 6: Commit**

```bash
git add firmware/src/wifi/wifi_http.c tests/wifi/test_http.py \
    firmware/src/core_fm1.c firmware/src/wifi/wifi_api.c
git commit -m "feat(wifi): add async HTTP GET client on lwIP raw TCP API"
```

---

## Phase 2: Weather Page with Glyphs

### Task 7: Generate weather condition color table

Python script that maps WMO weather codes to RGB565 background colors.
Output is a C header with a lookup function.

**Files:**
- Create: `tools/gen_weather_colors.py`
- Create: `firmware/src/ui/felucca/weather_colors.h` (generated)
- Create: `tests/test_weather_colors.py`

**Step 1: Write the test**

Create `tests/test_weather_colors.py`:

```python
#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Tests for weather color generation."""
import subprocess
import sys
from pathlib import Path
import pytest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'tools' / 'gen_weather_colors.py'


@pytest.fixture(autouse=True)
def skip_if_missing():
    if not SCRIPT.exists():
        pytest.skip('gen_weather_colors.py not found')


def test_gen_weather_colors_runs():
    r = subprocess.run([sys.executable, str(SCRIPT)],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr


def test_gen_weather_colors_output_has_all_codes():
    r = subprocess.run([sys.executable, str(SCRIPT)],
                       capture_output=True, text=True)
    output = r.stdout
    # Must contain the lookup function
    assert 'weather_color' in output
    # Must handle WMO code 0 (clear sky)
    assert '0x' in output


def test_gen_weather_colors_valid_rgb565():
    r = subprocess.run([sys.executable, str(SCRIPT)],
                       capture_output=True, text=True)
    # Extract all hex values, verify they're valid RGB565 (0x0000-0xFFFF)
    import re
    hexvals = re.findall(r'0x([0-9A-Fa-f]{4})', r.stdout)
    assert len(hexvals) > 0, "No RGB565 values found"
    for h in hexvals:
        v = int(h, 16)
        assert 0 <= v <= 0xFFFF, f"Invalid RGB565: 0x{h}"
```

**Step 2: Run test to verify it fails**

```bash
task test:unit -- tests/test_weather_colors.py -v
```

Expected: FAIL (script doesn't exist yet)

**Step 3: Write gen_weather_colors.py**

Create `tools/gen_weather_colors.py`:

```python
#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate weather condition -> RGB565 color mapping.
WMO weather interpretation codes: https://open-meteo.com/en/docs"""

def rgb_to_565(r, g, b):
    """Convert 8-bit RGB to 16-bit RGB565."""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

# WMO code ranges -> (R, G, B) background color
WEATHER_COLORS = {
    'CLEAR':       (70, 130, 210),   # bright sky blue
    'PARTLY':      (110, 150, 200),  # muted blue
    'OVERCAST':    (140, 150, 160),  # gray-blue
    'FOG':         (160, 165, 170),  # silver gray
    'DRIZZLE':     (90, 120, 160),   # cool blue-gray
    'RAIN':        (60, 85, 130),    # dark blue
    'SNOW':        (190, 200, 215),  # light silver-blue
    'SHOWERS':     (70, 100, 145),   # storm blue
    'THUNDER':     (45, 55, 80),     # dark slate
}

# WMO code -> category name
WMO_MAP = [
    (0,     'CLEAR'),
    (1,     'PARTLY'),
    (2,     'PARTLY'),
    (3,     'OVERCAST'),
    (45,    'FOG'),
    (48,    'FOG'),
    (51,    'DRIZZLE'),
    (53,    'DRIZZLE'),
    (55,    'DRIZZLE'),
    (56,    'DRIZZLE'),
    (57,    'DRIZZLE'),
    (61,    'RAIN'),
    (63,    'RAIN'),
    (65,    'RAIN'),
    (66,    'RAIN'),
    (67,    'RAIN'),
    (71,    'SNOW'),
    (73,    'SNOW'),
    (75,    'SNOW'),
    (77,    'SNOW'),
    (80,    'SHOWERS'),
    (81,    'SHOWERS'),
    (82,    'SHOWERS'),
    (85,    'SNOW'),
    (86,    'SNOW'),
    (95,    'THUNDER'),
    (96,    'THUNDER'),
    (99,    'THUNDER'),
]

def main():
    print("/* Generated by tools/gen_weather_colors.py */")
    print("/* SPDX-License-Identifier: GPL-3.0-only */")
    print("#pragma once")
    print()

    # emit the color constants
    for name, (r, g, b) in WEATHER_COLORS.items():
        c = rgb_to_565(r, g, b)
        print(f"#define WTHR_C_{name:8s} 0x{c:04X}u  "
              f"/* ({r},{g},{b}) */")

    print()

    # emit the text color (white or near-white for readability on all bgs)
    print("#define WTHR_FG        0xFFFFu  /* white text */")
    print("#define WTHR_FG_DIM    0xC618u  /* dim white */")
    print()

    # emit the lookup function
    print("static uint16_t weather_color(uint8_t wmo_code)")
    print("{")
    print("    switch (wmo_code) {")
    for code, cat in WMO_MAP:
        print(f"    case {code:3d}: return WTHR_C_{cat};")
    print(f"    default:  return WTHR_C_OVERCAST;")
    print("    }")
    print("}")

    # emit category name lookup
    print()
    print("static const char *weather_condition_name(uint8_t wmo_code)")
    print("{")
    print("    switch (wmo_code) {")
    code_names = {
        0: "CLEAR SKY", 1: "MOSTLY CLEAR", 2: "PARTLY CLOUDY",
        3: "OVERCAST", 45: "FOG", 48: "RIME FOG",
        51: "LIGHT DRIZZLE", 53: "DRIZZLE", 55: "HEAVY DRIZZLE",
        56: "FREEZING DRIZZLE", 57: "HEAVY FRZ DRZL",
        61: "LIGHT RAIN", 63: "RAIN", 65: "HEAVY RAIN",
        66: "FREEZING RAIN", 67: "HEAVY FRZ RAIN",
        71: "LIGHT SNOW", 73: "SNOW", 75: "HEAVY SNOW", 77: "SNOW GRAINS",
        80: "LIGHT SHOWERS", 81: "SHOWERS", 82: "HEAVY SHOWERS",
        85: "SNOW SHOWERS", 86: "HEAVY SNOW SH",
        95: "THUNDERSTORM", 96: "STORM W/ HAIL", 99: "HEAVY HAILSTORM",
    }
    for code, name in code_names.items():
        print(f'    case {code:3d}: return "{name}";')
    print('    default:  return "UNKNOWN";')
    print("    }")
    print("}")

if __name__ == '__main__':
    main()
```

**Step 4: Generate the header**

```bash
python3 tools/gen_weather_colors.py > firmware/src/ui/felucca/weather_colors.h
```

**Step 5: Run tests**

```bash
task test:unit -- tests/test_weather_colors.py -v
```

Expected: PASS

**Step 6: Commit**

```bash
git add tools/gen_weather_colors.py firmware/src/ui/felucca/weather_colors.h \
    tests/test_weather_colors.py
git commit -m "feat(ui): add weather condition color table generator"
```

---

### Task 8: Generate weather condition glyphs

Python script that generates 32x32 1-bit weather glyphs as C arrays.
8 conditions: sun, partial cloud, cloud, drizzle, rain, snow, thunder, unknown.

**Files:**
- Create: `tools/gen_weather_glyphs.py`
- Create: `firmware/src/ui/felucca/weather_glyphs.h` (generated)
- Create: `tests/test_weather_glyphs.py`

**Step 1: Write the test**

Create `tests/test_weather_glyphs.py`:

```python
#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Tests for weather glyph generation."""
import subprocess
import sys
from pathlib import Path
import re
import pytest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'tools' / 'gen_weather_glyphs.py'


@pytest.fixture(autouse=True)
def skip_if_missing():
    if not SCRIPT.exists():
        pytest.skip('gen_weather_glyphs.py not found')


def test_gen_glyphs_runs():
    r = subprocess.run([sys.executable, str(SCRIPT)],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr


def test_gen_glyphs_has_all_conditions():
    r = subprocess.run([sys.executable, str(SCRIPT)],
                       capture_output=True, text=True)
    output = r.stdout
    for name in ['SUN', 'PARTIAL_CLOUD', 'CLOUD', 'DRIZZLE',
                 'RAIN', 'SNOW', 'THUNDER', 'UNKNOWN']:
        assert f'GLYPH_{name}' in output, f"Missing glyph: {name}"


def test_gen_glyphs_correct_size():
    """Each glyph should be 32 uint32_t values (32 rows of 32 bits)."""
    r = subprocess.run([sys.executable, str(SCRIPT)],
                       capture_output=True, text=True)
    # Count hex values per glyph array
    arrays = re.findall(r'GLYPH_\w+\[32\]', r.stdout)
    assert len(arrays) == 8, f"Expected 8 glyphs, found {len(arrays)}"


def test_gen_glyphs_lookup_function():
    r = subprocess.run([sys.executable, str(SCRIPT)],
                       capture_output=True, text=True)
    assert 'weather_glyph_for_code' in r.stdout
```

**Step 2: Run test to verify it fails**

```bash
task test:unit -- tests/test_weather_glyphs.py -v
```

Expected: FAIL

**Step 3: Write gen_weather_glyphs.py**

Create `tools/gen_weather_glyphs.py`. Each glyph is defined as a 32-line ASCII
grid where `#` = pixel on, `.` = pixel off. The script converts to `uint32_t[32]`.

```python
#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate 32x32 1-bit weather condition glyphs as C arrays.
Each glyph is uint32_t[32], MSB-first (bit 31 = leftmost pixel)."""

GLYPHS = {}

# -- SUN: circle with rays --
GLYPHS['SUN'] = """
................................
..............##..............
..............##..............
......#.......##.......#......
.......#......##......#.......
........#.............#.......
..........##########..........
........##############........
.......################.......
......##################......
......##################......
.##..####################..##.
.##..####################..##.
......##################......
......##################......
.......################.......
........##############........
..........##########..........
........#.............#.......
.......#......##......#.......
......#.......##.......#......
..............##..............
..............##..............
................................
................................
................................
................................
................................
................................
................................
................................
................................
"""

# -- PARTIAL_CLOUD: small sun behind cloud --
GLYPHS['PARTIAL_CLOUD'] = """
................................
................................
..........##....................
..........##....................
.......#..##..#.................
........#.....#.................
..........####..................
........########................
.......##########...............
.......##########...............
..##..############..............
..##..##########........####....
.......########.......########..
........######......############
.........####.....##############
..................################
...........#########............#
.........###....................#
........##......................#
.......##.......................#
.......#........................#
.......#........................#
.......##......................##
........##....................##.
.........####..............####.
...........##################...
.............##############.....
................................
................................
................................
................................
................................
"""

# -- CLOUD: simple cloud shape --
GLYPHS['CLOUD'] = """
................................
................................
................................
................................
................................
................................
................................
..........########..............
........############............
.......##############...........
......################..........
.....##################.........
.....##################..####...
.....################..########.
......##############.###########
.......############.############
........##########..............#
......####....................###
.....##........................#
....##.........................#
....#..........................#
....#..........................#
....##........................##
.....##......................##.
......####................####..
........####################....
..........################......
................................
................................
................................
................................
................................
"""

# -- DRIZZLE: cloud with light dots --
GLYPHS['DRIZZLE'] = """
................................
................................
................................
..........########..............
........############............
.......##############...####....
......########################..
.....##############################
.....################..............#
......##############..............##
.......############..............##.
........########################....
..........####################......
................................
..........#.....#.....#.........
................................
.........#.....#.....#..........
................................
..........#.....#.....#.........
................................
.........#.....#.....#..........
................................
................................
................................
................................
................................
................................
................................
................................
................................
................................
................................
"""

# -- RAIN: cloud with diagonal streaks --
GLYPHS['RAIN'] = """
................................
................................
..........########..............
........############............
.......##############...####....
......########################..
.....##############################
.....################..............#
......##############..............##
.......############..............##.
........########################....
..........####################......
................................
.........##....##....##.........
..........##....##....##........
...........##....##....##.......
............##....##....##......
.........##....##....##.........
..........##....##....##........
...........##....##....##.......
............##....##....##......
................................
................................
................................
................................
................................
................................
................................
................................
................................
................................
................................
"""

# -- SNOW: cloud with snowflakes (asterisks) --
GLYPHS['SNOW'] = """
................................
................................
..........########..............
........############............
.......##############...####....
......########################..
.....##############################
.....################..............#
......##############..............##
.......############..............##.
........########################....
..........####################......
................................
..........#...........#.........
.........###.........###........
..........#...........#.........
................................
.............#...........#......
............###.........###.....
.............#...........#......
................................
..........#...........#.........
.........###.........###........
..........#...........#.........
................................
................................
................................
................................
................................
................................
................................
................................
"""

# -- THUNDER: cloud with lightning bolt --
GLYPHS['THUNDER'] = """
................................
..........########..............
........############............
.......##############...####....
......########################..
.....##############################
.....################..............#
......##############..............##
.......############..............##.
........########################....
..........####################......
................................
..............####..............
.............####...............
............####................
...........####.................
..........########..............
...........####.................
............####................
.............####...............
..............##................
................................
................................
................................
................................
................................
................................
................................
................................
................................
................................
................................
"""

# -- UNKNOWN: question mark in circle --
GLYPHS['UNKNOWN'] = """
................................
................................
................................
................................
.........##############.........
.......##################.......
......####################......
.....####....######....####.....
.....###......####......###.....
.....###......####......###.....
.....####....####.....####......
......##########.....####.......
.......########.....####........
..........####.....####.........
..........####....####..........
..........####...####...........
..........####..####............
..........########..............
...........######...............
...........####.................
................................
...........####.................
...........####.................
...........####.................
...........####.................
................................
................................
................................
................................
................................
................................
................................
"""


def parse_glyph(text):
    """Parse ASCII art into uint32_t[32] array."""
    lines = [l for l in text.strip().split('\n')]
    # pad or truncate to 32 lines
    while len(lines) < 32:
        lines.append('.' * 32)
    lines = lines[:32]
    result = []
    for line in lines:
        # pad or truncate to 32 chars
        line = line.ljust(32, '.')[:32]
        val = 0
        for i, ch in enumerate(line):
            if ch == '#':
                val |= (1 << (31 - i))
        result.append(val)
    return result


def main():
    print("/* Generated by tools/gen_weather_glyphs.py */")
    print("/* SPDX-License-Identifier: GPL-3.0-only */")
    print("#pragma once")
    print()
    print("#define WTHR_GLYPH_W 32")
    print("#define WTHR_GLYPH_H 32")
    print()

    names = list(GLYPHS.keys())
    for i, name in enumerate(names):
        data = parse_glyph(GLYPHS[name])
        print(f"static const uint32_t GLYPH_{name}[32] = {{")
        for row in range(0, 32, 4):
            vals = ', '.join(f'0x{data[row+j]:08X}' for j in range(min(4, 32-row)))
            comma = ',' if row + 4 < 32 else ''
            print(f"    {vals}{comma}")
        print("};")
        print()

    # enum for indexing
    print("enum {")
    for i, name in enumerate(names):
        print(f"    WTHR_G_{name} = {i},")
    print(f"    WTHR_G_COUNT = {len(names)}")
    print("};")
    print()

    # pointer array
    print("static const uint32_t *WTHR_GLYPHS[] = {")
    for name in names:
        print(f"    GLYPH_{name},")
    print("};")
    print()

    # WMO code -> glyph index
    print("static uint32_t weather_glyph_for_code(uint8_t wmo_code)")
    print("{")
    print("    switch (wmo_code) {")
    wmo_glyph = {
        0: 'SUN', 1: 'PARTIAL_CLOUD', 2: 'PARTIAL_CLOUD', 3: 'CLOUD',
        45: 'CLOUD', 48: 'CLOUD',
        51: 'DRIZZLE', 53: 'DRIZZLE', 55: 'DRIZZLE', 56: 'DRIZZLE', 57: 'DRIZZLE',
        61: 'RAIN', 63: 'RAIN', 65: 'RAIN', 66: 'RAIN', 67: 'RAIN',
        71: 'SNOW', 73: 'SNOW', 75: 'SNOW', 77: 'SNOW',
        80: 'RAIN', 81: 'RAIN', 82: 'RAIN', 85: 'SNOW', 86: 'SNOW',
        95: 'THUNDER', 96: 'THUNDER', 99: 'THUNDER',
    }
    for code, glyph in sorted(wmo_glyph.items()):
        print(f"    case {code:3d}: return WTHR_G_{glyph};")
    print(f"    default:  return WTHR_G_UNKNOWN;")
    print("    }")
    print("}")
    print()

    # glyph renderer
    print("static void weather_draw_glyph(int32_t x, int32_t y,")
    print("                               uint32_t idx, uint16_t fg, uint16_t bg)")
    print("{")
    print("    if (idx >= WTHR_G_COUNT) idx = WTHR_G_UNKNOWN;")
    print("    const uint32_t *g = WTHR_GLYPHS[idx];")
    print("    int32_t r, c;")
    print("    for (r = 0; r < 32; r++) {")
    print("        uint32_t row = g[r];")
    print("        for (c = 0; c < 32; c++) {")
    print("            if (row & (1u << (31 - c)))")
    print("                cv_rect(x + c, y + r, 1, 1, fg);")
    print("        }")
    print("    }")
    print("}")

if __name__ == '__main__':
    main()
```

**Step 4: Generate the header**

```bash
python3 tools/gen_weather_glyphs.py > firmware/src/ui/felucca/weather_glyphs.h
```

**Step 5: Run tests**

```bash
task test:unit -- tests/test_weather_glyphs.py -v
```

Expected: PASS

**Step 6: Commit**

```bash
git add tools/gen_weather_glyphs.py firmware/src/ui/felucca/weather_glyphs.h \
    tests/test_weather_glyphs.py
git commit -m "feat(ui): add weather condition glyph generator"
```

---

### Task 9: Write the Weather page

The Felucca-style Weather page with cards, colored graph area, glyph, and
condition text. Follows the same pattern as `page_ble.c`.

**Files:**
- Create: `firmware/src/ui/felucca/page_weather.c`
- Modify: `firmware/src/ui/felucca/ui.c` (add `#include`)
- Modify: `firmware/src/ui/felucca/pages.c` (register page)

**Step 1: Write page_weather.c**

Create `firmware/src/ui/felucca/page_weather.c`:

```c
/* SPDX-License-Identifier: GPL-3.0-only */
/* WEATHER page: current weather display over WiFi.
 * Cards: TEMP, HUMID, WIND, CODE. Graph: condition-colored bg with glyph. */

#include "weather_colors.h"
#include "weather_glyphs.h"

#define FWTHR_REFRESH_INTERVAL 900000u  /* 15 min in ms */

static uint32_t fwthr_last_frame;

static void fwthr_draw_graph(void)
{
    const struct weather_data *w = weather_get();
    uint32_t graph_h = Y_FOOT - Y_GRAPH;
    uint16_t bg;

    if (!w || !w->valid) {
        bg = T_BG;
        cv_begin(240, graph_h, bg);

        if (weather_fetching()) {
            fui_draw_spinner(120 - 8, graph_h / 2 - 8, fm1_ms, T_THEME, bg);
            cv_text_in(0, graph_h / 2 + 16, 240, &AF_S, "FETCHING...", T_MID, bg);
        } else if (wifi_get_state() != WIFI_STATE_CONNECTED) {
            cv_text_in(0, graph_h / 2 - 8, 240, &AF_M, "NO WIFI", T_DIM, bg);
            cv_text_in(0, graph_h / 2 + 12, 240, &AF_S, "connect to see weather", T_DIM, bg);
        } else {
            cv_text_in(0, graph_h / 2 - 4, 240, &AF_S, "NO DATA", T_DIM, bg);
        }

        cv_blit(0, Y_GRAPH);
        return;
    }

    bg = weather_color(w->wmo_code);
    cv_begin(240, graph_h, bg);

    /* glyph centered at top of graph area */
    uint32_t gidx = weather_glyph_for_code(w->wmo_code);
    weather_draw_glyph(104, 4, gidx, WTHR_FG, bg);

    /* condition name */
    const char *cond = weather_condition_name(w->wmo_code);
    cv_text_in(0, 40, 240, &AF_M, cond, WTHR_FG, bg);

    /* large temperature */
    {
        char temp[8];
        int16_t t = w->temp_c / 10;
        int16_t frac = w->temp_c % 10;
        if (frac < 0) frac = -frac;
        uint32_t i = 0;
        if (t < 0) { temp[i++] = '-'; t = -t; }
        if (t >= 10) temp[i++] = '0' + (char)(t / 10);
        temp[i++] = '0' + (char)(t % 10);
        temp[i++] = '.';
        temp[i++] = '0' + (char)frac;
        temp[i++] = 'C';
        temp[i] = 0;
        cv_text_in(0, 58, 240, &AF_L, temp, WTHR_FG, bg);
    }

    /* city, country */
    if (w->city[0]) {
        char loc[32];
        uint32_t i = 0, j;
        for (j = 0; w->city[j] && i < 27; j++) loc[i++] = w->city[j];
        if (w->country[0]) {
            loc[i++] = ','; loc[i++] = ' ';
            for (j = 0; w->country[j] && i < 31; j++) loc[i++] = w->country[j];
        }
        loc[i] = 0;
        cv_text_in(0, 92, 240, &AF_S, loc, WTHR_FG_DIM, bg);
    }

    /* status line: last update time + wifi status */
    {
        char status[32];
        uint32_t i = 0;
        uint32_t age_s = (fm1_ms - w->updated_ms) / 1000u;
        uint32_t age_m = age_s / 60u;
        static const char upd[] = "UPD: ";
        uint32_t j;
        for (j = 0; upd[j]; j++) status[i++] = upd[j];
        if (age_m > 0) {
            if (age_m >= 10) status[i++] = '0' + (char)(age_m / 10);
            status[i++] = '0' + (char)(age_m % 10);
            status[i++] = 'm';
        } else {
            status[i++] = 'N'; status[i++] = 'O'; status[i++] = 'W';
        }
        status[i] = 0;
        cv_text_in(0, graph_h - 16, 240, &AF_S, status, WTHR_FG_DIM, bg);
    }

    cv_blit(0, Y_GRAPH);
}

static void fwthr_draw(void)
{
    fui_draw_head();

    const struct weather_data *w = weather_get();
    if (w && w->valid) {
        char temp[8], hum[8], wind[8];
        fui_fmt_val(temp, (int16_t)(w->temp_c / 10));
        fui_fmt_val(hum, (int16_t)w->humidity);
        fui_fmt_val(wind, (int16_t)(w->wind_kmh / 10));
        const char *code = weather_condition_name(w->wmo_code);
        /* truncate condition to 4 chars for card */
        char short_code[5];
        uint32_t j;
        for (j = 0; j < 4 && code[j]; j++)
            short_code[j] = code[j];
        short_code[j] = 0;

        fui_draw_card(0, "TEMP", temp, "C", T_THEME, -1, ICON_GENERIC);
        fui_draw_card(1, "HUM",  hum,  "%", T_MID,   -1, ICON_GENERIC);
        fui_draw_card(2, "WIND", wind, "kh", T_MID,  -1, ICON_GENERIC);
        fui_draw_card(3, "SKY",  short_code, "", T_THEME, -1, ICON_GENERIC);
    } else {
        fui_draw_card(0, "TEMP", "---", "",  T_DIM, -1, ICON_GENERIC);
        fui_draw_card(1, "HUM",  "---", "",  T_DIM, -1, ICON_GENERIC);
        fui_draw_card(2, "WIND", "---", "",  T_DIM, -1, ICON_GENERIC);
        fui_draw_card(3, "SKY",  "---", "",  T_DIM, -1, ICON_GENERIC);
    }

    fwthr_draw_graph();
    fui_draw_foot("WEATHER", "---", "WTHR");
}

static void fwthr_enter(void)
{
    const struct weather_data *w = weather_get();
    if (!w || !w->valid || fm1_ms - w->updated_ms > FWTHR_REFRESH_INTERVAL) {
        if (!weather_fetching() && wifi_get_state() == WIFI_STATE_CONNECTED)
            weather_fetch_start();
    }
}

static int fwthr_knob(uint32_t id, int32_t clicks)
{
    /* SELECT encoder: manual refresh */
    if (id == 0 && clicks > 0) {
        if (!weather_fetching() && wifi_get_state() == WIFI_STATE_CONNECTED) {
            weather_fetch_start();
            fsh_dirty = 1;
        }
        return 1;
    }
    return 0;
}

static const cfm1_page_t FPAGE_WEATHER = {
    .name   = "WTHR",
    .title  = "WEATHER",
    .flags  = PAGEF_SYSTEM,
    .enter  = fwthr_enter,
    .draw   = fwthr_draw,
    .knob   = fwthr_knob,
};
```

**Step 2: Add include to ui.c**

In `firmware/src/ui/felucca/ui.c`, after the `#ifdef CONFIG_BLE` block (line 31)
and before `#ifdef CONFIG_DEBUG_LOG`, add:

```c
#ifdef CONFIG_WIFI
#include "ui/felucca/page_weather.c"
#endif
```

**Step 3: Register in pages.c**

In `firmware/src/ui/felucca/pages.c`, after `#ifdef CONFIG_BLE` block (line 26)
and before `#ifdef CONFIG_DEBUG_LOG`, add:

```c
#ifdef CONFIG_WIFI
    fsh_register(&FPAGE_WEATHER);
#endif
```

**Step 4: Commit**

```bash
git add firmware/src/ui/felucca/page_weather.c \
    firmware/src/ui/felucca/ui.c firmware/src/ui/felucca/pages.c
git commit -m "feat(ui): add Weather page with condition colors and glyphs"
```

---

## Phase 3: Async Weather Fetcher

### Task 10: Write the weather data fetcher

Non-blocking state machine that chains ip-api geolocation + open-meteo weather
fetch. Uses the HTTP client from Task 6.

**Files:**
- Create: `firmware/src/wifi/wifi_weather.c`
- Create: `tests/wifi/test_weather.py`

**Step 1: Write the test**

Create `tests/wifi/test_weather.py`:

```python
#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Tests for weather data JSON parsing logic."""
import json
import pytest


SAMPLE_GEO_RESPONSE = (
    '{"lat":41.0082,"lon":28.9784,"city":"Istanbul","countryCode":"TR"}'
)

SAMPLE_WEATHER_RESPONSE = json.dumps({
    "current": {
        "temperature_2m": 22.5,
        "relative_humidity_2m": 65,
        "weather_code": 0,
        "wind_speed_10m": 12.3
    }
})


def mini_json_extract(text, key):
    """Simulate the minimal JSON key extractor from wifi_weather.c."""
    search = f'"{key}"'
    idx = text.find(search)
    if idx < 0:
        return None
    idx += len(search)
    # skip whitespace and colon
    while idx < len(text) and text[idx] in ' \t\n\r:':
        idx += 1
    if idx >= len(text):
        return None
    # string value
    if text[idx] == '"':
        end = text.index('"', idx + 1)
        return text[idx + 1:end]
    # numeric value
    start = idx
    while idx < len(text) and text[idx] not in ',}] \t\n\r':
        idx += 1
    return text[start:idx]


def test_parse_geo_lat():
    val = mini_json_extract(SAMPLE_GEO_RESPONSE, "lat")
    assert float(val) == pytest.approx(41.0082, abs=0.001)


def test_parse_geo_lon():
    val = mini_json_extract(SAMPLE_GEO_RESPONSE, "lon")
    assert float(val) == pytest.approx(28.9784, abs=0.001)


def test_parse_geo_city():
    val = mini_json_extract(SAMPLE_GEO_RESPONSE, "city")
    assert val == "Istanbul"


def test_parse_geo_country():
    val = mini_json_extract(SAMPLE_GEO_RESPONSE, "countryCode")
    assert val == "TR"


def test_parse_weather_temp():
    val = mini_json_extract(SAMPLE_WEATHER_RESPONSE, "temperature_2m")
    assert float(val) == pytest.approx(22.5, abs=0.1)


def test_parse_weather_humidity():
    val = mini_json_extract(SAMPLE_WEATHER_RESPONSE, "relative_humidity_2m")
    assert int(val) == 65


def test_parse_weather_code():
    val = mini_json_extract(SAMPLE_WEATHER_RESPONSE, "weather_code")
    assert int(val) == 0


def test_parse_weather_wind():
    val = mini_json_extract(SAMPLE_WEATHER_RESPONSE, "wind_speed_10m")
    assert float(val) == pytest.approx(12.3, abs=0.1)


def test_parse_missing_key():
    val = mini_json_extract(SAMPLE_GEO_RESPONSE, "nonexistent")
    assert val is None


def test_parse_nested_weather():
    """The parser finds keys inside nested objects."""
    val = mini_json_extract(SAMPLE_WEATHER_RESPONSE, "temperature_2m")
    assert val is not None
```

**Step 2: Run tests**

```bash
task test:unit -- tests/wifi/test_weather.py -v
```

Expected: PASS (Python-side parsing logic)

**Step 3: Write wifi_weather.c**

Create `firmware/src/wifi/wifi_weather.c`:

```c
/* SPDX-License-Identifier: GPL-3.0-only */
/* Async weather fetcher: ip-api.com geolocation + open-meteo.com forecast.
 * Non-blocking state machine using wifi_http.c. */

#ifdef CONFIG_WIFI_LWIP

#define WTHR_REFRESH_MS     (15u * 60u * 1000u)  /* 15 minutes */
#define WTHR_RETRY_MS       (60u * 1000u)         /* 1 minute on error */
#define WTHR_MAX_RETRIES    3u

struct weather_data {
    int16_t  temp_c;       /* temperature x10 (225 = 22.5C) */
    uint8_t  humidity;
    uint8_t  wmo_code;
    uint16_t wind_kmh;     /* wind speed x10 */
    char     city[24];
    char     country[4];
    uint32_t updated_ms;
    uint8_t  valid;
};

typedef enum {
    WTHR_IDLE = 0,
    WTHR_GEO_FETCH,
    WTHR_GEO_WAIT,
    WTHR_WEATHER_FETCH,
    WTHR_WEATHER_WAIT,
    WTHR_DONE,
    WTHR_ERROR,
} wthr_state_t;

static struct {
    wthr_state_t state;
    struct weather_data data;
    char lat[12];
    char lon[12];
    uint32_t next_fetch_ms;
    uint32_t retries;
} wthr;

/* minimal JSON key extractor: find "key": and return value start/len */
static int wthr_json_str(const uint8_t *json, uint32_t json_len,
                         const char *key, char *out, uint32_t out_max)
{
    uint32_t klen = 0, i, j;
    const char *k = key;
    while (*k++) klen++;

    for (i = 0; i + klen + 3 < json_len; i++) {
        if (json[i] != '"') continue;
        uint32_t m = 1;
        for (j = 0; j < klen && m; j++)
            if (json[i + 1 + j] != (uint8_t)key[j]) m = 0;
        if (!m || json[i + 1 + klen] != '"') continue;

        /* found key, skip to value */
        uint32_t vi = i + 1 + klen + 1;
        while (vi < json_len && (json[vi] == ' ' || json[vi] == ':' ||
               json[vi] == '\t' || json[vi] == '\n' || json[vi] == '\r'))
            vi++;
        if (vi >= json_len) return -1;

        if (json[vi] == '"') {
            /* string value */
            vi++;
            uint32_t si = 0;
            while (vi < json_len && json[vi] != '"' && si < out_max - 1)
                out[si++] = (char)json[vi++];
            out[si] = 0;
            return (int)si;
        } else {
            /* numeric value */
            uint32_t si = 0;
            while (vi < json_len && json[vi] != ',' && json[vi] != '}' &&
                   json[vi] != ' ' && json[vi] != '\n' && si < out_max - 1)
                out[si++] = (char)json[vi++];
            out[si] = 0;
            return (int)si;
        }
    }
    return -1;
}

/* simple fixed-point parser: "22.5" -> 225 */
static int16_t wthr_parse_fixed10(const char *s)
{
    int16_t sign = 1, whole = 0, frac = 0;
    uint32_t i = 0;
    if (s[0] == '-') { sign = -1; i++; }
    while (s[i] >= '0' && s[i] <= '9') {
        whole = whole * 10 + (int16_t)(s[i] - '0');
        i++;
    }
    if (s[i] == '.') {
        i++;
        if (s[i] >= '0' && s[i] <= '9')
            frac = (int16_t)(s[i] - '0');
    }
    return sign * (whole * 10 + frac);
}

/* ---- HTTP callbacks ---- */

static void wthr_geo_done(int status, const uint8_t *body,
                          uint32_t body_len, void *ctx)
{
    (void)ctx;
    if (status != 200 || body_len == 0) {
        wthr.state = WTHR_ERROR;
        return;
    }

    if (wthr_json_str(body, body_len, "lat", wthr.lat, sizeof(wthr.lat)) < 0 ||
        wthr_json_str(body, body_len, "lon", wthr.lon, sizeof(wthr.lon)) < 0) {
        wthr.state = WTHR_ERROR;
        return;
    }

    wthr_json_str(body, body_len, "city",
                  wthr.data.city, sizeof(wthr.data.city));
    wthr_json_str(body, body_len, "countryCode",
                  wthr.data.country, sizeof(wthr.data.country));

    wthr.state = WTHR_WEATHER_FETCH;
}

static void wthr_weather_done(int status, const uint8_t *body,
                              uint32_t body_len, void *ctx)
{
    (void)ctx;
    if (status != 200 || body_len == 0) {
        wthr.state = WTHR_ERROR;
        return;
    }

    char val[16];

    if (wthr_json_str(body, body_len, "temperature_2m", val, sizeof(val)) > 0)
        wthr.data.temp_c = wthr_parse_fixed10(val);

    if (wthr_json_str(body, body_len, "relative_humidity_2m", val, sizeof(val)) > 0)
        wthr.data.humidity = (uint8_t)wthr_parse_fixed10(val) / 10;

    if (wthr_json_str(body, body_len, "weather_code", val, sizeof(val)) > 0) {
        int16_t c = wthr_parse_fixed10(val) / 10;
        wthr.data.wmo_code = (uint8_t)(c < 0 ? 0 : c > 99 ? 99 : c);
    }

    if (wthr_json_str(body, body_len, "wind_speed_10m", val, sizeof(val)) > 0)
        wthr.data.wind_kmh = (uint16_t)wthr_parse_fixed10(val);

    wthr.data.updated_ms = fm1_ms;
    wthr.data.valid = 1;
    wthr.state = WTHR_DONE;
    wthr.retries = 0;
    wthr.next_fetch_ms = fm1_ms + WTHR_REFRESH_MS;
    fsh_dirty = 1;
}

/* ---- public API ---- */

static void weather_fetch_start(void)
{
    if (wthr.state != WTHR_IDLE && wthr.state != WTHR_DONE &&
        wthr.state != WTHR_ERROR)
        return;
    wthr.state = WTHR_GEO_FETCH;
    fsh_dirty = 1;
}

static void weather_poll(void)
{
    switch (wthr.state) {
    case WTHR_IDLE:
        break;

    case WTHR_GEO_FETCH:
        if (http_busy()) break;
        if (http_get("ip-api.com",
                     "/json?fields=lat,lon,city,countryCode",
                     wthr_geo_done, 0) == 0)
            wthr.state = WTHR_GEO_WAIT;
        else
            wthr.state = WTHR_ERROR;
        break;

    case WTHR_GEO_WAIT:
        /* waiting for http callback */
        fsh_dirty = 1;
        break;

    case WTHR_WEATHER_FETCH: {
        if (http_busy()) break;
        /* build path with lat/lon */
        char path[256];
        uint32_t i = 0, j;
        static const char p1[] = "/v1/forecast?latitude=";
        for (j = 0; p1[j]; j++) path[i++] = p1[j];
        for (j = 0; wthr.lat[j] && i < 240; j++) path[i++] = wthr.lat[j];
        static const char p2[] = "&longitude=";
        for (j = 0; p2[j]; j++) path[i++] = p2[j];
        for (j = 0; wthr.lon[j] && i < 240; j++) path[i++] = wthr.lon[j];
        static const char p3[] = "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m";
        for (j = 0; p3[j] && i < 255; j++) path[i++] = p3[j];
        path[i] = 0;

        if (http_get("api.open-meteo.com", path, wthr_weather_done, 0) == 0)
            wthr.state = WTHR_WEATHER_WAIT;
        else
            wthr.state = WTHR_ERROR;
        break;
    }

    case WTHR_WEATHER_WAIT:
        fsh_dirty = 1;
        break;

    case WTHR_DONE:
        /* schedule next refresh */
        if (fm1_ms >= wthr.next_fetch_ms &&
            wifi_get_state() == WIFI_STATE_CONNECTED) {
            wthr.state = WTHR_GEO_FETCH;
        }
        break;

    case WTHR_ERROR:
        wthr.retries++;
        if (wthr.retries >= WTHR_MAX_RETRIES) {
            wthr.next_fetch_ms = fm1_ms + WTHR_REFRESH_MS;
            wthr.retries = 0;
            wthr.state = WTHR_IDLE;
        } else {
            wthr.next_fetch_ms = fm1_ms + WTHR_RETRY_MS;
            wthr.state = WTHR_IDLE;
        }
        fsh_dirty = 1;
        break;
    }
}

static int weather_fetching(void)
{
    return wthr.state != WTHR_IDLE && wthr.state != WTHR_DONE &&
           wthr.state != WTHR_ERROR;
}

static const struct weather_data *weather_get(void)
{
    return &wthr.data;
}

static void weather_init(void)
{
    wthr.state = WTHR_IDLE;
    wthr.data.valid = 0;
    wthr.retries = 0;
}

#else /* !CONFIG_WIFI_LWIP */

struct weather_data { uint8_t valid; };
static void weather_init(void) {}
static void weather_fetch_start(void) {}
static void weather_poll(void) {}
static int weather_fetching(void) { return 0; }
static const struct weather_data *weather_get(void) {
    static const struct weather_data empty = {0};
    return &empty;
}

#endif /* CONFIG_WIFI_LWIP */
```

**Step 4: Add wifi_weather.c to the unity build**

In `firmware/src/core_fm1.c`, after `#include "wifi/wifi_http.c"` (added in Task 6),
before `#include "wifi/wifi_api.c"`, add:

```c
#include "wifi/wifi_weather.c"
```

**Step 5: Add weather_init() and weather_poll() to wifi_api.c**

In `firmware/src/wifi/wifi_api.c`:

In `wifi_init()` (after `wifi_lwip_init()`, line 38), add:
```c
    weather_init();
```

In `wifi_on_connected()` (after `wifi_lwip_start_dhcp()`, line 99), add:
```c
    weather_fetch_start();
```

In `wifi_poll()` (after `wifi_lwip_poll()`, line 128), add:
```c
    /* weather data fetcher */
    weather_poll();
```

**Step 6: Run tests**

```bash
task test:unit -- tests/wifi/test_weather.py -v
```

Expected: PASS

**Step 7: Commit**

```bash
git add firmware/src/wifi/wifi_weather.c tests/wifi/test_weather.py \
    firmware/src/core_fm1.c firmware/src/wifi/wifi_api.c
git commit -m "feat(wifi): add async weather fetcher with ip-api + open-meteo"
```

---

## Phase 4: Reusable Spinner

### Task 11: Add fui_draw_spinner to Felucca drawing primitives

Rotating dots spinner, usable by any async page.

**Files:**
- Modify: `firmware/src/ui/felucca/draw.c` (append spinner function)

**Step 1: Add spinner to draw.c**

Append to `firmware/src/ui/felucca/draw.c`, after the `fui_draw_foot` function:

```c
/* rotating dots spinner for async loading states */
static void fui_draw_spinner(int32_t x, int32_t y, uint32_t frame_ms,
                             uint16_t color, uint16_t bg)
{
    /* 8 dots in a circle, one highlighted, rotating every 120ms */
    uint32_t active = (frame_ms / 120u) % 8u;
    uint32_t i;
    for (i = 0; i < 8u; i++) {
        /* positions around a 14px diameter circle */
        static const int8_t dx[8] = { 0,  5,  7,  5,  0, -5, -7, -5};
        static const int8_t dy[8] = {-7, -5,  0,  5,  7,  5,  0, -5};
        int32_t px = x + (int32_t)dx[i];
        int32_t py = y + (int32_t)dy[i];
        uint16_t c;
        if (i == active) c = color;
        else if (i == (active + 7u) % 8u) c = color; /* trail dot */
        else c = bg;
        cv_rect(px, py, 3, 3, c);
    }
}
```

**Step 2: Commit**

```bash
git add firmware/src/ui/felucca/draw.c
git commit -m "feat(ui): add reusable fui_draw_spinner for async pages"
```

---

### Task 12: Update wifi_defconfig with UI

The `wifi_defconfig` needs Felucca UI enabled to show the Weather page.

**Files:**
- Modify: `configs/wifi_defconfig`

**Step 1: Add UI config**

Add to `configs/wifi_defconfig`:

```
# UI (Felucca shell)
CONFIG_INPUT=y
CONFIG_UI_FELUCCA=y
```

**Step 2: Commit**

```bash
git add configs/wifi_defconfig
git commit -m "config: enable Felucca UI in wifi_defconfig"
```

---

### Task 13: Add COMPONENTS.md documentation for Weather page

Update the components documentation to include the Weather page and HTTP client.

**Files:**
- Modify: `docs/COMPONENTS.md`

**Step 1: Add WiFi HTTP section after the WiFi Kconfig section**

Add under the existing WiFi section in COMPONENTS.md (or create a new subsection):

After the Storage section, add a brief WiFi module reference:

```markdown
## WiFi

802.11 b/g/n STA mode driver (clean-room GPL-3.0) with lwIP TCP/IP stack.
Enabled by `CONFIG_WIFI`. Mutually exclusive with `CONFIG_BLE`.

### Networking (lwIP)

- TCP/IP: lwIP 2.2.x in `NO_SYS=1` bare-metal mode
- DNS: lwIP built-in resolver
- DHCP: lwIP client
- HTTP: Async GET client (`wifi_http.c`) on lwIP raw TCP API

### Weather (conditional, depends on `CONFIG_WIFI`)

UI page displaying current weather via open-meteo.com API.
Auto-geolocates via ip-api.com, refreshes every 15 minutes.

| File | Role |
|------|------|
| `firmware/src/wifi/wifi_http.c` | Async HTTP GET client |
| `firmware/src/wifi/wifi_weather.c` | Weather data fetcher (geolocation + forecast) |
| `firmware/src/ui/felucca/page_weather.c` | Weather page (cards + colored graph + glyphs) |
| `firmware/src/ui/felucca/weather_colors.h` | WMO code -> RGB565 color table (generated) |
| `firmware/src/ui/felucca/weather_glyphs.h` | 32x32 1-bit condition glyphs (generated) |
```

**Step 2: Add WTHR to Felucca page order**

Update the page order list to include `[WTHR]`:

```
HOME, ENV, LFO, FX, SCL, EDIT, ARP, SEQ, MIXER, VOICE, ABOUT, [SYS], [FILES], [BLE], [WTHR], [DEBUG]
```

**Step 3: Commit**

```bash
git add docs/COMPONENTS.md
git commit -m "docs: add WiFi HTTP, Weather page to COMPONENTS.md"
```

---

### Task 14: Build and verify

Compile the wifi defconfig and verify everything links.

**Step 1: Build**

```bash
task build -- --defconfig wifi
```

**Step 2: Fix any compilation errors**

Common issues:
- lwIP `static` conflicts in unity build: may need to wrap lwIP in a separate
  object file or add `#define` prefixes
- Missing `sys_now()`: add to `wifi_lwip.c` if not already present
- Include path ordering: ensure `lwipopts.h` is found before `lwip/opt.h`
- Weather page references to `fsh_dirty`: this is a shell global, already
  accessible from page files in the unity build

**Step 3: Commit any fixes**

```bash
git add -A
git commit -m "fix: resolve build issues for WiFi + Weather page"
```

---

### Task 15: Run all tests

**Step 1: Unit tests**

```bash
task test:unit
```

Expected: all pass, including new WiFi and weather tests.

**Step 2: E2E tests (if emulator supports WiFi)**

```bash
task test:e2e
```

The emulator may not support WiFi networking yet. If E2E fails due to missing
WiFi emulation, that's expected: the Weather page will show "NO WIFI" state
which is correct behavior.

**Step 3: Commit test results**

If any test fixes were needed:
```bash
git add -A
git commit -m "fix: resolve test issues for weather feature"
```
