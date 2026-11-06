/* SPDX-License-Identifier: GPL-3.0-only */
/* Clean-room implementation from docs/hardware/wifi/SPECSHEET.md and IEEE 802.11-2020.
 * No vendor code, IR or disassembly.
 *
 * 802.11 MAC frame parsing and generation. Frame type/subtype definitions,
 * management frame builders, beacon/probe response parser, RX dispatch.
 * Information Element handling per IEEE 802.11-2020 section 9.4.2. */

/* ---- 802.11 frame control field (IEEE 802.11-2020, 9.2.4.1) ---- */

/* type field [3:2] of frame control (little-endian byte 0) */
#define WIFI_FC_TYPE_MGMT   0x00
#define WIFI_FC_TYPE_CTRL   0x04
#define WIFI_FC_TYPE_DATA   0x08

/* subtype field [7:4] of frame control (little-endian byte 0) */
#define WIFI_FC_STYPE_ASSOC_REQ   0x00
#define WIFI_FC_STYPE_ASSOC_RSP   0x10
#define WIFI_FC_STYPE_PROBE_REQ   0x40
#define WIFI_FC_STYPE_PROBE_RSP   0x50
#define WIFI_FC_STYPE_BEACON      0x80
#define WIFI_FC_STYPE_DISASSOC    0xA0
#define WIFI_FC_STYPE_AUTH        0xB0
#define WIFI_FC_STYPE_DEAUTH      0xC0

/* frame control flags (little-endian byte 1) */
#define WIFI_FC_TODS    0x0100
#define WIFI_FC_FROMDS  0x0200

/* IEEE 802.11 Information Element IDs (9.4.2.1) */
#define WIFI_IE_SSID            0
#define WIFI_IE_SUPP_RATES      1
#define WIFI_IE_DS_PARAMS       3
#define WIFI_IE_RSN             48
#define WIFI_IE_EXT_SUPP_RATES  50

/* ---- 802.11 MAC header (IEEE 802.11-2020, 9.2.4) ---- */

struct wifi_hdr {
    uint16_t frame_control;
    uint16_t duration;
    uint8_t  addr1[6]; /* receiver / destination */
    uint8_t  addr2[6]; /* transmitter / source */
    uint8_t  addr3[6]; /* BSSID or other */
    uint16_t seq_ctrl;
};

#define WIFI_HDR_SIZE 24

/* ---- BSS table (scan results, struct wifi_bss in cfm1_wifi.h) ---- */

static struct wifi_bss wifi_bss_table[WIFI_MAX_BSS];
static uint32_t wifi_bss_count;

/* ---- local state ---- */

static uint8_t wifi_mac_addr[6];
static uint16_t wifi_mac_seq_num;
static const uint8_t wifi_bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/* IEEE 802.11b/g mandatory rates (9.4.2.3) */
static const uint8_t wifi_rates[] = {
    0x82, /* 1 Mbps, basic */
    0x84, /* 2 Mbps, basic */
    0x8B, /* 5.5 Mbps, basic */
    0x96, /* 11 Mbps, basic */
    0x0C, /* 6 Mbps */
    0x12, /* 9 Mbps */
    0x18, /* 12 Mbps */
    0x24, /* 18 Mbps */
};

/* ---- helpers ---- */

