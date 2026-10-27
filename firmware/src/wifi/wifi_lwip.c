/* SPDX-License-Identifier: GPL-3.0-only */
/* Clean-room implementation from docs/hardware/wifi/SPECSHEET.md and lwIP BSD documentation.
 * No vendor code, IR or disassembly.
 *
 * lwIP network interface driver for the AC791N WiFi stack. Bridges wifi_data.c
 * TX/RX to lwIP's netif. The lwIP source tree (BSD-licensed) must be added to
 * the build for CONFIG_WIFI_LWIP; without it this file compiles as stubs. */

#ifdef CONFIG_WIFI_LWIP

/* ---- lwIP headers (BSD-licensed lwIP 2.x) ---- */
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/timeouts.h"
#include "lwip/ip4_addr.h"
#include "netif/ethernet.h"

#define WIFI_LWIP_MTU        1500u
#define WIFI_LWIP_HWADDR_LEN 6u

static struct netif wifi_netif;
static uint8_t wifi_lwip_up;

/* MAC address: locally-administered default until VM/OTP path is shared with
 * BLE (VM 104 holds the provisioned BLE public address). */
static uint8_t wifi_mac_addr[WIFI_LWIP_HWADDR_LEN] = {
    0x02, 0x00, 0x00, 0x00, 0x00, 0x01
};

/* ---- low-level output: lwIP -> WiFi TX ---- */

static err_t wifi_lwip_linkoutput(struct netif *nif, struct pbuf *p)
{
    uint8_t buf[WIFI_LWIP_MTU + 14u];
    uint32_t len = 0;
    struct pbuf *q;

    (void)nif;
    for (q = p; q != 0; q = q->next) {
        uint32_t chunk = (uint32_t)q->len;
        if (len + chunk > sizeof buf)
            chunk = sizeof buf - len;
        wifi_memcpy(buf + len, (const uint8_t *)q->payload, chunk);
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
        wifi_memcpy((uint8_t *)q->payload, data + off, chunk);
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
        nif->hwaddr[i] = wifi_mac_addr[i];
    nif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
    return ERR_OK;
}

/* ---- public interface ---- */

static void wifi_lwip_init(void)
{
    ip4_addr_t z;
    ip4_addr_set_zero(&z);

    lwip_init();
    netif_add(&wifi_netif, &z, &z, &z, 0,
              wifi_lwip_netif_init, ethernet_input);
    netif_set_default(&wifi_netif);
    wifi_lwip_up = 0;
}

static void wifi_lwip_start_dhcp(void)
{
    netif_set_up(&wifi_netif);
    netif_set_link_up(&wifi_netif);
    dhcp_start(&wifi_netif);
    wifi_lwip_up = 1;
}

static void wifi_lwip_stop_dhcp(void)
{
    wifi_lwip_up = 0;
    dhcp_stop(&wifi_netif);
    netif_set_link_down(&wifi_netif);
    netif_set_down(&wifi_netif);
}

static uint32_t wifi_lwip_get_ip(void)
{
    return netif_ip4_addr(&wifi_netif)->addr;
}

/* called from wifi_poll in the main loop */
static void wifi_lwip_poll(void)
{
    sys_check_timeouts();
}

#else /* !CONFIG_WIFI_LWIP */

/* Stubs: the WiFi stack compiles without lwIP, but IP networking is unavailable. */
static uint8_t wifi_mac_addr[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
static void wifi_lwip_init(void) {}
static void wifi_lwip_input(const uint8_t *data, uint32_t len) { (void)data; (void)len; }
static void wifi_lwip_start_dhcp(void) {}
static void wifi_lwip_stop_dhcp(void) {}
static uint32_t wifi_lwip_get_ip(void) { return 0; }
static void wifi_lwip_poll(void) {}

#endif /* CONFIG_WIFI_LWIP */
