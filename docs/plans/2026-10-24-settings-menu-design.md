# Settings Menu System Design

Tabbed settings menu for core-fm1, accessed via long-HOME. Adapter-driven:
the menu data layer is UI-agnostic, each UI lineage (Felucca, Melodee)
renders it in its own visual style. Items are static via Kconfig; persistence
is backend-swappable.

## Motivation

The USB OTG dual-role FSM needs a user-configurable USB mode (DEVICE / HOST)
to prevent OTG from interfering with MIDI SysEx on iPhone. This is the first
setting; the framework must support future settings (MIDI channel, BLE on/off,
brightness, etc.) without per-setting UI work.

## Architecture overview

```
+------------------+     +---------------------+     +--------------------+
| cfm1_sys_menu.h  |---->| page_sys.c          |     | cfm1_settings.h   |
| (adapter: items, |     | (Felucca renderer)  |     | (adapter: load/   |
|  tabs, get/set)  |     |                     |     |  save contract)   |
+------------------+     +---------------------+     +--------------------+
        |                         |                           |
   #ifdef CONFIG_*           reads adapter              Kconfig radio:
   guards per item           renders tabs+rows          FMLAB / FELUCCA / NONE
        |                         |                           |
+------------------+     +---------------------+     +--------------------+
| sys_settings_t   |     | Future: page_menu.c |     | FM Lab impl        |
| (flat struct,    |     | (Melodee renderer)  |     | (crc32, 1 sector)  |
|  stored as blob) |     +---------------------+     +--------------------+
+------------------+
```

Three layers:
1. **Settings data** (`sys_settings_t`): flat struct, one field per setting
2. **Menu adapter** (`cfm1_sys_menu.h`): item descriptors with get/set, tabs
3. **UI page** (`page_sys.c`): Felucca-specific rendering of the adapter data

## Layer 1: Settings data and persistence

### `cfm1_settings.h` (MIT adapter)

Defines the persistence contract:

```c
static int  settings_load(void *p, uint32_t n);  /* 0: loaded, -1: defaults */
static int  settings_save(const void *p, uint32_t n);
```

Selected by Kconfig radio choice:

```c
#if defined(CONFIG_SETTINGS_FMLAB)
#include "settings/settings_fmlab.c"
#elif defined(CONFIG_SETTINGS_NONE)
/* stubs: load always returns -1, save is a no-op */
static int  settings_load(void *p, uint32_t n) { (void)p; (void)n; return -1; }
static int  settings_save(const void *p, uint32_t n) { (void)p; (void)n; return 0; }
#endif
```

### `settings_fmlab.c` (GPL-3.0, from FM Lab)

Extracted from `firmware/upstream/fmlab/firmware/src/platform.c`. ~40 lines:

- `crc32(p, n)`: standard CRC-32/ISO-HDLC
- `settings_load(p, n)`: reads `[magic][length][crc32][data]` from one 4 KiB
  sector. A shorter stored length loads what it has into the first bytes of `p`,
  leaving the rest at caller defaults. This makes append-only growth free.
- `settings_save(p, n)`: erases sector, writes data then header (header last
  so a power cut during write leaves the sector invalid, not half-written).
  Max 256 bytes.

Flash address `SETTINGS_AT` from Kconfig (default 0xFC000, same as all
upstream firmwares).

Deferred save: `settings_save` is never called from the audio ISR or while
audio is rendering. The UI sets a `sys_settings_dirty` flag; the main loop
calls `sys_settings_flush()` when the audio core is idle.

### `sys_settings_t`

```c
typedef struct {
#ifdef CONFIG_SYS_MENU_USB_MODE
    uint8_t usb_mode;       /* 0 = DEVICE, 1 = HOST */
#endif
#ifdef CONFIG_SYS_MENU_MIDI_CH
    uint8_t midi_ch;        /* 0..15 */
#endif
#ifdef CONFIG_SYS_MENU_BLE
    uint8_t ble_on;         /* 0 = OFF, 1 = ON */
#endif
    /* Append new fields here. Never reorder or remove. */
} sys_settings_t;
```

