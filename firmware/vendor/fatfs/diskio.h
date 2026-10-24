/* FatFs disk I/O interface for core-fm1 unity build.
 * All functions are static (single translation unit). */
#ifndef _DISKIO_DEFINED
#define _DISKIO_DEFINED

typedef BYTE DSTATUS;

typedef enum {
    RES_OK = 0,
    RES_ERROR,
    RES_WRPRT,
    RES_NOTRDY,
    RES_PARERR
} DRESULT;

static DSTATUS disk_initialize(BYTE pdrv);
static DSTATUS disk_status(BYTE pdrv);
static DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count);
static DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count);
static DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff);

#define STA_NOINIT  0x01
#define STA_NODISK  0x02
#define STA_PROTECT 0x04

#define CTRL_SYNC       0
#define GET_SECTOR_COUNT 1
#define GET_SECTOR_SIZE  2
#define GET_BLOCK_SIZE   3
#define CTRL_TRIM        4

#endif
