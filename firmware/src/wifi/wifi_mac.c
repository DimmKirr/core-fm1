/* SPDX-License-Identifier: GPL-3.0-only */
/* 802.11 MAC frame handling: parse, generate, TX/RX management and data frames.
 * Derived from Ralink RT30xx GPL driver architecture. */

/* ---- 802.11 frame types ---- */

#define WIFI_FC_TYPE_MGMT   0x00
#define WIFI_FC_TYPE_CTRL   0x04
#define WIFI_FC_TYPE_DATA   0x08

#define WIFI_FC_STYPE_BEACON     0x80
#define WIFI_FC_STYPE_PROBE_REQ  0x40
#define WIFI_FC_STYPE_PROBE_RSP  0x50
#define WIFI_FC_STYPE_AUTH       0xB0
#define WIFI_FC_STYPE_DEAUTH     0xC0
#define WIFI_FC_STYPE_ASSOC_REQ  0x00
#define WIFI_FC_STYPE_ASSOC_RSP  0x10
#define WIFI_FC_STYPE_DISASSOC   0xA0
#define WIFI_FC_STYPE_DATA       0x00

/* ---- 802.11 header ---- */

struct wifi_hdr {
    uint16_t frame_control;
    uint16_t duration;
    uint8_t  addr1[6]; /* receiver */
    uint8_t  addr2[6]; /* transmitter */
    uint8_t  addr3[6]; /* BSSID */
    uint16_t seq_ctrl;
};

/* ---- BSS descriptor (scan result) ---- */

#define WIFI_MAX_SSID_LEN 32
#define WIFI_MAX_BSS       16

struct wifi_bss {
    uint8_t  bssid[6];
    char     ssid[WIFI_MAX_SSID_LEN + 1];
    uint8_t  ssid_len;
    uint8_t  channel;
    int8_t   rssi;
    uint8_t  has_rsn; /* WPA2 capable */
    uint16_t beacon_interval;
    uint16_t capability;
    uint8_t  in_use;
};

static struct wifi_bss wifi_bss_table[WIFI_MAX_BSS];
static uint8_t wifi_bss_count;

/* ---- our MAC address (read from hardware or set via init) ---- */

static uint8_t wifi_mac_addr[6];

/* ---- broadcast address ---- */

static const uint8_t wifi_bcast_addr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/* ---- supported rates IE ---- */

static const uint8_t wifi_supported_rates[] = {
    0x82, /* 1 Mbps (basic) */
    0x84, /* 2 Mbps (basic) */
    0x8B, /* 5.5 Mbps (basic) */
    0x96, /* 11 Mbps (basic) */
    0x0C, /* 6 Mbps */
    0x12, /* 9 Mbps */
    0x18, /* 12 Mbps */
    0x24, /* 18 Mbps */
};

/* ---- beacon/probe response parser ---- */

static void wifi_mac_parse_beacon(const uint8_t *frame, uint32_t len)
{
    if (len < sizeof(struct wifi_hdr) + 12) return;

    const struct wifi_hdr *hdr = (const struct wifi_hdr *)frame;
    (void)hdr;

    /* fixed fields: timestamp(8) + interval(2) + capability(2) */
    const uint8_t *ies = frame + sizeof(struct wifi_hdr) + 12;
    uint32_t ie_len = len - sizeof(struct wifi_hdr) - 12;

    struct wifi_bss bss;
    for (int i = 0; i < 6; i++)
        bss.bssid[i] = hdr->addr2[i];
    bss.ssid[0] = 0;
    bss.ssid_len = 0;
    bss.channel = 0;
    bss.rssi = 0;
    bss.has_rsn = 0;
    bss.beacon_interval = ies[-4] | (ies[-3] << 8);
    bss.capability = ies[-2] | (ies[-1] << 8);

    /* parse IEs */
    uint32_t pos = 0;
    while (pos + 2 <= ie_len) {
        uint8_t id = ies[pos];
        uint8_t el = ies[pos + 1];
        if (pos + 2 + el > ie_len) break;
        const uint8_t *val = &ies[pos + 2];

        if (id == 0 && el <= WIFI_MAX_SSID_LEN) {
            /* SSID */
            for (uint8_t i = 0; i < el; i++)
                bss.ssid[i] = (char)val[i];
            bss.ssid[el] = 0;
            bss.ssid_len = el;
        } else if (id == 3 && el >= 1) {
            /* DS Parameter Set: channel */
            bss.channel = val[0];
        } else if (id == 48) {
            /* RSN: WPA2 */
            bss.has_rsn = 1;
        }
        pos += 2 + el;
    }

    /* add to BSS table (replace if BSSID already present) */
    int slot = -1;
    for (int i = 0; i < (int)wifi_bss_count; i++) {
        int match = 1;
        for (int j = 0; j < 6; j++)
            if (wifi_bss_table[i].bssid[j] != bss.bssid[j]) match = 0;
        if (match) { slot = i; break; }
    }
    if (slot < 0 && wifi_bss_count < WIFI_MAX_BSS)
        slot = wifi_bss_count++;
    if (slot >= 0) {
        bss.in_use = 1;
        wifi_bss_table[slot] = bss;
    }
}