static int wifi_mac_addr_eq(const uint8_t *a, const uint8_t *b)
{
    uint32_t i;
    for (i = 0; i < 6; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static void wifi_mac_addr_copy(uint8_t *dst, const uint8_t *src)
{
    uint32_t i;
    for (i = 0; i < 6; i++)
        dst[i] = src[i];
}

static void wifi_mac_memcpy(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        dst[i] = src[i];
}

static uint16_t wifi_mac_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void wifi_mac_put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

/* ---- sequence number ---- */

static void wifi_mac_set_seq(struct wifi_hdr *hdr)
{
    hdr->seq_ctrl = (uint16_t)((wifi_mac_seq_num & 0x0FFFu) << 4);
    wifi_mac_seq_num++;
}

/* ---- header builder ---- */

static void wifi_mac_fill_hdr(struct wifi_hdr *hdr, uint16_t fc,
                               const uint8_t *da, const uint8_t *bssid)
{
    hdr->frame_control = fc;
    hdr->duration = 0;
    wifi_mac_addr_copy(hdr->addr1, da);
    wifi_mac_addr_copy(hdr->addr2, wifi_mac_addr);
    wifi_mac_addr_copy(hdr->addr3, bssid);
    wifi_mac_set_seq(hdr);
}

/* ---- Information Element builder ---- */

static uint8_t *wifi_mac_append_ie(uint8_t *p, uint8_t id,
                                    const uint8_t *data, uint8_t len)
{
    uint8_t i;
    *p++ = id;
    *p++ = len;
    for (i = 0; i < len; i++)
        *p++ = data[i];
    return p;
}

/* ---- beacon / probe response parser (IEEE 802.11-2020, 9.3.3.3) ----
 *
 * Fixed fields after MAC header: Timestamp(8) + Beacon Interval(2) + Capability(2) = 12 bytes,
 * followed by tagged IEs. */

static void wifi_mac_parse_beacon(const uint8_t *frame, uint32_t len,
                                  int8_t rssi)
{
    const struct wifi_hdr *hdr;
    const uint8_t *fixed, *ies;
    uint32_t ie_len, pos;
    struct wifi_bss bss;
    int slot;
    uint32_t i;

    if (len < WIFI_HDR_SIZE + 12)
        return;

    hdr = (const struct wifi_hdr *)frame;
    fixed = frame + WIFI_HDR_SIZE;
    ies = fixed + 12;
    ie_len = len - WIFI_HDR_SIZE - 12;

    wifi_mac_addr_copy(bss.bssid, hdr->addr2);
    bss.ssid[0] = 0;
    bss.ssid_len = 0;
    bss.channel = 0;
    bss.rssi = rssi;
    bss.has_rsn = 0;
    bss.beacon_interval = wifi_mac_le16(fixed + 8);
    bss.capability = wifi_mac_le16(fixed + 10);

    /* walk IEs (IEEE 802.11-2020, 9.4.2) */
    pos = 0;
    while (pos + 2 <= ie_len) {
        uint8_t id = ies[pos];
        uint8_t elen = ies[pos + 1];
        const uint8_t *val = &ies[pos + 2];

        if (pos + 2 + elen > ie_len)
            break;

        if (id == WIFI_IE_SSID && elen <= WIFI_MAX_SSID_LEN) {
            for (i = 0; i < elen; i++)
                bss.ssid[i] = (char)val[i];
            bss.ssid[elen] = 0;
            bss.ssid_len = elen;
        } else if (id == WIFI_IE_DS_PARAMS && elen >= 1) {
            bss.channel = val[0];
        } else if (id == WIFI_IE_RSN) {
            bss.has_rsn = 1;
        }
        pos += 2 + elen;
    }

    /* update BSS table: replace existing BSSID, add, or evict weakest */
    slot = -1;
    for (i = 0; i < wifi_bss_count; i++) {
        if (wifi_mac_addr_eq(wifi_bss_table[i].bssid, bss.bssid)) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0 && wifi_bss_count < WIFI_MAX_BSS) {
        slot = (int)wifi_bss_count++;
    } else if (slot < 0) {
        /* table full: evict weakest if incoming is stronger */
        int weakest = 0;
        int8_t weakest_rssi = wifi_bss_table[0].rssi;
        for (i = 1; i < WIFI_MAX_BSS; i++) {
            if (wifi_bss_table[i].rssi < weakest_rssi) {
                weakest = (int)i;
                weakest_rssi = wifi_bss_table[i].rssi;
            }
        }
        if (rssi > weakest_rssi)
            slot = weakest;
    }
    if (slot >= 0) {
        int is_new = !wifi_bss_table[slot].in_use;
        bss.in_use = 1;
        wifi_bss_table[slot] = bss;
        if (is_new) {
            cfm1_log_hex("wifi: rx bss=", bss.bssid, 6);
            if (bss.ssid_len > 0)
                cfm1_log(bss.ssid);
        }
        /* keep table sorted by RSSI descending (strongest first) */
        while (slot > 0 &&
               wifi_bss_table[slot].rssi > wifi_bss_table[slot - 1].rssi) {
            struct wifi_bss tmp = wifi_bss_table[slot - 1];
            wifi_bss_table[slot - 1] = wifi_bss_table[slot];
            wifi_bss_table[slot] = tmp;
            slot--;
        }
        while (slot + 1 < (int)wifi_bss_count &&
               wifi_bss_table[slot].rssi < wifi_bss_table[slot + 1].rssi) {
            struct wifi_bss tmp = wifi_bss_table[slot + 1];
            wifi_bss_table[slot + 1] = wifi_bss_table[slot];
            wifi_bss_table[slot] = tmp;
            slot++;
        }
    }
}

static uint8_t wifi_mac_strlen(const char *s)
{
    uint8_t n = 0;
    while (s[n] && n < WIFI_MAX_SSID_LEN)
        n++;
    return n;
}

static int wifi_mac_tx(const uint8_t *frame, uint32_t len)
{
    return wifi_sdio_tx(frame, len);
}

/* ---- management frame TX ---- */

/* probe request (IEEE 802.11-2020, 9.3.3.10) */
static int wifi_mac_tx_probe_req(const char *ssid, uint8_t channel)
{
    uint8_t frame[256];
    struct wifi_hdr *hdr = (struct wifi_hdr *)frame;
    uint8_t *p = frame + WIFI_HDR_SIZE;
    uint8_t slen;
    uint32_t total;

    (void)channel;
    wifi_mac_fill_hdr(hdr, WIFI_FC_TYPE_MGMT | WIFI_FC_STYPE_PROBE_REQ,
                      wifi_bcast, wifi_bcast);

    slen = ssid ? wifi_mac_strlen(ssid) : 0;
    p = wifi_mac_append_ie(p, WIFI_IE_SSID, (const uint8_t *)ssid, slen);
    p = wifi_mac_append_ie(p, WIFI_IE_SUPP_RATES, wifi_rates, sizeof wifi_rates);

    total = (uint32_t)(p - frame);
    return wifi_sdio_tx(frame, total);
}

/* authentication (IEEE 802.11-2020, 9.3.3.12): Open System, seq 1 */
static int wifi_mac_tx_auth(const uint8_t *bssid, uint16_t algo, uint16_t seq_nr)
{
    uint8_t frame[64];
    struct wifi_hdr *hdr = (struct wifi_hdr *)frame;
    uint8_t *p = frame + WIFI_HDR_SIZE;
    uint32_t total;

    wifi_mac_fill_hdr(hdr, WIFI_FC_TYPE_MGMT | WIFI_FC_STYPE_AUTH,
                      bssid, bssid);

    /* fixed fields: Algorithm Number(2) + Auth Seq(2) + Status Code(2) */
    wifi_mac_put_le16(p, algo);  p += 2;
    wifi_mac_put_le16(p, seq_nr); p += 2;
    wifi_mac_put_le16(p, 0);     p += 2; /* status: success */

    total = (uint32_t)(p - frame);
    return wifi_sdio_tx(frame, total);
}

/* association request (IEEE 802.11-2020, 9.3.3.6) */
static int wifi_mac_tx_assoc_req(const uint8_t *bssid, const char *ssid, uint8_t ssid_len)
{
    uint8_t frame[320];
    struct wifi_hdr *hdr = (struct wifi_hdr *)frame;
    uint8_t *p = frame + WIFI_HDR_SIZE;
    uint32_t total;

    /* RSN IE for WPA2-PSK with CCMP (IEEE 802.11-2020, 9.4.2.25) */
    static const uint8_t rsn_ie[] = {
        0x01, 0x00,             /* RSN version 1 */
        0x00, 0x0F, 0xAC, 0x04, /* group cipher: CCMP-128 */
        0x01, 0x00,             /* pairwise cipher count: 1 */
        0x00, 0x0F, 0xAC, 0x04, /* pairwise cipher: CCMP-128 */
        0x01, 0x00,             /* AKM count: 1 */
        0x00, 0x0F, 0xAC, 0x02, /* AKM type: PSK */
        0x00, 0x00,             /* RSN capabilities */
    };

    wifi_mac_fill_hdr(hdr, WIFI_FC_TYPE_MGMT | WIFI_FC_STYPE_ASSOC_REQ,
                      bssid, bssid);

    /* fixed fields: Capability Info(2) + Listen Interval(2) */
    wifi_mac_put_le16(p, 0x0431); p += 2; /* ESS, short preamble, short slot */
    wifi_mac_put_le16(p, 10);     p += 2; /* listen interval: 10 beacons */

    if (ssid_len > WIFI_MAX_SSID_LEN)
        ssid_len = WIFI_MAX_SSID_LEN;
    p = wifi_mac_append_ie(p, WIFI_IE_SSID, (const uint8_t *)ssid, ssid_len);
    p = wifi_mac_append_ie(p, WIFI_IE_SUPP_RATES, wifi_rates, sizeof wifi_rates);
    p = wifi_mac_append_ie(p, WIFI_IE_RSN, rsn_ie, sizeof rsn_ie);

    total = (uint32_t)(p - frame);
    return wifi_sdio_tx(frame, total);
}

/* deauthentication (IEEE 802.11-2020, 9.3.3.1) */
static int wifi_mac_tx_deauth(const uint8_t *bssid, uint16_t reason)
{
    uint8_t frame[32];
    struct wifi_hdr *hdr = (struct wifi_hdr *)frame;
    uint8_t *p = frame + WIFI_HDR_SIZE;
    uint32_t total;

    wifi_mac_fill_hdr(hdr, WIFI_FC_TYPE_MGMT | WIFI_FC_STYPE_DEAUTH,
                      bssid, bssid);

    wifi_mac_put_le16(p, reason); p += 2;

    total = (uint32_t)(p - frame);
    return wifi_sdio_tx(frame, total);
}

/* ---- data frame TX (IEEE 802.11-2020, 9.2.4, 9.3.2) ----
 *
 * Converts an Ethernet frame (14-byte header + payload) to an 802.11 data
 * frame with LLC/SNAP encapsulation and To-DS bit set (STA -> AP). */

static int wifi_mac_tx_data(struct wifi_buf *buf)
{
    struct wifi_hdr wh;
    uint16_t ethertype;
    uint8_t *payload;
    uint32_t payload_len, pos, i;
    static uint8_t wifi_mac_tx_buf[WIFI_BUF_SIZE];
    uint8_t *hp;

    if (!buf || buf->len < 14)
        return -1;

    /* 802.11 header: To-DS, addr1=BSSID, addr2=SA, addr3=DA */
    wh.frame_control = WIFI_FC_TYPE_DATA | WIFI_FC_TODS;
    wh.duration = 0;
    wifi_mac_addr_copy(wh.addr1, wifi_sta_bssid);
    wifi_mac_addr_copy(wh.addr2, wifi_mac_addr);
    wifi_mac_addr_copy(wh.addr3, buf->data); /* DA from ethernet header */
    wifi_mac_set_seq(&wh);

    ethertype = ((uint16_t)buf->data[12] << 8) | buf->data[13];
    payload = buf->data + 14;
    payload_len = buf->len - 14;

    /* assemble: wifi_hdr(24) + LLC/SNAP(8) + payload */
    pos = 0;
    hp = (uint8_t *)&wh;
    for (i = 0; i < WIFI_HDR_SIZE; i++)
        wifi_mac_tx_buf[pos++] = hp[i];

    /* LLC/SNAP header (IEEE 802.2 + RFC 1042) */
    wifi_mac_tx_buf[pos++] = 0xAA;
    wifi_mac_tx_buf[pos++] = 0xAA;
    wifi_mac_tx_buf[pos++] = 0x03;
    wifi_mac_tx_buf[pos++] = 0x00;
    wifi_mac_tx_buf[pos++] = 0x00;
    wifi_mac_tx_buf[pos++] = 0x00;
    wifi_mac_tx_buf[pos++] = (uint8_t)(ethertype >> 8);
    wifi_mac_tx_buf[pos++] = (uint8_t)(ethertype & 0xFF);

    for (i = 0; i < payload_len && pos < sizeof wifi_mac_tx_buf; i++)
        wifi_mac_tx_buf[pos++] = payload[i];

    return wifi_sdio_tx(wifi_mac_tx_buf, pos);
}

/* ---- RX dispatch ---- */

static uint32_t wifi_mac_rx_dbg;

static void wifi_mac_rx(uint8_t *data, uint32_t len, int8_t rssi)
{
    const struct wifi_hdr *hdr;
    uint16_t fc;
    uint8_t type, stype;

    if (len < WIFI_HDR_SIZE)
        return;

    hdr = (const struct wifi_hdr *)data;
    fc = hdr->frame_control;
    type = (uint8_t)(fc & 0x0C);
    stype = (uint8_t)(fc & 0xF0);

    if (wifi_mac_rx_dbg < 10u) {
        cfm1_log_int("wifi: mac rx fc=", (int32_t)fc);
        cfm1_log_int("wifi: mac rx len=", (int32_t)len);
        wifi_mac_rx_dbg++;
    }

    if (type == WIFI_FC_TYPE_MGMT) {
        if (stype == WIFI_FC_STYPE_BEACON || stype == WIFI_FC_STYPE_PROBE_RSP)
            wifi_mac_parse_beacon(data, len, rssi);
        wifi_sta_rx_mgmt(data, len, rssi);
    } else if (type == WIFI_FC_TYPE_DATA) {
        /* RXWI MPDU length excludes L2PAD but buffer contains it;
         * inflate len once here so all downstream functions are consistent */
        {
            uint32_t mhdr = WIFI_HDR_SIZE;
            if (fc & 0x0080) mhdr += 2; /* QoS */
            len += ((mhdr + 3u) & ~3u) - mhdr;
        }
        if (len >= 10 && wifi_mac_addr_eq(data + 4, wifi_mac_addr)) {
            cfm1_log_int("wifi: rx data4us fc=", (int32_t)fc);
            cfm1_log_int("wifi: rx data4us len=", (int32_t)len);
            cfm1_log_hex("wifi: d4us hdr=", data, len > 28 ? 28 : len);
            if (len > 26)
                cfm1_log_hex("wifi: d4us snap=", data + 26, len > 40 ? 14 : len - 26);
        }
        wifi_data_rx(data, len);
    }
}

/* MAC_SYS_CTRL bits [IP:RT2x00, SPECSHEET §7.2] */
#define MAC_TX_EN  (1u << 2)
#define MAC_RX_EN  (1u << 3)

/* RX_FILTER_CFG bit definitions [RT5350 §3.20.3.5]
 * 0 = accept (disable drop), 1 = drop (enable drop) */
#define RXFILT_DROP_CRC_ERR    (1u << 0)
#define RXFILT_DROP_PHY_ERR    (1u << 1)
#define RXFILT_DROP_UC_NOME    (1u << 2)
#define RXFILT_DROP_NOT_MYBSS  (1u << 3)
#define RXFILT_DROP_VER_ERR    (1u << 4)
#define RXFILT_DROP_MC         (1u << 5)
#define RXFILT_DROP_BC         (1u << 6)
#define RXFILT_DROP_DUPL       (1u << 7)
#define RXFILT_DROP_CFACK      (1u << 8)
#define RXFILT_DROP_CFEND      (1u << 9)
#define RXFILT_DROP_ACK        (1u << 10)
#define RXFILT_DROP_CTS        (1u << 11)
#define RXFILT_DROP_RTS        (1u << 12)
#define RXFILT_DROP_PSPOLL     (1u << 13)
#define RXFILT_DROP_BA         (1u << 14)
#define RXFILT_DROP_BAR        (1u << 15)
#define RXFILT_DROP_CTRL_RSV   (1u << 16)

/* Linux rt2800 config_filter(FIF_ALLMULTI): drop control/error junk,
 * accept data+mgmt from all BSSes. NOT_MYBSS=0 for scan. [IP:RT2x00] */
#define RX_FILTER_INIT  (RXFILT_DROP_CRC_ERR | RXFILT_DROP_PHY_ERR     \
                       | RXFILT_DROP_UC_NOME  | RXFILT_DROP_VER_ERR     \
                       | RXFILT_DROP_DUPL     | RXFILT_DROP_CFACK       \
                       | RXFILT_DROP_CFEND    | RXFILT_DROP_ACK         \
                       | RXFILT_DROP_CTS      | RXFILT_DROP_RTS         \
                       | RXFILT_DROP_PSPOLL   | RXFILT_DROP_BAR         \
                       | RXFILT_DROP_CTRL_RSV)  /* = 0x1BF97 */

/* Scanning: clear NOT_TO_ME so we hear everything while MAC addr is unverified */
#define RX_FILTER_SCAN  (RX_FILTER_INIT & ~RXFILT_DROP_UC_NOME)  /* = 0x1BF93 */

/* After association: tighten to our BSS only */
#define RX_FILTER_ASSOC (RX_FILTER_INIT | RXFILT_DROP_NOT_MYBSS) /* = 0x1BF9F */

static void wifi_mac_init_registers(void)
{
    volatile uint32_t dummy;

    cfm1_log("wifi: mac regs init");

    /* RX_FILTER_CFG (0x31400) [IP:RT2x00, rt2800.h line 1756]
     * Hardware default likely has DROP_NOT_MYBSS=1, which drops beacons
     * during scanning. Set permissive scan filter: accept from all BSSes,
     * clear NOT_TO_ME until MAC addr matching is verified on AC791N. */
    cfm1_log_int("wifi: rxfilt pre=", (int32_t)WIFI_RX_FILTER_CFG);
    WIFI_RX_FILTER_CFG = RX_FILTER_SCAN;
    cfm1_log_int("wifi: rxfilt post=", (int32_t)WIFI_RX_FILTER_CFG);

    /* AUTO_RSP_CFG (0x31404) [SPECSHEET §7.10 MACRegTable]
     * bit0=AUTORESPONDER, bit1=BAC_ACK_POLICY, bit4=CCK_SHORT */
    WIFI_AUTO_RSP_CFG = 0x13u;

    /* LEGACY_BASIC_RATE (0x31408) [IP:RT2x00, rt2800lib.c line 5863] */
    WIFI_LEGACY_BASIC_RATE = 0x015Fu;

    /* HT_BASIC_RATE (0x3140C) [IP:RT2x00, rt2800lib.c line 5864] */
    WIFI_HT_BASIC_RATE = 0x8003u;

    /* MAX_LEN_CFG (0x31018) [IP:RT2x00, rt2800lib.c line 6046]
     * Vendor value 0x000A3FFF: bits 17,19 enable A-MPDU + aggregate. */
    WIFI_MAX_LEN_CFG = 0x000A3FFFu;

    /* TX_LINK_CFG (0x31350) [vendor baseline: 0x007F1020] */
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1350u) = 0x007F1020u;

    /* EDCA_TID_AC_MAP (0x31340) [SPECSHEET §7.10 MACRegTable]
     * Maps 802.11e TID values to hardware AC queues */
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1340u) = 0x0000583Fu;

    /* TX_TIMEOUT_CFG (0x31344) [SPECSHEET §7.10 MACRegTable] */
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1344u) = 0x00092B20u;

    /* TX_RETRY_CFG (0x31348) [vendor baseline] */
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1348u) = 0x000F1290u;

    /* TX_RTS_CFG (0x3134C) [SPECSHEET §7.10 MACRegTable] */
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x134Cu) = 0x47D01F0Fu;

    /* Protection configs [vendor baseline, iteration 7 observation] */
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x136Cu) = 0x01754004u;
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1364u) = 0x07F40000u;  /* OFDM_PROT_CFG */
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1368u) = 0x07F40000u;  /* MM20_PROT_CFG */
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1370u) = 0x03F54084u;  /* MM40_PROT_CFG */
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1374u) = 0x01754004u;  /* GF20_PROT_CFG */
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1378u) = 0x03F54084u;  /* GF40_PROT_CFG */
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x137Cu) = 0x0038013Au;
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1380u) = 0x002400CAu;  /* EXP_ACK_TIME */

    /* EDCA queue parameters [vendor baseline, iteration 7 observation] */
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1300u) = 0x000A4300u;  /* EDCA_AC0 (Best Effort) */
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1304u) = 0x000A4700u;  /* EDCA_AC1 (Background) */
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1308u) = 0x00043538u;  /* EDCA_AC2 (Video) */
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x130Cu) = 0x0003222Fu;  /* EDCA_AC3 (Voice) */

    /* Rate protection [SPECSHEET §7.10 MACRegTable] */
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x040Cu) = 0x1F3FBF9Fu;  /* CCK_PROT_CFG */
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x042Cu) = 0xF8F0E8E0u;  /* LEGACY_PROT_CFG */
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x0430u) = 0x6F77D0C8u;  /* HT_PROT_CFG */
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x0408u) = 0x00F40006u;

    /* Remaining MACRegTable entries [SPECSHEET §7.10] */
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1608u) = 0x00000002u;
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1204u) = 0x00000003u;

    /* Clear error counters by reading them (read-to-clear) */
    dummy = WIFI_RX_STA_CNT0;
    dummy = WIFI_RX_STA_CNT1;
    dummy = WIFI_RX_STA_CNT2;
    dummy = WIFI_TX_STA_CNT0;
    dummy = WIFI_TX_STA_CNT1;
    dummy = WIFI_TX_STA_CNT2;
    (void)dummy;

    cfm1_log("wifi: mac regs done");
}

