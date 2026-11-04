/* SPDX-License-Identifier: GPL-3.0-only */
/* Clean-room implementation from docs/hardware/wifi/SPECSHEET.md and IEEE 802.11-2020.
 * No vendor code, IR or disassembly.
 * STA state machine: scan, auth, assoc, connection management.
 * Architecture informed by Ralink RT2x00 (GPL-2.0+) and ESP-IDF (Apache 2.0). */

/* ---- scan result storage ---- */

#define WIFI_STA_MAX_SCAN   8
#define WIFI_STA_SSID_MAX   32
#define WIFI_STA_DWELL_MS   100   /* per-channel dwell time during scan */
#define WIFI_STA_TIMEOUT_MS 3000  /* general state timeout */
#define WIFI_STA_KEEPALIVE_MS 30000

struct wifi_scan_entry {
    uint8_t bssid[6];
    uint8_t ssid[WIFI_STA_SSID_MAX];
    uint8_t ssid_len;
    uint8_t channel;
    int8_t  rssi;
    uint8_t security;  /* 0=open, 1=WPA, 2=WPA2 */
};

#define WIFI_SEC_OPEN 0
#define WIFI_SEC_WPA  1
#define WIFI_SEC_WPA2 2

static struct {
    wifi_state_t state;
    struct wifi_scan_entry results[WIFI_STA_MAX_SCAN];
    uint32_t nresults;
    uint32_t scan_ch;           /* current channel being scanned (1-13) */
    uint32_t scan_start_ms;     /* timestamp when current channel scan started */
    void (*scan_cb)(const char *ssid, int rssi, int channel);

    /* connection target */
    uint8_t target_ssid[WIFI_STA_SSID_MAX];
    uint8_t target_ssid_len;
    uint8_t target_bssid[6];
    uint8_t target_channel;
    uint8_t target_security;
    char    target_password[64];

    /* association state */
    uint16_t aid;               /* association ID from AP */
    uint16_t seq_num;           /* TX sequence number */

    /* timing */
    uint32_t state_enter_ms;
    uint32_t last_keepalive_ms;
    uint32_t last_rx_ms;

    uint8_t our_mac[6];

    uint8_t assoc_sent;
    uint32_t assoc_retry_ms;
} wifi_sta;

static uint32_t wifi_sta_now_ms(void)
{
    return fm1_ticks() / (FM1_TICKS_PER_US * 1000u);
}

/* ---- 802.11 management frame helpers ---- */

/* IEEE 802.11 frame control field bits */
#define FC_TYPE_MGMT       0x0000
#define FC_SUBTYPE_BEACON  0x0080
#define FC_SUBTYPE_PREQ    0x0040  /* probe request */
#define FC_SUBTYPE_PRESP   0x0050  /* probe response */
#define FC_SUBTYPE_AUTH    0x00B0
#define FC_SUBTYPE_ASSOCR  0x0000  /* assoc request */
#define FC_SUBTYPE_ASSOCS  0x0010  /* assoc response */
#define FC_SUBTYPE_DEAUTH  0x00C0
#define FC_SUBTYPE_DISASC  0x00A0

/* IEEE 802.11 information element IDs */
#define IE_SSID            0
#define IE_RATES           1
#define IE_DS_PARAM        3
#define IE_RSN             48
#define IE_VENDOR          221

/* wifi_bcast defined in wifi_mac.c (included before this file in the unity build) */

/* supported rates: 1, 2, 5.5, 11 Mbps (802.11b) + 6, 12, 24 Mbps (802.11g mandatory) */
static const uint8_t wifi_sta_rates[] = {0x82, 0x84, 0x8B, 0x96, 0x0C, 0x18, 0x30};

static uint16_t wifi_sta_rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static void wifi_sta_wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wifi_sta_mcpy(uint8_t *d, const uint8_t *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) d[i] = s[i];
}

static void wifi_sta_mset(uint8_t *d, uint8_t v, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) d[i] = v;
}

static int wifi_sta_mcmp(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    }
    return 0;
}

