/* SPDX-License-Identifier: GPL-3.0-only */
/* Minimal TCP server on top of wifi_lwip.c frame I/O.
 * Single-connection, polled (no threads). Designed for the CDP debug server. */

#define TCP_STATE_CLOSED    0
#define TCP_STATE_LISTEN    1
#define TCP_STATE_SYN_RCVD  2
#define TCP_STATE_ESTAB     3
#define TCP_STATE_FIN_WAIT  4

#define TCP_FLAG_FIN  0x01
#define TCP_FLAG_SYN  0x02
#define TCP_FLAG_RST  0x04
#define TCP_FLAG_PSH  0x08
#define TCP_FLAG_ACK  0x10

#define TCP_RX_BUF_SIZE  2048
#define TCP_TX_BUF_SIZE  2048
#define TCP_MSS          1400

struct tcp_conn {
    uint8_t  state;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t remote_ip;
    uint8_t  remote_mac[6];
    uint32_t seq_local;
    uint32_t ack_local;
    uint32_t seq_remote;
    uint8_t  rx_buf[TCP_RX_BUF_SIZE];
    uint32_t rx_len;
    uint8_t  tx_buf[TCP_TX_BUF_SIZE];
    uint32_t tx_len;
    uint32_t tx_sent;
};

static struct tcp_conn tcp_srv;

