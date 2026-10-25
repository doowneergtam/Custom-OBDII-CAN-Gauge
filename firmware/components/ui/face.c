// Gauge face: one lv_obj drawn entirely in LV_EVENT_DRAW_MAIN with the LVGL
// draw API. Matches design/pod-preview (ring, dial, numeric) at any radius.
#include "face.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "theme.h"
#include "fonts.h"

#define PI_F 3.14159265f
#define FLASH_US (400 * 1000)

// Resolved per-draw state for a channel.
typedef struct {
    const ch_def_t *d;
    float v, pk, lo, frac, pkfrac, lofrac, zero;
    bool  dead, dim, flash, has_pk, has_lo;
    lv_color_t bar, digits;
    char str[16], pkstr[16], lostr[16];
} res_t;

typedef struct {
    ch_id_t  ch;
    int32_t  cx, cy, R;          // centre and radius in screen coords
    bool     full;
    bool     focused;
    int      style_override;     // -1 = theme
    int64_t  now_us;
    // needle/bar motion: a critically damped spring chasing the data at frame rate
    float    disp, vel;          // shown fraction of the range and its speed (per second)
    int64_t  anim_us;
    bool     anim_init;
    // the reading updates at DIGIT_US, slower than the needle
    float    dig_val;
    int64_t  dig_us;
    // static artwork cached in PSRAM, redrawn only when the theme or state behind it changes
    lv_obj_t      *canvas;
    lv_draw_buf_t *bg;
    uint32_t       bg_gen;
    int            bg_style;
    bool           bg_dim, bg_valid;
    // where the moving parts were drawn last frame, so the next frame repaints exactly those
    lv_area_t      dyn[24];
    uint8_t        dyng[24];     // which moving part each rectangle belongs to (G_*)
    int            ndyn;
    char           last_str[16];
    int            last_flash, last_focus;
    float          last_peak, last_floor, last_disp;
    int64_t        last_full_us;
    res_t          res;          // resolved state, computed once per frame
    int64_t        res_us;
} face_t;

#define DIGIT_US (125 * 1000)    // reading refresh, 8 per second
static uint32_t s_face_gen = 1;  // bumped by face_restyle_all(): every cached background is stale


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

// Drawing passes. A face with a cached background draws in two halves: P_BG
// renders the static artwork once into the cache, P_FG draws only the moving
// parts over the cached image each frame. P_MEASURE draws nothing and records
// where the moving parts would land, so a frame repaints only those rectangles.
// Style code marks its moving parts with DYN_BEGIN()/DYN_END().
int64_t g_prim_us[9];   // console perf: label, arc, line, rect, triangle, image
#define PT(k, call) do { int64_t _t = esp_timer_get_time(); call; g_prim_us[k] += esp_timer_get_time() - _t; } while (0)
enum { P_ALL = 0, P_BG, P_FG, P_MEASURE };
static uint8_t s_pass;
static bool    s_dyn;
#define MEAS_MAX 24
#ifndef NEEDLE_BOX_PX
#define NEEDLE_BOX_PX 80.0f   // length of each box along a moving needle: fewer, larger areas beat many small ones
#endif
// Moving parts come in groups that change for different reasons; a frame repaints a group only when it changed.
enum { G_TEXT = 0, G_NEEDLE, G_MARK };
static uint8_t s_grp, *s_measg;
static lv_area_t *s_meas; static int *s_nmeas;
static uint32_t s_rawmask;   // rectangles recorded by meas_line/meas_raw: never merged into
static bool s_nomeas;        // a part recording its own rectangles turns the per-primitive ones off
#define STATIC_ON (s_pass == P_ALL || s_pass == P_BG)   // skip static loops entirely when only moving parts are wanted
#define WANT() (gate() || (s_pass == P_MEASURE && s_dyn))
#define DYN_BEGIN() (s_dyn = true, s_grp = G_TEXT)
#define DYN_END()   (s_dyn = false)
static bool gate(void)
{
    switch (s_pass) { case P_BG: return !s_dyn; case P_FG: return s_dyn; case P_MEASURE: return false; default: return true; }
}
static void meas(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    if (s_pass != P_MEASURE || !s_dyn || !s_meas || s_nomeas) return;
    lv_area_t a = { x1 - 3, y1 - 3, x2 + 3, y2 + 3 };
    for (int i = 0; i < *s_nmeas; i++) {   // merge with any rectangle it touches
        if ((s_rawmask & (1u << i)) || s_measg[i] != s_grp) continue;
        lv_area_t *b = &s_meas[i];
        if (a.x1 <= b->x2 + 8 && a.x2 >= b->x1 - 8 && a.y1 <= b->y2 + 8 && a.y2 >= b->y1 - 8) {
            b->x1 = LV_MIN(a.x1, b->x1); b->y1 = LV_MIN(a.y1, b->y1); b->x2 = LV_MAX(a.x2, b->x2); b->y2 = LV_MAX(a.y2, b->y2);
            return;
        }
    }
    if (*s_nmeas < MEAS_MAX) { s_measg[*s_nmeas] = s_grp; s_meas[(*s_nmeas)++] = a; return; }
    lv_area_t *b = &s_meas[MEAS_MAX - 1];
    b->x1 = LV_MIN(a.x1, b->x1); b->y1 = LV_MIN(a.y1, b->y1); b->x2 = LV_MAX(a.x2, b->x2); b->y2 = LV_MAX(a.y2, b->y2);
}

