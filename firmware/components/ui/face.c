// Gauge face: one lv_obj drawn entirely in LV_EVENT_DRAW_MAIN with the LVGL
// draw API. Matches design/pod-preview (ring, dial, numeric) at any radius.
#include "face.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "esp_timer.h"
#include "theme.h"
#include "fonts.h"

#define PI_F 3.14159265f
#define FLASH_US (400 * 1000)

typedef struct {
    ch_id_t  ch;
    int32_t  cx, cy, R;          // centre and radius in screen coords
    bool     full;
    bool     focused;
    int      style_override;     // -1 = theme
    int64_t  now_us;
} face_t;

// Resolved per-draw state for a channel.
typedef struct {
    const ch_def_t *d;
    float v, pk, lo, frac, pkfrac, lofrac, zero;
    bool  dead, dim, flash, has_pk, has_lo;
    lv_color_t bar, digits;
    char str[16], pkstr[16], lostr[16];
} res_t;

// ---------------------------------------------------------------- helpers
static const lv_font_t *font_px(int px)
{
    if (px >= 48) return &lv_font_montserrat_48;
    if (px >= 40) return &lv_font_montserrat_40;
    if (px >= 32) return &lv_font_montserrat_32;
    if (px >= 28) return &lv_font_montserrat_28;
    if (px >= 24) return &lv_font_montserrat_24;
    if (px >= 20) return &lv_font_montserrat_20;
    if (px >= 18) return &lv_font_montserrat_18;
    if (px >= 16) return &lv_font_montserrat_16;
    if (px >= 14) return &lv_font_montserrat_14;
    return &lv_font_montserrat_12;
}

