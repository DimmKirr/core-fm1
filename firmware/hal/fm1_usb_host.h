/* SPDX-License-Identifier: GPL-3.0-only */
/* USB host controller HAL for AC791N OTG.
 * Switches the USB PHY into host mode and provides low-level transfer
 * primitives for Bulk-Only Transport (Mass Storage Class). */
#ifndef FM1_USB_HOST_H
#define FM1_USB_HOST_H
#include <stdint.h>

/* OTG mode register (USB_CON1 bits) */
#define USB_HOST_CON1     (*(volatile uint32_t *)0x11804)
#define USB_HOST_IO_CON0  (*(volatile uint32_t *)0x51000)

#define OTG_HOST_MODE     0x01
#define OTG_SLAVE_MODE    0x02

/* simplified host state */
#define UHOST_DISCONNECTED  0
#define UHOST_ATTACHED      1
#define UHOST_ENUMERATED    2
#define UHOST_MSC_READY     3
#define UHOST_ERROR         0xFF

typedef struct {
    uint8_t  state;
    int8_t   last_err;     /* negative error code from enum/msc init */
    uint8_t  ep_in;
    uint8_t  ep_out;
    uint16_t max_packet;
    uint16_t vid;
    uint16_t pid;
    uint8_t  dev_class;
    uint32_t capacity;     /* sectors */
    uint32_t sector_size;  /* typically 512 */
} usb_host_t;

static usb_host_t uhost;

/* CBW/CSW for Bulk-Only Transport */
#define CBW_SIGNATURE  0x43425355
#define CSW_SIGNATURE  0x53425355

typedef struct __attribute__((packed)) {
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t  bmCBWFlags;
    uint8_t  bCBWLUN;
    uint8_t  bCBWCBLength;
    uint8_t  CBWCB[16];
} usb_cbw_t;

typedef struct __attribute__((packed)) {
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t  bCSWStatus;
} usb_csw_t;

static void usb_host_init(void)
{
    uhost.state = UHOST_DISCONNECTED;
    uhost.last_err = 0;
    uhost.ep_in = 0;
    uhost.ep_out = 0;
    uhost.max_packet = 64;
    uhost.vid = 0;
    uhost.pid = 0;
    uhost.dev_class = 0;
    uhost.capacity = 0;
    uhost.sector_size = 512;
    usb_h_sie_init(MUSB_ID);
    usb_h_chirp_and_reset(MUSB_ID, 50, 1000);
}

static void usb_host_deinit(void)
{
    uhost.state = UHOST_DISCONNECTED;
    usb_h_sie_close(MUSB_ID);
    USB_HOST_CON1 = (USB_HOST_CON1 & ~0x03u) | OTG_SLAVE_MODE;
    USB_HOST_IO_CON0 &= ~(1u << 4);
}

static int usb_host_connected(void)
{
    return (usb_h_dev_status(MUSB_ID) != 0) ? 1 : 0;
}

/* Forward declarations for Phase 2-5 (defined in udisk/*.c, included after) */
static int usb_host_enumerate(void);
static int msc_init_device(void);
static int msc_read_sectors(uint32_t lba, uint8_t *buf, uint32_t count);

/* glue for diskio.c */
static int usb_host_read_sectors(uint32_t lba, uint8_t *buf, uint32_t count)
{
    if (uhost.state != UHOST_MSC_READY) return -1;
    return msc_read_sectors(lba, buf, count);
}

static int usb_host_read_capacity(uint32_t *sectors, uint32_t *sector_sz)
{
    if (uhost.state != UHOST_MSC_READY) return -1;
    *sectors = uhost.capacity;
    *sector_sz = uhost.sector_size;
    return 0;
}

static void usb_host_log_fail(int err)
{
    char line[CFM1_LOG_LINE_LEN];
    uint32_t ds = usb_h_dev_status(MUSB_ID);
    uint32_t dc = usb_read_devctl(MUSB_ID);
    /* "enum -N ds:NN dc:NNN" fits in 40 chars */
    int n = 0;
    const char *p = "enum ";
    while (*p) line[n++] = *p++;
    if (err < 0) { line[n++] = '-'; err = -err; }
    line[n++] = (char)('0' + err);
    p = " ds:"; while (*p) line[n++] = *p++;
    line[n++] = (char)('0' + ds);
    p = " dc:"; while (*p) line[n++] = *p++;
    if (dc >= 100) line[n++] = (char)('0' + dc / 100);
    if (dc >= 10) line[n++] = (char)('0' + (dc / 10) % 10);
    line[n++] = (char)('0' + dc % 10);
    line[n] = 0;
    cfm1_log(line);
}

static void usb_host_poll(void)
{
    if (uhost.state == UHOST_DISCONNECTED) {
        if (usb_host_connected()) {
            uhost.state = UHOST_ATTACHED;
            usb_mdelay(200);
            int r = usb_host_enumerate();
            if (r == 0) {
                cfm1_log_int("USB host: VID ", uhost.vid);
                cfm1_log_int("USB host: PID ", uhost.pid);
                cfm1_log("USB host: device connected");
                r = msc_init_device();
                if (r == 0) {
                    cfm1_log_int("USB host: sectors ", (int32_t)uhost.capacity);
                } else {
                    uhost.last_err = (int8_t)(r - 10);
                    uhost.state = UHOST_ERROR;
                    cfm1_log_int("USB host: MSC fail ", r);
                }
            } else {
                uhost.last_err = (int8_t)r;
                uhost.state = UHOST_ERROR;
                usb_host_log_fail(r);
            }
        }
    } else if (uhost.state >= UHOST_ATTACHED) {
        if (!usb_host_connected()) {
            cfm1_log("USB: device removed");
            uhost.state = UHOST_DISCONNECTED;
        }
    }
}

#endif /* FM1_USB_HOST_H */
