/* SPDX-License-Identifier: BSD-3-Clause */
/* Unity-build include for lwIP 2.2.0 core sources.
 * This file #includes the minimal set of lwIP .c files required for
 * TCP, UDP, DNS, DHCP, ARP, and ICMP on a bare-metal NO_SYS=1 target.
 *
 * Include paths required by the build:
 *   firmware/src/wifi/          (lwipopts.h, arch/cc.h)
 *   firmware/upstream/lwip/src/include/  (lwIP headers)
 *
 * lwIP functions are NOT static -- this is intentional. lwIP is designed
 * as a separately-linked library; its symbols are extern by default. */

/* ---- core ------------------------------------------------------------- */
#include "upstream/lwip/src/core/init.c"
#include "upstream/lwip/src/core/def.c"
#include "upstream/lwip/src/core/mem.c"
#include "upstream/lwip/src/core/memp.c"
#include "upstream/lwip/src/core/pbuf.c"
#include "upstream/lwip/src/core/netif.c"
#include "upstream/lwip/src/core/ip.c"
#include "upstream/lwip/src/core/tcp.c"
#include "upstream/lwip/src/core/tcp_in.c"
#include "upstream/lwip/src/core/tcp_out.c"
#include "upstream/lwip/src/core/udp.c"
#include "upstream/lwip/src/core/dns.c"
#include "upstream/lwip/src/core/timeouts.c"
#include "upstream/lwip/src/core/inet_chksum.c"

/* ---- IPv4 ------------------------------------------------------------- */
#include "upstream/lwip/src/core/ipv4/etharp.c"
#include "upstream/lwip/src/core/ipv4/ip4.c"
#include "upstream/lwip/src/core/ipv4/ip4_addr.c"
#include "upstream/lwip/src/core/ipv4/ip4_frag.c"
#include "upstream/lwip/src/core/ipv4/dhcp.c"
#include "upstream/lwip/src/core/ipv4/icmp.c"

/* ---- netif ------------------------------------------------------------ */
#include "upstream/lwip/src/netif/ethernet.c"
