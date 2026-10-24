/* SPDX-License-Identifier: MIT */
/* OTG dual-role state machine: auto-switch between USB device and host.
 * Requires: cfm1_log.h, fm1_usb.h (device HAL), fm1_usb_host.h (host HAL).
 * Device-mode globals: usb.up, usb.config, usb.frame_same, usb.detached.
 * Host-mode globals: uhost (from fm1_usb_host.h). */
#ifndef CFM1_OTG_H
#define CFM1_OTG_H
#include <stdint.h>

#define OTG_DEVICE  0
#define OTG_HOST    1

#define OTG_FRAME_IDLE_THRESHOLD  100   /* ~1s at 10ms/tick */
#define OTG_DEVICE_DEBOUNCE_MS    500
#define OTG_HOST_REMOVAL_MS      2000
#define OTG_HOST_COOLDOWN_MS     5000   /* wait before retrying host after enum fail */
#define OTG_SETTLE_MS              50

/* MUSB DEVCTL register VBUS bits [4:3]: 11 = above VBusValid (~4.4V).
 * When an external host (iPhone, computer) supplies 5V, this reads true.
 * A passive USB device (flash drive) cannot supply VBUS. */
#define OTG_VBUS_VALID(dc)  (((dc) & 0x18u) == 0x18u)

static int otg_vbus_present(void)
{
    uint32_t dc = usb_read_devctl(MUSB_ID);
    return OTG_VBUS_VALID(dc);
}

typedef struct {
    uint8_t  mode;
    uint32_t debounce_ms;
    uint32_t idle_ms;
    uint32_t cooldown_until;
} otg_state_t;

static otg_state_t otg;

static void otg_init(void)
{
    otg.mode = OTG_DEVICE;
    otg.debounce_ms = 0;
    otg.idle_ms = 0;
}

static void otg_switch_to_host(void)
{
    usb_detach();
    FM1_USB_IO_CON0 &= ~0x40u;   /* D+ pull-up off */
    fm1_delay_ms(OTG_SETTLE_MS);
    usb_host_init();
    cfm1_log("OTG: -> host");
    otg.mode = OTG_HOST;
    otg.debounce_ms = 0;
    otg.idle_ms = 0;
}

static void otg_switch_to_device(void)
{
    usb_host_deinit();
    USB_HOST_IO_CON0 &= ~0x10u;  /* D- pull-down off */
    usb.detached = 0;
    usb.retry_ms = 0;
    usb.config = 0;
    usb.frame_same = 0;
    usb.resets = 0;
    cfm1_log("OTG: -> device");
    otg.mode = OTG_DEVICE;
    otg.debounce_ms = 0;
    otg.idle_ms = 0;
}

static void otg_poll(uint32_t now_ms)
{
    if (otg.mode == OTG_DEVICE) {
        int should_switch;
#ifdef CONFIG_SYS_MENU_USB_MODE
        if (sys_settings.usb_mode == 0) {
            /* User chose MIDI-only: never switch to host */
            should_switch = 0;
        } else
#endif
        if (otg_vbus_present()) {
            /* External host (iPhone, computer) is supplying 5V: stay device */
            should_switch = 0;
        } else if (!usb.up) {
            should_switch = 0;
        } else if (usb.config) {
            /* Was enumerated: detect disconnect via SOF frame freeze */
            should_switch = usb.frame_same >= OTG_FRAME_IDLE_THRESHOLD;
        } else if (usb.resets > 0) {
            /* Host sent bus reset but hasn't configured: enumeration in progress */
            should_switch = 0;
        } else {
            /* SIE alive, no resets, no config: no host present */
            should_switch = 1;
        }
        /* Active config with live SOF: host is still here */
        if (usb.config && usb.frame_same < OTG_FRAME_IDLE_THRESHOLD)
            should_switch = 0;

        if (should_switch && (otg.cooldown_until == 0 || now_ms >= otg.cooldown_until)) {
            if (otg.debounce_ms == 0) {
                otg.debounce_ms = now_ms;
                if (usb.config)
                    cfm1_log("OTG: frames frozen, debouncing");
                else
                    cfm1_log("OTG: no host, debouncing");
            } else if (now_ms - otg.debounce_ms >= OTG_DEVICE_DEBOUNCE_MS)
                otg_switch_to_host();
        } else {
            if (otg.debounce_ms != 0)
                cfm1_log("OTG: debounce cancelled");
            otg.debounce_ms = 0;
            otg.cooldown_until = 0;
        }
    } else if (otg.mode == OTG_HOST) {
        usb_host_poll();
        int failed = uhost.state == UHOST_ERROR;
        int connected = usb_host_connected();
        int idle = !connected || failed;
        if (idle) {
            if (otg.idle_ms == 0) {
                otg.idle_ms = now_ms;
                if (failed)
                    cfm1_log("OTG: host enum failed, waiting");
                else
                    cfm1_log("OTG: device removed, waiting");
            } else if (now_ms - otg.idle_ms >= OTG_HOST_REMOVAL_MS) {
                if (failed) {
                    cfm1_log_int("OTG: cooldown ms", OTG_HOST_COOLDOWN_MS);
                    otg.cooldown_until = now_ms + OTG_HOST_COOLDOWN_MS;
                }
                otg_switch_to_device();
            }
        } else {
            if (otg.idle_ms != 0)
                cfm1_log("OTG: device reconnected");
            otg.idle_ms = 0;
        }
    }
}

#endif /* CFM1_OTG_H */
