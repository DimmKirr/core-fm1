/* SPDX-License-Identifier: GPL-3.0-only */
/* Clean-room implementation from docs/hardware/wifi/SPECSHEET.md and IEEE 802.11-2020.
 * No vendor code, IR or disassembly.
 * TX/RX data path: Ethernet <-> 802.11 frame conversion, encryption hooks.
 * Architecture informed by Ralink RT2x00 (GPL-2.0+). */

/* ---- constants ---- */

/* LLC/SNAP header: DSAP(AA) SSAP(AA) ctrl(03) OUI(00:00:00) */
static const uint8_t wifi_llc_snap[6] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00};

#define WIFI_ETHERTYPE_EAPOL 0x888Eu
#define WIFI_ETHERTYPE_IP    0x0800u
#define WIFI_ETHERTYPE_ARP   0x0806u

/* 802.11 frame control bits */
#define WIFI_FC_DATA       0x0008  /* type: data */
#define WIFI_FC_TODS       0x0100  /* to-DS */
#define WIFI_FC_FROMDS     0x0200  /* from-DS */
#define WIFI_FC_PROTECTED  0x4000  /* protected frame */

/* 802.11 data frame header size (3-address, no QoS) */
#define WIFI_DATA_HDR_LEN  24
/* LLC/SNAP overhead */
#define WIFI_SNAP_LEN      8  /* DSAP(1)+SSAP(1)+ctrl(1)+OUI(3)+ethertype(2) */
/* ethernet header size */
#define WIFI_ETH_HDR_LEN   14

/* max frame sizes */
#define WIFI_DATA_MTU      1500
#define WIFI_DATA_MAX_FRAME (WIFI_DATA_HDR_LEN + WIFI_SNAP_LEN + WIFI_DATA_MTU)

/* ---- helpers ---- */

static uint16_t wifi_data_rd16be(const uint8_t *p)
{
    return (uint16_t)p[0] << 8 | (uint16_t)p[1];
}

static void wifi_data_wr16be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void wifi_data_wr16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wifi_data_cpy(uint8_t *d, const uint8_t *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) d[i] = s[i];
}

/* TX sequence counter (shared with STA for management frames via wifi_sta) */
static uint16_t wifi_data_seq;

/* ---- Ethernet -> 802.11 ---- */

/* Convert an Ethernet frame to an 802.11 data frame (to-DS).
 * eth points to a complete Ethernet frame: dst(6) src(6) ethertype(2) payload.
 * out must have room for WIFI_DATA_MAX_FRAME + CCMP overhead.
 * Returns the total 802.11 frame length, or 0 on error. */
static uint32_t wifi_data_eth_to_80211(const uint8_t *eth, uint32_t eth_len,
                                       uint8_t *out)
{
    uint32_t payload_len, pos;
    uint16_t ethertype;

    if (eth_len < WIFI_ETH_HDR_LEN) return 0;
    payload_len = eth_len - WIFI_ETH_HDR_LEN;
    if (payload_len > WIFI_DATA_MTU) return 0;

    ethertype = wifi_data_rd16be(eth + 12);

    /* 802.11 header: to-DS data frame */
    pos = 0;
    wifi_data_wr16le(out + pos, WIFI_FC_DATA | WIFI_FC_TODS);
    pos += 2;
    wifi_data_wr16le(out + pos, 0); /* duration */
    pos += 2;
    /* to-DS addressing: addr1=BSSID, addr2=SA (us), addr3=DA (final dst) */
    wifi_data_cpy(out + pos, wifi_sta.target_bssid, 6); pos += 6; /* addr1: BSSID */
    wifi_data_cpy(out + pos, wifi_sta.our_mac, 6);      pos += 6; /* addr2: SA */
    wifi_data_cpy(out + pos, eth, 6);                    pos += 6; /* addr3: DA */
    wifi_data_wr16le(out + pos, wifi_data_seq++ << 4);
    pos += 2;

    /* LLC/SNAP header */
    wifi_data_cpy(out + pos, wifi_llc_snap, 6); pos += 6;
    wifi_data_wr16be(out + pos, ethertype);     pos += 2;

    /* payload (skip Ethernet header) */
    wifi_data_cpy(out + pos, eth + WIFI_ETH_HDR_LEN, payload_len);
    pos += payload_len;

    return pos;
}

/* ---- 802.11 -> Ethernet ---- */

