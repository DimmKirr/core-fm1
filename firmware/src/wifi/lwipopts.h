/* SPDX-License-Identifier: BSD-3-Clause */
/* lwIP configuration for core-fm1 (AC791N, bare-metal, NO_SYS=1).
 * This header is BSD-licensed: it configures BSD-licensed lwIP code. */

#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/* --- JieLi bare-metal: skip system headers lwIP would normally pull in -- */
#define LWIP_NO_INTTYPES_H      1
#define LWIP_NO_CTYPE_H         1
#define LWIP_NO_LIMITS_H        1

/* --- NO_SYS mode: no OS, raw API only --------------------------------- */
#define NO_SYS                  1
#define LWIP_SOCKET             0
#define LWIP_NETCONN            0
#define SYS_LIGHTWEIGHT_PROT    0

/* --- enabled protocols ------------------------------------------------- */
#define LWIP_TCP                1
#define LWIP_UDP                1
#define LWIP_DNS                1
#define LWIP_DHCP               1
#define LWIP_ARP                1
#define LWIP_ICMP               1

/* --- disabled protocols ------------------------------------------------ */
#define LWIP_IPV6               0
#define LWIP_IGMP               0
#define LWIP_PPP                0
#define LWIP_SNMP               0
#define LWIP_RAW                0
#define LWIP_AUTOIP             0
#define LWIP_ACD                0
#define LWIP_DHCP_DOES_ACD_CHECK 0

/* --- memory tuning (fits in ~12 KB RAM) -------------------------------- */
#define MEM_SIZE                4096
#define MEM_ALIGNMENT           4
#define PBUF_POOL_SIZE          8
#define PBUF_POOL_BUFSIZE       1536

/* --- TCP tuning -------------------------------------------------------- */
#define TCP_MSS                 1400
#define TCP_SND_BUF             (2 * TCP_MSS)
#define TCP_WND                 (2 * TCP_MSS)

/* --- DNS --------------------------------------------------------------- */
#define DNS_TABLE_SIZE          2
#define DNS_MAX_SERVERS         1

/* --- DHCP -------------------------------------------------------------- */
#define DHCP_DOES_ARP_CHECK     0

/* --- statistics (disabled to save RAM) --------------------------------- */
#define LWIP_STATS              0

/* --- checksums: software (no hardware offload on AC791N) --------------- */
#define CHECKSUM_GEN_IP         1
#define CHECKSUM_GEN_UDP        1
#define CHECKSUM_GEN_TCP        1
#define CHECKSUM_GEN_ICMP       1
#define CHECKSUM_CHECK_IP       1
#define CHECKSUM_CHECK_UDP      1
#define CHECKSUM_CHECK_TCP      1
#define CHECKSUM_CHECK_ICMP     1

/* --- netif callbacks --------------------------------------------------- */
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK   1

/* --- RNG for DNS transaction IDs --------------------------------------- */
static unsigned rng(void);
#define LWIP_RAND()             rng()

#endif /* LWIPOPTS_H */
