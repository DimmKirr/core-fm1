/* SPDX-License-Identifier: GPL-3.0-only */
/* USB Mass Storage Class: Bulk-Only Transport (BBB) + SCSI commands. */

static uint32_t msc_tag;

/* SCSI opcodes */
#define SCSI_TEST_UNIT_READY  0x00
#define SCSI_REQUEST_SENSE    0x03
#define SCSI_INQUIRY          0x12
#define SCSI_READ_CAPACITY10  0x25
#define SCSI_READ10           0x28

static int msc_bot_xfer(uint8_t *cb, uint8_t cb_len, uint8_t dir_in,
                        uint8_t *data, uint32_t data_len)
{
    usb_cbw_t cbw;
    usb_csw_t csw;

    cbw.dCBWSignature = CBW_SIGNATURE;
    cbw.dCBWTag = ++msc_tag;
    cbw.dCBWDataTransferLength = data_len;
    cbw.bmCBWFlags = dir_in ? 0x80 : 0x00;
    cbw.bCBWLUN = 0;
    cbw.bCBWCBLength = cb_len;

    uint32_t i;
    for (i = 0; i < 16; i++)
        cbw.CBWCB[i] = (i < cb_len) ? cb[i] : 0;

    /* send CBW */
    int r = usb_bulk_out(uhost.ep_out, (const uint8_t *)&cbw, 31);
    if (r < 0) return -1;

    /* data phase */
    if (data_len && data) {
        if (dir_in) {
            r = usb_bulk_in(uhost.ep_in, data, data_len);
        } else {
            r = usb_bulk_out(uhost.ep_out, data, data_len);
        }
        if (r < 0) return -2;
    }

    /* read CSW */
    r = usb_bulk_in(uhost.ep_in, (uint8_t *)&csw, 13);
    if (r < 0) return -3;
    if (csw.dCSWSignature != CSW_SIGNATURE) return -4;
    if (csw.dCSWTag != cbw.dCBWTag) return -5;
    if (csw.bCSWStatus != 0) return -6;
    return 0;
}

static int msc_test_unit_ready(void)
{
    uint8_t cb[6] = { SCSI_TEST_UNIT_READY, 0, 0, 0, 0, 0 };
    return msc_bot_xfer(cb, 6, 0, 0, 0);
}

static int msc_request_sense(uint8_t *buf)
{
    uint8_t cb[6] = { SCSI_REQUEST_SENSE, 0, 0, 0, 18, 0 };
    return msc_bot_xfer(cb, 6, 1, buf, 18);
}

static int msc_inquiry(uint8_t *buf)
{
    uint8_t cb[6] = { SCSI_INQUIRY, 0, 0, 0, 36, 0 };
    return msc_bot_xfer(cb, 6, 1, buf, 36);
}

static uint32_t msc_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static int msc_read_capacity(uint32_t *last_lba, uint32_t *block_size)
{
    uint8_t buf[8];
    uint8_t cb[10] = { SCSI_READ_CAPACITY10, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    int r = msc_bot_xfer(cb, 10, 1, buf, 8);
    if (r) return r;
    *last_lba = msc_be32(buf);
    *block_size = msc_be32(buf + 4);
    return 0;
}

static int msc_read_sectors(uint32_t lba, uint8_t *buf, uint32_t count)
{
    uint8_t cb[10];
    cb[0] = SCSI_READ10;
    cb[1] = 0;
    cb[2] = (uint8_t)(lba >> 24);
    cb[3] = (uint8_t)(lba >> 16);
    cb[4] = (uint8_t)(lba >> 8);
    cb[5] = (uint8_t)(lba);
    cb[6] = 0;
    cb[7] = (uint8_t)(count >> 8);
    cb[8] = (uint8_t)(count);
    cb[9] = 0;
    return msc_bot_xfer(cb, 10, 1, buf, count * uhost.sector_size);
}

static int msc_init_device(void)
{
    uint8_t buf[36];
    int r, retries;

    r = msc_inquiry(buf);
    if (r) return r;

    for (retries = 0; retries < 10; retries++) {
        r = msc_test_unit_ready();
        if (r == 0) break;
        msc_request_sense(buf);
        usb_mdelay(100);
    }
    if (r) return r;

    uint32_t last_lba, block_size;
    r = msc_read_capacity(&last_lba, &block_size);
    if (r) return r;

    uhost.capacity = last_lba + 1;
    uhost.sector_size = block_size;
    uhost.state = UHOST_MSC_READY;
    return 0;
}
