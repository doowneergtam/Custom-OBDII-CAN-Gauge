// Included at the end of face_styles.inc: the face object, its cached
// background, the needle spring and the per-frame invalidation.
//
// Per frame (UI timer, ~60 Hz): the spring moves the shown needle/bar position
// toward the data; a P_MEASURE pass of the style records where the moving parts
// land; the rectangles they covered last frame and this frame are invalidated.
// LVGL then redraws only those rectangles: a blit of the cached static artwork
// plus the moving parts (P_FG). The static artwork is rendered once into a PSRAM
// image (P_BG) and redone only when the theme, style, channel or stale state
// changes.

extern int64_t g_face_inv_px, g_face_inv_rects;
typedef void (*style_fn)(lv_layer_t *, face_t *, res_t *);
static int face_style(const face_t *f) { return f->style_override >= 0 ? f->style_override : g_theme.style; }
static style_fn style_of(int style)
{
    switch (style) {
    case STYLE_DIAL:    return style_dial;
    case STYLE_NUMERIC: return style_numeric;
    case STYLE_STOCK:   return style_stock;
    case STYLE_STOCK2:  return style_stock2;
    default:            return style_ring;
    }
}
static bool cacheable(int style) { return style != STYLE_NUMERIC; }
static void centre_of(lv_obj_t *obj, face_t *f)
{
    lv_area_t co; lv_obj_get_coords(obj, &co);
    f->cx = (co.x1 + co.x2 + 1) / 2; f->cy = (co.y1 + co.y2 + 1) / 2;
}

// Render the static artwork into the PSRAM cache. Called from the UI timer, never inside a draw.
static void build_bg(lv_obj_t *obj, face_t *f, int style, bool dim)
{
    int32_t w = lv_obj_get_width(obj), h = lv_obj_get_height(obj);
    if (w <= 0 || h <= 0) { f->bg_valid = false; return; }   // not laid out yet: try again next frame
    lv_color_format_t cf = f->full ? LV_COLOR_FORMAT_RGB565 : LV_COLOR_FORMAT_ARGB8888;   // small cells keep their corners see-through
    if (f->bg && (f->bg->header.w != w || f->bg->header.h != h || f->bg->header.cf != cf)) {
        if (f->canvas) { lv_obj_delete(f->canvas); f->canvas = NULL; }
        lv_draw_buf_destroy(f->bg); f->bg = NULL;
    }
    if (!f->bg) f->bg = lv_draw_buf_create(w, h, cf, LV_STRIDE_AUTO);
    if (!f->bg) { f->bg_valid = false; return; }   // out of memory: the face simply draws everything every frame
    if (!f->canvas) { f->canvas = lv_canvas_create(lv_layer_sys()); lv_obj_add_flag(f->canvas, LV_OBJ_FLAG_HIDDEN); }
    lv_canvas_set_draw_buf(f->canvas, f->bg);
    lv_canvas_fill_bg(f->canvas, theme_color(TC_BG), f->full ? LV_OPA_COVER : LV_OPA_TRANSP);
    lv_layer_t layer;
    lv_canvas_init_layer(f->canvas, &layer);
    face_t fc = *f; fc.cx = w / 2; fc.cy = h / 2; fc.focused = false;
    res_t r; resolve(&fc, &r); r.dim = dim;
    s_pass = P_BG; s_dyn = false;
    style_of(style)(&layer, &fc, &r);
    s_pass = P_ALL; s_dyn = false;
    lv_canvas_finish_layer(f->canvas, &layer);
    f->bg_valid = true; f->bg_gen = s_face_gen; f->bg_style = style; f->bg_dim = dim;
    { const uint16_t *px = (const uint16_t *)f->bg->data; (void)px; ESP_LOGD("face", "bg built %ldx%ld cf %d", (long)w, (long)h, (int)cf); }
}

int64_t g_face_draw_us, g_face_draw_n;   // console perf: time inside the face drawing code
static void draw_cb(lv_event_t *e)
{
    int64_t t0 = esp_timer_get_time();
    lv_obj_t *obj = lv_event_get_target(e);
    face_t *f = lv_obj_get_user_data(obj);
    if (!f || !channels_def(f->ch)) return;
    lv_layer_t *l = lv_event_get_layer(e);
    centre_of(obj, f);
    res_t r; resolve(f, &r);
    int style = face_style(f);
    if (cacheable(style) && f->bg_valid && f->bg_style == style && f->bg
        && (int32_t)f->bg->header.w == lv_obj_get_width(obj) && (int32_t)f->bg->header.h == lv_obj_get_height(obj)) {
        lv_area_t co; lv_obj_get_coords(obj, &co);
        lv_draw_image_dsc_t id; lv_draw_image_dsc_init(&id);
        id.src = f->bg;
        PT(5, lv_draw_image(l, &id, &co));
        s_pass = P_FG;
    } else {
        s_pass = P_ALL;
    }
    s_dyn = false;
    style_of(style)(l, f, &r);
    s_pass = P_ALL; s_dyn = false;
    if (f->focused) draw_arc(l, f->cx, f->cy, f->R + 6, 2, 0, 360, theme_color(TC_DIGITS), 150, false);
    g_face_draw_us += esp_timer_get_time() - t0; g_face_draw_n++;
}