/* Convert an 802.11 data frame (from-DS) to an Ethernet frame.
 * dot11 points to the raw 802.11 frame.
 * out must have room for WIFI_ETH_HDR_LEN + payload.
 * Returns the Ethernet frame length, or 0 on error. */
static uint32_t wifi_data_80211_to_eth(const uint8_t *dot11, uint32_t dot11_len,
                                       uint8_t *out)
{
    uint32_t hdr_len, payload_off;
    uint16_t fc, ethertype;

    if (dot11_len < WIFI_DATA_HDR_LEN + WIFI_SNAP_LEN) return 0;

    fc = (uint16_t)dot11[0] | (uint16_t)dot11[1] << 8;

    /* only handle from-DS data frames */
    if ((fc & 0x000C) != 0x0008) return 0; /* not data */
    if (!(fc & WIFI_FC_FROMDS)) return 0;

    hdr_len = WIFI_DATA_HDR_LEN;
    /* QoS data adds 2 bytes (subtype bit 7) */
    if (fc & 0x0080) hdr_len += 2;

    payload_off = hdr_len;
    if (payload_off + WIFI_SNAP_LEN > dot11_len) return 0;

    /* verify LLC/SNAP */
    if (dot11[payload_off] != 0xAA || dot11[payload_off + 1] != 0xAA ||
        dot11[payload_off + 2] != 0x03)
        return 0;

    ethertype = wifi_data_rd16be(dot11 + payload_off + 6);

    /* from-DS addressing: addr1=DA, addr2=BSSID, addr3=SA */
    wifi_data_cpy(out, dot11 + 4, 6);       /* DA from addr1 */
    wifi_data_cpy(out + 6, dot11 + 16, 6);  /* SA from addr3 */
    wifi_data_wr16be(out + 12, ethertype);

    /* copy payload after LLC/SNAP */
    {
        uint32_t data_off = payload_off + WIFI_SNAP_LEN;
        uint32_t data_len = dot11_len - data_off;
        if (data_len > WIFI_DATA_MTU) return 0;
        wifi_data_cpy(out + WIFI_ETH_HDR_LEN, dot11 + data_off, data_len);
        return WIFI_ETH_HDR_LEN + data_len;
    }
}

/* ---- encryption hooks ---- */

/* Encrypt an 802.11 data frame in place using CCMP (IEEE 802.11i).
 * Sets the Protected bit in frame control, inserts CCMP header (8 bytes),
 * appends MIC (8 bytes). buf must have room for the expansion.
 * Returns new frame length, or 0 on error / no keys installed. */
static uint32_t wifi_data_encrypt(uint8_t *frame, uint32_t len)
{
    uint32_t hdr_len, data_len;

    if (!wifi_wpa_has_ptk()) return 0;

    hdr_len = WIFI_DATA_HDR_LEN;
    if (frame[0] & 0x80) hdr_len += 2; /* QoS */

    if (len < hdr_len) return 0;
    data_len = len - hdr_len;

    /* set Protected bit */
    frame[1] |= 0x40;

    /* make room for CCMP header (8 bytes) between header and payload:
     * shift payload right by 8 */
    {
        uint32_t i;
        for (i = len; i > hdr_len; i--)
            frame[i + 7] = frame[i - 1];
    }

    /* CCMP header: PN0, PN1, 0, ExtIV|KeyID<<6, PN2, PN3, PN4, PN5 */
    wifi_wpa_ccmp_hdr(frame + hdr_len);

    /* encrypt payload and compute MIC (8 bytes appended) */
    wifi_wpa_ccmp_encrypt(frame, hdr_len,
                          frame + hdr_len + 8, data_len,
                          frame + hdr_len + 8 + data_len);

    return hdr_len + 8 + data_len + 8; /* hdr + ccmp_hdr + data + MIC */
}

/* Decrypt a received CCMP-protected 802.11 data frame in place.
 * Removes the CCMP header and MIC, clears Protected bit.
 * Returns payload length, or 0 on error / MIC failure. */
