/* SPDX-License-Identifier: MIT */
/* Settings menu adapter: item descriptors and tabs.
 * UI-agnostic: any renderer (Felucca, Melodee, core) can iterate this.
 * Items are static via Kconfig. See design doc for dynamic registration TODO. */
#ifndef CFM1_SYS_MENU_H
#define CFM1_SYS_MENU_H

/* --- Tabs: match upstream Felucca order --- */

enum {
    SMTAB_DISPLAY, SMTAB_CONTROL, SMTAB_AUDIO,
    SMTAB_MIDI, SMTAB_SYSTEM,
#ifdef CONFIG_BLE
    SMTAB_BLE,
#endif
    SMTAB_COUNT
};

static const char *const SMTAB_NAME[SMTAB_COUNT] = {
    "DISPLAY", "CONTROL", "AUDIO", "MIDI", "SYSTEM",
#ifdef CONFIG_BLE
    "BLE",
#endif
};

/* --- Item descriptor --- */

typedef struct {
    const char *name;
    uint8_t     tab;
    uint8_t     n_values;
    const char *const *values;
    uint8_t   (*get)(void);
    void      (*set)(uint8_t v);
} sys_menu_item_t;

/* --- Value name arrays --- */

static const char *const SM_ONOFF[] = {"OFF", "ON"};

#ifdef CONFIG_SYS_MENU_USB_MODE
static const char *const SM_USB_MODE[] = {"MIDI", "OTG"};

static uint8_t sm_usb_mode_get(void) { return sys_settings.usb_mode; }
static void    sm_usb_mode_set(uint8_t v)
{
    if (v > 1u) v = 1u;
    sys_settings.usb_mode = v;
    sys_settings_dirty = 1;
}
#endif

#ifdef CONFIG_SYS_MENU_MIDI_CH
static const char *const SM_MIDI_CH[] = {
    "1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16"
};

static uint8_t sm_midi_ch_get(void) { return sys_settings.midi_ch; }
static void    sm_midi_ch_set(uint8_t v)
{
    if (v > 15u) v = 15u;
    sys_settings.midi_ch = v;
    sys_settings_dirty = 1;
}
#endif

#ifdef CONFIG_SYS_MENU_BLE
static uint8_t sm_ble_on_get(void) { return sys_settings.ble_on; }
static void    sm_ble_on_set(uint8_t v)
{
    if (v > 1u) v = 1u;
    sys_settings.ble_on = v;
    sys_settings_dirty = 1;
}
#endif

/* --- Item array (static, Kconfig-gated) --- */

static const sys_menu_item_t SYS_MENU[] = {
#ifdef CONFIG_SYS_MENU_USB_MODE
    { "USB MODE",   SMTAB_SYSTEM, 2,  SM_USB_MODE,  sm_usb_mode_get, sm_usb_mode_set },
#endif
#ifdef CONFIG_SYS_MENU_MIDI_CH
    { "MIDI CH",    SMTAB_MIDI,  16,  SM_MIDI_CH,   sm_midi_ch_get,  sm_midi_ch_set },
#endif
#ifdef CONFIG_SYS_MENU_BLE
    { "BLUETOOTH",  SMTAB_BLE,    2,  SM_ONOFF,     sm_ble_on_get,   sm_ble_on_set },
#endif
    { 0, 0, 0, 0, 0, 0 }  /* sentinel */
};
#define SYS_MENU_COUNT \
    ((sizeof SYS_MENU / sizeof SYS_MENU[0]) - 1u)  /* exclude sentinel */

/* --- TODO: Dynamic item registration (future) ---
 *
 * Subsystems loaded at runtime (plugins, external modules) could register
 * items via sys_menu_register(item). This would require:
 *
 *   #define SYS_MENU_DYN_MAX 16
 *   static sys_menu_item_t sys_menu_dyn[SYS_MENU_DYN_MAX];
 *   static uint32_t sys_menu_dyn_count;
 *
 *   static void sys_menu_register(const sys_menu_item_t *item) {
 *       if (sys_menu_dyn_count < SYS_MENU_DYN_MAX)
 *           sys_menu_dyn[sys_menu_dyn_count++] = *item;
 *   }
 *
 * UI iteration would walk SYS_MENU[] first, then sys_menu_dyn[].
 * Dynamic items could also bring new tabs (SMTAB_DYN_BASE + n).
 * Not implemented: all items are currently static via Kconfig.
 */

#endif /* CFM1_SYS_MENU_H */
