/* SPDX-License-Identifier: MIT */
/* Debug log adapter: fixed-size ring buffer for diagnostic messages.
 * Any subsystem can call cfm1_log() to push a line; the debug UI page
 * reads the ring to display recent messages on the LCD. */
#ifndef CFM1_LOG_H
#define CFM1_LOG_H
#include <stdint.h>

#define CFM1_LOG_LINES    32
#define CFM1_LOG_LINE_LEN 40

static char     cfm1_log_buf[CFM1_LOG_LINES][CFM1_LOG_LINE_LEN];
static uint32_t cfm1_log_ts[CFM1_LOG_LINES];
static uint32_t cfm1_log_head;
static uint32_t cfm1_log_count;
static uint32_t cfm1_log_scroll;
static volatile uint32_t cfm1_log_ms;

static void cfm1_log_init(void)
{
    uint32_t i, j;
    for (i = 0; i < CFM1_LOG_LINES; i++) {
        for (j = 0; j < CFM1_LOG_LINE_LEN; j++)
            cfm1_log_buf[i][j] = 0;
        cfm1_log_ts[i] = 0;
    }
    cfm1_log_head = 0;
    cfm1_log_count = 0;
    cfm1_log_scroll = 0;
    cfm1_log_ms = 0;
}

static uint32_t cfm1_log_oldest_index(void)
{
    if (cfm1_log_count < CFM1_LOG_LINES)
        return 0;
    return cfm1_log_head;
}

static const char *cfm1_log_line(uint32_t idx)
{
    uint32_t base = cfm1_log_oldest_index();
    return cfm1_log_buf[(base + idx) % CFM1_LOG_LINES];
}

static uint32_t cfm1_log_time(uint32_t idx)
{
    uint32_t base = cfm1_log_oldest_index();
    return cfm1_log_ts[(base + idx) % CFM1_LOG_LINES];
}

#ifdef CONFIG_DEBUG_LOG

static void cfm1_log(const char *msg)
{
    uint32_t slot = cfm1_log_head % CFM1_LOG_LINES;
    char *dst = cfm1_log_buf[slot];
    uint32_t i;
    for (i = 0; i < CFM1_LOG_LINE_LEN - 1 && msg[i]; i++)
        dst[i] = msg[i];
    dst[i] = 0;
    cfm1_log_ts[slot] = cfm1_log_ms;
    cfm1_log_head = (cfm1_log_head + 1) % CFM1_LOG_LINES;
    if (cfm1_log_count < CFM1_LOG_LINES)
        cfm1_log_count++;
}

static void cfm1_log_int(const char *prefix, int32_t val)
{
    char line[CFM1_LOG_LINE_LEN];
    uint32_t i = 0;
    while (i < CFM1_LOG_LINE_LEN - 12 && prefix[i]) {
        line[i] = prefix[i];
        i++;
    }
    if (val < 0) {
        line[i++] = '-';
        val = -val;
    }
    {
        char tmp[12];
        uint32_t j = 0;
        do {
            tmp[j++] = '0' + (char)(val % 10);
            val /= 10;
        } while (val && j < 11);
        while (j) line[i++] = tmp[--j];
    }
    line[i] = 0;
    cfm1_log(line);
}

static void cfm1_log_hex(const char *prefix, const uint8_t *data, uint32_t len)
{
    char line[CFM1_LOG_LINE_LEN];
    uint32_t i = 0, j;
    static const char hex[] = "0123456789ABCDEF";
    while (i < CFM1_LOG_LINE_LEN - 4 && prefix[i]) {
        line[i] = prefix[i];
        i++;
    }
    for (j = 0; j < len && i + 3 < CFM1_LOG_LINE_LEN; j++) {
        line[i++] = hex[data[j] >> 4];
        line[i++] = hex[data[j] & 0x0F];
        if (j + 1 < len && i < CFM1_LOG_LINE_LEN - 1)
            line[i++] = ' ';
    }
    line[i] = 0;
    cfm1_log(line);
}

#else /* !CONFIG_DEBUG_LOG */

static void cfm1_log(const char *msg) { (void)msg; }
static void cfm1_log_int(const char *prefix, int32_t val)
{
    (void)prefix; (void)val;
}
static void cfm1_log_hex(const char *prefix, const uint8_t *data, uint32_t len)
{
    (void)prefix; (void)data; (void)len;
}

#endif /* CONFIG_DEBUG_LOG */

#endif /* CFM1_LOG_H */