/* MAC front end after every channel switch [SPECSHEET §4.2.1 C2-C3, §14 rows
 * 3-4, M:s wl_wifi_sta.a, 2026-11-04]: TX_BAND_CFG band select (bits [2:1]
 * cleared, bit 2 set for 2.4 GHz), then TX_PIN_CFG = 0x00050302 (RT2800 bit
 * names: PA_PE_G0_EN, LNA_PE_A0_EN, LNA_PE_G0_EN, RFTR_EN, TRSW_EN). The GPL
 * driver never wrote either register before iteration 12l: with the LNA and
 * RF switch enables off, the baseband hears noise but never a frame [I]. */
static void wifi_mac_front_end(void)
{
    WIFI_TX_BAND_CFG = (WIFI_TX_BAND_CFG & ~0x6u) | 0x4u;
    WIFI_TX_PIN_CFG = 0x00050302u;
}

static void wifi_mac_init_wcid_tables(void)
{
    uint32_t i;

    cfm1_log("wifi: wcid table init");

    /* Invalidate all 254 station WCID slots (MAC+0x1800, 8 bytes each)
     * [SPECSHEET §4.2.1 T8, IP:RT2x00 rt2800lib.c] */
    for (i = 0; i < 254; i++) {
        *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1800u + 8u * i) = 0xFFFFFFFFu;
        *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1804u + 8u * i) = 0x0000FFFFu;
    }

    /* Attribute table: 256 entries, 4 bytes each at MAC+0x6800.
     * Bit 0 = key-table select (1 = pairwise), [3:1] = cipher (0 = none).
     * Init value 1: pairwise-key table, no cipher [SPECSHEET §4.2.1 B5] */
    for (i = 0; i < 256; i++)
        *(volatile uint32_t *)(WIFI_MAC_BASE + 0x6800u + 4u * i) = 1u;

    /* Shared key mode registers: MAC+0x7000..0x700C (4 words)
     * 0 at init [SPECSHEET §4.2.1 B5]; cipher codes written at key install */
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x7000u) = 0u;
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x7004u) = 0u;
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x7008u) = 0u;
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x700Cu) = 0u;

    /* Disable TSF/beacon sync: MAC+0x1114 [19:16] = 0 [IP:RT2x00] */
    {
        uint32_t val = *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1114u);
        val &= ~0x001F0000u;
        *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1114u) = val;
    }

    cfm1_log("wifi: wcid table done");
}

