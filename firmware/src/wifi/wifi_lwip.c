/* SPDX-License-Identifier: GPL-3.0-only */
/* lwIP 2.x netif driver for WiFi.
 * Standard netif pattern: init, output, input, DHCP.
 * lwIP itself is BSD-licensed; this glue code is GPL-3.0. */

/* lwIP netif state */
static uint8_t wifi_lwip_up;
static uint32_t wifi_ip_addr;
static uint32_t wifi_netmask;
static uint32_t wifi_gateway;

/* MAC address (read from hardware during init) */
static uint8_t wifi_mac_addr[6];

/* simple DHCP state */
#define DHCP_STATE_IDLE     0
#define DHCP_STATE_DISCOVER 1
#define DHCP_STATE_REQUEST  2
#define DHCP_STATE_BOUND    3
static uint8_t wifi_dhcp_state;
static uint32_t wifi_dhcp_xid;
static uint32_t wifi_dhcp_server;
static uint32_t wifi_dhcp_offered_ip;

static void wifi_lwip_init(void)
{
    wifi_lwip_up = 0;
    wifi_ip_addr = 0;
    wifi_netmask = 0;
    wifi_gateway = 0;
    wifi_dhcp_state = DHCP_STATE_IDLE;
    wifi_dhcp_xid = 0x12345678; /* simple starting xid */
}

static int wifi_lwip_output(const uint8_t *data, uint32_t len)
{
    struct sk_buff *skb = alloc_skb(len + 128, 0);
    if (!skb) return -1;
    skb_reserve(skb, 64);
    uint8_t *p = skb_put(skb, len);
    for (uint32_t i = 0; i < len; i++) p[i] = data[i];
    return wifi_data_tx(skb);
}

static void wifi_lwip_input(struct sk_buff *skb)
{
    if (!skb || skb->len < 14) {
        if (skb) kfree_skb(skb);
        return;
    }

    /* extract ethertype from ethernet header */
    uint16_t ethertype = ((uint16_t)skb->data[12] << 8) | skb->data[13];

    if (ethertype == 0x0800) {
        /* IPv4: check protocol and route */
        if (skb->len >= 14 + 20) {
            uint8_t proto = skb->data[14 + 9];
            if (proto == 17 && skb->len >= 14 + 20 + 8) {
                /* UDP: check for DHCP (dst port 68) */
                uint16_t udp_dst = ((uint16_t)skb->data[14+20+2] << 8) | skb->data[14+20+3];
                if (udp_dst == 68) {
                    wifi_lwip_dhcp_rx(skb->data, skb->len);
                }
            }
#ifdef CONFIG_CDP
            else if (proto == 6) {
                /* TCP: route to TCP handler */
                tcp_handle_rx(skb->data, skb->len);
            }
#endif
        }
        kfree_skb(skb);
    } else if (ethertype == 0x0806) {
        /* ARP: handle ARP requests/replies */
        kfree_skb(skb);
    } else {
        kfree_skb(skb);
    }
}

/* ---- minimal DHCP client ---- */

static void wifi_dhcp_build_discover(uint8_t *pkt, uint32_t *len)
{
    /* UDP/IP DHCP discover: broadcast to 255.255.255.255:67 from 0.0.0.0:68 */
    /* Ethernet header */
    for (int i = 0; i < 6; i++) pkt[i] = 0xFF; /* broadcast dst */
    for (int i = 0; i < 6; i++) pkt[6+i] = wifi_mac_addr[i]; /* src */
    pkt[12] = 0x08; pkt[13] = 0x00; /* ethertype: IPv4 */

    /* IP header (20 bytes) */
    uint8_t *ip = pkt + 14;
    ip[0] = 0x45; ip[1] = 0x00; /* version, IHL, DSCP */
    /* total length filled below */
    ip[4] = 0; ip[5] = 0; /* identification */
    ip[6] = 0; ip[7] = 0; /* flags, fragment */
    ip[8] = 64; /* TTL */
    ip[9] = 17; /* protocol: UDP */
    ip[10] = 0; ip[11] = 0; /* checksum (skip for broadcast) */
    ip[12] = 0; ip[13] = 0; ip[14] = 0; ip[15] = 0; /* src: 0.0.0.0 */
    ip[16] = 255; ip[17] = 255; ip[18] = 255; ip[19] = 255; /* dst: broadcast */

    /* UDP header (8 bytes) */
    uint8_t *udp = ip + 20;
    udp[0] = 0; udp[1] = 68; /* src port: 68 */
    udp[2] = 0; udp[3] = 67; /* dst port: 67 */
    /* length filled below */
    udp[6] = 0; udp[7] = 0; /* checksum: 0 (optional for UDP) */

    /* DHCP message (240 base + options) */
    uint8_t *dhcp = udp + 8;
    for (int i = 0; i < 240; i++) dhcp[i] = 0;
    dhcp[0] = 1; /* op: BOOTREQUEST */
    dhcp[1] = 1; /* htype: ethernet */
    dhcp[2] = 6; /* hlen: 6 */
    dhcp[3] = 0; /* hops */
    /* xid */
    dhcp[4] = (uint8_t)(wifi_dhcp_xid >> 24);
    dhcp[5] = (uint8_t)(wifi_dhcp_xid >> 16);
    dhcp[6] = (uint8_t)(wifi_dhcp_xid >> 8);
    dhcp[7] = (uint8_t)(wifi_dhcp_xid);
    /* chaddr: our MAC */
    for (int i = 0; i < 6; i++) dhcp[28+i] = wifi_mac_addr[i];
    /* magic cookie */
    dhcp[236] = 99; dhcp[237] = 130; dhcp[238] = 83; dhcp[239] = 99;

    /* options */
    uint8_t *opt = dhcp + 240;
    opt[0] = 53; opt[1] = 1; opt[2] = 1; /* DHCP Discover */
    opt[3] = 255; /* end */
    uint32_t dhcp_len = 244;

    /* fill lengths */
    uint32_t udp_len = 8 + dhcp_len;
    udp[4] = (uint8_t)(udp_len >> 8);
    udp[5] = (uint8_t)(udp_len);
    uint32_t ip_len = 20 + udp_len;
    ip[2] = (uint8_t)(ip_len >> 8);
    ip[3] = (uint8_t)(ip_len);

    *len = 14 + ip_len;
}