static void draw_text(lv_layer_t *l, const char *s, const lv_font_t *f, lv_color_t c, int32_t cx, int32_t cy, int32_t spacing, lv_text_align_t align)
{
    lv_point_t sz;
    lv_text_get_size(&sz, s, f, spacing, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_draw_label_dsc_t d; lv_draw_label_dsc_init(&d);
    d.text = s; d.font = f; d.color = c; d.letter_space = spacing; d.align = LV_TEXT_ALIGN_CENTER;
    lv_area_t a;
    int32_t x1 = align == LV_TEXT_ALIGN_LEFT ? cx : align == LV_TEXT_ALIGN_RIGHT ? cx - sz.x : cx - sz.x / 2;
    a.x1 = x1; a.x2 = x1 + sz.x; a.y1 = cy - sz.y / 2; a.y2 = a.y1 + sz.y;
    lv_draw_label(l, &d, &a);
}

static void draw_arc(lv_layer_t *l, int32_t cx, int32_t cy, int32_t r, int32_t w, float a0, float a1, lv_color_t c, lv_opa_t opa, bool rounded)
{
    if (a1 <= a0 || r <= 0) return;
    lv_draw_arc_dsc_t d; lv_draw_arc_dsc_init(&d);
    d.center.x = cx; d.center.y = cy; d.radius = (uint16_t)r; d.width = w;
    d.start_angle = a0; d.end_angle = a1; d.color = c; d.opa = opa; d.rounded = rounded ? 1 : 0;
    lv_draw_arc(l, &d);
}

static void draw_disc(lv_layer_t *l, int32_t cx, int32_t cy, int32_t r, lv_color_t c, lv_opa_t opa)
{
    draw_arc(l, cx, cy, r, r, 0, 360, c, opa, false);
}

static void draw_line(lv_layer_t *l, float x1, float y1, float x2, float y2, int32_t w, lv_color_t c, bool round)
{
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
    d.p1.x = x1; d.p1.y = y1; d.p2.x = x2; d.p2.y = y2; d.width = w; d.color = c; d.opa = LV_OPA_COVER;
    d.round_start = d.round_end = round ? 1 : 0;
    lv_draw_line(l, &d);
}

static void draw_rect(lv_layer_t *l, int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, lv_color_t c, lv_opa_t opa)
{
    if (w <= 0 || h <= 0) return;
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_color = c; d.bg_opa = opa; d.radius = radius; d.border_width = 0;
    lv_area_t a = { x, y, x + w - 1, y + h - 1 };
    lv_draw_rect(l, &d, &a);
}

static void draw_tri(lv_layer_t *l, float x0, float y0, float x1, float y1, float x2, float y2, lv_color_t c)
{
    lv_draw_triangle_dsc_t d; lv_draw_triangle_dsc_init(&d);
    d.p[0].x = x0; d.p[0].y = y0; d.p[1].x = x1; d.p[1].y = y1; d.p[2].x = x2; d.p[2].y = y2;
    d.color = c; d.opa = LV_OPA_COVER;
    lv_draw_triangle(l, &d);
}

static void fmt_val(char *buf, size_t n, float v, uint8_t dp)
{
    if (isnan(v) || isinf(v)) { snprintf(buf, n, "---"); return; }
    snprintf(buf, n, "%.*f", dp, (double)v);
}

static void resolve(face_t *f, res_t *r)
{
    const ch_def_t *d = channels_def(f->ch);
    const ch_state_t *s = channels_state(f->ch);
    ch_freshness_t fr = channels_freshness(f->ch, f->now_us);
    memset(r, 0, sizeof *r);
    r->d = d;
    r->dead = fr == CH_DEAD; r->dim = fr != CH_FRESH;
    r->v = s->value; r->pk = s->peak; r->lo = s->floor;
    r->frac = r->dead ? 0 : channels_frac(d, r->v);
    r->has_pk = !r->dead && isfinite(r->pk) && d->show_max;
    r->pkfrac = r->has_pk ? channels_frac(d, r->pk) : 0;
    r->has_lo = !r->dead && isfinite(r->lo) && d->show_min;
    r->lofrac = r->has_lo ? channels_frac(d, r->lo) : 0;
    r->zero = (d->min < 0 && d->max > 0) ? -d->min / (d->max - d->min) : 0;
    r->bar = theme_bar_color(d, r->v, !r->dim);
    bool bad = !r->dim && channels_zone_of(d, r->v) == CH_ZONE_BAD;
    r->flash = bad && ((f->now_us / FLASH_US) & 1) == 0;
    r->digits = r->dim ? theme_color(TC_STALE) : r->flash ? theme_color(TC_BAD) : theme_color(TC_DIGITS);
    uint8_t dp = channels_display_decimals(d);
    if (r->dead) strcpy(r->str, "---"); else fmt_val(r->str, sizeof r->str, channels_to_display(d, r->v), dp);
    fmt_val(r->pkstr, sizeof r->pkstr, channels_to_display(d, r->pk), dp);
    fmt_val(r->lostr, sizeof r->lostr, channels_to_display(d, r->lo), dp);
}

static lv_color_t band_color(const ch_def_t *d, float frac)
{
    return theme_zone_color(channels_zone_of(d, d->min + frac * (d->max - d->min)));
}

// Draw faint zone bands (anything not GOOD) along an arc.
static void arc_bands(lv_layer_t *l, const ch_def_t *d, int32_t cx, int32_t cy, int32_t r, int32_t w, float a0, float span, lv_opa_t opa)
{
    for (int i = 0; i < d->zone_count; i++) {
        float f0 = channels_frac(d, d->zones[i].from), f1 = channels_frac(d, d->zones[i].to);
        if (f1 <= f0) continue;
        draw_arc(l, cx, cy, r, w, a0 + span * f0, a0 + span * f1, theme_zone_color(d->zones[i].level), opa, false);
    }
}

static bool    el(theme_el_t e)              { return g_theme.el_on[e]; }
static int32_t dyp(theme_el_t e, int32_t R)  { return (int32_t)lroundf(g_theme.el_dy[e] * R); }
static float   sweep0(float a0)              { return a0 + g_theme.sweep_rot; }
static const lv_font_t *dfont(float px) { return fonts_digits((int)(px * g_theme.digit_scale)); }
static const lv_font_t *lfont(float px) { return font_px((int)(px * g_theme.label_scale)); }
static const lv_font_t *ufont(float px) { return font_px((int)(px * g_theme.units_scale)); }
static const lv_font_t *mfont(float px) { return font_px((int)(px * g_theme.minmax_scale)); }
static const lv_font_t *nfont(float px) { return fonts_numerals((int)(px * g_theme.label_scale)); }   // Stock numerals and labels: bold italic when the TTF is compiled in
// Digit font at the requested size, stepped down until the string fits max_w.
static const lv_font_t *fit_digits(const char *s, float px, int32_t max_w)
{
    (void)s; (void)max_w;   // no clamping by design: the user sizes it, even past the edge
    return fonts_digits((int)(px * g_theme.digit_scale));
}
// "lo/hi", or just the one that is switched on; false when nothing to show.
static bool minmax_text(const res_t *r, char *b, size_t n)
{
    bool lo = r->d->show_min && r->has_lo, hi = r->d->show_max && r->has_pk;
    if (lo && hi) snprintf(b, n, "%s/%s", r->lostr, r->pkstr);
    else if (lo) snprintf(b, n, "%s", r->lostr);
    else if (hi) snprintf(b, n, "%s", r->pkstr);
    else return false;
    return true;
}
// Units label, blank when it just repeats the channel name (RPM, AFR).
static const char *units_for(const ch_def_t *d) { if (!d->show_units) return ""; const char *u = channels_units_label(d); return strcasecmp(u, d->name) == 0 ? "" : u; }

// ---------------------------------------------------------------- ring
static void style_ring(lv_layer_t *l, face_t *f, res_t *r)
{
    const ch_def_t *d = r->d;
    int32_t cx = f->cx, cy = f->cy, R = f->R;
    float s = R / 229.0f;
    bool full = f->full;
    float span = full ? 270 : 240, a0 = sweep0(full ? 135 : 150);
    int32_t w = (int32_t)fmaxf(3, R * 0.072f * g_theme.bar_scale), rr = R - w / 2 - 2;
    float flo = fminf(r->zero, r->frac), fhi = fmaxf(r->zero, r->frac);
    bool lit = !r->dead && r->frac != r->zero;

    if (!el(EL_BAR)) { /* no track, no fill, no markers */ }
    else if (g_theme.fill == FILL_SEGMENTS) {
        int n = full ? 54 : 24; float gap = full ? 1.4f : 2.2f, step = span / n;
        for (int i = 0; i < n; i++) {
            float fm = (i + 0.5f) / n;
            bool on = lit && fm >= flo && fm <= fhi;
            lv_color_t c; lv_opa_t opa = LV_OPA_COVER;
            if (on) c = r->dim ? theme_color(TC_STALE) : (d->zone_count ? band_color(d, fm) : theme_color(TC_NEUTRAL));
            else if (g_theme.bands && d->zone_count && channels_zone_of(d, d->min + fm * (d->max - d->min)) != CH_ZONE_GOOD) { c = band_color(d, fm); opa = 72; }
            else c = theme_color(TC_TRACK);
            draw_arc(l, cx, cy, rr, w, a0 + i * step + gap / 2, a0 + (i + 1) * step - gap / 2, c, opa, false);
        }
    } else {
        draw_arc(l, cx, cy, rr, w, a0, a0 + span, theme_color(TC_TRACK), LV_OPA_COVER, true);
        if (g_theme.bands) arc_bands(l, d, cx, cy, rr, w, a0, span, 72);
        if (lit) {
            draw_arc(l, cx, cy, rr, w, a0 + span * flo, a0 + span * fhi, r->bar, LV_OPA_COVER, true);
            float a = (a0 + span * r->frac) * PI_F / 180.0f;
            draw_disc(l, cx + (int32_t)(rr * cosf(a)), cy + (int32_t)(rr * sinf(a)), (int32_t)fmaxf(2, w * 0.22f), lv_color_white(), 217);
        }
    }
    for (int k = 0; k < 2 && el(EL_BAR); k++) {   // min and max markers
        if (k == 0 ? !r->has_lo : !r->has_pk) continue;
        float a = (a0 + span * (k == 0 ? r->lofrac : r->pkfrac)) * PI_F / 180.0f, r0 = rr + w / 2.0f + 2 * s, r1 = r0 + 7 * s;
        draw_line(l, cx + r0 * cosf(a), cy + r0 * sinf(a), cx + r1 * cosf(a), cy + r1 * sinf(a), (int32_t)fmaxf(2, 2.5f * s), r->dim ? theme_color(TC_STALE) : theme_color(TC_DIGITS), false);
    }
    if (full && el(EL_BAR)) {
        float ri = rr - w - 14 * s; char b[16];
        fmt_val(b, sizeof b, channels_to_display(d, d->min), 0);
        draw_text(l, b, font_px((int)(13 * s)), theme_color(TC_TICK), cx + (int32_t)(ri * cosf(a0 * PI_F / 180)), cy + (int32_t)(ri * sinf(a0 * PI_F / 180)), 0, LV_TEXT_ALIGN_CENTER);
        fmt_val(b, sizeof b, channels_to_display(d, d->max), 0);
        draw_text(l, b, font_px((int)(13 * s)), theme_color(TC_TICK), cx + (int32_t)(ri * cosf((a0 + span) * PI_F / 180)), cy + (int32_t)(ri * sinf((a0 + span) * PI_F / 180)), 0, LV_TEXT_ALIGN_CENTER);
    }
    int lab = full ? 18 : (int)(14 * R / 90.0f);
    if (el(EL_LABEL)) draw_text(l, d->name, lfont(lab), r->dim ? theme_color(TC_STALE) : theme_color(TC_LABEL), cx, cy - (int32_t)(R * 0.5f) + dyp(EL_LABEL, R), lab / 5, LV_TEXT_ALIGN_CENTER);
    if (el(EL_DIGITS)) draw_text(l, r->str, fit_digits(r->str, full ? 80 : fmaxf(22, R * 0.36f), (int32_t)(R * 1.45f)), r->digits, cx, cy + (int32_t)(R * 0.02f) + dyp(EL_DIGITS, R), 0, LV_TEXT_ALIGN_CENTER);
    if (el(EL_UNITS)) draw_text(l, units_for(d), ufont(full ? 18 : (int)(11 * R / 90.0f)), theme_color(TC_UNITS), cx, cy + (int32_t)(R * (full ? 0.3f : 0.34f)) + dyp(EL_UNITS, R), 0, LV_TEXT_ALIGN_CENTER);
    if (el(EL_MINMAX)) {
        char b[40];
        if (minmax_text(r, b, sizeof b)) draw_text(l, b, mfont(full ? 13 * s : 9 * R / 90.0f), theme_color(TC_UNITS), cx, cy + (int32_t)(R * (full ? 0.86f : 0.58f)) + dyp(EL_MINMAX, R), full ? 1 : 0, LV_TEXT_ALIGN_CENTER);
    }
}

#include "face_styles.inc"
