/* SPDX-License-Identifier: GPL-3.0-only */
/* FatFs disk I/O glue: routes FatFs sector reads to the USB host driver. */

static DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv; (void)buff; (void)sector; (void)count;
    return RES_WRPRT;
}

static DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0) return STA_NOINIT;
    if (uhost.state != UHOST_MSC_READY) return STA_NOINIT;
    uint32_t cap, ssz;
    if (usb_host_read_capacity(&cap, &ssz) != 0)
        return STA_NOINIT;
    uhost.capacity = cap;
    uhost.sector_size = ssz;
    return 0;
}

static DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != 0) return STA_NOINIT;
    if (uhost.state != UHOST_MSC_READY) return STA_NODISK;
    return 0;
}

static DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0) return RES_PARERR;
    if (uhost.state != UHOST_MSC_READY) return RES_NOTRDY;
    if (usb_host_read_sectors(sector, buff, count) != 0)
        return RES_ERROR;
    return RES_OK;
}

static DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0) return RES_PARERR;
    if (uhost.state != UHOST_MSC_READY) return RES_NOTRDY;
    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(DWORD *)buff = uhost.capacity;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = (WORD)uhost.sector_size;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