static void wifi_dhcp_build_request(uint8_t *pkt, uint32_t *len)
{
    wifi_dhcp_build_discover(pkt, len);
    /* change to DHCP Request: option 53 = 3 */
    uint8_t *opt = pkt + 14 + 20 + 8 + 240;
    opt[2] = 3; /* DHCP Request */
    /* add requested IP option */
    opt[3] = 50; opt[4] = 4;
    opt[5] = (uint8_t)(wifi_dhcp_offered_ip >> 24);
    opt[6] = (uint8_t)(wifi_dhcp_offered_ip >> 16);
    opt[7] = (uint8_t)(wifi_dhcp_offered_ip >> 8);
    opt[8] = (uint8_t)(wifi_dhcp_offered_ip);
    /* server ID */
    opt[9] = 54; opt[10] = 4;
    opt[11] = (uint8_t)(wifi_dhcp_server >> 24);
    opt[12] = (uint8_t)(wifi_dhcp_server >> 16);
    opt[13] = (uint8_t)(wifi_dhcp_server >> 8);
    opt[14] = (uint8_t)(wifi_dhcp_server);
    opt[15] = 255; /* end */

    /* recalculate lengths */
    uint32_t dhcp_len = 256;
    uint32_t udp_len = 8 + dhcp_len;
    uint8_t *udp = pkt + 14 + 20;
    udp[4] = (uint8_t)(udp_len >> 8);
    udp[5] = (uint8_t)(udp_len);
    uint32_t ip_len = 20 + udp_len;
    uint8_t *ip = pkt + 14;
    ip[2] = (uint8_t)(ip_len >> 8);
    ip[3] = (uint8_t)(ip_len);
    *len = 14 + ip_len;
}

static void wifi_dhcp_parse_offer(const uint8_t *data, uint32_t len)
{
    /* data starts at ethernet header */
    if (len < 14 + 20 + 8 + 240) return;
    const uint8_t *dhcp = data + 14 + 20 + 8;

    /* check xid matches */
    uint32_t xid = ((uint32_t)dhcp[4] << 24) | ((uint32_t)dhcp[5] << 16) |
                   ((uint32_t)dhcp[6] << 8) | dhcp[7];
    if (xid != wifi_dhcp_xid) return;

    /* yiaddr: offered IP */
    wifi_dhcp_offered_ip = ((uint32_t)dhcp[16] << 24) | ((uint32_t)dhcp[17] << 16) |
                           ((uint32_t)dhcp[18] << 8) | dhcp[19];

    /* parse options for server ID, subnet, router */
    const uint8_t *opt = dhcp + 240;
    uint32_t pos = 0;
    uint32_t opt_len = len - (14 + 20 + 8 + 240);
    while (pos < opt_len && opt[pos] != 255) {
        if (opt[pos] == 0) { pos++; continue; }
        uint8_t oid = opt[pos];
        uint8_t olen = opt[pos+1];
        const uint8_t *oval = &opt[pos+2];
        if (oid == 54 && olen >= 4) { /* Server Identifier */
            wifi_dhcp_server = ((uint32_t)oval[0] << 24) | ((uint32_t)oval[1] << 16) |
                               ((uint32_t)oval[2] << 8) | oval[3];
        } else if (oid == 1 && olen >= 4) { /* Subnet Mask */
            wifi_netmask = ((uint32_t)oval[0] << 24) | ((uint32_t)oval[1] << 16) |
                           ((uint32_t)oval[2] << 8) | oval[3];
        } else if (oid == 3 && olen >= 4) { /* Router */
            wifi_gateway = ((uint32_t)oval[0] << 24) | ((uint32_t)oval[1] << 16) |
                           ((uint32_t)oval[2] << 8) | oval[3];
        }
        pos += 2 + olen;
    }
}

static void wifi_lwip_dhcp_start(void)
{
    wifi_dhcp_state = DHCP_STATE_DISCOVER;
    uint8_t pkt[600];
    uint32_t len = 0;
    wifi_dhcp_build_discover(pkt, &len);
    wifi_lwip_output(pkt, len);
}

static void wifi_lwip_dhcp_stop(void)
{
    wifi_dhcp_state = DHCP_STATE_IDLE;
    wifi_lwip_up = 0;
    wifi_ip_addr = 0;
}

static void wifi_lwip_dhcp_rx(const uint8_t *data, uint32_t len)
{
    if (wifi_dhcp_state == DHCP_STATE_DISCOVER) {
        wifi_dhcp_parse_offer(data, len);
        if (wifi_dhcp_offered_ip) {
            wifi_dhcp_state = DHCP_STATE_REQUEST;
            uint8_t pkt[600];
            uint32_t plen = 0;
            wifi_dhcp_build_request(pkt, &plen);
            wifi_lwip_output(pkt, plen);
        }
    } else if (wifi_dhcp_state == DHCP_STATE_REQUEST) {
        wifi_dhcp_parse_offer(data, len);
        wifi_ip_addr = wifi_dhcp_offered_ip;
        wifi_dhcp_state = DHCP_STATE_BOUND;
        wifi_lwip_up = 1;
    }
}