At boot: `sys_settings_defaults(&sys_settings)` fills all fields, then
`settings_load(&sys_settings, sizeof sys_settings)` overwrites with stored
values (partial load is fine per FM Lab's append-only contract).

## Layer 2: Menu adapter (`cfm1_sys_menu.h`)

MIT-licensed. Defines the item array and tab structure. UI-agnostic: any
renderer can iterate it.

### Tabs

Match upstream Felucca's tab order exactly:

```c
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
```

### Item descriptor

```c
typedef struct {
    const char *name;            /* row label: "USB MODE", "MIDI CH" */
    uint8_t     tab;             /* SMTAB_* */
    uint8_t     n_values;        /* number of discrete choices */
    const char *const *values;   /* display strings per choice index */
    uint8_t   (*get)(void);      /* read current value index */
    void      (*set)(uint8_t v); /* write new value index, apply to subsystem */
} sys_menu_item_t;
```

### Item array (static, Kconfig-gated)

```c
static const char *const ONOFF_V[]    = {"OFF", "ON"};
static const char *const USB_MODE_V[] = {"DEVICE", "HOST"};
static const char *const MIDI_CH_V[]  = {
    "1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16"
};

/* get/set read/write sys_settings fields, set also pokes the subsystem */
#ifdef CONFIG_SYS_MENU_USB_MODE
static uint8_t usb_mode_get(void) { return sys_settings.usb_mode; }
static void    usb_mode_set(uint8_t v) {
    sys_settings.usb_mode = v;
    /* apply: poke OTG FSM */
    sys_settings_dirty = 1;
}
#endif

static const sys_menu_item_t SYS_MENU[] = {
#ifdef CONFIG_SYS_MENU_USB_MODE
    { "USB MODE",   SMTAB_SYSTEM, 2,  USB_MODE_V, usb_mode_get, usb_mode_set },
#endif
#ifdef CONFIG_SYS_MENU_MIDI_CH
    { "MIDI CH",    SMTAB_MIDI,  16,  MIDI_CH_V,  midi_ch_get,  midi_ch_set },
#endif
#ifdef CONFIG_SYS_MENU_BLE
    { "BLUETOOTH",  SMTAB_BLE,    2,  ONOFF_V,    ble_on_get,   ble_on_set },
#endif
};
#define SYS_MENU_COUNT (sizeof SYS_MENU / sizeof SYS_MENU[0])
```

### TODO: Dynamic item registration (future)

Subsystems loaded at runtime (plugins, external modules) could register
items via `sys_menu_register(item)`. This would require:

```c
/* Placeholder for future dynamic registration:
 *
 * #define SYS_MENU_DYN_MAX 16
 * static sys_menu_item_t sys_menu_dyn[SYS_MENU_DYN_MAX];
 * static uint32_t sys_menu_dyn_count;
 *
 * static void sys_menu_register(const sys_menu_item_t *item) {
 *     if (sys_menu_dyn_count < SYS_MENU_DYN_MAX)
 *         sys_menu_dyn[sys_menu_dyn_count++] = *item;
 * }
 *
 * UI iteration would walk SYS_MENU[] first, then sys_menu_dyn[].
 * Dynamic items could also register new tabs via a similar mechanism.
 * Not implemented: all items are currently static via Kconfig.
 */
```

## Layer 3: Felucca settings page (`page_sys.c`)

Felucca-specific renderer. Reads from `cfm1_sys_menu.h`, draws in Felucca's
visual language. Registered as `FPAGE_SYS` in `pages.c`.

### Entry

Long-press HOME opens the page. In `cfm1_controls.h`, Felucca's map changes:

```c
.btn_hold = {
    [BTN_HOME] = "SYS",   /* was "ABOUT" */
},
```

HOME tap from SYS page returns to previous page (button handler navigates
back to the page that was active before SYS).

### Layout

Standard Felucca page structure:

```
+--------------------------------------+  0
|  fui_draw_head() - status bar        | 24
+--------------------------------------+ 28
|  Tab bar: [DISPLAY] CONTROL AUDIO ...|
|  (highlighted current tab)           | ~48
+--------------------------------------+
|  Rows for current tab:               |
|    USB MODE .................. DEVICE |  <- T_DIM label, T_THEME value
|  > MIDI CH ...................... 1   |  <- selected: T_SURF bg
|    BLUETOOTH ................... OFF  |
|                                       |
+--------------------------------------+ 202
|  fui_draw_foot()                     | 240
+--------------------------------------+
```

- Tab bar at top of graph area: tab names drawn horizontally, current tab
  highlighted with `T_THEME`, others `T_DIM`
- Rows: label left-aligned (`cv_text_fit`, `T_DIM`), value right-aligned
  (`cv_text_r`, `T_THEME`). Selected row has `T_SURF` background via
  `cv_rrect`. ~6-8 rows visible (ROW_H 18-20px).

### Navigation

Matches upstream Felucca `ui_menu.c` encoder roles:

| Encoder | Action |
|---------|--------|
| `ENC_ALGO` | Switch tabs (left/right through SMTAB_*) |
| `ENC_PRESET` | Scroll rows within current tab |
| `ENC_SELECT` | Change value of selected row (cycle through values[]) |
| `BTN_HOME` tap | Return to previous page |

### State

```c
static struct {
    uint8_t tab;        /* current SMTAB_* */
    uint8_t row;        /* selected row within filtered items */
    uint8_t prev_page;  /* page index to return to on HOME tap */
} fsys;
```

### Draw logic (pseudocode)

```c
static void fsys_draw(void) {
    fui_draw_head();

    /* tab bar */
    for (t = 0; t < SMTAB_COUNT; t++)
        draw tab name, highlighted if t == fsys.tab

    /* filter items for current tab */
    n = 0;
    for (i = 0; i < SYS_MENU_COUNT; i++)
        if (SYS_MENU[i].tab == fsys.tab)
            visible[n++] = &SYS_MENU[i];

    /* draw visible rows */
    for (r = 0; r < n && r < MAX_VISIBLE; r++) {
        selected = (r == fsys.row);
        if (selected) cv_rrect(... T_SURF ...);
        cv_text_fit(x, y, label, T_DIM);
        cv_text_r(xr, y, values[get()], T_THEME);
    }

    fui_draw_foot("core-fm1", CONFIG_FIRMWARE_VERSION, "SYSTEM");
}
```

## Kconfig

New file `kconf/sys_menu.kconfig`, sourced from root `Kconfig`:

```kconfig
menu "Settings Menu"

config SYS_MENU
    bool "Settings menu (long-HOME)"
    default y
    depends on UI
    help
      Tabbed settings menu accessible via long-press HOME.
      Items are populated by enabled subsystems below.

# --- Persistence backend ---

choice
    prompt "Settings persistence backend"
    depends on SYS_MENU
    default SETTINGS_FMLAB
    help
      How user settings (USB mode, MIDI channel, etc.) are stored
      across power cycles. Each backend uses a different flash layout.

config SETTINGS_FMLAB
    bool "Single-sector CRC32 (FM Lab)"
    help
      One 4 KiB flash sector at SETTINGS_AT. Header: [magic][length][CRC32],
      then the settings bytes. Append-only growth: a shorter record from an
      older firmware loads what it has, new fields keep their defaults.
      Erase takes ~50 ms with interrupts off; save is deferred while audio
      plays. Ported from FM Lab (platform.c).

config SETTINGS_FELUCCA
    bool "A/B sector with st_save (Felucca/SLOOP) [not implemented]"
    depends on STORAGE_FELUCCA
    help
      Dual-sector alternating write via st_save(OBJ_SETTINGS). Power-cut
      safe: one sector is always valid. Used by upstream Felucca, SLOOP,
      Melodee, and Optimist. Requires the Felucca storage driver.
      NOT YET IMPLEMENTED: selecting this will fail to build.

config SETTINGS_NONE
    bool "RAM only (no persistence)"
    help
      Settings live in RAM and reset to defaults on every power cycle.
      Useful for development and testing without flash wear.

endchoice

config SETTINGS_AT
    hex "Flash address for settings sector"
    default 0xFC000
    depends on SETTINGS_FMLAB
    help
      Start address of the 4 KiB flash sector used for settings storage.
      Default 0xFC000 matches all upstream firmwares.

# --- Individual menu items ---
# Each depends on SYS_MENU + its subsystem.

config SYS_MENU_USB_MODE
    bool "USB mode setting (DEVICE / HOST)"
    default y
    depends on SYS_MENU && USB && USB_HOST
    help
      Lets the user lock USB role to DEVICE (MIDI) or HOST (flash drive).
      Without USB_HOST, device mode is the only option: no setting needed.

config SYS_MENU_MIDI_CH
    bool "MIDI channel setting"
    default y
    depends on SYS_MENU && MIDI
    help
      Select the MIDI receive channel (1-16) from the settings menu.

config SYS_MENU_BLE
    bool "Bluetooth on/off setting"
    default y
    depends on SYS_MENU && BLE
    help
      Enable or disable BLE radio from the settings menu.

endmenu
```

Dependency chain:
```
UI ──> SYS_MENU ──> SYS_MENU_USB_MODE (also requires USB + USB_HOST)
                ──> SYS_MENU_MIDI_CH   (also requires MIDI)
                ──> SYS_MENU_BLE       (also requires BLE)
                ──> SETTINGS_FMLAB | SETTINGS_FELUCCA | SETTINGS_NONE
```

## File plan

| File | License | Layer | New/Modified |
|------|---------|-------|-------------|
| `kconf/sys_menu.kconfig` | GPL-3.0 | Config | New |
| `Kconfig` | GPL-3.0 | Config | Modified (source sys_menu.kconfig) |
| `firmware/adapters/cfm1_settings.h` | MIT | Persistence adapter | New |
| `firmware/src/settings/settings_fmlab.c` | GPL-3.0 | Persistence impl | New (extracted from FM Lab) |
| `firmware/adapters/cfm1_sys_menu.h` | MIT | Menu adapter | New |
| `firmware/src/ui/felucca/page_sys.c` | GPL-3.0 | UI renderer | New |
| `firmware/src/ui/felucca/pages.c` | GPL-3.0 | Page registration | Modified (add FPAGE_SYS) |
| `firmware/adapters/cfm1_controls.h` | MIT | Button map | Modified (BTN_HOME hold -> "SYS") |
| `firmware/adapters/cfm1_otg.h` | MIT | OTG FSM | Modified (read sys_settings.usb_mode) |
| `firmware/src/core_fm1.c` | GPL-3.0 | Unity root | Modified (include settings, init) |
| `tests/test_sys_menu.py` | GPL-3.0 | Tests | New |
| `configs/*_defconfig` | GPL-3.0 | Defconfigs | Modified (add SYS_MENU defaults) |

## OTG integration

With the USB MODE setting in place, `cfm1_otg.h` changes from VBUS-only
gating to a settings-driven approach:

```c
static void otg_poll(uint32_t now_ms)
{
    if (otg.mode == OTG_DEVICE) {
#ifdef CONFIG_SYS_MENU_USB_MODE
        if (sys_settings.usb_mode == 0) {
            /* User chose DEVICE: never switch to host */
            otg.debounce_ms = 0;
            return;
        }
#endif
        /* ... existing host-switch logic (with VBUS guard) ... */
    }
    /* ... */
}
```

When `USB MODE = DEVICE`: OTG never switches to host. MIDI SysEx works on
iPhone unconditionally.
When `USB MODE = HOST`: OTG switches to host mode for flash drive access.
The VBUS guard remains as a safety net within host-switch logic.

## Testing

- `test_sys_menu.py`: verify item filtering by tab, get/set round-trip,
  Kconfig guard coverage (items only present when their CONFIG is set)
- `test_otg_fsm.py`: add tests for USB MODE setting overriding OTG behavior
- E2E: navigate to SYS page via long-HOME, change USB MODE, verify LCD
