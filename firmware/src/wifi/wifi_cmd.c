/* SPDX-License-Identifier: GPL-3.0-only */
/* Cross-task command queue: UI -> WiFi.
 * Commands are copied (not pointer-based) to avoid cross-yield aliasing. */

#ifdef CONFIG_SCHED

#define WIFI_CMD_QUEUE_SIZE 4

typedef enum {
    WIFI_CMD_NONE = 0,
    WIFI_CMD_CONNECT,
    WIFI_CMD_DISCONNECT,
    WIFI_CMD_SCAN,
} wifi_cmd_type_t;

typedef struct {
    wifi_cmd_type_t type;
    char ssid[WIFI_STA_SSID_MAX + 1];
    char password[64];
    void (*scan_cb)(const char *ssid, int rssi, int channel);
} wifi_cmd_t;

static struct {
    wifi_cmd_t cmds[WIFI_CMD_QUEUE_SIZE];
    uint32_t head;
    uint32_t tail;
} wifi_cmd_q;

static int wifi_cmd_enqueue(const wifi_cmd_t *cmd)
{
    uint32_t next = (wifi_cmd_q.head + 1u) % WIFI_CMD_QUEUE_SIZE;
    if (next == wifi_cmd_q.tail)
        return -1;
    wifi_cmd_q.cmds[wifi_cmd_q.head] = *cmd;
    wifi_cmd_q.head = next;
    return 0;
}

static int wifi_cmd_dequeue(wifi_cmd_t *out)
{
    if (wifi_cmd_q.tail == wifi_cmd_q.head)
        return -1;
    *out = wifi_cmd_q.cmds[wifi_cmd_q.tail];
    wifi_cmd_q.tail = (wifi_cmd_q.tail + 1u) % WIFI_CMD_QUEUE_SIZE;
    return 0;
}

static void wifi_cmd_poll(void)
{
    wifi_cmd_t cmd;
    while (wifi_cmd_dequeue(&cmd) == 0) {
        switch (cmd.type) {
        case WIFI_CMD_CONNECT:
            wifi_connect(cmd.ssid, cmd.password);
            break;
        case WIFI_CMD_DISCONNECT:
            wifi_disconnect();
            break;
        case WIFI_CMD_SCAN:
            wifi_scan(cmd.scan_cb);
            break;
        default:
            break;
        }
    }
}

#endif /* CONFIG_SCHED */
