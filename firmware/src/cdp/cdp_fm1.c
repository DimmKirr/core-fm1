/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1.* CDP domain handlers: firmware inspection and control. */

static void cdp_fm1_get_info(int32_t id, struct cdp_tx *tx)
{
    cdp_response_begin(tx, id);
    cdp_tx_key_str(tx, "firmware", "core-fm1");
    cdp_tx_char(tx, ',');
    cdp_tx_key_str(tx, "version", CONFIG_FIRMWARE_VERSION);
    cdp_tx_char(tx, ',');
#if defined(CONFIG_UI_FELUCCA)
    cdp_tx_key_str(tx, "lineage", "felucca");
#elif defined(CONFIG_UI_MELODEE)
    cdp_tx_key_str(tx, "lineage", "melodee");
#else
    cdp_tx_key_str(tx, "lineage", "core");
#endif
    cdp_tx_char(tx, ',');
    cdp_tx_key_uint(tx, "uptime_ms", fm1_ms);
    cdp_tx_char(tx, ',');
    cdp_tx_key_uint(tx, "wifi_ip", wifi_ip_addr);
    cdp_response_end(tx);
}

static void cdp_fm1_mem_read(const char *js, int ntokens,
                             const struct cdp_tok *tokens,
                             int32_t id, struct cdp_tx *tx)
{
    int ai = cdp_json_find(js, tokens, ntokens, "address");
    int li = cdp_json_find(js, tokens, ntokens, "length");
    if (ai < 0 || li < 0) {
        cdp_error(tx, id, -32602, "Missing address or length");
        return;
    }
    uint32_t addr = (uint32_t)cdp_tok_int(js, &tokens[ai]);
    uint32_t len  = (uint32_t)cdp_tok_int(js, &tokens[li]);
    if (len > 768) len = 768; /* keep response under WS_MAX_FRAME with base64 */

    cdp_response_begin(tx, id);
    cdp_tx_key_uint(tx, "address", addr);
    cdp_tx_char(tx, ',');
    cdp_tx_key_uint(tx, "length", len);
    cdp_tx_char(tx, ',');
    cdp_tx_char(tx, '"');
    cdp_tx_raw(tx, "data");
    cdp_tx_raw(tx, "\":\"");
    {
        char b64[1100];
        base64_encode((const uint8_t *)addr, len, b64);
        cdp_tx_raw(tx, b64);
    }
    cdp_tx_char(tx, '"');
    cdp_response_end(tx);
}

static void cdp_fm1_mem_write(const char *js, int ntokens,
                              const struct cdp_tok *tokens,
                              int32_t id, struct cdp_tx *tx)
{
    int ai = cdp_json_find(js, tokens, ntokens, "address");
    int di = cdp_json_find(js, tokens, ntokens, "data");
    if (ai < 0 || di < 0) {
        cdp_error(tx, id, -32602, "Missing address or data");
        return;
    }
    uint32_t addr = (uint32_t)cdp_tok_int(js, &tokens[ai]);
    char data_str[1400];
    uint32_t dlen = cdp_tok_str(js, &tokens[di], data_str, sizeof(data_str));
    uint8_t decoded[1024];
    uint32_t written = base64_decode(data_str, dlen, decoded);

    uint32_t i;
    uint8_t *dst = (uint8_t *)addr;
    for (i = 0; i < written; i++) dst[i] = decoded[i];

    cdp_response_begin(tx, id);
    cdp_tx_key_uint(tx, "written", written);
    cdp_response_end(tx);
}

static void cdp_fm1_audio_metrics(int32_t id, struct cdp_tx *tx)
{
    cdp_response_begin(tx, id);
#ifdef CONFIG_AUDIO
    cdp_tx_key_uint(tx, "render_ticks_last", fm1_audio_irq_elapsed_ticks);
    cdp_tx_char(tx, ',');
    cdp_tx_key_uint(tx, "sample_rate", 44100);
    cdp_tx_char(tx, ',');
    cdp_tx_key_uint(tx, "block_size", 128);
    cdp_tx_char(tx, ',');
    cdp_tx_key_int(tx, "voice_count", (int32_t)song.nvoice);
#else
    cdp_tx_key_str(tx, "error", "audio not configured");
#endif
    cdp_response_end(tx);
}

static void cdp_fm1_get_engine_state(int32_t id, struct cdp_tx *tx)
{
    cdp_response_begin(tx, id);
#ifdef CONFIG_AUDIO
    cdp_tx_key_int(tx, "engine_index", (int32_t)song.sel_eng);
    cdp_tx_char(tx, ',');
    cdp_tx_key_int(tx, "voice_count", (int32_t)song.nvoice);
#else
    cdp_tx_key_str(tx, "error", "audio not configured");
#endif
    cdp_response_end(tx);
}