/* ---- management frame TX helpers ---- */

static uint16_t wifi_seq_num;

static void wifi_mac_set_seq(struct wifi_hdr *hdr)
{
    hdr->seq_ctrl = (wifi_seq_num++ & 0xFFF) << 4;
}

static void wifi_mac_fill_hdr(struct wifi_hdr *hdr, uint16_t fc,
                               const uint8_t *addr1, const uint8_t *addr3)
{
    hdr->frame_control = fc;
    hdr->duration = 0;
    for (int i = 0; i < 6; i++) hdr->addr1[i] = addr1[i];
    for (int i = 0; i < 6; i++) hdr->addr2[i] = wifi_mac_addr[i];
    for (int i = 0; i < 6; i++) hdr->addr3[i] = addr3[i];
    wifi_mac_set_seq(hdr);
}

static uint8_t *wifi_mac_append_ie(uint8_t *p, uint8_t id, const uint8_t *data, uint8_t len)
{
    *p++ = id;
    *p++ = len;
    for (uint8_t i = 0; i < len; i++)
        *p++ = data[i];
    return p;
}

/* ---- probe request TX ---- */

static int wifi_mac_tx_probe_req(const char *ssid, uint8_t channel)
{
    (void)channel;
    uint8_t frame[256];
    struct wifi_hdr *hdr = (struct wifi_hdr *)frame;

    wifi_mac_fill_hdr(hdr, WIFI_FC_TYPE_MGMT | WIFI_FC_STYPE_PROBE_REQ,
                      wifi_bcast_addr, wifi_bcast_addr);

    uint8_t *p = frame + sizeof(struct wifi_hdr);

    /* IE 0: SSID (empty = wildcard, or specific) */
    uint8_t ssid_len = 0;
    while (ssid[ssid_len] && ssid_len < WIFI_MAX_SSID_LEN) ssid_len++;
    p = wifi_mac_append_ie(p, 0, (const uint8_t *)ssid, ssid_len);

    /* IE 1: Supported Rates */
    p = wifi_mac_append_ie(p, 1, wifi_supported_rates, sizeof(wifi_supported_rates));

    uint32_t total = (uint32_t)(p - frame);
    return wifi_sdio_tx(frame, total);
}

/* ---- authentication TX ---- */

static int wifi_mac_tx_auth(const uint8_t *bssid, uint16_t algo, uint16_t seq_nr)
{
    uint8_t frame[64];
    struct wifi_hdr *hdr = (struct wifi_hdr *)frame;

    wifi_mac_fill_hdr(hdr, WIFI_FC_TYPE_MGMT | WIFI_FC_STYPE_AUTH,
                      bssid, bssid);

    uint8_t *p = frame + sizeof(struct wifi_hdr);

    /* auth fixed fields: algorithm(2) + sequence(2) + status(2) */
    *p++ = (uint8_t)(algo & 0xFF);
    *p++ = (uint8_t)(algo >> 8);
    *p++ = (uint8_t)(seq_nr & 0xFF);
    *p++ = (uint8_t)(seq_nr >> 8);
    *p++ = 0; /* status: success */
    *p++ = 0;

    uint32_t total = (uint32_t)(p - frame);
    return wifi_sdio_tx(frame, total);
}

/* ---- association request TX ---- */

static int wifi_mac_tx_assoc_req(const uint8_t *bssid, const char *ssid, uint8_t ssid_len)
{
    uint8_t frame[320];
    struct wifi_hdr *hdr = (struct wifi_hdr *)frame;

    wifi_mac_fill_hdr(hdr, WIFI_FC_TYPE_MGMT | WIFI_FC_STYPE_ASSOC_REQ,
                      bssid, bssid);

    uint8_t *p = frame + sizeof(struct wifi_hdr);

    /* fixed fields: capability(2) + listen interval(2) */
    *p++ = 0x31; /* capability: ESS + short preamble + short slot */
    *p++ = 0x04;
    *p++ = 0x0A; /* listen interval: 10 */
    *p++ = 0x00;

    /* IE 0: SSID */
    if (ssid_len > WIFI_MAX_SSID_LEN) ssid_len = WIFI_MAX_SSID_LEN;
    p = wifi_mac_append_ie(p, 0, (const uint8_t *)ssid, ssid_len);

    /* IE 1: Supported Rates */
    p = wifi_mac_append_ie(p, 1, wifi_supported_rates, sizeof(wifi_supported_rates));

    /* IE 48: RSN (WPA2-PSK) */
    static const uint8_t rsn_ie[] = {
        0x01, 0x00,             /* version 1 */
        0x00, 0x0F, 0xAC, 0x04, /* group cipher: CCMP */
        0x01, 0x00,             /* pairwise count: 1 */
        0x00, 0x0F, 0xAC, 0x04, /* pairwise cipher: CCMP */
        0x01, 0x00,             /* AKM count: 1 */
        0x00, 0x0F, 0xAC, 0x02, /* AKM: PSK */
        0x00, 0x00,             /* RSN capabilities */
    };
    p = wifi_mac_append_ie(p, 48, rsn_ie, sizeof(rsn_ie));

    uint32_t total = (uint32_t)(p - frame);
    return wifi_sdio_tx(frame, total);
}

