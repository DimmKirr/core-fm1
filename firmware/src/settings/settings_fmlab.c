/* SPDX-License-Identifier: GPL-3.0-only
 * Extracted from FM Lab platform.c (Dinghy).
 * Single 4 KiB flash sector: [magic][length][crc32] then the bytes.
 * Append-only: a shorter store is an older build, load what it has. */

#define SETTINGS_MAGIC  0x53455401u   /* "SET\x01" */

static uint32_t settings_crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}

static int settings_fl_read(uint32_t off, void *dst, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    while (n) {
        uint32_t k = n > 256u ? 256u : n;
        uint32_t f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc) return rc;
        off += k; d += k; n -= k;
    }
    return 0;
}

static int sys_fl_load(void *p, uint32_t n)
{
    static uint8_t buf[256];
    uint32_t h[3];
    if (n > sizeof buf)
        return -1;
    if (settings_fl_read(CONFIG_SETTINGS_AT, h, sizeof h))
        return -1;
    if (h[0] != SETTINGS_MAGIC)
        return -1;
    if (h[1] == 0u || h[1] > n)
        return -1;
    if (settings_fl_read(CONFIG_SETTINGS_AT + sizeof h, buf, h[1]))
        return -1;
    if (settings_crc32(buf, h[1]) != h[2])
        return -1;
    memcpy(p, buf, h[1]);
    return 0;
}

static int sys_fl_save(const void *p, uint32_t n)
{
    uint32_t h[3] = {SETTINGS_MAGIC, n, 0}, took;
    int rc;
    if (n > 256u)
        return -1;
    h[2] = settings_crc32((const uint8_t *)p, n);
    if ((rc = fl_erase4k(CONFIG_SETTINGS_AT, &took)) != 0)
        return rc;
    if ((rc = fl_write(CONFIG_SETTINGS_AT + sizeof h, (const uint8_t *)p, n)) != 0)
        return rc;
    return fl_write(CONFIG_SETTINGS_AT, (const uint8_t *)h, sizeof h);
}