// The strip LVGL is rendering: a primitive outside it is not issued at all.
static bool on_clip(lv_layer_t *l, int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    if (!l) return false;
    const lv_area_t *c = &l->_clip_area;
    return !(x2 < c->x1 || x1 > c->x2 || y2 < c->y1 || y1 > c->y2);
}
static void meas_raw(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    if (s_pass != P_MEASURE || !s_meas) return;
    lv_area_t a = { x1, y1, x2, y2 };
    if (*s_nmeas < MEAS_MAX) { s_rawmask |= 1u << *s_nmeas; s_measg[*s_nmeas] = G_NEEDLE; s_meas[(*s_nmeas)++] = a; return; }
    lv_area_t *b = &s_meas[MEAS_MAX - 1];
    b->x1 = LV_MIN(a.x1, b->x1); b->y1 = LV_MIN(a.y1, b->y1); b->x2 = LV_MAX(a.x2, b->x2); b->y2 = LV_MAX(a.y2, b->y2);
}
// A thin moving part from (x0,y0) to (x1,y1), half-width hw: a chain of small
// boxes along it rather than one box around it, so a diagonal needle repaints
// a strip of the face instead of a big square of it.
static void meas_line(float x0, float y0, float x1, float y1, float hw)
{
    if (s_pass != P_MEASURE || !s_meas) return;
    float len = hypotf(x1 - x0, y1 - y0);
    int n = (int)(len / NEEDLE_BOX_PX) + 1;
    for (int i = 0; i < n; i++) {
        float ax = x0 + (x1 - x0) * i / n, ay = y0 + (y1 - y0) * i / n;
        float bx = x0 + (x1 - x0) * (i + 1) / n, by = y0 + (y1 - y0) * (i + 1) / n;
        meas_raw((int32_t)(fminf(ax, bx) - hw - 3), (int32_t)(fminf(ay, by) - hw - 3), (int32_t)(fmaxf(ax, bx) + hw + 3), (int32_t)(fmaxf(ay, by) + hw + 3));
    }
}