static void wifi_mac_init(void)
{
    uint32_t i;
    cfm1_log("wifi: mac init");
    wifi_bss_count = 0;
    wifi_mac_seq_num = 0;
    for (i = 0; i < WIFI_MAX_BSS; i++)
        wifi_bss_table[i].in_use = 0;

    wifi_mac_init_registers();
    wifi_mac_init_wcid_tables();
    WIFI_MAC_SYS_CTRL = MAC_TX_EN | MAC_RX_EN;
    cfm1_log_int("wifi: mac sys_ctrl=", (int32_t)WIFI_MAC_SYS_CTRL);

    /* Try to read MAC from SDIO (chip OTP at fn1 offset 0x1000).
     * If SDIO is broken, generate a locally-administered dev MAC
     * from the chip's unique ID so we have something usable. */
    {
        uint8_t otp[6];
        int got_mac = 0;
        if (wifi_sdio_state.initialized && wifi_sdio_state.error_count == 0) {
            if (wifi_sdio_cmd53_read(1, 0x1000u, otp, 6, 1) == 0 &&
                (otp[0] | otp[1] | otp[2] | otp[3] | otp[4] | otp[5]) != 0 &&
                !(otp[0] & 0x01u)) {  /* reject multicast MACs */
                for (i = 0; i < 6; i++)
                    wifi_mac_addr[i] = otp[i];
                got_mac = 1;
            }
        }
        if (!got_mac) {
            /* locally-administered dev MAC: 02:FE:FM:10:xx:xx
             * Last two bytes from P33 reset source for per-unit variation */
            uint8_t p3 = fm1_p33_read(FM1_P3_RST_SRC);
            wifi_mac_addr[0] = 0x02;  /* locally administered, unicast */
            wifi_mac_addr[1] = 0xFE;
            wifi_mac_addr[2] = 0xF1;
            wifi_mac_addr[3] = 0x10;
            wifi_mac_addr[4] = p3;
            wifi_mac_addr[5] = (uint8_t)(fm1_ticks() & 0xFFu);
            cfm1_log("wifi: mac generated (dev)");
        }
    }
#ifdef CONFIG_WIFI_MAC_OVERRIDE
    {
        static const char ov[] = CONFIG_WIFI_MAC_OVERRIDE;
        uint8_t mac[6];
        uint32_t n = 0, k = 0, acc = 0;
        const char *s = ov;
        for (; *s && n < 6; s++) {
            uint32_t d;
            if (*s >= '0' && *s <= '9')      d = (uint32_t)(*s - '0');
            else if (*s >= 'a' && *s <= 'f') d = (uint32_t)(*s - 'a' + 10);
            else if (*s >= 'A' && *s <= 'F') d = (uint32_t)(*s - 'A' + 10);
            else continue;
            acc = (acc << 4) | d;
            if (++k == 2) { mac[n++] = (uint8_t)acc; acc = 0; k = 0; }
        }
        if (n == 6) {
            for (i = 0; i < 6; i++)
                wifi_mac_addr[i] = mac[i];
            cfm1_log("wifi: mac override (CONFIG_WIFI_MAC_OVERRIDE)");
        }
    }
#endif
    cfm1_log_hex("wifi: mac=", wifi_mac_addr, 6);

    /* Write MAC to the operational register at 0x1008/0x100C [IP:RT2x00].
     * The 0x0300 register is OTP/EEPROM mirror; the RX filter uses 0x1008.
     * DW1 bits [23:16] = UNICAST_TO_ME_MASK: 0xFF means all 6 bytes must
     * match for NOT_TO_ME filter. Without this, NOT_TO_ME drops everything. */
    {
        uint32_t dw0 = (uint32_t)wifi_mac_addr[0]
                     | ((uint32_t)wifi_mac_addr[1] << 8)
                     | ((uint32_t)wifi_mac_addr[2] << 16)
                     | ((uint32_t)wifi_mac_addr[3] << 24);
        uint32_t dw1 = (uint32_t)wifi_mac_addr[4]
                     | ((uint32_t)wifi_mac_addr[5] << 8)
                     | (0xFFu << 16);  /* UNICAST_TO_ME_MASK */
        WIFI_MAC_ADDR_HI0 = dw0;
        WIFI_MAC_ADDR_HI1 = dw1;
        cfm1_log_int("wifi: mac_hi0=", (int32_t)WIFI_MAC_ADDR_HI0);
        cfm1_log_int("wifi: mac_hi1=", (int32_t)WIFI_MAC_ADDR_HI1);
    }
}

