/* SPDX-License-Identifier: GPL-3.0-only */
/* USB host transfer engine: polled EP0 control + bulk IN/OUT.
 * Uses cpu.a low-level MUSB accessors; no ISRs, no RTOS. */

#define USB_XFER_TIMEOUT  5000  /* ms */
#define USB_EP0_MAXP      64

/* ---- Phase 2: Control Transfers (EP0) ---- */

static int usb_ctrl_setup(const uint8_t setup[8])
{
    usb_write_ep0(MUSB_ID, setup, 8);
    usb_write_csr0(MUSB_ID, CSR0H_SetupPkt | CSR0H_TxPktRdy);

    uint32_t t = USB_XFER_TIMEOUT;
    while (t--) {
        uint32_t csr = usb_read_csr0(MUSB_ID);
        if (csr & CSR0H_Error)  return -1;
        if (csr & CSR0H_RxStall) return -2;
        if (!(csr & CSR0H_TxPktRdy)) return 0;
        usb_mdelay(1);
    }
    return -3;
}

static int usb_ctrl_in(uint8_t *buf, uint32_t len, uint32_t *actual)
{
    uint32_t done = 0;
    while (done < len) {
        usb_write_csr0(MUSB_ID, CSR0H_ReqPkt);

        uint32_t t = USB_XFER_TIMEOUT;
        while (t--) {
            uint32_t csr = usb_read_csr0(MUSB_ID);
            if (csr & CSR0H_Error)   { *actual = done; return -1; }
            if (csr & CSR0H_RxStall) { *actual = done; return -2; }
            if (csr & CSR0H_RxPktRdy) break;
            usb_mdelay(1);
        }
        if (!t) { *actual = done; return -3; }

        uint32_t cnt = usb_read_count0(MUSB_ID);
        if (cnt > len - done) cnt = len - done;
        usb_read_ep0(MUSB_ID, buf + done, cnt);
        done += cnt;

        usb_write_csr0(MUSB_ID, 0);

        if (cnt < USB_EP0_MAXP) break;
    }
    *actual = done;
    return 0;
}

static int usb_ctrl_status_in(void)
{
    usb_write_csr0(MUSB_ID, CSR0H_ReqPkt | CSR0H_StatusPkt);
    uint32_t t = USB_XFER_TIMEOUT;
    while (t--) {
        uint32_t csr = usb_read_csr0(MUSB_ID);
        if (csr & CSR0H_RxPktRdy) {
            usb_write_csr0(MUSB_ID, 0);
            return 0;
        }
        if (csr & CSR0H_Error)  return -1;
        if (csr & CSR0H_RxStall) return -2;
        usb_mdelay(1);
    }
    return -3;
}

static int usb_ctrl_status_out(void)
{
    usb_write_csr0(MUSB_ID, CSR0H_TxPktRdy | CSR0H_StatusPkt);
    uint32_t t = USB_XFER_TIMEOUT;
    while (t--) {
        uint32_t csr = usb_read_csr0(MUSB_ID);
        if (!(csr & CSR0H_TxPktRdy)) return 0;
        if (csr & CSR0H_Error)  return -1;
        if (csr & CSR0H_RxStall) return -2;
        usb_mdelay(1);
    }
    return -3;
}

static int usb_control_msg(uint8_t bmReq, uint8_t bReq, uint16_t wValue,
                           uint16_t wIndex, uint8_t *data, uint16_t wLength)
{
    uint8_t setup[8];
    setup[0] = bmReq;
    setup[1] = bReq;
    setup[2] = (uint8_t)(wValue & 0xFF);
    setup[3] = (uint8_t)(wValue >> 8);
    setup[4] = (uint8_t)(wIndex & 0xFF);
    setup[5] = (uint8_t)(wIndex >> 8);
    setup[6] = (uint8_t)(wLength & 0xFF);
    setup[7] = (uint8_t)(wLength >> 8);

    int r = usb_ctrl_setup(setup);
    if (r) return r;

    if (wLength && data) {
        uint32_t actual = 0;
        if (bmReq & 0x80) {
            r = usb_ctrl_in(data, wLength, &actual);
            if (r) return r;
            r = usb_ctrl_status_out();
        } else {
            (void)actual;
            r = usb_ctrl_status_in();
        }
        if (r) return r;
        return (int)actual;
    }
    return usb_ctrl_status_in();
}

/* ---- Phase 3: Bulk Transfers ---- */

#define USB_BULK_HOST_EP  1
#define USB_BULK_XFER     0  /* polled (no DMA) */

static int usb_bulk_out(uint8_t target_ep, const uint8_t *data, uint32_t len)
{
    return usb_h_ep_write(MUSB_ID, USB_BULK_HOST_EP, uhost.max_packet,
                          target_ep, data, len, USB_BULK_XFER);
}

static int usb_bulk_in(uint8_t target_ep, uint8_t *data, uint32_t len)
{
    return usb_h_ep_read(MUSB_ID, USB_BULK_HOST_EP, uhost.max_packet,
                         target_ep, data, len, USB_BULK_XFER);
}
