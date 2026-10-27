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

/* ---- BSS table (scan results) ---- */

#define WIFI_MAX_SSID_LEN 32
#define WIFI_MAX_BSS      16

struct wifi_bss {
    uint8_t  bssid[6];
    char     ssid[WIFI_MAX_SSID_LEN + 1];
    uint8_t  ssid_len;
    uint8_t  channel;
    int8_t   rssi;
    uint8_t  has_rsn;
    uint16_t beacon_interval;
    uint16_t capability;
    uint8_t  in_use;
};

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

static void wifi_mac_parse_beacon(const uint8_t *frame, uint32_t len)
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
    bss.rssi = 0;
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

    /* update BSS table: replace existing BSSID or add new entry */
    slot = -1;
    for (i = 0; i < wifi_bss_count; i++) {
        if (wifi_mac_addr_eq(wifi_bss_table[i].bssid, bss.bssid)) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0 && wifi_bss_count < WIFI_MAX_BSS)
        slot = (int)wifi_bss_count++;
    if (slot >= 0) {
        bss.in_use = 1;
        wifi_bss_table[slot] = bss;
    }
}

static uint8_t wifi_mac_strlen(const char *s)
{
    uint8_t n = 0;
    while (s[n] && n < WIFI_MAX_SSID_LEN)
        n++;
    return n;
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

static void wifi_mac_rx(uint8_t *data, uint32_t len)
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

    if (type == WIFI_FC_TYPE_MGMT) {
        if (stype == WIFI_FC_STYPE_BEACON || stype == WIFI_FC_STYPE_PROBE_RSP)
            wifi_mac_parse_beacon(data, len);
        /* forward all management frames to the STA state machine */
        wifi_sta_rx_mgmt(data, len);
    } else if (type == WIFI_FC_TYPE_DATA) {
        if (wifi_data_is_eapol(data, len))
            wifi_wpa_rx_eapol(data, len);
        else
            wifi_data_rx(data, len);
    }
}

static void wifi_mac_init(void)
{
    uint32_t i;
    wifi_bss_count = 0;
    wifi_mac_seq_num = 0;
    for (i = 0; i < WIFI_MAX_BSS; i++)
        wifi_bss_table[i].in_use = 0;
    for (i = 0; i < 6; i++)
        wifi_mac_addr[i] = 0;
}

static void wifi_mac_bss_clear(void)
{
    uint32_t i;
    wifi_bss_count = 0;
    for (i = 0; i < WIFI_MAX_BSS; i++)
        wifi_bss_table[i].in_use = 0;
}