static uint32_t wifi_data_decrypt(uint8_t *frame, uint32_t len)
{
    uint32_t hdr_len, data_len;

    if (!wifi_wpa_has_ptk()) return 0;

    hdr_len = WIFI_DATA_HDR_LEN;
    if (frame[0] & 0x80) hdr_len += 2;

    /* CCMP: 8-byte header + 8-byte MIC minimum */
    if (len < hdr_len + 16) return 0;
    data_len = len - hdr_len - 8 - 8; /* minus ccmp_hdr and MIC */

    /* verify and decrypt */
    if (wifi_wpa_ccmp_decrypt(frame, hdr_len,
                              frame + hdr_len + 8, data_len,
                              frame + hdr_len + 8 + data_len) != 0)
        return 0;

    /* shift payload left to remove CCMP header */
    {
        uint32_t i;
        for (i = 0; i < data_len; i++)
            frame[hdr_len + i] = frame[hdr_len + 8 + i];
    }

    /* clear Protected bit */
    frame[1] &= ~0x40;

    return hdr_len + data_len;
}

/* ---- TX path (from lwIP) ---- */

/* Accept an Ethernet frame from the network stack, convert to 802.11,
 * optionally encrypt, and hand to the MAC for transmission.
 * Returns 0 on success. */
static int wifi_data_tx(const uint8_t *eth, uint32_t eth_len)
{
    uint8_t frame[WIFI_DATA_MAX_FRAME + 16]; /* +16 for CCMP overhead */
    uint32_t frame_len;

    if (wifi_sta.state != WIFI_STATE_CONNECTED) return -1;

    frame_len = wifi_data_eth_to_80211(eth, eth_len, frame);
    if (!frame_len) return -1;

    /* encrypt if keys installed (skip EAPOL: sent unencrypted) */
    {
        uint16_t etype = wifi_data_rd16be(eth + 12);
        if (etype != WIFI_ETHERTYPE_EAPOL && wifi_wpa_has_ptk()) {
            frame_len = wifi_data_encrypt(frame, frame_len);
            if (!frame_len) return -1;
        }
    }

    return wifi_mac_tx(frame, frame_len);
}

/* ---- RX path (from MAC) ---- */

/* Process a received 802.11 data frame: decrypt if needed, convert to
 * Ethernet, deliver to lwIP or WPA handshake. */
static void wifi_data_rx(const uint8_t *frame, uint32_t len)
{
    uint8_t buf[WIFI_DATA_MAX_FRAME + 16];
    uint32_t dlen, eth_len;
    uint16_t fc, ethertype;

    if (len < WIFI_DATA_HDR_LEN) return;

    fc = (uint16_t)frame[0] | (uint16_t)frame[1] << 8;

    /* only from-DS data frames */
    if ((fc & 0x000C) != 0x0008) return;
    if (!(fc & WIFI_FC_FROMDS)) return;

    /* copy for in-place operations */
    if (len > sizeof buf) return;
    wifi_data_cpy(buf, frame, len);
    dlen = len;

    /* decrypt if protected */
    if (fc & WIFI_FC_PROTECTED) {
        dlen = wifi_data_decrypt(buf, dlen);
        if (!dlen) return;
    }

    /* check for EAPOL: deliver to WPA handshake */
    {
        uint32_t hdr = WIFI_DATA_HDR_LEN;
        if (buf[0] & 0x80) hdr += 2; /* QoS */
        if (dlen >= hdr + WIFI_SNAP_LEN) {
            ethertype = wifi_data_rd16be(buf + hdr + 6);
            if (ethertype == WIFI_ETHERTYPE_EAPOL) {
                wifi_wpa_rx_eapol(buf + hdr + WIFI_SNAP_LEN,
                                  dlen - hdr - WIFI_SNAP_LEN);
                return;
            }
        }
    }

    /* convert to Ethernet and deliver to lwIP */
    {
        uint8_t eth[WIFI_ETH_HDR_LEN + WIFI_DATA_MTU];
        eth_len = wifi_data_80211_to_eth(buf, dlen, eth);
        if (eth_len > 0)
            wifi_lwip_input(eth, eth_len);
    }
}

/* detect EAPOL in a raw 802.11 frame (for MAC RX dispatch) */
static int wifi_data_is_eapol(const uint8_t *frame, uint32_t len)
{
    uint32_t hdr = WIFI_DATA_HDR_LEN;
    if (len < hdr + WIFI_SNAP_LEN) return 0;
    if (frame[0] & 0x80) hdr += 2;
    if (frame[hdr] != 0xAA || frame[hdr + 1] != 0xAA) return 0;
    return wifi_data_rd16be(frame + hdr + 6) == WIFI_ETHERTYPE_EAPOL;
}
