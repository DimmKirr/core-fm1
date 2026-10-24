/* SPDX-License-Identifier: GPL-3.0-only */
/* BLE File Transfer GATT service: file listing, chunked upload from phone.
 * Service UUID E4B3FA01-1B2C-4D5E-8A9F-3C6D7E8F9A0B. */
#ifndef BLE_FT_H
#define BLE_FT_H
#include <stdint.h>
#include "ble_cfg.h"

/* Service and characteristic UUIDs (128-bit, little-endian byte order).
 * Common base: E4B3FAxx-1B2C-4D5E-8A9F-3C6D7E8F9A0B */
static const uint8_t BLE_UUID_FT_SVC[16]  = {0x0B,0x9A,0x8F,0x7E,0x6D,0x3C,0x9F,0x8A,
                                              0x5E,0x4D,0x2C,0x1B,0x01,0xFA,0xB3,0xE4};
static const uint8_t BLE_UUID_FT_LIST[16] = {0x0B,0x9A,0x8F,0x7E,0x6D,0x3C,0x9F,0x8A,
                                              0x5E,0x4D,0x2C,0x1B,0x02,0xFA,0xB3,0xE4};
static const uint8_t BLE_UUID_FT_CTL[16]  = {0x0B,0x9A,0x8F,0x7E,0x6D,0x3C,0x9F,0x8A,
                                              0x5E,0x4D,0x2C,0x1B,0x03,0xFA,0xB3,0xE4};
static const uint8_t BLE_UUID_FT_RX[16]   = {0x0B,0x9A,0x8F,0x7E,0x6D,0x3C,0x9F,0x8A,
                                              0x5E,0x4D,0x2C,0x1B,0x04,0xFA,0xB3,0xE4};
static const uint8_t BLE_UUID_FT_TX[16]   = {0x0B,0x9A,0x8F,0x7E,0x6D,0x3C,0x9F,0x8A,
                                              0x5E,0x4D,0x2C,0x1B,0x05,0xFA,0xB3,0xE4};

/* control commands (phone -> FM-1, FT_CTL write) */
enum { FT_CMD_START = 0x01, FT_CMD_ABORT = 0x02, FT_CMD_STATUS = 0x03 };

/* response codes (FM-1 -> phone, FT_TX notify) */
enum { FT_RSP_ACK = 0x01, FT_RSP_STATUS = 0x02, FT_RSP_ERROR = 0x03, FT_RSP_DONE = 0x04 };

/* file types */
enum { FT_TYPE_FIRMWARE = 0x01, FT_TYPE_PRESET = 0x02, FT_TYPE_WAVETABLE = 0x03, FT_TYPE_BACKUP = 0x04 };

/* transfer state */
enum { FT_IDLE = 0, FT_RECEIVING, FT_VERIFYING, FT_DONE, FT_ERROR };

/* error codes */
enum { FT_ERR_NONE = 0, FT_ERR_BAD_TYPE = 0x01, FT_ERR_TOO_LARGE = 0x02, FT_ERR_SEQ = 0x03,
       FT_ERR_CRC = 0x04, FT_ERR_BUSY = 0x05 };

#define FT_WINDOW 4u
#define FT_LIST_MAX 128u

BLE_API void ble_ft_init(void);
BLE_API uint8_t ble_ft_list(uint8_t *buf, uint8_t max);
BLE_API void ble_ft_on_ctl(const uint8_t *data, uint16_t len);
BLE_API void ble_ft_on_data(const uint8_t *data, uint16_t len);
BLE_API void ble_ft_ccc_set(uint8_t val);
BLE_API uint8_t ble_ft_ccc_get(void);
BLE_API void ble_ft_event(void);

#endif