static uint32_t wifi_sta_slen(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

/* build 802.11 management frame header (24 bytes) */
static uint32_t wifi_sta_mgmt_hdr(uint8_t *buf, uint16_t fc,
                                  const uint8_t *da, const uint8_t *sa,
                                  const uint8_t *bssid)
{
    wifi_sta_wr16(buf + 0, fc);
    wifi_sta_wr16(buf + 2, 0);  /* duration */
    wifi_sta_mcpy(buf + 4, da, 6);
    wifi_sta_mcpy(buf + 10, sa, 6);
    wifi_sta_mcpy(buf + 16, bssid, 6);
    wifi_sta_wr16(buf + 22, wifi_sta.seq_num++ << 4);
    return 24;
}

/* append an information element */
static uint32_t wifi_sta_ie(uint8_t *buf, uint8_t id, const uint8_t *data, uint8_t len)
{
    buf[0] = id;
    buf[1] = len;
    if (len && data)
        wifi_sta_mcpy(buf + 2, data, len);
    return 2u + len;
}

/* ---- state transitions ---- */

static void wifi_sta_set_state(wifi_state_t s)
{
    cfm1_log_int("wifi: sta state=", (int32_t)s);
    wifi_sta.state = s;
    wifi_sta.state_enter_ms = wifi_sta_now_ms();
    if (s == WIFI_STATE_ASSOC)
        wifi_sta.assoc_sent = 0;
}

static int wifi_sta_timed_out(void)
{
    return (wifi_sta_now_ms() - wifi_sta.state_enter_ms) > WIFI_STA_TIMEOUT_MS;
}

/* ---- probe request ---- */

static void wifi_sta_send_probe_req(uint8_t channel)
{
    uint8_t frame[128];
    uint32_t pos;
    int tx_rc;

    wifi_rf_set_channel(channel);
    wifi_mac_front_end();

    pos = wifi_sta_mgmt_hdr(frame, FC_TYPE_MGMT | FC_SUBTYPE_PREQ,
                            wifi_bcast, wifi_sta.our_mac, wifi_bcast);

    /* SSID: wildcard (empty) for broadcast scan */
    pos += wifi_sta_ie(frame + pos, IE_SSID, NULL, 0);
    /* supported rates */
    pos += wifi_sta_ie(frame + pos, IE_RATES,
                       wifi_sta_rates, sizeof wifi_sta_rates);

    tx_rc = wifi_mac_tx(frame, pos);
    cfm1_log_int("wifi: probe tx=", (int32_t)tx_rc);
    if (wifi_sta.scan_ch <= 2) {
        /* TX-side behaviour [IP:RT2x00]:
         *  qpcnt   TXRXQ_PCNT sampled before the WDMA ack: 0 = the frame never
         *          entered a MAC TX queue, [7:0] = TX0Q depth
         *  mstat   MAC_STATUS_CFG: [0] BBP/RF busy TX, [1] busy RX
         *  tsfifo  TX_STA_FIFO: [0] valid, [5] success, [15:8] WCID
         *  tx_cnt0 [15:0] fail, [31:16] beacon;  tx_cnt1 [15:0] success, [31:16] retry */
        cfm1_log_int("wifi: qpcnt=", (int32_t)wifi_sdio_state.tx_qpcnt);
        cfm1_log_int("wifi: txgate=", (int32_t)wifi_sdio_state.tx_gate);
        cfm1_log_int("wifi: txg_pcnt=", (int32_t)wifi_sdio_state.tx_gate_pcnt);
        cfm1_log_int("wifi: txg_434=", (int32_t)wifi_sdio_state.tx_gate_qsta);
        cfm1_log_int("wifi: txg_43c=", (int32_t)wifi_sdio_state.tx_gate_dbg);
        cfm1_log_int("wifi: mstat=", (int32_t)WIFI_MAC_STATUS);
        cfm1_log_int("wifi: tsfifo=", (int32_t)WIFI_TX_STA_FIFO);
        cfm1_log_int("wifi: tx_cnt0=", (int32_t)WIFI_TX_STA_CNT0);
        cfm1_log_int("wifi: tx_cnt1=", (int32_t)WIFI_TX_STA_CNT1);
    }
}

/* ---- scan result parsing ---- */

static void wifi_sta_parse_beacon(const uint8_t *frame, uint32_t len)
{
    const uint8_t *body;
    uint32_t body_len, ie_off, i;
    struct wifi_scan_entry *e;

    /* header(24) + fixed: timestamp(8) + interval(2) + capability(2) */
    if (len < 24 + 12) return;

    body = frame + 24;
    body_len = len - 24;
    ie_off = 12;

    /* BSSID = addr2 (transmitter) in beacons */
    /* check for duplicate */
    e = NULL;
    for (i = 0; i < wifi_sta.nresults; i++) {
        if (wifi_sta_mcmp(wifi_sta.results[i].bssid, frame + 10, 6) == 0) {
            e = &wifi_sta.results[i];
            break;
        }
    }
    if (!e) {
        if (wifi_sta.nresults >= WIFI_STA_MAX_SCAN) {
            /* replace weakest signal */
            uint32_t weakest = 0;
            int8_t weakest_rssi = 0;
            for (i = 0; i < WIFI_STA_MAX_SCAN; i++) {
                if (i == 0 || wifi_sta.results[i].rssi < weakest_rssi) {
                    weakest = i;
                    weakest_rssi = wifi_sta.results[i].rssi;
                }
            }
            e = &wifi_sta.results[weakest];
        } else {
            e = &wifi_sta.results[wifi_sta.nresults++];
        }
    }

    wifi_sta_mcpy(e->bssid, frame + 10, 6);
    e->rssi = 0; /* TODO: read RSSI from MAC RX descriptor */
    e->security = WIFI_SEC_OPEN;
    e->ssid_len = 0;
    e->channel = 0;

    /* walk information elements */
    while (ie_off + 2 <= body_len) {
        uint8_t ie_id = body[ie_off];
        uint8_t ie_len = body[ie_off + 1];
        const uint8_t *ie_data = body + ie_off + 2;

        if (ie_off + 2 + ie_len > body_len) break;

        switch (ie_id) {
        case IE_SSID:
            if (ie_len <= WIFI_STA_SSID_MAX) {
                wifi_sta_mcpy(e->ssid, ie_data, ie_len);
                e->ssid_len = ie_len;
            }
            break;
        case IE_DS_PARAM:
            if (ie_len >= 1) e->channel = ie_data[0];
            break;
        case IE_RSN:
            e->security = WIFI_SEC_WPA2;
            break;
        case IE_VENDOR:
            /* WPA1 OUI: 00:50:F2 type 1 */
            if (ie_len >= 4 && ie_data[0] == 0x00 && ie_data[1] == 0x50 &&
                ie_data[2] == 0xF2 && ie_data[3] == 0x01) {
                if (e->security < WIFI_SEC_WPA)
                    e->security = WIFI_SEC_WPA;
            }
            break;
        }
        ie_off += 2u + ie_len;
    }
}

/* ---- authentication (IEEE 802.11 Open System) ---- */

static void wifi_sta_send_auth(void)
{
    uint8_t frame[30];
    uint32_t pos;

    pos = wifi_sta_mgmt_hdr(frame, FC_TYPE_MGMT | FC_SUBTYPE_AUTH,
                            wifi_sta.target_bssid, wifi_sta.our_mac,
                            wifi_sta.target_bssid);

    wifi_sta_wr16(frame + pos, 0); pos += 2; /* algorithm: Open System */
    wifi_sta_wr16(frame + pos, 1); pos += 2; /* sequence: 1 */
    wifi_sta_wr16(frame + pos, 0); pos += 2; /* status: reserved */

    wifi_mac_tx(frame, pos);
}

static int wifi_sta_parse_auth_resp(const uint8_t *frame, uint32_t len)
{
    if (len < 24 + 6) return -1;
    if (wifi_sta_mcmp(frame + 10, wifi_sta.target_bssid, 6) != 0) return -1;
    if (wifi_sta_rd16(frame + 24) != 0) return -1; /* algorithm */
    if (wifi_sta_rd16(frame + 26) != 2) return -1; /* sequence */
    if (wifi_sta_rd16(frame + 28) != 0) return -1; /* status */
    return 0;
}

/* ---- association ---- */

static void wifi_sta_send_assoc_req(void)
{
    uint8_t frame[200];
    uint32_t pos;

    pos = wifi_sta_mgmt_hdr(frame, FC_TYPE_MGMT | FC_SUBTYPE_ASSOCR,
                            wifi_sta.target_bssid, wifi_sta.our_mac,
                            wifi_sta.target_bssid);

    /* capability: ESS + short preamble */
    wifi_sta_wr16(frame + pos, 0x0021); pos += 2;
    /* listen interval */
    wifi_sta_wr16(frame + pos, 10); pos += 2;

    /* SSID */
    pos += wifi_sta_ie(frame + pos, IE_SSID,
                       wifi_sta.target_ssid, wifi_sta.target_ssid_len);
    /* supported rates */
    pos += wifi_sta_ie(frame + pos, IE_RATES,
                       wifi_sta_rates, sizeof wifi_sta_rates);

    /* RSN IE for WPA2 */
    if (wifi_sta.target_security == WIFI_SEC_WPA2) {
        /* version(2), group CCMP(4), pairwise count(2)+CCMP(4),
         * AKM count(2)+PSK(4), capabilities(2) = 20 bytes */
        static const uint8_t rsn_ie[] = {
            0x01, 0x00,             /* version 1 */
            0x00, 0x0F, 0xAC, 0x04, /* group cipher: CCMP */
            0x01, 0x00,             /* pairwise count */
            0x00, 0x0F, 0xAC, 0x04, /* pairwise: CCMP */
            0x01, 0x00,             /* AKM count */
            0x00, 0x0F, 0xAC, 0x02, /* AKM: PSK */
            0x00, 0x00              /* RSN capabilities */
        };
        pos += wifi_sta_ie(frame + pos, IE_RSN, rsn_ie, sizeof rsn_ie);
    }

    wifi_mac_tx(frame, pos);
}

static int wifi_sta_parse_assoc_resp(const uint8_t *frame, uint32_t len)
{
    if (len < 24 + 6) return -1;
    if (wifi_sta_mcmp(frame + 10, wifi_sta.target_bssid, 6) != 0) return -1;
    /* body: capability(2) + status(2) + AID(2) */
    if (wifi_sta_rd16(frame + 26) != 0) return -1; /* status */
    wifi_sta.aid = wifi_sta_rd16(frame + 28) & 0x3FFF;
    return 0;
}

/* ---- keepalive: null data frame ---- */

static void wifi_sta_send_null_data(void)
{
    uint8_t frame[24];

    /* type=data(0x08), subtype=null(0x40), to-DS(0x01) -> 0x0148 */
    wifi_sta_wr16(frame + 0, 0x0148);
    wifi_sta_wr16(frame + 2, 0);
    /* to-DS: addr1=BSSID, addr2=SA, addr3=BSSID */
    wifi_sta_mcpy(frame + 4, wifi_sta.target_bssid, 6);
    wifi_sta_mcpy(frame + 10, wifi_sta.our_mac, 6);
    wifi_sta_mcpy(frame + 16, wifi_sta.target_bssid, 6);
    wifi_sta_wr16(frame + 22, wifi_sta.seq_num++ << 4);

    wifi_mac_tx(frame, 24);
}

/* ---- management frame RX dispatch ---- */

static void wifi_sta_rx_mgmt(const uint8_t *frame, uint32_t len)
{
    uint16_t fc;

    if (len < 24) return;
    fc = wifi_sta_rd16(frame);

    /* management frames: type field [3:2] == 0 */
    if ((fc & 0x000C) != 0x0000) return;

    switch (fc & 0x00F0) {
    case 0x0080: /* beacon */
    case 0x0050: /* probe response */
        if (wifi_sta.state == WIFI_STATE_SCANNING)
            wifi_sta_parse_beacon(frame, len);
        break;

    case 0x00B0: /* authentication */
        if (wifi_sta.state == WIFI_STATE_AUTH) {
            if (wifi_sta_parse_auth_resp(frame, len) == 0) {
                cfm1_log("wifi: auth ok");
                wifi_sta_set_state(WIFI_STATE_ASSOC);
            }
        }
        break;

    case 0x0010: /* association response */
        if (wifi_sta.state == WIFI_STATE_ASSOC) {
            if (wifi_sta_parse_assoc_resp(frame, len) == 0) {
                cfm1_log_int("wifi: assoc ok aid=", (int32_t)wifi_sta.aid);
                if (wifi_sta.target_security >= WIFI_SEC_WPA) {
                    wifi_sta_set_state(WIFI_STATE_WPA);
                } else {
                    wifi_sta_set_state(WIFI_STATE_CONNECTED);
                    wifi_on_connected();
                }
            }
        }
        break;

    case 0x00C0: /* deauthentication */
        if (wifi_sta.state >= WIFI_STATE_AUTH &&
            wifi_sta_mcmp(frame + 10, wifi_sta.target_bssid, 6) == 0) {
            cfm1_log("wifi: rx deauth");
            if (wifi_sta.state == WIFI_STATE_CONNECTED)
                wifi_on_disconnected();
            wifi_sta_set_state(WIFI_STATE_IDLE);
        }
        break;

    case 0x00A0: /* disassociation */
        if (wifi_sta.state >= WIFI_STATE_ASSOC &&
            wifi_sta_mcmp(frame + 10, wifi_sta.target_bssid, 6) == 0) {
            cfm1_log("wifi: rx disassoc");
            if (wifi_sta.state == WIFI_STATE_CONNECTED)
                wifi_on_disconnected();
            wifi_sta_set_state(WIFI_STATE_IDLE);
        }
        break;
    }
}

/* ---- init, connect, disconnect ---- */

static void wifi_sta_init(const uint8_t *mac)
{
    wifi_sta_mset((uint8_t *)&wifi_sta, 0, sizeof wifi_sta);
    wifi_sta_mcpy(wifi_sta.our_mac, mac, 6);
    wifi_sta.state = WIFI_STATE_IDLE;
    cfm1_log("wifi: sta init");
    cfm1_log_hex("wifi: sta mac=", mac, 6);
}

static int wifi_sta_start_scan(void (*cb)(const char *ssid, int rssi, int channel))
{
    if (wifi_sta.state != WIFI_STATE_IDLE) return -1;
    cfm1_log("wifi: scan start");
    cfm1_log_hex("wifi: scan mac=", wifi_sta.our_mac, 6);
    wifi_sta.nresults = 0;
    wifi_sta.scan_ch = 1;
    wifi_sta.scan_cb = cb;
    wifi_sta.scan_start_ms = wifi_sta_now_ms();
    wifi_sta_set_state(WIFI_STATE_SCANNING);
    cfm1_log_int("wifi: scan wdma_ctl=", (int32_t)WDMA_CTL);
    cfm1_log_int("wifi: scan irq_en=", (int32_t)WIFI_MAC_IRQ_EN);
    wifi_sta_send_probe_req(1);
    return 0;
}

/* find best scan result matching ssid -> index, or -1 */
static int wifi_sta_find_ap(const char *ssid)
{
    uint32_t slen = wifi_sta_slen(ssid);
    uint32_t i;
    int best = -1;
    int8_t best_rssi = -128;

    for (i = 0; i < wifi_sta.nresults; i++) {
        if (wifi_sta.results[i].ssid_len == slen &&
            wifi_sta_mcmp(wifi_sta.results[i].ssid,
                          (const uint8_t *)ssid, slen) == 0) {
            if (best < 0 || wifi_sta.results[i].rssi > best_rssi) {
                best = (int)i;
                best_rssi = wifi_sta.results[i].rssi;
            }
        }
    }
    return best;
}

static int wifi_sta_start_connect(const char *ssid, const char *password)
{
    int idx;
    uint32_t slen, plen, i;
    struct wifi_scan_entry *ap;

    idx = wifi_sta_find_ap(ssid);
    if (idx < 0) {
        cfm1_log("wifi: sta AP not found");
        return -1;
    }

    ap = &wifi_sta.results[idx];
    slen = wifi_sta_slen(ssid);
    plen = password ? wifi_sta_slen(password) : 0;

    wifi_sta_mcpy(wifi_sta.target_ssid, (const uint8_t *)ssid, slen);
    wifi_sta.target_ssid_len = (uint8_t)slen;
    wifi_sta_mcpy(wifi_sta.target_bssid, ap->bssid, 6);
    wifi_sta_mcpy(wifi_sta_bssid, ap->bssid, 6);
    wifi_mac_set_bssid(ap->bssid);
    wifi_sta.target_channel = ap->channel;
    wifi_sta.target_security = ap->security;

    if (plen > 0 && plen < sizeof wifi_sta.target_password) {
        for (i = 0; i < plen; i++)
            wifi_sta.target_password[i] = password[i];
        wifi_sta.target_password[plen] = '\0';
    } else {
        wifi_sta.target_password[0] = '\0';
    }

    wifi_rf_set_channel(ap->channel);
    wifi_mac_front_end();
    cfm1_log("wifi: sta connecting");
    cfm1_log_int("wifi: sta ch=", (int32_t)ap->channel);
    cfm1_log_hex("wifi: sta bssid=", ap->bssid, 6);

    wifi_sta_set_state(WIFI_STATE_AUTH);
    wifi_sta_send_auth();
    return 0;
}

static void wifi_sta_do_disconnect(void)
{
    cfm1_log("wifi: sta disconnect");
    if (wifi_sta.state >= WIFI_STATE_AUTH) {
        uint8_t frame[26];
        uint32_t pos;
        pos = wifi_sta_mgmt_hdr(frame, FC_TYPE_MGMT | FC_SUBTYPE_DEAUTH,
                                wifi_sta.target_bssid, wifi_sta.our_mac,
                                wifi_sta.target_bssid);
        /* reason: STA is leaving (3) */
        wifi_sta_wr16(frame + pos, 3);
        pos += 2;
        wifi_mac_tx(frame, pos);
    }
    wifi_mac_clear_bssid();
    wifi_sta_set_state(WIFI_STATE_IDLE);
}

/* ---- main loop poll ---- */

static void wifi_sta_poll(void)
{
    uint32_t now = wifi_sta_now_ms();

    switch (wifi_sta.state) {
    case WIFI_STATE_OFF:
    case WIFI_STATE_IDLE:
        break;

    case WIFI_STATE_SCANNING:
        if ((now - wifi_sta.scan_start_ms) >= WIFI_STA_DWELL_MS) {
            /* deliver results found on current channel */
            if (wifi_sta.scan_cb) {
                uint32_t i;
                for (i = 0; i < wifi_sta.nresults; i++) {
                    struct wifi_scan_entry *e = &wifi_sta.results[i];
                    if (e->channel == wifi_sta.scan_ch && e->ssid_len > 0) {
                        char buf[WIFI_STA_SSID_MAX + 1];
                        uint32_t j;
                        for (j = 0; j < e->ssid_len; j++)
                            buf[j] = (char)e->ssid[j];
                        buf[e->ssid_len] = '\0';
                        wifi_sta.scan_cb(buf, (int)e->rssi, (int)e->channel);
                    }
                }
            }
            cfm1_log_int("wifi: scan ch=", (int32_t)wifi_sta.scan_ch);
            cfm1_log_int("wifi: scan rx=", (int32_t)wifi_sdio_state.rx_count);
            /* Per channel (12m): rx0 [15:0] CRC / [31:16] PHY errors, rx1
             * [15:0] false CCA / [31:16] PLCP errors (read-to-clear), the
             * channel code in WF_CON7 and whether the RF die applied the LUT
             * entry: divider 0x1193C [12:5], bank 0x11938 [25:19]. */
            cfm1_log_int("wifi: rx0=", (int32_t)WIFI_RX_STA_CNT0);
            cfm1_log_int("wifi: rx1=", (int32_t)WIFI_RX_STA_CNT1);
            cfm1_log_int("wifi: wcon7=", (int32_t)FM1_RF_REG(0x1405Cu));
            cfm1_log_int("wifi: pll=", (int32_t)FM1_RF_REG(0x11938u));
            cfm1_log_int("wifi: fine=", (int32_t)FM1_RF_REG(0x1193Cu));
            if (wifi_sta.scan_ch <= 2) {
                /* RX-side behaviour [IP:RT2x00]: rx counters are read-to-clear,
                 * so each value is the count since the previous read.
                 *  mac_rx0 [15:0] CRC errors, [31:16] PHY errors
                 *  mac_rx1 [15:0] false CCA,  [31:16] PLCP errors
                 * Any non-zero value means the baseband detected energy. */
                cfm1_log_int("wifi: mac_rx0=", (int32_t)WIFI_RX_STA_CNT0);
                cfm1_log_int("wifi: mac_rx1=", (int32_t)WIFI_RX_STA_CNT1);
                cfm1_log_int("wifi: isr_n=", (int32_t)wifi_isr_count);
                /* CCA occupancy over the 100 ms dwell [IP:RT2x00 CH_IDLE/BUSY_STA]:
                 * busy/(idle+busy) near 1 means carrier sense is stuck and the
                 * MAC defers every transmission. */
                cfm1_log_int("wifi: ch_idle=", (int32_t)WIFI_CH_IDLE_STA);
                cfm1_log_int("wifi: ch_busy=", (int32_t)WIFI_CH_BUSY_STA);
                cfm1_log_int("wifi: ch_bsec=", (int32_t)WIFI_CH_BUSY_SEC);
                /* Diffs still open against the vendor after iteration 11.
                 * RF analog and MAC config readbacks were removed: all equal. */
                cfm1_log_int("wifi: wctl=", (int32_t)WDMA_CTL);       /* vendor 0x10037 (bit 16) */
                cfm1_log_int("wifi: sirq=", (int32_t)WIFI_SOC_IRQ_EN); /* vendor 0x00C80001 (bit 23) */
                /* SoC wireless control block [SPECSHEET §4.2, §4.5] for the
                 * vendor comparison: bus gates, clock tree, unknown configs */
                cfm1_log_int("wifi: wctl0=", (int32_t)(*(volatile uint32_t *)0x14000u));
                cfm1_log_int("wifi: wctl8=", (int32_t)(*(volatile uint32_t *)0x14008u));
                cfm1_log_int("wifi: clk0=", (int32_t)(*(volatile uint32_t *)0x14040u));
                cfm1_log_int("wifi: clk3=", (int32_t)(*(volatile uint32_t *)0x1404Cu));
                cfm1_log_int("wifi: cfg54=", (int32_t)(*(volatile uint32_t *)0x14054u));
                cfm1_log_int("wifi: cfg58=", (int32_t)(*(volatile uint32_t *)0x14058u));
                cfm1_log_int("wifi: cfg60=", (int32_t)(*(volatile uint32_t *)0x14060u));
                cfm1_log_int("wifi: sysclk2=", (int32_t)(*(volatile uint32_t *)0x10010u));
                cfm1_log_int("wifi: sysclk1=", (int32_t)(*(volatile uint32_t *)0x10008u));
                cfm1_log_int("wifi: wlid=", (int32_t)(*(volatile uint32_t *)0x1400Cu));
                cfm1_log_int("wifi: cfg90=", (int32_t)(*(volatile uint32_t *)0x14090u));
                cfm1_log_int("wifi: mac_sys=", (int32_t)WIFI_MAC_SYS_CTRL);
                cfm1_log_int("wifi: txpin=", (int32_t)WIFI_TX_PIN_CFG);
                cfm1_log_int("wifi: txband=", (int32_t)WIFI_TX_BAND_CFG);
                cfm1_log_int("wifi: pbfmax=", (int32_t)WIFI_PBF_MAX_PCNT);
                /* txadr == txbuf means WDMA_TX_ADR reads back the DMA start we
                 * wrote; txadr > txbuf means the engine advances it (end pointer).
                 * Needed to interpret the vendor TX ring samples. */
                cfm1_log_int("wifi: txadr=", (int32_t)WDMA_TX_ADR);
                cfm1_log_int("wifi: txbuf=", (int32_t)(uint32_t)&wifi_tx_buf[0]);
                cfm1_log_int("wifi: txlen=", (int32_t)WDMA_TX_LEN);
                /* Baseband snapshot, once per scan, for the 1:1 BBP comparison */
                if (wifi_sta.scan_ch == 1) {
                    wifi_bbp_snapshot();
                    wifi_ind_log("scan", 1);
                }
            }
            wifi_sta.scan_ch++;
            if (wifi_sta.scan_ch > 13) {
                cfm1_log_int("wifi: scan done n=", (int32_t)wifi_sta.nresults);
                cfm1_log_int("wifi: rx total=", (int32_t)wifi_sdio_state.rx_count);
                /* log BSS table entries */
                {
                    uint32_t bi;
                    for (bi = 0; bi < wifi_bss_count; bi++) {
                        struct wifi_bss *b = &wifi_bss_table[bi];
                        if (!b->in_use) continue;
                        cfm1_log_hex("wifi: bss=", b->bssid, 6);
                        if (b->ssid_len > 0)
                            cfm1_log(b->ssid);
                        cfm1_log_int("wifi:  ch=", (int32_t)b->channel);
                    }
                }
                wifi_sta_set_state(WIFI_STATE_IDLE);
            } else {
                wifi_sta.scan_start_ms = now;
                wifi_sta_send_probe_req((uint8_t)wifi_sta.scan_ch);
            }
        }
        break;

    case WIFI_STATE_AUTH:
        if (wifi_sta_timed_out()) {
            cfm1_log("wifi: auth timeout");
            wifi_sta_set_state(WIFI_STATE_IDLE);
        }
        break;

    case WIFI_STATE_ASSOC:
        if (!wifi_sta.assoc_sent) {
            wifi_sta_send_assoc_req();
            wifi_sta.assoc_sent = 1;
            wifi_sta.assoc_retry_ms = now + 500u;
        } else if ((int32_t)(now - wifi_sta.assoc_retry_ms) >= 0) {
            wifi_sta_send_assoc_req();
            wifi_sta.assoc_retry_ms = now + 500u;
        }
        if (wifi_sta_timed_out()) {
            cfm1_log("wifi: assoc timeout");
            wifi_sta_set_state(WIFI_STATE_IDLE);
        }
        break;

    case WIFI_STATE_WPA:
        /* the 4-way handshake is driven by wifi_wpa via EAPOL frames;
         * completion calls wifi_sta_wpa_complete() */
        if (wifi_sta_timed_out()) {
            cfm1_log("wifi: wpa timeout");
            wifi_sta_set_state(WIFI_STATE_IDLE);
        }
        break;

    case WIFI_STATE_CONNECTED:
        if ((now - wifi_sta.last_keepalive_ms) >= WIFI_STA_KEEPALIVE_MS) {
            wifi_sta_send_null_data();
            wifi_sta.last_keepalive_ms = now;
        }
        break;
    }
}

/* called by wifi_wpa when the 4-way handshake succeeds */
static void wifi_sta_wpa_complete(void)
{
    if (wifi_sta.state == WIFI_STATE_WPA) {
        cfm1_log("wifi: wpa handshake ok");
        wifi_sta_set_state(WIFI_STATE_CONNECTED);
        wifi_on_connected();
    }
}

/* called by the MAC RX path for management frames */
static void wifi_sta_on_rx(const uint8_t *frame, uint32_t len)
{
    wifi_sta.last_rx_ms = wifi_sta_now_ms();
    wifi_sta_rx_mgmt(frame, len);
}
