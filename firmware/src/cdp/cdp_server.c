/* SPDX-License-Identifier: GPL-3.0-only */
/* CDP server: TCP listen -> WebSocket upgrade -> JSON-RPC dispatch.
 * Called from the main loop via cdp_poll(). */

#ifndef CONFIG_CDP_PORT
#define CONFIG_CDP_PORT 9222
#endif

static uint8_t cdp_active;

static void cdp_init(void)
{
    ws_init();
    tcp_listen_start(CONFIG_CDP_PORT);
    cdp_active = 1;
}

static void cdp_dispatch(const char *js, uint32_t len, struct cdp_tx *tx)
{
    struct cdp_tok tokens[CDP_JSON_MAX_TOKENS];
    int ntokens = cdp_json_parse(js, len, tokens, CDP_JSON_MAX_TOKENS);
    if (ntokens < 3) {
        cdp_error(tx, 0, -32700, "Parse error");
        return;
    }

    /* extract id and method */
    int id_idx = cdp_json_find(js, tokens, ntokens, "id");
    int meth_idx = cdp_json_find(js, tokens, ntokens, "method");
    if (id_idx < 0 || meth_idx < 0) {
        cdp_error(tx, 0, -32600, "Missing id or method");
        return;
    }

    int32_t id = cdp_tok_int(js, &tokens[id_idx]);
    char method[64];
    cdp_tok_str(js, &tokens[meth_idx], method, sizeof(method));

    /* find params sub-object (optional) */
    int params_idx = cdp_json_find(js, tokens, ntokens, "params");
    const char *params_js = js;
    const struct cdp_tok *params_toks = tokens;
    int params_ntokens = ntokens;

    /* re-tokenize params if present */
    struct cdp_tok ptokens[CDP_JSON_MAX_TOKENS];
    int pntokens = 0;
    if (params_idx >= 0 && tokens[params_idx].type == 0) {
        const char *pjs = js + tokens[params_idx].start;
        uint32_t plen = tokens[params_idx].end - tokens[params_idx].start;
        pntokens = cdp_json_parse(pjs, plen, ptokens, CDP_JSON_MAX_TOKENS);
        params_js = pjs;
        params_toks = ptokens;
        params_ntokens = pntokens;
    }

    /* dispatch by method name */
    if (method[0] == 'F' && method[1] == 'M' && method[2] == '1' && method[3] == '.') {
        const char *sub = method + 4;
        uint32_t i;
        int match = 1;

        /* FM1.getInfo */
        {
            const char *cmp = "getInfo";
            match = 1;
            for (i = 0; cmp[i]; i++) { if (sub[i] != cmp[i]) { match = 0; break; } }
            if (match && sub[i] == '\0') { cdp_fm1_get_info(id, tx); return; }
        }
        /* FM1.memoryRead */
        {
            const char *cmp = "memoryRead";
            match = 1;
            for (i = 0; cmp[i]; i++) { if (sub[i] != cmp[i]) { match = 0; break; } }
            if (match && sub[i] == '\0') { cdp_fm1_mem_read(params_js, params_ntokens, params_toks, id, tx); return; }
        }
        /* FM1.memoryWrite */
        {
            const char *cmp = "memoryWrite";
            match = 1;
            for (i = 0; cmp[i]; i++) { if (sub[i] != cmp[i]) { match = 0; break; } }
            if (match && sub[i] == '\0') { cdp_fm1_mem_write(params_js, params_ntokens, params_toks, id, tx); return; }
        }
        /* FM1.getAudioMetrics */
        {
            const char *cmp = "getAudioMetrics";
            match = 1;
            for (i = 0; cmp[i]; i++) { if (sub[i] != cmp[i]) { match = 0; break; } }
            if (match && sub[i] == '\0') { cdp_fm1_audio_metrics(id, tx); return; }
        }
        /* FM1.getEngineState */
        {
            const char *cmp = "getEngineState";
            match = 1;
            for (i = 0; cmp[i]; i++) { if (sub[i] != cmp[i]) { match = 0; break; } }
            if (match && sub[i] == '\0') { cdp_fm1_get_engine_state(id, tx); return; }
        }
        /* FM1.sendMidi */
        {
            const char *cmp = "sendMidi";
            match = 1;
            for (i = 0; cmp[i]; i++) { if (sub[i] != cmp[i]) { match = 0; break; } }
            if (match && sub[i] == '\0') { cdp_fm1_send_midi(params_js, params_ntokens, params_toks, id, tx); return; }
        }
        /* FM1.pressButton */
        {
            const char *cmp = "pressButton";
            match = 1;
            for (i = 0; cmp[i]; i++) { if (sub[i] != cmp[i]) { match = 0; break; } }
            if (match && sub[i] == '\0') { cdp_fm1_press_button(params_js, params_ntokens, params_toks, id, tx); return; }
        }
        /* FM1.turnEncoder */
        {
            const char *cmp = "turnEncoder";
            match = 1;
            for (i = 0; cmp[i]; i++) { if (sub[i] != cmp[i]) { match = 0; break; } }
            if (match && sub[i] == '\0') { cdp_fm1_turn_encoder(params_js, params_ntokens, params_toks, id, tx); return; }
        }
        /* FM1.getLog */
        {
            const char *cmp = "getLog";
            match = 1;
            for (i = 0; cmp[i]; i++) { if (sub[i] != cmp[i]) { match = 0; break; } }
            if (match && sub[i] == '\0') { cdp_fm1_get_log(id, tx); return; }
        }
    }

    /* standard-ish domains (minimal stubs) */

    /* Target.getTargetInfo (Playwright probes this) */
    {
        const char *cmp = "Target.getTargetInfo";
        uint32_t i;
        int match = 1;
        for (i = 0; cmp[i]; i++) { if (method[i] != cmp[i]) { match = 0; break; } }
        if (match && method[i] == '\0') {
            cdp_response_begin(tx, id);
            cdp_tx_raw(tx, "\"targetInfo\":{");
            cdp_tx_key_str(tx, "targetId", "fm1-0");
            cdp_tx_char(tx, ',');
            cdp_tx_key_str(tx, "type", "other");
            cdp_tx_char(tx, ',');
            cdp_tx_key_str(tx, "title", "FM-1 Synthesizer");
            cdp_tx_char(tx, ',');
            cdp_tx_key_str(tx, "url", "fm1://local");
            cdp_tx_char(tx, ',');
            cdp_tx_key_bool(tx, "attached", 1);
            cdp_tx_char(tx, '}');
            cdp_response_end(tx);
            return;
        }
    }

    cdp_error(tx, id, -32601, "Method not found");
}