/* Write AP MAC to WCID slot 1 and clear its attribute to no-cipher.
 * Required before auth so EAPOL (data) frames are accepted. [SPECSHEET §15 T20] */
static void wifi_mac_set_wcid1(const uint8_t *mac)
{
    uint32_t dw0 = (uint32_t)mac[0]
                 | ((uint32_t)mac[1] << 8)
                 | ((uint32_t)mac[2] << 16)
                 | ((uint32_t)mac[3] << 24);
    uint32_t dw1 = (uint32_t)mac[4]
                 | ((uint32_t)mac[5] << 8);
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x1808u) = dw0;
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x180Cu) = dw1;
    *(volatile uint32_t *)(WIFI_MAC_BASE + 0x6804u) = 1u;
}

/* Write BSSID to hardware filter register and tighten RX filter.
 * Called when associating to an AP. [IP:RT2x00, rt2800.h line 764] */
static void wifi_mac_set_bssid(const uint8_t *bssid)
{
    uint32_t dw0 = (uint32_t)bssid[0]
                 | ((uint32_t)bssid[1] << 8)
                 | ((uint32_t)bssid[2] << 16)
                 | ((uint32_t)bssid[3] << 24);
    uint32_t dw1 = (uint32_t)bssid[4]
                 | ((uint32_t)bssid[5] << 8);
    WIFI_MAC_BSSID_DW0 = dw0;
    WIFI_MAC_BSSID_DW1 = dw1;
    /* keep scan filter during auth/assoc; tighten after 4-way handshake */
    cfm1_log_hex("wifi: bssid hw=", bssid, 6);
}

/* Tighten RX filter after association + WPA completes. */
static void wifi_mac_tighten_filter(void)
{
    WIFI_RX_FILTER_CFG = RX_FILTER_ASSOC;
}

/* Restore permissive scan filter. Called on disconnect. */
static void wifi_mac_clear_bssid(void)
{
    WIFI_MAC_BSSID_DW0 = 0;
    WIFI_MAC_BSSID_DW1 = 0;
    WIFI_RX_FILTER_CFG = RX_FILTER_SCAN;
}

static void wifi_mac_bss_clear(void)
{
    uint32_t i;
    wifi_bss_count = 0;
    for (i = 0; i < WIFI_MAX_BSS; i++)
        wifi_bss_table[i].in_use = 0;
}
