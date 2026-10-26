/* SPDX-License-Identifier: GPL-3.0-only */
/* WiFi TX/RX data path: ethernet <-> 802.11 conversion, encryption hooks. */

/* ---- ethernet header ---- */

struct wifi_eth_hdr {
    uint8_t dst[6];
    uint8_t src[6];
    uint16_t ethertype;
};

/* ---- 802.11 to ethernet conversion ---- */

static int wifi_data_rx(const uint8_t *frame, uint32_t len)
{
    if (len < sizeof(struct wifi_hdr) + 8) return -1; /* min: hdr + LLC/SNAP */

    /* strip 802.11 header, LLC/SNAP, build ethernet frame */
    const struct wifi_hdr *wh = (const struct wifi_hdr *)frame;
    const uint8_t *payload = frame + sizeof(struct wifi_hdr);
    uint32_t payload_len = len - sizeof(struct wifi_hdr);

    /* check LLC/SNAP header (AA-AA-03 00-00-00 xx-xx) */
    if (payload_len < 8) return -1;
    if (payload[0] != 0xAA || payload[1] != 0xAA || payload[2] != 0x03)
        return -1;

    uint16_t ethertype = ((uint16_t)payload[6] << 8) | payload[7];
    const uint8_t *data = payload + 8;
    uint32_t data_len = payload_len - 8;

    /* allocate sk_buff with ethernet header */
    struct sk_buff *skb = alloc_skb(data_len + 14, 0);
    if (!skb) return -1;

    uint8_t *p = skb_put(skb, data_len + 14);
    /* ethernet header: dst from addr1, src from addr3 (AP->STA: addr3=src) */
    for (int i = 0; i < 6; i++) p[i] = wh->addr1[i];
    for (int i = 0; i < 6; i++) p[6 + i] = wh->addr3[i];
    p[12] = (uint8_t)(ethertype >> 8);
    p[13] = (uint8_t)(ethertype & 0xFF);
    for (uint32_t i = 0; i < data_len; i++) p[14 + i] = data[i];

    /* forward to lwIP */
    netif_rx(skb);
    return 0;
}

/* ---- ethernet to 802.11 conversion ---- */

static int wifi_data_tx(struct sk_buff *skb)
{
    if (!skb || skb->len < 14) return -1;

    /* build 802.11 data frame: prepend header + LLC/SNAP */
    struct wifi_hdr wh;
    wh.frame_control = WIFI_FC_TYPE_DATA | 0x0100; /* to-DS bit */
    wh.duration = 0;
    /* addr1 = BSSID (AP), addr2 = src (us), addr3 = dst (final) */
    for (int i = 0; i < 6; i++) wh.addr1[i] = wifi_sta_bssid[i];
    /* addr2 = our MAC (TODO: read from hardware) */
    for (int i = 0; i < 6; i++) wh.addr2[i] = 0;
    for (int i = 0; i < 6; i++) wh.addr3[i] = skb->data[i]; /* dst from eth hdr */
    wifi_mac_set_seq(&wh);

    /* prepend 802.11 header + LLC/SNAP (replace ethernet header) */
    /* TODO: proper sk_buff manipulation */
    return wifi_mac_tx(skb);
}

/* ---- EAPOL detection (for WPA handshake) ---- */

#define WIFI_ETHERTYPE_EAPOL 0x888E

static int wifi_data_is_eapol(const uint8_t *frame, uint32_t len)
{
    if (len < sizeof(struct wifi_hdr) + 8) return 0;
    const uint8_t *snap = frame + sizeof(struct wifi_hdr);
    if (snap[0] != 0xAA || snap[1] != 0xAA) return 0;
    uint16_t etype = ((uint16_t)snap[6] << 8) | snap[7];
    return (etype == WIFI_ETHERTYPE_EAPOL);
}
