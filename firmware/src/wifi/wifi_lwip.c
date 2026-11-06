/* SPDX-License-Identifier: GPL-3.0-only */
/* Clean-room implementation from docs/hardware/wifi/SPECSHEET.md and lwIP BSD documentation.
 * No vendor code, IR or disassembly.
 *
 * lwIP network interface driver for the AC791N WiFi stack. Bridges wifi_data.c
 * TX/RX to lwIP's netif. The lwIP source tree (BSD-licensed) must be added to
 * the build for CONFIG_WIFI_LWIP; without it this file compiles as stubs. */

#ifdef CONFIG_WIFI_LWIP

/* ---- sys_now: lwIP timer tick (must be extern, called by lwIP core) --- */
uint32_t sys_now(void) { return fm1_ms; }

/* ---- lwIP headers (BSD-licensed lwIP 2.x) ---- */
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/timeouts.h"
#include "lwip/ip4_addr.h"
#include "lwip/dns.h"
#include "netif/ethernet.h"

#define WIFI_LWIP_MTU        1500u
#define WIFI_LWIP_HWADDR_LEN 6u

static struct netif wifi_netif;
static uint8_t wifi_lwip_up;

/* MAC address: shared with wifi_mac.c via wifi_sta.our_mac.
 * This pointer is set by wifi_lwip_set_mac() before lwIP netif init. */
static const uint8_t *wifi_lwip_mac;

/* static TX buffer (moved from stack for scheduler safety) */
static uint8_t wifi_tx_eth[WIFI_LWIP_MTU + 14u] __attribute__((section(".pool")));

/* ---- low-level output: lwIP -> WiFi TX ---- */

static err_t wifi_lwip_linkoutput(struct netif *nif, struct pbuf *p)
{
    uint8_t *buf = wifi_tx_eth;
    uint32_t len = 0;
    struct pbuf *q;

    (void)nif;
    for (q = p; q != 0; q = q->next) {
        uint32_t chunk = (uint32_t)q->len;
        if (len + chunk > (WIFI_LWIP_MTU + 14u))
            chunk = (WIFI_LWIP_MTU + 14u) - len;
        memcpy(buf + len, (const uint8_t *)q->payload, chunk);
        len += chunk;
    }
    if (len < 14u)
        return ERR_BUF;

    wifi_data_tx(buf, len);
    return ERR_OK;
}

/* ---- low-level input: WiFi RX -> lwIP ---- */

static void wifi_lwip_input(const uint8_t *data, uint32_t len)
{
    struct pbuf *p, *q;
    uint32_t off;

    if (!wifi_lwip_up || len < 14u || len > WIFI_LWIP_MTU + 14u)
        return;

    p = pbuf_alloc(PBUF_RAW, (uint16_t)len, PBUF_POOL);
    if (!p)
        return;

    off = 0;
    for (q = p; q != 0 && off < len; q = q->next) {
        uint32_t chunk = (uint32_t)q->len;
        if (off + chunk > len)
            chunk = len - off;
        memcpy((uint8_t *)q->payload, data + off, chunk);
        off += chunk;
    }

    if (wifi_netif.input(p, &wifi_netif) != ERR_OK)
        pbuf_free(p);
}

/* ---- netif init callback (called by netif_add) ---- */

static err_t wifi_lwip_netif_init(struct netif *nif)
{
    uint32_t i;
    nif->name[0] = 'w';
    nif->name[1] = 'f';
    nif->output     = etharp_output;
    nif->linkoutput = wifi_lwip_linkoutput;
    nif->mtu        = WIFI_LWIP_MTU;
    nif->hwaddr_len = WIFI_LWIP_HWADDR_LEN;
    for (i = 0; i < WIFI_LWIP_HWADDR_LEN; i++)
        nif->hwaddr[i] = wifi_lwip_mac ? wifi_lwip_mac[i] : 0;
    nif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
    return ERR_OK;
}

static void weather_fetch_start(void);

/* ---- netif status callback (DHCP bound, etc.) ---- */

static void wifi_lwip_status_cb(struct netif *nif)
{
    uint32_t ip = nif->ip_addr.addr;
    if (ip) {
        cfm1_log_hex("wifi: dhcp bound ip=", (const uint8_t *)&ip, 4);
        cfm1_log_hex("wifi: dhcp gw=", (const uint8_t *)&nif->gw.addr, 4);
        weather_fetch_start();
    }
}

/* ---- public interface ---- */

static void wifi_lwip_set_mac(const uint8_t *mac)
{
    wifi_lwip_mac = mac;
}

static void wifi_lwip_init(void)
{
    ip4_addr_t z;
    ip4_addr_set_zero(&z);

    lwip_init();
    netif_add(&wifi_netif, &z, &z, &z, 0,
              wifi_lwip_netif_init, ethernet_input);
    netif_set_default(&wifi_netif);
    netif_set_status_callback(&wifi_netif, wifi_lwip_status_cb);
    wifi_lwip_up = 0;
    cfm1_log("wifi: lwip init");
}

static void wifi_lwip_start_dhcp(void)
{
    cfm1_log("wifi: dhcp start");
    netif_set_up(&wifi_netif);
    netif_set_link_up(&wifi_netif);
    dhcp_start(&wifi_netif);
    wifi_lwip_up = 1;
}

static void wifi_lwip_stop_dhcp(void)
{
    cfm1_log("wifi: dhcp stop");
    wifi_lwip_up = 0;
    dhcp_stop(&wifi_netif);
    netif_set_link_down(&wifi_netif);
    netif_set_down(&wifi_netif);
}

static uint32_t wifi_lwip_get_ip(void)
{
    return netif_ip4_addr(&wifi_netif)->addr;
}

static uint32_t wifi_lwip_get_gw(void)
{
    return netif_ip4_gw(&wifi_netif)->addr;
}

static uint32_t wifi_lwip_get_dns(void)
{
    const ip_addr_t *d = dns_getserver(0);
    return d ? ip_2_ip4(d)->addr : 0;
}

/* called from wifi_poll in the main loop */
static void wifi_lwip_poll(void)
{
    sys_check_timeouts();
}

#else /* !CONFIG_WIFI_LWIP */

/* Stubs: the WiFi stack compiles without lwIP, but IP networking is unavailable. */
static void wifi_lwip_set_mac(const uint8_t *mac) { (void)mac; }
static void wifi_lwip_init(void) {}
static void wifi_lwip_input(const uint8_t *data, uint32_t len) { (void)data; (void)len; }
static void wifi_lwip_start_dhcp(void) {}
static void wifi_lwip_stop_dhcp(void) {}
static uint32_t wifi_lwip_get_ip(void) { return 0; }
static uint32_t wifi_lwip_get_gw(void) { return 0; }
static uint32_t wifi_lwip_get_dns(void) { return 0; }
static void wifi_lwip_poll(void) {}

#endif /* CONFIG_WIFI_LWIP */
