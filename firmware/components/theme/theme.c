#include "theme.h"
#include <string.h>

theme_t g_theme;

typedef struct { const char *name; uint32_t rgb[TC_COUNT]; } preset_t;
// Order: bg, digits, label, units, track, tick, neutral, good, warn, bad, cold, stale
static const preset_t PRESETS[] = {
    { "Ice",   { 0x000000, 0xF4F6F8, 0x8A93A0, 0x6B737F, 0x1B1F26, 0x5A6270, 0x38BDF8, 0x34D399, 0xFBBF24, 0xFF3B30, 0x60A5FA, 0x4B5058 } },
    { "Ember", { 0x000000, 0xFFF3E0, 0x9C8A78, 0x75655A, 0x221A14, 0x6A5A4C, 0xFF8A00, 0x9AE66E, 0xFFD166, 0xFF2D55, 0x7FB3FF, 0x4F463F } },
    { "Mono",  { 0x000000, 0xFFFFFF, 0x7A7A7A, 0x5C5C5C, 0x1C1C1C, 0x505050, 0xFFFFFF, 0xCFD6D0, 0xFFD36B, 0xFF5C5C, 0x9CC9FF, 0x454545 } },
    { "Miata", { 0x000000, 0xFFFFFF, 0x8A7F80, 0x6A6162, 0x201617, 0x5E5052, 0xE0202A, 0x35D07F, 0xFFB020, 0xFF2B2B, 0x6CA8FF, 0x4A4042 } },
    { "Stock", { 0xECE7D4, 0x14161A, 0x1E2024, 0x4A4C50, 0xC7CBD0, 0x1A1C20, 0xEA3640, 0x2F9E5B, 0xF2A21C, 0xF0701E, 0x3B7DD8, 0x9A9A90 } },   // factory NB1 cluster: cream face, black type, chrome bezel, red needle, orange redline
};
static const char *COLOR_NAMES[TC_COUNT] = {
    "Background", "Digits", "Labels", "Units", "Track", "Ticks",
    "Neutral", "Good", "Warn", "Bad", "Cold", "Stale",
};
static const char *STYLE_NAMES[STYLE_COUNT] = { "Ring", "Dial", "Numeric", "Stock" };
static const char *FILL_NAMES[FILL_COUNT]   = { "Gradient", "Segments", "Solid" };

int         theme_preset_count(void)        { return sizeof PRESETS / sizeof PRESETS[0]; }
const char *theme_preset_name(int i)        { return (i >= 0 && i < theme_preset_count()) ? PRESETS[i].name : "Custom"; }
const char *theme_color_name(theme_color_t c){ return c < TC_COUNT ? COLOR_NAMES[c] : "?"; }
const char *theme_style_name(gauge_style_t s){ return s < STYLE_COUNT ? STYLE_NAMES[s] : "?"; }
const char *theme_fill_name(bar_fill_t f)   { return f < FILL_COUNT ? FILL_NAMES[f] : "?"; }

void theme_apply_preset(int i)
{
    if (i < 0 || i >= theme_preset_count()) return;
    memcpy(g_theme.rgb, PRESETS[i].rgb, sizeof g_theme.rgb);
    g_theme.preset = (int8_t)i;
}

lv_color_t theme_color(theme_color_t c) { return lv_color_hex(g_theme.rgb[c < TC_COUNT ? c : 0]); }

lv_color_t theme_zone_color(ch_zone_level_t level)
{
    switch (level) {
    case CH_ZONE_COLD: return theme_color(TC_COLD);
    case CH_ZONE_WARN: return theme_color(TC_WARN);
    case CH_ZONE_BAD:  return theme_color(TC_BAD);
    default:           return theme_color(TC_GOOD);
    }
}

// Gradient through the zones: one stop at the midpoint of every region
// (zones and the good gaps between them), linearly blended in between.
#define STOPS_MAX (2 * CH_ZONES_MAX + 2)
static int build_stops(const ch_def_t *d, float *pos, lv_color_t *col)
{
    float edges[STOPS_MAX]; int ne = 0;
    edges[ne++] = 0.0f; edges[ne++] = 1.0f;
    for (int i = 0; i < d->zone_count; i++) {
        float f0 = channels_frac(d, d->zones[i].from), f1 = channels_frac(d, d->zones[i].to);
        if (f0 > 0 && f0 < 1 && ne < STOPS_MAX) edges[ne++] = f0;
        if (f1 > 0 && f1 < 1 && ne < STOPS_MAX) edges[ne++] = f1;
    }
    // insertion sort
    for (int i = 1; i < ne; i++) { float v = edges[i]; int j = i - 1; while (j >= 0 && edges[j] > v) { edges[j + 1] = edges[j]; j--; } edges[j + 1] = v; }
    int n = 0;
    for (int i = 0; i + 1 < ne; i++) {
        if (edges[i + 1] - edges[i] < 1e-4f) continue;
        float mid = (edges[i] + edges[i + 1]) * 0.5f;
        pos[n] = mid;
        col[n] = theme_zone_color(channels_zone_of(d, d->min + mid * (d->max - d->min)));
        n++;
    }
    return n;
}

lv_color_t theme_color_at_frac(const ch_def_t *d, float f)
{
    if (d->zone_count == 0) return theme_color(TC_NEUTRAL);
    float pos[STOPS_MAX]; lv_color_t col[STOPS_MAX];
    int n = build_stops(d, pos, col);
    if (n == 0) return theme_color(TC_GOOD);
    if (f <= pos[0]) return col[0];
    for (int i = 0; i + 1 < n; i++) {
        if (f <= pos[i + 1]) {
            float t = (f - pos[i]) / (pos[i + 1] - pos[i]);
            return lv_color_mix(col[i + 1], col[i], (uint8_t)(t * 255.0f));
        }
    }
    return col[n - 1];
}

lv_color_t theme_bar_color(const ch_def_t *d, float value, bool fresh)
{
    if (!fresh) return theme_color(TC_STALE);
    if (d->zone_count == 0) return theme_color(TC_NEUTRAL);
    if (g_theme.fill == FILL_GRADIENT) return theme_color_at_frac(d, channels_frac(d, value));
    return theme_zone_color(channels_zone_of(d, value));
}
