/* SPDX-License-Identifier: GPL-3.0-only */
/* BLE File Transfer service logic: file listing, chunked upload state machine.
 * Flash writes are stubbed; the state machine tracks bytes received. */
#include "ble_ft.h"
#include "ble_host.h"
#include "ble_ll.h"
#include "ble_util.h"

static struct {
    uint8_t state;
    uint8_t file_type;
    uint8_t ccc_ft;
    uint8_t err;
    uint16_t next_seq;
    uint16_t chunks_since_ack;
    uint32_t total_size;
    uint32_t received;
    uint32_t crc32;
    uint8_t pending_rsp;
    uint8_t rsp_buf[12];
    uint8_t rsp_len;
} bft;

BLE_API void ble_ft_init(void)
{
    ble_zero((uint8_t *)&bft, sizeof bft);
}

BLE_API uint8_t ble_ft_list(uint8_t *buf, uint8_t max)
{
    static const struct {
        uint8_t type;
        const char *name;
        uint32_t size;
    } entries[] = {
        {0, "presets/",       0},
        {0, "wavetables/",    0},
        {1, "firmware.fwsc",  0},
    };
    uint8_t pos = 0, i;
    uint8_t count = sizeof entries / sizeof entries[0];
    if (pos >= max)
        return 0;
    buf[pos++] = count;
    for (i = 0; i < count && pos + 7u < max; i++) {
        uint8_t nlen = 0;
        const char *n = entries[i].name;
        while (n[nlen])
            nlen++;
        if (pos + 2u + nlen + 4u > max)
            break;
        buf[pos++] = entries[i].type;
        buf[pos++] = nlen;
        ble_cpy(buf + pos, (const uint8_t *)n, nlen);
        pos = (uint8_t)(pos + nlen);
        buf[pos++] = (uint8_t)(entries[i].size);
        buf[pos++] = (uint8_t)(entries[i].size >> 8);
        buf[pos++] = (uint8_t)(entries[i].size >> 16);
        buf[pos++] = (uint8_t)(entries[i].size >> 24);
    }
    return pos;
}

static void ft_queue_rsp(uint8_t code, const uint8_t *data, uint8_t len)
{
    bft.rsp_buf[0] = code;
    if (len > sizeof bft.rsp_buf - 1u)
        len = sizeof bft.rsp_buf - 1u;
    if (len)
        ble_cpy(bft.rsp_buf + 1, data, len);
    bft.rsp_len = (uint8_t)(1u + len);
    bft.pending_rsp = 1;
}

static void ft_ack(void)
{
    uint8_t d[3];
    ble_wr16(d, bft.next_seq);
    d[2] = FT_WINDOW;
    ft_queue_rsp(FT_RSP_ACK, d, 3);
}

BLE_API void ble_ft_on_ctl(const uint8_t *data, uint16_t len)
{
    if (!len)
        return;
    switch (data[0]) {
    case FT_CMD_START:
        if (bft.state == FT_RECEIVING) {
            uint8_t e = FT_ERR_BUSY;
            ft_queue_rsp(FT_RSP_ERROR, &e, 1);
            return;
        }
        if (len < 11u) {
            uint8_t e = FT_ERR_BAD_TYPE;
            ft_queue_rsp(FT_RSP_ERROR, &e, 1);
            return;
        }
        bft.file_type = data[1];
        if (bft.file_type < FT_TYPE_FIRMWARE || bft.file_type > FT_TYPE_BACKUP) {
            uint8_t e = FT_ERR_BAD_TYPE;
            ft_queue_rsp(FT_RSP_ERROR, &e, 1);
            return;
        }
        bft.total_size = ble_rd32(data + 2);
        bft.crc32 = ble_rd32(data + 6);
        bft.received = 0;
        bft.next_seq = 0;
        bft.chunks_since_ack = 0;
        bft.err = FT_ERR_NONE;
        bft.state = FT_RECEIVING;
        ft_ack();
        break;
    case FT_CMD_ABORT:
        bft.state = FT_IDLE;
        ft_ack();
        break;
    case FT_CMD_STATUS: {
        uint8_t d[9];
        d[0] = bft.state;
        d[1] = (uint8_t)(bft.received);
        d[2] = (uint8_t)(bft.received >> 8);
        d[3] = (uint8_t)(bft.received >> 16);
        d[4] = (uint8_t)(bft.received >> 24);
        d[5] = (uint8_t)(bft.total_size);
        d[6] = (uint8_t)(bft.total_size >> 8);
        d[7] = (uint8_t)(bft.total_size >> 16);
        d[8] = (uint8_t)(bft.total_size >> 24);
        ft_queue_rsp(FT_RSP_STATUS, d, 9);
        break;
    }
    }
}

BLE_API void ble_ft_on_data(const uint8_t *data, uint16_t len)
{
    uint16_t seq;
    uint16_t payload;
    if (bft.state != FT_RECEIVING)
        return;
    if (len < 3u)
        return;
    seq = ble_rd16(data);
    payload = (uint16_t)(len - 2u);
    if (seq != bft.next_seq) {
        uint8_t e = FT_ERR_SEQ;
        ft_queue_rsp(FT_RSP_ERROR, &e, 1);
        bft.state = FT_ERROR;
        return;
    }
    bft.received += payload;
    bft.next_seq++;
    bft.chunks_since_ack++;
    if (bft.received >= bft.total_size) {
        bft.state = FT_DONE;
        uint8_t d[1] = {1};
        ft_queue_rsp(FT_RSP_DONE, d, 1);
        return;
    }
    if (bft.chunks_since_ack >= FT_WINDOW) {
        bft.chunks_since_ack = 0;
        ft_ack();
    }
}

BLE_API void ble_ft_ccc_set(uint8_t val)
{
    bft.ccc_ft = val & 1u;
}

BLE_API uint8_t ble_ft_ccc_get(void)
{
    return bft.ccc_ft;
}

BLE_API void ble_ft_event(void)
{
    /* defined externally by ble_att_ft.c: uses batt.buf and att_send */
}
