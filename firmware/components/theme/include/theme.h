// Theme: colours, gauge style and bar fill mode. Pure data plus a few colour
// helpers; the ui component turns it into drawing.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "channels.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { STYLE_RING = 0, STYLE_DIAL, STYLE_NUMERIC, STYLE_STOCK, STYLE_STOCK2, STYLE_COUNT } gauge_style_t;
typedef enum { FILL_GRADIENT = 0, FILL_SEGMENTS, FILL_SOLID, FILL_COUNT } bar_fill_t;

// Colour slots, in the order the config file and the menu list them.
typedef enum {
    TC_BG = 0, TC_DIGITS, TC_LABEL, TC_UNITS, TC_TRACK, TC_TICK,
    TC_NEUTRAL, TC_GOOD, TC_WARN, TC_BAD, TC_COLD, TC_STALE,
    TC_COUNT
} theme_color_t;

// Face elements the user can switch off and move. Scale lives in the *_scale
// fields below (digits, label, units, minmax, bar); dy is a vertical offset
// as a fraction of the cell radius, positive = down.
typedef enum { EL_DIGITS = 0, EL_LABEL, EL_UNITS, EL_MINMAX, EL_TICKS, EL_BAR, EL_NEEDLE, EL_WINDOW, EL_COUNT } theme_el_t;

typedef struct {
    float maj_w, maj_len, min_w, min_len;   // tick width/length multipliers, 1.0 = style default
    uint8_t maj_n, min_n;                   // major count over the range / minors per major; 0 = style default
} theme_ticks_t;

typedef struct {
    uint32_t      rgb[TC_COUNT];   // 0xRRGGBB
    gauge_style_t style;
    bar_fill_t    fill;
    bool          bands;           // faint zone bands on the track
    float         digit_scale;     // 0.6..1.4
    float         label_scale;     // 0.6..2.5
    float         cell_scale;      // 1.0..1.8, radius of the outer cells in multi-cell layouts
    float         units_scale;     // 0.6..2.5
    float         minmax_scale;    // 0.6..2.5
    float         bar_scale;       // 0.5..2.5, ring and segment thickness
    int8_t        preset;          // index into theme_preset_name(), -1 = custom
    bool          el_on[EL_COUNT];
    float         el_dy[EL_COUNT]; // -1..1, fraction of R
    theme_ticks_t ticks;
    float         needle_w;        // base width multiplier
    float         needle_len;      // length multiplier
    float         hub_scale;       // dial/stock centre disc size multiplier
    float         sweep_rot;       // degrees added to the sweep start, -90..90
} theme_t;

void         theme_defaults_extra(void);           // elements on, offsets 0, ticks and needle at 1.0
const char  *theme_el_id(theme_el_t e);            // json key: digits, label, ...

extern theme_t g_theme;

int          theme_preset_count(void);
const char  *theme_preset_name(int i);
void         theme_apply_preset(int i);
int          theme_preset_find(const char *name);   // built-in preset index by name, -1 if none
const char  *theme_color_name(theme_color_t c);
const char  *theme_style_name(gauge_style_t s);
const char  *theme_fill_name(bar_fill_t f);

lv_color_t   theme_color(theme_color_t c);
lv_color_t   theme_zone_color(ch_zone_level_t level);
// Colour of the live bar/needle for a channel at `value` given the fill mode.
lv_color_t   theme_bar_color(const ch_def_t *d, float value, bool fresh);
// Colour of the gradient at fraction f of the range (used by segments too).
lv_color_t   theme_color_at_frac(const ch_def_t *d, float f);

#ifdef __cplusplus
}
#endif
