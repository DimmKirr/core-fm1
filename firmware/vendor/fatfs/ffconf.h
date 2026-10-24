/* FatFs configuration for core-fm1.
 * Based on ChaN's FatFs R0.15 ffconf.h template.
 * See http://elm-chan.org/fsw/ff/ for the full library. */
#ifndef FFCONF_DEF
#define FFCONF_DEF  80286

/* Function configuration */
#define FF_FS_READONLY   1   /* read-only: no write, mkfs, truncate */
#define FF_FS_MINIMIZE   1   /* remove f_stat, f_getfree, f_unlink, f_mkdir, f_truncate, f_rename */
#define FF_USE_FIND      0
#define FF_USE_MKFS      0
#define FF_USE_FASTSEEK  0
#define FF_USE_EXPAND    0
#define FF_USE_CHMOD     0
#define FF_USE_LABEL     0
#define FF_USE_FORWARD   0
#define FF_USE_STRFUNC   0
#define FF_PRINT_LLI     0
#define FF_PRINT_FLOAT   0
#define FF_STRF_ENCODE   0

/* Locale and namespace */
#define FF_CODE_PAGE     437  /* US English */
#define FF_USE_LFN       0   /* SFN only (ffunicode.c too large for MCU) */
#define FF_MAX_LFN       255
#define FF_LFN_UNICODE   0   /* ANSI/OEM */
#define FF_LFN_BUF       255
#define FF_SFN_BUF       12
#define FF_FS_RPATH      0

/* Drive/volume */
#define FF_VOLUMES       1
#define FF_STR_VOLUME_ID 0
#define FF_MULTI_PARTITION 0
#define FF_MIN_SS        512
#define FF_MAX_SS        512
#define FF_LBA64         0
#define FF_MIN_GPT       0x10000000
#define FF_USE_TRIM      0

/* System */
#define FF_FS_TINY       1   /* smaller buffer: 1 sector per file object */
#define FF_FS_EXFAT      0   /* FAT32 only (exFAT patents) */
#define FF_FS_NORTC      1   /* no RTC: fixed timestamp */
#define FF_NORTC_MON     1
#define FF_NORTC_MDAY    1
#define FF_NORTC_YEAR    2025
#define FF_FS_NOFSINFO   0
#define FF_FS_LOCK       0
#define FF_FS_REENTRANT  0

#endif /* FFCONF_DEF */