static void delete_cb(lv_event_t *e)
{
    face_t *f = lv_event_get_user_data(e);
    if (f->canvas) lv_obj_delete(f->canvas);   // lives on the system layer, not under the face
    if (f->bg) lv_draw_buf_destroy(f->bg);
    lv_free(f);
}

lv_obj_t *face_create(lv_obj_t *parent, ch_id_t ch, int32_t cx, int32_t cy, int32_t radius, bool full)
{
    face_t *f = lv_malloc_zeroed(sizeof(face_t));
    f->ch = ch; f->R = radius; f->full = full; f->style_override = -1; f->last_focus = -1;
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    int32_t pad = 10;   // focus ring and peak ticks draw just outside R
    lv_obj_set_size(obj, 2 * (radius + pad), 2 * (radius + pad));
    lv_obj_set_pos(obj, cx - radius - pad, cy - radius - pad);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_user_data(obj, f);
    lv_obj_add_event_cb(obj, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(obj, delete_cb, LV_EVENT_DELETE, f);
    return obj;
}

// Critically damped spring: the shown position chases the data like a real
// needle, so 20 Hz data moves smoothly at the frame rate. W sets the speed
// (about 4/W seconds to settle).
#define SPRING_W 32.0f
static void spring(face_t *f, float target, int64_t now_us)
{
    float dt = f->anim_us ? (now_us - f->anim_us) / 1e6f : 0;
    f->anim_us = now_us;
    if (!f->anim_init) { f->disp = target; f->vel = 0; f->anim_init = true; return; }
    if (dt <= 0) return;
    if (dt > 0.2f) dt = 0.2f;
    float x0 = f->disp - target, v0 = f->vel, ex = expf(-SPRING_W * dt);
    float x = (x0 + (v0 + SPRING_W * x0) * dt) * ex;
    float v = (v0 - SPRING_W * (v0 + SPRING_W * x0) * dt) * ex;
    if (fabsf(x) < 0.0004f && fabsf(v) < 0.004f) { x = 0; v = 0; }
    f->disp = target + x; f->vel = v;
}

void face_refresh(lv_obj_t *face, int64_t now_us)
{
    face_t *f = lv_obj_get_user_data(face);
    if (!f) return;
    const ch_def_t *d = channels_def(f->ch);
    if (!d) return;
    const ch_state_t *st = channels_state(f->ch);
    ch_freshness_t fr = channels_freshness(f->ch, now_us);
    spring(f, fr == CH_DEAD ? 0 : channels_frac(d, st->value), now_us);
    if (!f->dig_us || now_us - f->dig_us >= DIGIT_US || fr == CH_DEAD) { f->dig_val = st->value; f->dig_us = now_us; }
    f->now_us = now_us;

    int style = face_style(f);
    bool dim = fr != CH_FRESH;
    if (lv_obj_get_width(face) <= 0) lv_obj_update_layout(face);   // a face rebuilt this tick has no size until layout runs
    centre_of(face, f);
    bool whole = false;
    if (cacheable(style) && (!f->bg_valid || f->bg_gen != s_face_gen || f->bg_style != style || f->bg_dim != dim)) {
        build_bg(face, f, style, dim);
        whole = true;
    }
    res_t r; resolve(f, &r);
    if (f->focused != (bool)f->last_focus) whole = true;
    bool ch_needle = f->disp != f->last_disp;
    bool ch_marks = st->peak != f->last_peak || st->floor != f->last_floor;
    bool ch_text = strcmp(r.str, f->last_str) != 0 || r.flash != f->last_flash || ch_marks;   // min/max text follows the marks
    bool changed = ch_needle || ch_marks || ch_text;
    if (!changed && !whole) return;
    if (!cacheable(style)) whole = true;

    lv_area_t now[MEAS_MAX]; uint8_t nowg[MEAS_MAX]; int n = 0;
    s_meas = now; s_measg = nowg; s_nmeas = &n; s_rawmask = 0; s_nomeas = false; s_pass = P_MEASURE; s_dyn = false;
    style_of(style)(NULL, f, &r);
    s_pass = P_ALL; s_dyn = false; s_meas = NULL; s_nmeas = NULL;

    { int64_t px = 0; if (whole) px = (int64_t)lv_obj_get_width(face) * lv_obj_get_height(face);
      else { for (int i = 0; i < f->ndyn; i++) px += lv_area_get_size(&f->dyn[i]); for (int i = 0; i < n; i++) px += lv_area_get_size(&now[i]); }   // upper bound
      g_face_inv_px += px; g_face_inv_rects += whole ? 1 : f->ndyn + n; }
    if (whole) lv_obj_invalidate(face);
    else {
        bool chg[3] = { ch_text, ch_needle, ch_marks };   // indexed by G_*
        for (int i = 0; i < f->ndyn; i++) if (chg[f->dyng[i]]) lv_obj_invalidate_area(face, &f->dyn[i]);
        for (int i = 0; i < n; i++) if (chg[nowg[i]]) lv_obj_invalidate_area(face, &now[i]);
    }
    memcpy(f->dyn, now, sizeof now); memcpy(f->dyng, nowg, sizeof nowg); f->ndyn = n;
    f->last_disp = f->disp; snprintf(f->last_str, sizeof f->last_str, "%s", r.str); f->last_flash = r.flash;
    f->last_peak = st->peak; f->last_floor = st->floor; f->last_focus = f->focused;
}

int64_t g_face_inv_px, g_face_inv_rects;   // console perf: area handed to LVGL
void face_restyle_all(void) { s_face_gen++; }
void face_set_channel(lv_obj_t *face, ch_id_t ch) { face_t *f = lv_obj_get_user_data(face); if (f) { f->ch = ch; f->bg_valid = false; f->anim_init = false; f->dig_us = 0; lv_obj_invalidate(face); } }
ch_id_t face_channel(lv_obj_t *face)             { face_t *f = lv_obj_get_user_data(face); return f ? f->ch : CH_RPM; }
void face_set_style(lv_obj_t *face, int style)   { face_t *f = lv_obj_get_user_data(face); if (f) { f->style_override = style; f->bg_valid = false; lv_obj_invalidate(face); } }
void face_set_focused(lv_obj_t *face, bool on)   { face_t *f = lv_obj_get_user_data(face); if (f) f->focused = on; }

// Console check: render the face as it is now (cached artwork, then the moving
// parts through the same code as the screen) into an RGB565 image and print it
// as hex rows, every `step`th pixel, so the picture can be viewed on a PC.
void face_snapshot_dump(lv_obj_t *obj, int step)
{
    face_t *f = lv_obj_get_user_data(obj);
    if (!f) return;
    if (step < 1) step = 1;
    int32_t w = lv_obj_get_width(obj), h = lv_obj_get_height(obj);
    lv_draw_buf_t *buf = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    if (!buf) { printf("snap nomem\n"); return; }
    lv_obj_t *cv = lv_canvas_create(lv_layer_sys());
    lv_obj_add_flag(cv, LV_OBJ_FLAG_HIDDEN);
    lv_canvas_set_draw_buf(cv, buf);
    lv_canvas_fill_bg(cv, theme_color(TC_BG), LV_OPA_COVER);
    lv_layer_t layer;
    lv_canvas_init_layer(cv, &layer);
    face_t fc = *f; fc.cx = w / 2; fc.cy = h / 2; fc.res_us = 0;
    res_t r; resolve(&fc, &r);
    int style = face_style(f);
    if (cacheable(style) && f->bg_valid) {
        lv_draw_image_dsc_t id; lv_draw_image_dsc_init(&id); id.src = f->bg;
        lv_area_t a = { 0, 0, w - 1, h - 1 };
        lv_draw_image(&layer, &id, &a);
        lv_canvas_finish_layer(cv, &layer);   // the background must be in the buffer before the needle is written into it
        lv_canvas_init_layer(cv, &layer);
        s_pass = P_FG;
    } else s_pass = P_ALL;
    s_dyn = false;
    style_of(style)(&layer, &fc, &r);
    s_pass = P_ALL; s_dyn = false;
    lv_canvas_finish_layer(cv, &layer);
    printf("snap %ld %ld %d\n", (long)w, (long)h, step);
    for (int32_t y = 0; y < h; y += step) {
        const uint16_t *row = (const uint16_t *)(buf->data + (size_t)y * buf->header.stride);
        for (int32_t x = 0; x < w; x += step) printf("%04x", row[x]);
        printf("\n");
    }
    printf("endsnap\n");
    lv_obj_delete(cv);
    lv_draw_buf_destroy(buf);
}