static void draw_text(lv_layer_t *l, const char *s, const lv_font_t *f, lv_color_t c, int32_t cx, int32_t cy, int32_t spacing, lv_text_align_t align)
{
    if (!s || !s[0] || !WANT()) return;
    lv_point_t sz;
    lv_text_get_size(&sz, s, f, spacing, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_area_t a;
    int32_t x1 = align == LV_TEXT_ALIGN_LEFT ? cx : align == LV_TEXT_ALIGN_RIGHT ? cx - sz.x : cx - sz.x / 2;
    a.x1 = x1; a.x2 = x1 + sz.x; a.y1 = cy - sz.y / 2; a.y2 = a.y1 + sz.y;
    meas(a.x1 - sz.y / 4, a.y1, a.x2 + sz.y / 4, a.y2);   // italic overhang
    if (!gate() || !on_clip(l, a.x1 - sz.y / 4, a.y1, a.x2 + sz.y / 4, a.y2)) return;
    lv_draw_label_dsc_t d; lv_draw_label_dsc_init(&d);
    d.text = s; d.font = f; d.color = c; d.letter_space = spacing; d.align = LV_TEXT_ALIGN_CENTER;
    d.text_local = 1;   // LVGL copies the text: on a canvas the draw runs later, after the caller's buffer is gone
    PT(0, lv_draw_label(l, &d, &a));
}

static void draw_arc(lv_layer_t *l, int32_t cx, int32_t cy, int32_t r, int32_t w, float a0, float a1, lv_color_t c, lv_opa_t opa, bool rounded)
{
    if (a1 <= a0 || r <= 0 || !WANT()) return;
    if (s_pass == P_MEASURE && s_dyn) {
        if (a1 - a0 >= 359) meas(cx - r, cy - r, cx + r, cy + r);
        else {
            float ri = (float)(r - w), x1 = 1e9f, y1 = 1e9f, x2 = -1e9f, y2 = -1e9f;
            int n = (int)((a1 - a0) / 10) + 2;
            for (int i = 0; i <= n; i++) {
                float a = (a0 + (a1 - a0) * i / n) * 3.14159265f / 180.0f, ca = cosf(a), sa = sinf(a);
                float px[2] = { cx + r * ca, cx + ri * ca }, py[2] = { cy + r * sa, cy + ri * sa };
                for (int k = 0; k < 2; k++) { x1 = fminf(x1, px[k]); x2 = fmaxf(x2, px[k]); y1 = fminf(y1, py[k]); y2 = fmaxf(y2, py[k]); }
            }
            meas((int32_t)x1 - w / 2, (int32_t)y1 - w / 2, (int32_t)x2 + w / 2, (int32_t)y2 + w / 2);
        }
    }
    if (!gate() || !on_clip(l, cx - r - w, cy - r - w, cx + r + w, cy + r + w)) return;
    lv_draw_arc_dsc_t d; lv_draw_arc_dsc_init(&d);
    d.center.x = cx; d.center.y = cy; d.radius = (uint16_t)r; d.width = w;
    d.start_angle = a0; d.end_angle = a1; d.color = c; d.opa = opa; d.rounded = rounded ? 1 : 0;
    PT(1, lv_draw_arc(l, &d));
}

static void draw_rect(lv_layer_t *l, int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, lv_color_t c, lv_opa_t opa);
static void draw_disc(lv_layer_t *l, int32_t cx, int32_t cy, int32_t r, lv_color_t c, lv_opa_t opa)
{
    draw_rect(l, cx - r, cy - r, 2 * r + 1, 2 * r + 1, LV_RADIUS_CIRCLE, c, opa);
}

static void draw_line(lv_layer_t *l, float x1, float y1, float x2, float y2, int32_t w, lv_color_t c, bool round)
{
    if (!WANT()) return;
    meas((int32_t)fminf(x1, x2) - w, (int32_t)fminf(y1, y2) - w, (int32_t)fmaxf(x1, x2) + w, (int32_t)fmaxf(y1, y2) + w);
    if (!gate() || !on_clip(l, (int32_t)fminf(x1, x2) - w, (int32_t)fminf(y1, y2) - w, (int32_t)fmaxf(x1, x2) + w, (int32_t)fmaxf(y1, y2) + w)) return;
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
    d.p1.x = x1; d.p1.y = y1; d.p2.x = x2; d.p2.y = y2; d.width = w; d.color = c; d.opa = LV_OPA_COVER;
    d.round_start = d.round_end = round ? 1 : 0;
    PT(2, lv_draw_line(l, &d));
}

static void draw_rect(lv_layer_t *l, int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, lv_color_t c, lv_opa_t opa)
{
    if (w <= 0 || h <= 0) return;
    meas(x, y, x + w - 1, y + h - 1);
    if (!gate() || !on_clip(l, x, y, x + w - 1, y + h - 1)) return;
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_color = c; d.bg_opa = opa; d.radius = radius; d.border_width = 0;
    lv_area_t a = { x, y, x + w - 1, y + h - 1 };
    PT(3, lv_draw_rect(l, &d, &a));
}

static void draw_tri(lv_layer_t *l, float x0, float y0, float x1, float y1, float x2, float y2, lv_color_t c)
{
    if (!WANT()) return;
    meas((int32_t)fminf(x0, fminf(x1, x2)), (int32_t)fminf(y0, fminf(y1, y2)), (int32_t)fmaxf(x0, fmaxf(x1, x2)) + 1, (int32_t)fmaxf(y0, fmaxf(y1, y2)) + 1);
    if (!gate() || !on_clip(l, (int32_t)fminf(x0, fminf(x1, x2)) - 1, (int32_t)fminf(y0, fminf(y1, y2)) - 1, (int32_t)fmaxf(x0, fmaxf(x1, x2)) + 2, (int32_t)fmaxf(y0, fmaxf(y1, y2)) + 2)) return;
    lv_draw_triangle_dsc_t d; lv_draw_triangle_dsc_init(&d);
    d.p[0].x = x0; d.p[0].y = y0; d.p[1].x = x1; d.p[1].y = y1; d.p[2].x = x2; d.p[2].y = y2;
    d.color = c; d.opa = LV_OPA_COVER;
    PT(4, lv_draw_triangle(l, &d));
}

#include "fast_raster.inc"

static void fmt_val(char *buf, size_t n, float v, uint8_t dp)
{
    if (isnan(v) || isinf(v)) { snprintf(buf, n, "---"); return; }
    snprintf(buf, n, "%.*f", dp, (double)v);
}

static void resolve_now(face_t *f, res_t *r);
static void resolve(face_t *f, res_t *r)
{
    if (f->res_us && f->res_us == f->now_us && f->res.d == channels_def(f->ch)) { *r = f->res; return; }
    PT(6, resolve_now(f, r));
    f->res = *r; f->res_us = f->now_us;
}
static void resolve_now(face_t *f, res_t *r)
{
    const ch_def_t *d = channels_def(f->ch);
    const ch_state_t *s = channels_state(f->ch);
    ch_freshness_t fr = channels_freshness(f->ch, f->now_us);
    memset(r, 0, sizeof *r);
    r->d = d;
    r->dead = fr == CH_DEAD; r->dim = fr != CH_FRESH;
    r->v = s->value; r->pk = s->peak; r->lo = s->floor;
    r->frac = f->anim_init ? f->disp : (r->dead ? 0 : channels_frac(d, r->v));   // the needle/bar follows the spring
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
    if (r->dead) strcpy(r->str, "---"); else fmt_val(r->str, sizeof r->str, channels_to_display(d, f->dig_us ? f->dig_val : r->v), dp);
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
static const lv_font_t *nfont(float px) { return fonts_numerals((int)(px * g_theme.label_scale)); }
static const lv_font_t *numfont(float px) { return fonts_numerals((int)(px * g_theme.num_scale)); }   // scale numbers: own size slider
static float scale_out(int32_t R) { return g_theme.el_dy[EL_SCALE] * R; }   // radial offset of the scale numbers, plus = outward   // Stock numerals and labels: bold italic when the TTF is compiled in
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
    bool lit = !r->dead && r->frac != r->zero && s_pass != P_BG;   // the cache holds the bar unlit

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
            s_dyn = on; s_grp = G_NEEDLE;
            draw_arc(l, cx, cy, rr, w, a0 + i * step + gap / 2, a0 + (i + 1) * step - gap / 2, c, opa, false);
        }
        DYN_END();
    } else {
        draw_arc(l, cx, cy, rr, w, a0, a0 + span, theme_color(TC_TRACK), LV_OPA_COVER, true);
        if (g_theme.bands) arc_bands(l, d, cx, cy, rr, w, a0, span, 72);
        if (lit) {
            DYN_BEGIN(); s_grp = G_NEEDLE;
            draw_arc(l, cx, cy, rr, w, a0 + span * flo, a0 + span * fhi, r->bar, LV_OPA_COVER, true);
            float a = (a0 + span * r->frac) * PI_F / 180.0f;
            draw_disc(l, cx + (int32_t)(rr * cosf(a)), cy + (int32_t)(rr * sinf(a)), (int32_t)fmaxf(2, w * 0.22f), lv_color_white(), 217);
            DYN_END();
        }
    }
    DYN_BEGIN(); s_grp = G_MARK;
    for (int k = 0; k < 2 && el(EL_BAR); k++) {   // min and max markers
        if (k == 0 ? !r->has_lo : !r->has_pk) continue;
        float a = (a0 + span * (k == 0 ? r->lofrac : r->pkfrac)) * PI_F / 180.0f, r0 = rr + w / 2.0f + 2 * s, r1 = r0 + 7 * s;
        draw_line(l, cx + r0 * cosf(a), cy + r0 * sinf(a), cx + r1 * cosf(a), cy + r1 * sinf(a), (int32_t)fmaxf(2, 2.5f * s), r->dim ? theme_color(TC_STALE) : theme_color(TC_DIGITS), false);
    }
    DYN_END();
    if (full && el(EL_SCALE)) {
        float ri = rr - w - 14 * s + scale_out(R); char b[16];
        fmt_val(b, sizeof b, channels_to_display(d, d->min), 0);
        draw_text(l, b, font_px((int)(13 * s * g_theme.num_scale)), theme_color(TC_TICK), cx + (int32_t)(ri * cosf(a0 * PI_F / 180)), cy + (int32_t)(ri * sinf(a0 * PI_F / 180)), 0, LV_TEXT_ALIGN_CENTER);
        fmt_val(b, sizeof b, channels_to_display(d, d->max), 0);
        draw_text(l, b, font_px((int)(13 * s * g_theme.num_scale)), theme_color(TC_TICK), cx + (int32_t)(ri * cosf((a0 + span) * PI_F / 180)), cy + (int32_t)(ri * sinf((a0 + span) * PI_F / 180)), 0, LV_TEXT_ALIGN_CENTER);
    }
    int lab = full ? 18 : (int)(14 * R / 90.0f);
    if (el(EL_LABEL)) draw_text(l, d->name, lfont(lab), r->dim ? theme_color(TC_STALE) : theme_color(TC_LABEL), cx, cy - (int32_t)(R * 0.5f) + dyp(EL_LABEL, R), lab / 5, LV_TEXT_ALIGN_CENTER);
    DYN_BEGIN();
    if (el(EL_DIGITS)) draw_text(l, r->str, fit_digits(r->str, full ? 80 : fmaxf(22, R * 0.36f), (int32_t)(R * 1.45f)), r->digits, cx, cy + (int32_t)(R * 0.02f) + dyp(EL_DIGITS, R), 0, LV_TEXT_ALIGN_CENTER);
    DYN_END();
    if (el(EL_UNITS)) draw_text(l, units_for(d), ufont(full ? 18 : (int)(11 * R / 90.0f)), theme_color(TC_UNITS), cx, cy + (int32_t)(R * (full ? 0.3f : 0.34f)) + dyp(EL_UNITS, R), 0, LV_TEXT_ALIGN_CENTER);
    if (el(EL_MINMAX)) {
        char b[40];
        DYN_BEGIN();
        if (minmax_text(r, b, sizeof b)) draw_text(l, b, mfont(full ? 13 * s : 9 * R / 90.0f), theme_color(TC_UNITS), cx, cy + (int32_t)(R * (full ? 0.86f : 0.58f)) + dyp(EL_MINMAX, R), full ? 1 : 0, LV_TEXT_ALIGN_CENTER);
        DYN_END();
    }
}

#include "face_styles.inc"
