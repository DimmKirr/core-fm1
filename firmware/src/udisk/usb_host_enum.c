/* SPDX-License-Identifier: GPL-3.0-only */
/* USB host enumeration: SET_ADDRESS, GET_DESCRIPTOR, find MSC bulk endpoints. */

/* Standard USB descriptor structures */
typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} usb_device_desc_t;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} usb_config_desc_t;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} usb_interface_desc_t;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} usb_endpoint_desc_t;

/* Standard requests */
#define USB_REQ_GET_DESCRIPTOR  0x06
#define USB_REQ_SET_ADDRESS     0x05
#define USB_REQ_SET_CONFIG      0x09

#define USB_DIR_IN   0x80
#define USB_DIR_OUT  0x00

/* Scratch buffer for configuration descriptor (max 256 bytes) */
static uint8_t usb_enum_buf[256];

static int usb_host_enumerate(void)
{
    usb_device_desc_t dev;
    int r;

    /* 1. GET_DESCRIPTOR(DEVICE) at addr 0, first 8 bytes for bMaxPacketSize0 */
    usb_write_faddr(MUSB_ID, 0);
    r = usb_control_msg(USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                        (USB_DT_DEVICE << 8), 0, (uint8_t *)&dev, 8);
    if (r < 0) return -1;

    /* 2. SET_ADDRESS(1) */
    r = usb_control_msg(USB_DIR_OUT, USB_REQ_SET_ADDRESS, 1, 0, 0, 0);
    if (r < 0) return -2;
    usb_mdelay(10);
    usb_write_faddr(MUSB_ID, 1);

    /* 3. GET_DESCRIPTOR(DEVICE) full 18 bytes at addr 1 */
    r = usb_control_msg(USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                        (USB_DT_DEVICE << 8), 0, (uint8_t *)&dev, 18);
    if (r < 0) return -3;

    /* 4. GET_DESCRIPTOR(CONFIG) header to learn wTotalLength */
    r = usb_control_msg(USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                        (USB_DT_CONFIG << 8), 0, usb_enum_buf, 9);
    if (r < 0) return -4;

    usb_config_desc_t *cfg = (usb_config_desc_t *)usb_enum_buf;
    uint16_t total = cfg->wTotalLength;
    if (total > sizeof(usb_enum_buf)) total = sizeof(usb_enum_buf);

    /* 5. GET_DESCRIPTOR(CONFIG) full */
    r = usb_control_msg(USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                        (USB_DT_CONFIG << 8), 0, usb_enum_buf, total);
    if (r < 0) return -5;

    /* 6. Parse: find MSC BBB interface + bulk endpoints */
    uint8_t ep_in = 0, ep_out = 0;
    uint16_t ep_maxp = 64;
    uint8_t msc_found = 0;
    uint32_t pos = cfg->bLength;

    while (pos + 2 <= total) {
        uint8_t dlen = usb_enum_buf[pos];
        uint8_t dtype = usb_enum_buf[pos + 1];
        if (dlen < 2 || pos + dlen > total) break;

        if (dtype == USB_DT_INTERFACE && dlen >= 9) {
            usb_interface_desc_t *iface = (usb_interface_desc_t *)&usb_enum_buf[pos];
            if (iface->bInterfaceClass == USB_CLASS_MSC &&
                iface->bInterfaceSubClass == USB_MSC_SUBCLASS_SCSI &&
                iface->bInterfaceProtocol == USB_MSC_PROTO_BBB) {
                msc_found = 1;
            } else {
                msc_found = 0;
            }
        }

        if (dtype == USB_DT_ENDPOINT && dlen >= 7 && msc_found) {
            usb_endpoint_desc_t *ep = (usb_endpoint_desc_t *)&usb_enum_buf[pos];
            if ((ep->bmAttributes & 0x03) == 0x02) {  /* bulk */
                if (ep->bEndpointAddress & USB_EP_DIR_IN) {
                    ep_in = ep->bEndpointAddress & 0x0F;
                    ep_maxp = ep->wMaxPacketSize;
                } else {
                    ep_out = ep->bEndpointAddress & 0x0F;
                }
            }
        }
        pos += dlen;
    }

    uhost.vid = dev.idVendor;
    uhost.pid = dev.idProduct;
    uhost.dev_class = dev.bDeviceClass;

    if (!ep_in || !ep_out) return -6;

    /* 7. SET_CONFIGURATION */
    r = usb_control_msg(USB_DIR_OUT, USB_REQ_SET_CONFIG,
                        cfg->bConfigurationValue, 0, 0, 0);
    if (r < 0) return -7;
    usb_mdelay(10);

    /* 8. Configure host bulk endpoint */
    usb_h_ep_config(MUSB_ID, USB_BULK_HOST_EP, 0x02, 0, 0, 0, 0);
    usb_write_txmaxp(MUSB_ID, USB_BULK_HOST_EP, ep_maxp);
    usb_write_rxmaxp(MUSB_ID, USB_BULK_HOST_EP, ep_maxp);

    uhost.ep_in = ep_in;
    uhost.ep_out = ep_out;
    uhost.max_packet = ep_maxp;
    uhost.state = UHOST_ENUMERATED;
    return 0;
}