/* ---- deauthentication TX ---- */

static int wifi_mac_tx_deauth(const uint8_t *bssid, uint16_t reason)
{
    uint8_t frame[32];
    struct wifi_hdr *hdr = (struct wifi_hdr *)frame;

    wifi_mac_fill_hdr(hdr, WIFI_FC_TYPE_MGMT | WIFI_FC_STYPE_DEAUTH,
                      bssid, bssid);

    uint8_t *p = frame + sizeof(struct wifi_hdr);

    /* fixed field: reason code (2 bytes, LE) */
    *p++ = (uint8_t)(reason & 0xFF);
    *p++ = (uint8_t)(reason >> 8);

    uint32_t total = (uint32_t)(p - frame);
    return wifi_sdio_tx(frame, total);
}

/* ---- data frame TX ---- */

static int wifi_mac_tx(struct sk_buff *skb)
{
    if (!skb || skb->len < 14) return -1;

    /* build 802.11 data header + LLC/SNAP, send raw via SDIO */
    struct wifi_hdr wh;
    wh.frame_control = WIFI_FC_TYPE_DATA | 0x0100; /* to-DS bit */
    wh.duration = 0;
    for (int i = 0; i < 6; i++) wh.addr1[i] = wifi_sta_bssid[i]; /* BSSID (AP) */
    for (int i = 0; i < 6; i++) wh.addr2[i] = wifi_mac_addr[i];  /* src (us) */
    for (int i = 0; i < 6; i++) wh.addr3[i] = skb->data[i];      /* dst from eth hdr */
    wifi_mac_set_seq(&wh);

    uint16_t ethertype = ((uint16_t)skb->data[12] << 8) | skb->data[13];
    uint32_t payload_len = skb->len - 14;
    uint8_t *payload = skb->data + 14;

    /* assemble in TX buffer: wifi_hdr(24) + LLC/SNAP(8) + payload */
    static uint8_t tx_buf[1600];
    uint32_t pos = 0;

    uint8_t *hp = (uint8_t *)&wh;
    for (uint32_t i = 0; i < sizeof(struct wifi_hdr); i++)
        tx_buf[pos++] = hp[i];

    /* LLC/SNAP: AA-AA-03 00-00-00 ethertype */
    tx_buf[pos++] = 0xAA;
    tx_buf[pos++] = 0xAA;
    tx_buf[pos++] = 0x03;
    tx_buf[pos++] = 0x00;
    tx_buf[pos++] = 0x00;
    tx_buf[pos++] = 0x00;
    tx_buf[pos++] = (uint8_t)(ethertype >> 8);
    tx_buf[pos++] = (uint8_t)(ethertype & 0xFF);

    for (uint32_t i = 0; i < payload_len && pos < sizeof(tx_buf); i++)
        tx_buf[pos++] = payload[i];

    return wifi_sdio_tx(tx_buf, pos);
}

/* ---- RX dispatch ---- */

static void wifi_mac_rx(uint8_t *data, uint32_t len)
{
    if (len < sizeof(struct wifi_hdr)) return;

    const struct wifi_hdr *hdr = (const struct wifi_hdr *)data;
    uint16_t fc = hdr->frame_control;
    uint8_t type = fc & 0x0C;
    uint8_t stype = fc & 0xF0;

    if (type == WIFI_FC_TYPE_MGMT) {
        if (stype == WIFI_FC_STYPE_BEACON || stype == WIFI_FC_STYPE_PROBE_RSP) {
            wifi_mac_parse_beacon(data, len);
        }
        wifi_sta_rx_mgmt(data, len);
    } else if (type == WIFI_FC_TYPE_DATA) {
        if (wifi_data_is_eapol(data, len)) {
            wifi_wpa_rx_eapol(data, len);
        } else {
            wifi_data_rx(data, len);
        }
    }
}
