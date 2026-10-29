# UI Architecture

core-fm1 has a cross-theme UI framework that separates navigation, content, and rendering. The framework defines three layers:

```
View ──button──> Page ──encoder──> Element
```

- **View**: a button-addressable group of related pages (e.g. the FX View opens when you press FX)
- **Page**: an individual screen within a view (e.g. Filter, Delay, Reverb pages within the FX View)
- **Element**: a rendering primitive used to build page content (e.g. header, grid, list, cards)

Each shell (Felucca, Melodee, SLOOP) provides a theme that renders elements with its own visual language. The navigation model and page structure are universal.

For physical controls (buttons, encoders), event flow, and current page card layouts, see [COMPONENTS.md](COMPONENTS.md#ui).

## Upstream Navigation Models

All three community firmwares use the same core pattern. They call views "families" (`fam` / `FAM_*` in source). This framework formalizes and renames the concept to "view."

| Property | Felucca | Melodee | SLOOP |
|---|---|---|---|
| Model | View + button | View + button | View + button |
| Views | 11 (HOME..TRK) | 11 (same IDs) | 11 (same IDs) |
| Button tap | Opens view; tap again cycles pages | Same | Same |
| Encoder (SELECT) | Cycles pages within current view | Same | Same |
| Cross-view nav | Buttons only, never encoder | Same | Same |
| System settings | Modal overlay (HOME hold) | Same | Same (core-fm1: Settings View) |
| Overlays | Menu, quick-layers | Menu, confirm, action sheets | Menu |

No upstream uses a flat encoder-scroll page list. Button selects view, encoder walks pages within that view.

## Framework Structure

```
firmware/src/ui/
  ui_view.h              # VIEW_* constants
  ui_theme.h             # ui_theme_t struct (metrics, fonts, renderers)
  ui_nav.c               # view navigation: open, cycle, scroll, memory

  themes/
    felucca/             # Felucca shell (active)
      ui.c               # unity root: includes gfx, draw, theme, shell, pages
      theme.c            # ui_theme_t instance: metrics, palette, fonts, renderers
      shell.c            # input routing, button/encoder dispatch
      draw.c             # Felucca-specific drawing helpers (header, cards, footer)
      gfx.c              # shim to upstream canvas renderer
      pages.c            # page registration with view assignments
      page_home.c        # page implementations
      page_params.c
      page_weather.c
      ...

    melodee/             # (future)
    sloop/               # (future)

  core/                  # minimal fallback shell (bitmap font, no theming)
```

The split:
- **Universal** (`ui_*.h`, `ui_*.c`): view model, navigation rules, element API, toast queue. Shared by all shells.
- **Shell-specific** (`felucca/`, `melodee/`, etc.): theme rendering, input routing, page implementations.

## Views

A view groups related pages under one physical button. Pressing a button opens the view. The encoder cycles pages within it.

| ID | Button | Pages | Notes |
|---|---|---|---|
| `VIEW_HOME` | HOME tap | Home | HOME tap from any view returns here |
| `VIEW_ENV` | ENV | Envelope, Env Dest | |
| `VIEW_LFO` | LFO | LFO, LFO Dest | |
| `VIEW_FX` | FX | FX, Filter, Delay, Reverb | Page count varies by effects config |
| `VIEW_SCL` | SCL | Scale Select | |
| `VIEW_EDIT` | EDIT | Engine Edit | Per-engine pages |
| `VIEW_GLO` | GLO tap | Mixer, Voice, MIDI | Global synth settings |
| `VIEW_SAVE` | SAVE | Presets, Files | Files when `CONFIG_UDISK` |
| `VIEW_ARP` | ARP | Arpeggiator | |
| `VIEW_SEQ` | SEQ | Sequencer, Pattern | |
| `VIEW_CONN` | GLO hold | WiFi, Weather, BLE | core-fm1 connectivity features |
| `VIEW_SETTINGS` | HOME hold | Display, Audio, System, About, Debug | Settings and device info |

### View Memory

Each view remembers its last active page (`view_last[VIEW_COUNT]`). Returning to a view resumes where you left off. Reset on power cycle.

### Navigation Rules

1. **Button tap**: if not in that view, open it (last-visited page). If already in the view, cycle to next page.
2. **Encoder SELECT**: prev/next page within current view. Wraps at boundaries. Never crosses views.
3. **HOME tap**: always returns to VIEW_HOME regardless of current view.
4. **HOME hold**: opens VIEW_SETTINGS (display, audio, system, about, debug).
5. **GLO hold**: opens VIEW_CONN (WiFi/Weather/BLE).

### Why "view"

Upstream firmwares use "family" (`FAM_*`) for this concept. We rename to "view" because it composes naturally: "the FX View has 4 pages." Alternatives considered:

| Term | Why not |
|---|---|
| Family (`fam`) | Upstream convention but doesn't compose: "page family" is ambiguous |
| Section | Overloaded (ELF sections, doc sections, menu sections) |
| Mode | Overloaded (USB mode, play mode) |
| Group | Collides with Kconfig menu groups |
| Tab | Implies a visual tab bar (misleading for button nav) |

## Settings View

Upstream firmwares implement settings as a modal overlay (HOME hold draws on top of the current page). core-fm1 promotes settings to a regular view (`VIEW_SETTINGS`) for consistency: every button action opens a view, no special cases. The Settings View contains pages like any other view. See the [page catalog](#view_settings-home-hold) for its pages.

A shell may choose to render VIEW_SETTINGS as an overlay (matching upstream visual style) while still using the view navigation model internally.

## Page Definition

Extends the existing `cfm1_page_t` (defined in `firmware/adapters/cfm1_page.h`) with a `view` field and optional `visible` callback:

```c
typedef struct cfm1_page {
    const char *name;           /* short ID: "HOME", "ENV", "FX_DLY" */
    const char *title;          /* display title for header */
    uint8_t     view;           /* VIEW_* */
    uint32_t    flags;          /* PAGEF_* */

    int  (*visible)(void);      /* optional: return 0 to skip in cycle */
    void (*enter)(void);        /* page became active */
    void (*leave)(void);        /* page lost focus */
    void (*draw)(void);         /* redraw content using elements */
    void (*status)(void);       /* optional: status bar contribution */

    int (*knob)(uint32_t id, int32_t clicks);   /* return 1 if handled */
    int (*button)(uint32_t id, uint32_t down);
    int (*key)(uint32_t note, uint32_t vel);
} cfm1_page_t;
```

Pages return 1 from knob/button/key if handled, 0 to let the shell handle navigation.

### Page Visibility

Pages can hide dynamically. Examples: FM6 operator pages hidden when engine is Analog. BLE page hidden without `CONFIG_BLE`. Weather hidden when WiFi not connected.

## Element System

Pages are built from universal elements. The element API is theme-agnostic: it describes *what* to draw, not *how*. Each shell's theme implements the rendering.

### Elements

| Element | Description | Used by |
|---|---|---|
| `header` | Title bar with page name, view indicator, status icons | Every page |
| `footer` | Bottom bar with button-action hint labels | Every page |
| `cards` | Row of up to 4 value cards (label + value + optional icon) | Home, WiFi, Weather, Mixer |
| `list` | Scrollable item list with selection, optional icons and secondary text | WiFi scan, Files, Presets, Scales |
| `grid` | Parameter display: label + value + bar, mapped to encoders K1-K4 | ENV, LFO, FX, Edit, ARP, Voice |
| `graph` | Visualization area: waveform, envelope shape, spectrum, level meter | ENV, LFO, Home |
| `text` | Styled text block (monospace or proportional, scrollable) | About, Debug log |
| `modal` | Overlay dialog: title, body, options with selection | Confirm, Error |
| `toast` | Temporary status message, auto-dismiss after timeout | Save OK, Connected |

### Element API

```c
void ui_header(const char *title, const char *right_text);
void ui_footer(const char *left, const char *center, const char *right);

void ui_card(uint8_t slot, const char *label, const char *value,
             uint16_t color, int8_t icon);
void ui_cards_flush(void);

void ui_list_begin(uint8_t visible_rows);
void ui_list_item(uint8_t idx, const char *text, const char *right,
                  uint8_t selected, uint8_t icon);
void ui_list_end(void);

void ui_grid_param(uint8_t slot, const char *label, const char *value,
                   int32_t bar_pct);
void ui_grid_flush(void);

void ui_graph_begin(uint16_t w, uint16_t h);
void ui_graph_line(const int16_t *points, uint16_t count, uint16_t color);
void ui_graph_bar(uint16_t x, uint16_t h, uint16_t color);
void ui_graph_end(void);

void ui_text_block(const char *text, uint16_t color);

void ui_modal_open(const char *title, const char *body);
void ui_modal_option(uint8_t idx, const char *label, uint8_t selected);
void ui_modal_close(void);

void ui_toast(const char *msg, uint16_t duration_ms);
```

### Theme Contract

Each shell registers a theme that implements the visual rendering of elements.

```c
typedef struct ui_theme {
    /* screen metrics */
    uint16_t screen_w, screen_h;
    uint16_t header_h, footer_h;
    uint16_t card_h, card_w;
    uint16_t list_row_h;
    uint16_t grid_row_h;

    /* palette (RGB565) */
    uint16_t bg, text, dim, theme, surface, error;

    /* fonts */
    const void *font_s;
    const void *font_m;
    const void *font_l;

    /* element renderers */
    void (*render_header)(const char *title, const char *right);
    void (*render_footer)(const char *l, const char *c, const char *r);
    void (*render_card)(uint8_t slot, const char *label, const char *value,
                        uint16_t color, int8_t icon);
    void (*render_list_item)(uint8_t row, const char *text, const char *right,
                             uint8_t selected, uint8_t icon);
    void (*render_grid_param)(uint8_t slot, const char *label, const char *value,
                              int32_t bar_pct);
    void (*render_modal)(const char *title, const char *body);
    void (*render_toast)(const char *msg);
} ui_theme_t;
```

## Page Catalog

Grouped by view. For encoder/button mappings and card parameter details, see [COMPONENTS.md](COMPONENTS.md#physical-controls).

### VIEW_HOME (HOME button)

| Page | Elements | Description |
|---|---|---|
| Home | header, cards, graph | Engine name, preset, voice count, waveform |

### VIEW_ENV (ENV button)

| Page | Elements | Description |
|---|---|---|
| Envelope | header, grid, graph, footer | ADSR knobs with envelope visualization |
| Env Dest | header, grid, footer | Envelope destination routing |

### VIEW_LFO (LFO button)

| Page | Elements | Description |
|---|---|---|
| LFO | header, grid, graph, footer | Rate, depth, shape, sync |
| LFO Dest | header, grid, footer | LFO destination routing |

### VIEW_FX (FX button)

| Page | Elements | Description |
|---|---|---|
| FX | header, cards, grid, footer | Effects overview and mix levels |
| Filter | header, grid, graph, footer | Cutoff, resonance, type, envelope |
| Delay | header, grid, footer | Time, feedback, mix, sync |
| Reverb | header, grid, footer | Size, decay, mix, damping |

### VIEW_SCL (SCL button)

| Page | Elements | Description |
|---|---|---|
| Scale | header, list, footer | Scale/tuning selection |

### VIEW_EDIT (EDIT button)

| Page | Elements | Description |
|---|---|---|
| Edit | header, grid, graph, footer | Engine-specific params (pages vary by engine) |

### VIEW_GLO (GLO button)

| Page | Elements | Description |
|---|---|---|
| Mixer | header, grid, footer | Track volumes, pan, mute/solo |
| Voice | header, cards, grid, footer | Polyphony, unison, glide |
| MIDI | header, grid, footer | Channel, velocity curve, CC map |

### VIEW_SAVE (SAVE button)

| Page | Elements | Description |
|---|---|---|
| Presets | header, list, footer | Save/load/rename presets |
| Files | header, list, footer | USB disk browser (`CONFIG_UDISK`) |

### VIEW_ARP (ARP button)

| Page | Elements | Description |
|---|---|---|
| Arp | header, grid, footer | Mode, rate, range, gate |

### VIEW_SEQ (SEQ button)

| Page | Elements | Description |
|---|---|---|
| Sequencer | header, grid, graph, footer | Step sequencer with note grid |
| Pattern | header, list, footer | Pattern select, copy, chain |

### VIEW_CONN (GLO hold)

| Page | Elements | Description |
|---|---|---|
| WiFi | header, cards, list, footer | Scan, connect, IP/status |
| Weather | header, cards, text, footer | Current conditions (when WiFi connected) |
| BLE | header, cards, text, footer | BLE status, MIDI transport (`CONFIG_BLE`) |

### VIEW_SETTINGS (HOME hold)

| Page | Elements | Description |
|---|---|---|
| Display | header, grid, footer | Color theme, font, brightness |
| Audio | header, grid, footer | Lowcut, buffer size |
| System | header, grid, footer | USB mode, MIDI channel |
| About | header, text, footer | Firmware version, MAC, build info |
| Debug | header, text, footer | Log ring viewer (`CONFIG_DEBUG_LOG`) |