/* Handle HTTP discovery request (GET /json/version) */
static int cdp_handle_http(const uint8_t *data, uint32_t len)
{
    /* check if this is a WebSocket upgrade request */
    uint32_t i;
    int is_upgrade = 0;
    {
        const char *needle = "Upgrade: websocket";
        uint32_t nlen = 18;
        for (i = 0; i + nlen <= len; i++) {
            uint32_t j, m = 1;
            for (j = 0; j < nlen; j++) {
                char a = (char)data[i+j];
                char b = needle[j];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) { m = 0; break; }
            }
            if (m) { is_upgrade = 1; break; }
        }
    }

    if (is_upgrade) {
        if (ws_handle_upgrade(data, len) == 0) {
            ws_state = WS_STATE_OPEN;
            return 1;
        }
        return -1;
    }

    /* HTTP GET /json/version */
    {
        const char *body =
            "{\"Browser\":\"FM-1/1.0\","
            "\"Protocol-Version\":\"1.0\","
            "\"webSocketDebuggerUrl\":\"ws://10.0.2.15:9222\"}";
        char resp[512];
        uint32_t rlen = 0;
        const char *hdr = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n";
        while (*hdr && rlen < 500) resp[rlen++] = *hdr++;
        while (*body && rlen < 510) resp[rlen++] = *body++;
        tcp_write((const uint8_t *)resp, rlen);
        tcp_close_conn();
        return 0;
    }
}

/* Main poll: called from firmware main loop */
static void cdp_poll(void)
{
    if (!cdp_active) return;
    if (!wifi_lwip_up) return;

    /* read TCP data */
    uint8_t tmp[512];
    uint32_t n = tcp_read(tmp, sizeof(tmp));
    if (n == 0) return;

    if (ws_state == WS_STATE_HTTP) {
        /* accumulate HTTP request */
        uint32_t i;
        for (i = 0; i < n && ws_rx_len < WS_MAX_FRAME; i++)
            ws_rx_buf[ws_rx_len++] = tmp[i];

        /* check for complete HTTP request (ends with \r\n\r\n) */
        if (ws_rx_len >= 4 &&
            ws_rx_buf[ws_rx_len-4] == '\r' && ws_rx_buf[ws_rx_len-3] == '\n' &&
            ws_rx_buf[ws_rx_len-2] == '\r' && ws_rx_buf[ws_rx_len-1] == '\n') {
            cdp_handle_http(ws_rx_buf, ws_rx_len);
            ws_rx_len = 0;
        }
        return;
    }

    if (ws_state == WS_STATE_OPEN) {
        /* accumulate WebSocket frames */
        uint32_t i;
        for (i = 0; i < n && ws_rx_len < WS_MAX_FRAME; i++)
            ws_rx_buf[ws_rx_len++] = tmp[i];

        /* try to decode a complete frame */
        while (ws_rx_len > 0) {
            uint8_t opcode;
            uint8_t *payload;
            uint32_t payload_len;
            int consumed = ws_decode_frame(ws_rx_buf, ws_rx_len,
                                           &opcode, &payload, &payload_len);
            if (consumed < 0) break; /* incomplete */

            if (opcode == WS_OP_TEXT) {
                struct cdp_tx tx;
                cdp_dispatch((const char *)payload, payload_len, &tx);
                if (tx.len > 0)
                    ws_send_text((const uint8_t *)tx.buf, tx.len);
            } else if (opcode == WS_OP_PING) {
                ws_send_pong(payload, payload_len);
            } else if (opcode == WS_OP_CLOSE) {
                uint8_t close_frame[4] = {0x88, 0x02, 0x03, 0xE8};
                tcp_write(close_frame, 4);
                tcp_close_conn();
                ws_init();
                return;
            }

            /* shift consumed bytes out of rx buffer */
            uint32_t rem = ws_rx_len - (uint32_t)consumed;
            for (i = 0; i < rem; i++)
                ws_rx_buf[i] = ws_rx_buf[(uint32_t)consumed + i];
            ws_rx_len = rem;
        }
    }
}