static uint16_t tcp_checksum(const uint8_t *pseudo, uint32_t pseudo_len,
                             const uint8_t *data, uint32_t data_len)
{
    uint32_t sum = 0;
    uint32_t i;
    for (i = 0; i + 1 < pseudo_len; i += 2)
        sum += ((uint32_t)pseudo[i] << 8) | pseudo[i + 1];
    if (pseudo_len & 1) sum += (uint32_t)pseudo[pseudo_len - 1] << 8;
    for (i = 0; i + 1 < data_len; i += 2)
        sum += ((uint32_t)data[i] << 8) | data[i + 1];
    if (data_len & 1) sum += (uint32_t)data[data_len - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

static void tcp_send_packet(struct tcp_conn *c, uint8_t flags,
                            const uint8_t *payload, uint32_t payload_len)
{
    uint8_t pkt[14 + 20 + 20 + TCP_MSS];
    uint32_t tcp_hdr_len = 20;
    if (flags & TCP_FLAG_SYN) tcp_hdr_len = 24; /* MSS option */

    uint32_t ip_len = 20 + tcp_hdr_len + payload_len;
    uint32_t total = 14 + ip_len;
    if (total > sizeof(pkt)) return;

    /* ethernet header */
    uint32_t i;
    for (i = 0; i < 6; i++) pkt[i] = c->remote_mac[i];
    for (i = 0; i < 6; i++) pkt[6 + i] = wifi_mac_addr[i];
    pkt[12] = 0x08; pkt[13] = 0x00;

    /* IP header */
    uint8_t *ip = pkt + 14;
    ip[0] = 0x45; ip[1] = 0x00;
    ip[2] = (uint8_t)(ip_len >> 8); ip[3] = (uint8_t)ip_len;
    ip[4] = 0; ip[5] = 0;
    ip[6] = 0x40; ip[7] = 0; /* DF */
    ip[8] = 64; ip[9] = 6; /* TTL, proto TCP */
    ip[10] = 0; ip[11] = 0; /* checksum (computed below) */
    ip[12] = (uint8_t)(wifi_ip_addr >> 24);
    ip[13] = (uint8_t)(wifi_ip_addr >> 16);
    ip[14] = (uint8_t)(wifi_ip_addr >> 8);
    ip[15] = (uint8_t)(wifi_ip_addr);
    ip[16] = (uint8_t)(c->remote_ip >> 24);
    ip[17] = (uint8_t)(c->remote_ip >> 16);
    ip[18] = (uint8_t)(c->remote_ip >> 8);
    ip[19] = (uint8_t)(c->remote_ip);
    /* IP checksum */
    {
        uint32_t s = 0;
        for (i = 0; i < 20; i += 2)
            s += ((uint32_t)ip[i] << 8) | ip[i + 1];
        while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
        uint16_t cs = (uint16_t)(~s);
        ip[10] = (uint8_t)(cs >> 8); ip[11] = (uint8_t)cs;
    }

    /* TCP header */
    uint8_t *tcp = ip + 20;
    tcp[0] = (uint8_t)(c->local_port >> 8);
    tcp[1] = (uint8_t)(c->local_port);
    tcp[2] = (uint8_t)(c->remote_port >> 8);
    tcp[3] = (uint8_t)(c->remote_port);
    /* seq */
    tcp[4]  = (uint8_t)(c->seq_local >> 24);
    tcp[5]  = (uint8_t)(c->seq_local >> 16);
    tcp[6]  = (uint8_t)(c->seq_local >> 8);
    tcp[7]  = (uint8_t)(c->seq_local);
    /* ack */
    tcp[8]  = (uint8_t)(c->ack_local >> 24);
    tcp[9]  = (uint8_t)(c->ack_local >> 16);
    tcp[10] = (uint8_t)(c->ack_local >> 8);
    tcp[11] = (uint8_t)(c->ack_local);
    /* data offset + flags */
    tcp[12] = (uint8_t)((tcp_hdr_len / 4) << 4);
    tcp[13] = flags;
    /* window */
    tcp[14] = (uint8_t)(TCP_RX_BUF_SIZE >> 8);
    tcp[15] = (uint8_t)(TCP_RX_BUF_SIZE);
    /* checksum placeholder */
    tcp[16] = 0; tcp[17] = 0;
    /* urgent */
    tcp[18] = 0; tcp[19] = 0;

    if (flags & TCP_FLAG_SYN) {
        /* MSS option: kind=2, len=4, mss=1400 */
        tcp[20] = 2; tcp[21] = 4;
        tcp[22] = (uint8_t)(TCP_MSS >> 8);
        tcp[23] = (uint8_t)(TCP_MSS);
    }

    /* copy payload */
    for (i = 0; i < payload_len; i++)
        tcp[tcp_hdr_len + i] = payload[i];

    /* TCP checksum (pseudo-header + TCP header + data) */
    {
        uint8_t pseudo[12];
        uint32_t seg_len = tcp_hdr_len + payload_len;
        for (i = 0; i < 4; i++) pseudo[i] = ip[12 + i]; /* src */
        for (i = 0; i < 4; i++) pseudo[4 + i] = ip[16 + i]; /* dst */
        pseudo[8] = 0; pseudo[9] = 6; /* proto TCP */
        pseudo[10] = (uint8_t)(seg_len >> 8);
        pseudo[11] = (uint8_t)(seg_len);
        uint16_t cs = tcp_checksum(pseudo, 12, tcp, seg_len);
        tcp[16] = (uint8_t)(cs >> 8); tcp[17] = (uint8_t)cs;
    }

    wifi_lwip_output(pkt, total);
    c->seq_local += payload_len;
    if (flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) c->seq_local++;
}

static void tcp_listen_start(uint16_t port)
{
    tcp_srv.state = TCP_STATE_LISTEN;
    tcp_srv.local_port = port;
    tcp_srv.rx_len = 0;
    tcp_srv.tx_len = 0;
    tcp_srv.tx_sent = 0;
}

static void tcp_handle_rx(const uint8_t *eth_frame, uint32_t frame_len)
{
    if (frame_len < 14 + 20 + 20) return;
    const uint8_t *ip = eth_frame + 14;
    if ((ip[0] & 0xF0) != 0x40) return;
    if (ip[9] != 6) return; /* not TCP */

    uint32_t ip_hdr_len = (ip[0] & 0x0F) * 4;
    const uint8_t *tcp = ip + ip_hdr_len;
    uint32_t tcp_total = ((uint32_t)ip[2] << 8 | ip[3]) - ip_hdr_len;
    if (tcp_total < 20) return;

    uint16_t src_port = ((uint16_t)tcp[0] << 8) | tcp[1];
    uint16_t dst_port = ((uint16_t)tcp[2] << 8) | tcp[3];
    uint32_t seq = ((uint32_t)tcp[4] << 24) | ((uint32_t)tcp[5] << 16) |
                   ((uint32_t)tcp[6] << 8) | tcp[7];
    uint32_t ack = ((uint32_t)tcp[8] << 24) | ((uint32_t)tcp[9] << 16) |
                   ((uint32_t)tcp[10] << 8) | tcp[11];
    uint8_t  flags = tcp[13];
    uint32_t tcp_hdr_len = ((tcp[12] >> 4) & 0xF) * 4;
    const uint8_t *payload = tcp + tcp_hdr_len;
    uint32_t payload_len = tcp_total - tcp_hdr_len;

    struct tcp_conn *c = &tcp_srv;

    if (flags & TCP_FLAG_RST) {
        if (c->state != TCP_STATE_LISTEN && c->state != TCP_STATE_CLOSED) {
            c->state = TCP_STATE_LISTEN;
            c->rx_len = 0;
            c->tx_len = 0;
            c->tx_sent = 0;
        }
        return;
    }

    if (c->state == TCP_STATE_LISTEN) {
        if (dst_port != c->local_port) return;
        if (!(flags & TCP_FLAG_SYN)) return;

        c->remote_port = src_port;
        c->remote_ip = ((uint32_t)ip[12] << 24) | ((uint32_t)ip[13] << 16) |
                       ((uint32_t)ip[14] << 8) | ip[15];
        uint32_t i;
        for (i = 0; i < 6; i++) c->remote_mac[i] = eth_frame[6 + i];
        c->seq_remote = seq + 1;
        c->ack_local = seq + 1;
        c->seq_local = 1000;
        c->rx_len = 0;
        c->tx_len = 0;
        c->tx_sent = 0;
        c->state = TCP_STATE_SYN_RCVD;
        tcp_send_packet(c, TCP_FLAG_SYN | TCP_FLAG_ACK, 0, 0);
        return;
    }

    if (c->state == TCP_STATE_SYN_RCVD) {
        if (flags & TCP_FLAG_ACK) {
            c->state = TCP_STATE_ESTAB;
            (void)ack;
        }
        return;
    }

    if (c->state == TCP_STATE_ESTAB) {
        if (payload_len > 0) {
            uint32_t space = TCP_RX_BUF_SIZE - c->rx_len;
            uint32_t copy = payload_len < space ? payload_len : space;
            uint32_t i;
            for (i = 0; i < copy; i++)
                c->rx_buf[c->rx_len + i] = payload[i];
            c->rx_len += copy;
            c->ack_local = seq + payload_len;
            tcp_send_packet(c, TCP_FLAG_ACK, 0, 0);
        }
        if (flags & TCP_FLAG_FIN) {
            c->ack_local = seq + payload_len + 1;
            tcp_send_packet(c, TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0);
            c->state = TCP_STATE_LISTEN;
            c->rx_len = 0;
            c->tx_len = 0;
            c->tx_sent = 0;
        }
        return;
    }
}

static uint32_t tcp_read(uint8_t *buf, uint32_t max)
{
    struct tcp_conn *c = &tcp_srv;
    if (c->state != TCP_STATE_ESTAB || c->rx_len == 0) return 0;
    uint32_t n = c->rx_len < max ? c->rx_len : max;
    uint32_t i;
    for (i = 0; i < n; i++) buf[i] = c->rx_buf[i];
    /* shift remaining */
    uint32_t rem = c->rx_len - n;
    for (i = 0; i < rem; i++) c->rx_buf[i] = c->rx_buf[n + i];
    c->rx_len = rem;
    return n;
}

static int tcp_write(const uint8_t *data, uint32_t len)
{
    struct tcp_conn *c = &tcp_srv;
    if (c->state != TCP_STATE_ESTAB) return -1;
    tcp_send_packet(c, TCP_FLAG_ACK | TCP_FLAG_PSH, data, len);
    return (int)len;
}

static int tcp_connected(void)
{
    return tcp_srv.state == TCP_STATE_ESTAB;
}

static void tcp_close_conn(void)
{
    struct tcp_conn *c = &tcp_srv;
    if (c->state == TCP_STATE_ESTAB) {
        tcp_send_packet(c, TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0);
    }
    c->state = TCP_STATE_LISTEN;
    c->rx_len = 0;
    c->tx_len = 0;
    c->tx_sent = 0;
}