static void cdp_fm1_send_midi(const char *js, int ntokens,
                              const struct cdp_tok *tokens,
                              int32_t id, struct cdp_tx *tx)
{
    /* params.data is an array of byte values: [status, data1, data2] */
    int di = cdp_json_find(js, tokens, ntokens, "data");
    if (di < 0 || tokens[di].type != 1) { /* type 1 = array */
        cdp_error(tx, id, -32602, "Missing data array");
        return;
    }

    /* parse the array contents (re-tokenize the array body) */
    uint32_t arr_start = tokens[di].start;
    uint32_t arr_end = tokens[di].end;
    const char *arr_js = js + arr_start;
    uint32_t arr_len = arr_end - arr_start;

    struct cdp_tok arr_toks[8];
    int an = cdp_json_parse(arr_js, arr_len, arr_toks, 8);

    /* first token is the array itself, values follow */
    uint8_t midi_msg[8];
    int midi_len = 0;
    int ti;
    for (ti = 1; ti < an && midi_len < 8; ti++) {
        if (arr_toks[ti].type == 3) { /* primitive (number) */
            midi_msg[midi_len++] = (uint8_t)cdp_tok_int(arr_js, &arr_toks[ti]);
        }
    }

    if (midi_len >= 2) {
        uint8_t status = midi_msg[0];
        uint8_t d1 = midi_msg[1];
        uint8_t d2 = midi_len > 2 ? midi_msg[2] : 0;
#ifdef CONFIG_AUDIO
        uint8_t type = status & 0xF0;
        if (type == 0x90 && d2 > 0) {
            trk_note_on(&trk[song.sel], d1, d2);
        } else if (type == 0x80 || (type == 0x90 && d2 == 0)) {
            trk_note_off(&trk[song.sel], d1);
        }
#endif
        (void)d1; (void)d2; (void)status;
    }

    cdp_response_begin(tx, id);
    cdp_tx_key_bool(tx, "ok", 1);
    cdp_response_end(tx);
}

static void cdp_fm1_press_button(const char *js, int ntokens,
                                 const struct cdp_tok *tokens,
                                 int32_t id, struct cdp_tx *tx)
{
    int bi = cdp_json_find(js, tokens, ntokens, "button");
    if (bi < 0) {
        cdp_error(tx, id, -32602, "Missing button");
        return;
    }
    int32_t btn_idx = cdp_tok_int(js, &tokens[bi]);

#ifdef CONFIG_INPUT
    /* simulate button press via input state */
    fm1_in.buttons |= (1u << (uint32_t)btn_idx);
#endif

    cdp_response_begin(tx, id);
    cdp_tx_key_bool(tx, "ok", 1);
    cdp_response_end(tx);
}

static void cdp_fm1_turn_encoder(const char *js, int ntokens,
                                 const struct cdp_tok *tokens,
                                 int32_t id, struct cdp_tx *tx)
{
    int ei = cdp_json_find(js, tokens, ntokens, "encoder");
    int ci = cdp_json_find(js, tokens, ntokens, "clicks");
    if (ei < 0 || ci < 0) {
        cdp_error(tx, id, -32602, "Missing encoder or clicks");
        return;
    }

    (void)ei; (void)ci; /* TODO: wire to fm1_enc when encoder injection API exists */

    cdp_response_begin(tx, id);
    cdp_tx_key_bool(tx, "ok", 1);
    cdp_response_end(tx);
}

static void cdp_fm1_get_log(int32_t id, struct cdp_tx *tx)
{
    cdp_response_begin(tx, id);
#ifdef CONFIG_DEBUG_LOG
    /* read last entries from log ring */
    cdp_tx_key_uint(tx, "log_pos", cfm1_log_pos);
    cdp_tx_char(tx, ',');
    cdp_tx_char(tx, '"');
    cdp_tx_raw(tx, "entries");
    cdp_tx_raw(tx, "\":[");
    {
        uint32_t start = cfm1_log_pos > 10 ? cfm1_log_pos - 10 : 0;
        uint32_t i;
        int first = 1;
        for (i = start; i < cfm1_log_pos && i < start + 10; i++) {
            if (!first) cdp_tx_char(tx, ',');
            first = 0;
            cdp_tx_char(tx, '"');
            const char *entry = cfm1_log_entry(i);
            if (entry) cdp_tx_raw(tx, entry);
            cdp_tx_char(tx, '"');
        }
    }
    cdp_tx_char(tx, ']');
#else
    cdp_tx_key_str(tx, "error", "debug log not configured");
#endif
    cdp_response_end(tx);
}
