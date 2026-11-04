/* SPDX-License-Identifier: GPL-3.0-only */
/* Debug DAP protocol handler: D_HELLO and D_LOG over USB SysEx.
 * Ported from fm1-dock (dock.c deploy_frame).  The host side is
 * tools/hbdeploy.py: hello(), log_text().
 *
 * Included in the unity build AFTER ota.c (needs ota_pack7, ota_wire_send). */

#ifdef CONFIG_DEBUG_DAP

#define DBG_D_HELLO 0x01u
#define DBG_D_LOG   0x20u
#define DBG_D_WIFI      0x25u
#define DBG_D_WIFI_BSS  0x26u
#define DBG_D_OK    0x00u
#define DBG_D_BAD   0x0Au

#if defined(CONFIG_WIFI) || defined(CONFIG_WIFI_NONFREE)
static uint32_t wifi_debug_fill(uint8_t *b);
static uint32_t wifi_debug_fill_bss(uint8_t *b, uint32_t max);
#endif
#ifdef CONFIG_WIFI_PROBES
#define DBG_D_PROBES    0x27u
static uint32_t wifi_probes_fill(uint8_t *b, uint32_t max);
#endif
#ifdef CONFIG_WIFI_DEBUG_WLCTL0_PROBE
#define DBG_D_WL0       0x29u
static void wifi_wl0_probe_request(uint32_t mode);
#endif
#ifdef CONFIG_WIFI
#define DBG_D_SWEEP     0x28u
static uint32_t wifi_sweep_fill(uint8_t *b, uint32_t max, const uint8_t *req, uint32_t len);
#endif

static uint8_t dbg_dec[60];               /* decoded incoming payload */
static uint8_t dbg_out[280];              /* outgoing SysEx frame */

static void dbg_reply(uint32_t cmd, uint32_t seq, const uint8_t *p, uint32_t n)
{
    uint8_t b[264];
    uint32_t w;
    if (n > 256u)
        return;
    memcpy(b, p, n);
    dbg_wr32(b + n, cfm1_dbg_crc32(0, p, n));
    dbg_out[0] = 0xF0;
    dbg_out[1] = 0x7D;
    dbg_out[2] = 0x48;
    dbg_out[3] = 0x42;
    dbg_out[4] = (uint8_t)(cmd | 0x40u);
    dbg_out[5] = (uint8_t)(seq & 0x7Fu);
    w = 6u + ota_pack7(b, n + 4u, dbg_out + 6);
    dbg_out[w++] = 0xF7;
    ota_wire_send(dbg_out, w);
}

static void cfm1_debug_poll(void)
{
    uint32_t cmd, seq, len;
    uint8_t r[48];
    uint32_t rn = 1;

    if (!cfm1_dbg_sx_ready)
        return;

    /* p[0]=7D p[1]=48 p[2]=42 p[3]=cmd p[4]=seq p[5..]=pack7(payload+CRC) */
    cmd = cfm1_dbg_sx[3];
    seq = cfm1_dbg_sx[4];
    len = ota_unpack7(cfm1_dbg_sx + 5, cfm1_dbg_sx_pos - 5u, dbg_dec, sizeof dbg_dec);

    cfm1_dbg_sx_ready = 0;

    if (len < 4u || cfm1_dbg_crc32(0, dbg_dec, len - 4u) != dbg_rd32(dbg_dec + len - 4u)) {
        r[0] = DBG_D_BAD;
        dbg_reply(0x3Fu, seq, r, 1);
        return;
    }
    len -= 4u;

    memset(r, 0, sizeof r);

    switch (cmd) {
    case DBG_D_HELLO: {
        uint32_t crash_pc = 0, crash_vec = 0;
        if (fm1_crash.magic == FM1_CRASH_MAGIC) {
            crash_pc  = fm1_crash.pc;
            crash_vec = fm1_crash.vec;
        }
        r[0] = DBG_D_OK;
        r[1] = 1;                                     /* proto version */
        r[2] = 0;                                     /* reserved (abi in dock) */
        r[3] = 0;                                     /* reserved (n_mods in dock) */
        r[4] = 0;
        r[5] = 0;
        dbg_wr32(r + 8,  0);                          /* largest (no modules) */
        dbg_wr32(r + 12, 0);                          /* free */
        dbg_wr32(r + 16, 0);                          /* max_file */
        dbg_wr32(r + 20, crash_pc);
        dbg_wr32(r + 24, crash_vec);
        dbg_wr32(r + 28, cfm1_dbg_nv.stats.worst);
        dbg_wr32(r + 32, cfm1_dbg_nv.stats.overruns);
        dbg_wr32(r + 36, cfm1_dbg_nv.log_w);
        dbg_wr32(r + 40, 0);                          /* block_ticks (unused) */
        rn = 44;
        break;
    }
    case DBG_D_LOG: {
        static uint8_t lb[200];
        uint32_t from = len >= 4u ? dbg_rd32(dbg_dec) : 0;
        uint32_t w = cfm1_dbg_nv.log_w, m = 0;
        if (w - from > CFM1_DBG_LOG_SIZE)
            from = w > CFM1_DBG_LOG_SIZE ? w - CFM1_DBG_LOG_SIZE : 0;
        if (from > w)
            from = w;
        lb[0] = DBG_D_OK;
        dbg_wr32(lb + 4, w);
        dbg_wr32(lb + 8, from);
        while (from + m < w && m < 180u) {
            lb[12 + m] = (uint8_t)cfm1_dbg_nv.log[(from + m) % CFM1_DBG_LOG_SIZE];
            m++;
        }
        dbg_reply(cmd, seq, lb, 12u + m);
        return;
    }
#if defined(CONFIG_WIFI) || defined(CONFIG_WIFI_NONFREE)
    case DBG_D_WIFI: {
        static uint8_t wb[48];
        rn = wifi_debug_fill(wb);
        dbg_reply(cmd, seq, wb, rn);
        return;
    }
    case DBG_D_WIFI_BSS: {
        static uint8_t bb[256];
        rn = wifi_debug_fill_bss(bb, sizeof bb);
        dbg_reply(cmd, seq, bb, rn);
        return;
    }
#endif
#ifdef CONFIG_WIFI_PROBES
    case DBG_D_PROBES: {
        static uint8_t pb[256];
        rn = wifi_probes_fill(pb, sizeof pb);
        dbg_reply(cmd, seq, pb, rn);
        return;
    }
#endif
#ifdef CONFIG_WIFI
    case DBG_D_SWEEP: {
        static uint8_t sb[248];                /* 8-byte header + 60 words */
        rn = wifi_sweep_fill(sb, sizeof sb, dbg_dec, len);
        if (rn) {
            dbg_reply(cmd, seq, sb, rn);
            return;
        }
        r[0] = DBG_D_BAD;
        rn = 1;
        break;
    }
#endif
#ifdef CONFIG_WIFI_DEBUG_WLCTL0_PROBE
    case DBG_D_WL0:
        wifi_wl0_probe_request(len >= 4u ? dbg_rd32(dbg_dec) : 0u);   /* 0: release + scan, 2: retry refused writes + scan */
        r[0] = DBG_D_OK;
        break;
#endif
    default:
        r[0] = DBG_D_BAD;
        break;
    }
    dbg_reply(cmd, seq, r, rn);
}

#endif /* CONFIG_DEBUG_DAP */
