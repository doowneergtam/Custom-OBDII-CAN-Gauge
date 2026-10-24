// Included at the end of face.c: dial, stock and numeric styles plus the object
// plumbing. Split only to keep each file a readable size; shares statics.

// ---------------------------------------------------------------- dial
static void style_dial(lv_layer_t *l, face_t *f, res_t *r)
{
    const ch_def_t *d = r->d;
    int32_t cx = f->cx, cy = f->cy, R = f->R;
    float s = R / 229.0f;
    bool full = f->full;
    const float span = 270, a0 = 135;
    int n = full ? 50 : 20;
    int32_t bw = (int32_t)fmaxf(2, R * 0.032f * g_theme.bar_scale);

    draw_arc(l, cx, cy, R - 2, 2, a0, a0 + span, theme_color(TC_TRACK), LV_OPA_COVER, false);
    if (g_theme.bands) arc_bands(l, d, cx, cy, R - 4 - bw / 2, bw, a0, span, 230);
    for (int i = 0; i <= n; i++) {
        float a = (a0 + span * i / n) * PI_F / 180.0f;
        bool major = i % (full ? 5 : 4) == 0;
        float r1 = R - 6 - bw - 2, r0 = r1 - (major ? R * 0.1f : R * 0.05f);
        draw_line(l, cx + r0 * cosf(a), cy + r0 * sinf(a), cx + r1 * cosf(a), cy + r1 * sinf(a),
                  major ? (int32_t)fmaxf(2, 2.2f * s) : 1, major ? theme_color(TC_DIGITS) : theme_color(TC_TICK), false);
        if (major && (full || R > 70)) {
            float v = channels_to_display(d, d->min + (d->max - d->min) * i / n);
            char b[16];
            if (fabsf(channels_to_display(d, d->max)) >= 1000) snprintf(b, sizeof b, "%.0f", (double)(v / 1000)); else fmt_val(b, sizeof b, v, 0);
            float rt = r0 - R * 0.09f;
            draw_text(l, b, font_px(full ? 18 : (int)(10 * R / 90.0f)), theme_color(TC_TICK), cx + (int32_t)(rt * cosf(a)), cy + (int32_t)(rt * sinf(a)), 0, LV_TEXT_ALIGN_CENTER);
        }
    }
    int lab = full ? 18 : (int)(13 * R / 90.0f);
    draw_text(l, d->name, lfont(lab), r->dim ? theme_color(TC_STALE) : theme_color(TC_LABEL), cx, cy - (int32_t)(R * 0.4f), lab / 5, LV_TEXT_ALIGN_CENTER);
    int vpx = full ? 48 : (int)fmaxf(18, R * 0.3f);
    draw_text(l, r->str, fit_digits(r->str, vpx, (int32_t)(R * 1.1f)), r->digits, cx, cy + (int32_t)(R * 0.5f), 0, LV_TEXT_ALIGN_CENTER);
    if (full) draw_text(l, units_for(d), ufont(16), theme_color(TC_UNITS), cx, cy + (int32_t)(R * 0.5f) + vpx * 3 / 4, 0, LV_TEXT_ALIGN_CENTER);
    if (!full) { char b[40]; if (minmax_text(r, b, sizeof b)) draw_text(l, b, mfont(9 * R / 90.0f), theme_color(TC_UNITS), cx, cy + (int32_t)(R * 0.74f), 0, LV_TEXT_ALIGN_CENTER); }

    // needle: tapered triangle in the bar colour, hub ring
    float a = (a0 + span * r->frac) * PI_F / 180.0f, L = R - 12 - bw - R * 0.1f, hub = fmaxf(5, R * 0.06f);
    float ca = cosf(a), sa = sinf(a);
    float bx = cx - hub * 1.3f * ca, by = cy - hub * 1.3f * sa;
    float px = -sa * hub * 0.45f, py = ca * hub * 0.45f;
    draw_tri(l, bx + px, by + py, bx - px, by - py, cx + L * ca, cy + L * sa, r->bar);
    draw_disc(l, cx, cy, (int32_t)hub, theme_color(TC_BG), LV_OPA_COVER);
    draw_arc(l, cx, cy, (int32_t)hub, (int32_t)fmaxf(2, 2.5f * s), 0, 360, r->bar, LV_OPA_COVER, false);
    // min and max markers on the rim
    for (int k = 0; k < 2; k++) {
        if (k == 0 ? !r->has_lo : !r->has_pk) continue;
        float am = (a0 + span * (k == 0 ? r->lofrac : r->pkfrac)) * PI_F / 180.0f, rr = R - 1, ri = R - 1 - 7 * s;
        draw_tri(l, cx + rr * cosf(am), cy + rr * sinf(am), cx + ri * cosf(am + 0.04f), cy + ri * sinf(am + 0.04f), cx + ri * cosf(am - 0.04f), cy + ri * sinf(am - 0.04f), r->dim ? theme_color(TC_STALE) : theme_color(TC_DIGITS));
    }
}

// ---------------------------------------------------------------- stock
// Factory NB1 cluster look: cream face, chrome bezel, black ticks on the rim
// with the numerals inside them, a thin red needle on a big dark hub, and the
// zones as blocks in the tick ring (the orange redline). Slots: BG face,
// DIGITS numerals and major ticks, TICK minor ticks and hub, TRACK bezel,
// NEUTRAL needle, LABEL name, UNITS units, zone colours for the rim blocks.
// Mirrors styleStock() in design/pod-preview/pod-preview.html.

// Smallest "nice" major step that gives at most 10 majors over the range, and
// the minor divisions it carries (stock tach: 1000 x 5, speedo: 20 x 4).
static float nice_step(float range, int *minors)
{
    static const float M[] = { 1, 2, 2.5f, 5 };
    static const int   D[] = { 5, 4, 5, 5 };
    for (int e = -2; e <= 6; e++) {
        float p = powf(10.0f, (float)e);
        for (int i = 0; i < 4; i++) if (range <= (10.0f + 1e-3f) * M[i] * p) { *minors = D[i]; return M[i] * p; }
    }
    *minors = 5;
    return range / 10;
}

static void style_stock(lv_layer_t *l, face_t *f, res_t *r)
{
    const ch_def_t *d = r->d;
    int32_t cx = f->cx, cy = f->cy, R = f->R;
    float s = R / 229.0f;
    bool full = f->full;
    const float span = 270, a0 = 135;
    lv_color_t ink = theme_color(TC_DIGITS), tick = theme_color(TC_TICK), chrome = theme_color(TC_TRACK);

    // bezel: a flat ring with a soft sheen on top and a fainter one below reads as chrome without a dark half
    int32_t bz = (int32_t)fmaxf(3, R * 0.045f), hw = (int32_t)fmaxf(1, bz * 0.4f);
    draw_arc(l, cx, cy, R, bz, 0, 360, chrome, LV_OPA_COVER, false);
    draw_arc(l, cx, cy, (int32_t)(R - bz * 0.15f), hw, 200, 340, lv_color_mix(lv_color_white(), chrome, 90), LV_OPA_COVER, false);
    draw_arc(l, cx, cy, (int32_t)(R - bz * 0.15f), hw, 20, 160, lv_color_mix(lv_color_white(), chrome, 45), LV_OPA_COVER, false);
    draw_arc(l, cx, cy, R - bz, (int32_t)fmaxf(1, 1.5f * s), 0, 360, lv_color_mix(lv_color_black(), chrome, 110), LV_OPA_COVER, false);

    // scale in display units: majors at nice multiples, minors between them
    float dmin = channels_to_display(d, d->min), dmax = channels_to_display(d, d->max);
    if (dmax < dmin) { float t = dmin; dmin = dmax; dmax = t; }
    if (dmax - dmin < 1e-6f) dmax = dmin + 1;
    int minors; float step = nice_step(dmax - dmin, &minors), ms = step / minors;
    uint8_t dec = fabsf(step - roundf(step)) < 1e-3f ? 0 : fabsf(step * 10 - roundf(step * 10)) < 1e-3f ? 1 : 2;
    bool thousands = fabsf(dmax) >= 1000;
    float rt1 = R - bz - 2 * s, lmaj = R * 0.085f, lmin = R * 0.045f;
    int j0 = (int)ceilf(dmin / ms - 1e-3f), j1 = (int)floorf(dmax / ms + 1e-3f);
    if (j1 - j0 > 160) j1 = j0 + 160;

    // zone blocks in the tick ring, one per minor division, a hairline of face between them
    if (g_theme.bands && d->zone_count) {
        for (int j = j0; j < j1; j++) {
            float v0 = j * ms, v1 = (j + 1) * ms;
            ch_zone_level_t z = channels_zone_of(d, channels_from_display(d, (v0 + v1) * 0.5f));
            if (z == CH_ZONE_GOOD) continue;
            draw_arc(l, cx, cy, (int32_t)rt1, (int32_t)lmaj, a0 + span * (v0 - dmin) / (dmax - dmin) + 0.5f,
                     a0 + span * (v1 - dmin) / (dmax - dmin) - 0.5f, theme_zone_color(z), LV_OPA_COVER, false);
        }
    }
    // ticks: thick majors in ink, hairline minors; minors inside a zone block are left out
    for (int j = j0; j <= j1; j++) {
        float v = j * ms, fr = (v - dmin) / (dmax - dmin);
        bool major = ((j % minors) + minors) % minors == 0;
        if (!major && g_theme.bands && d->zone_count && channels_zone_of(d, channels_from_display(d, v)) != CH_ZONE_GOOD) continue;
        float a = (a0 + span * fr) * PI_F / 180.0f, ca = cosf(a), sa = sinf(a), r0 = rt1 - (major ? lmaj : lmin);
        draw_line(l, cx + r0 * ca, cy + r0 * sa, cx + rt1 * ca, cy + rt1 * sa,
                  major ? (int32_t)fmaxf(2, R * 0.022f) : (int32_t)fmaxf(1, R * 0.008f), major ? ink : tick, false);
        if (major && (full || R > 70)) {
            char b[16];
            if (thousands) snprintf(b, sizeof b, step >= 1000 ? "%.0f" : "%.1f", (double)(v / 1000)); else fmt_val(b, sizeof b, v, dec);
            float rn = r0 - R * 0.105f;
            draw_text(l, b, nfont(full ? 40 : 13 * R / 90.0f), ink, cx + (int32_t)(rn * ca), cy + (int32_t)(rn * sa), 0, LV_TEXT_ALIGN_CENTER);
        }
    }

    // name and units under the hub ("x1000 rpm"), the value in an odometer-style
    // window at the bottom, min/max under that
    int lab = full ? 20 : (int)(11 * R / 90.0f);
    draw_text(l, d->name, nfont(lab), r->dim ? theme_color(TC_STALE) : theme_color(TC_LABEL), cx, cy + (int32_t)(R * 0.30f), 1, LV_TEXT_ALIGN_CENTER);
    char ub[32]; const char *u = units_for(d);
    if (thousands) snprintf(ub, sizeof ub, "x1000%s%s", u[0] ? " " : "", u); else snprintf(ub, sizeof ub, "%s", u);
    if (ub[0]) draw_text(l, ub, nfont(full ? 16 : 9 * R / 90.0f), theme_color(TC_UNITS), cx, cy + (int32_t)(R * 0.42f), 0, LV_TEXT_ALIGN_CENTER);
    int32_t ww = (int32_t)(R * 0.46f), wh = (int32_t)(R * 0.17f), wy = cy + (int32_t)(R * 0.62f);
    draw_rect(l, cx - ww / 2, wy - wh / 2, ww, wh, wh / 4, lv_color_mix(tick, theme_color(TC_BG), 28), LV_OPA_COVER);
    draw_text(l, r->str, dfont(full ? 30 : fmaxf(12, 13 * R / 90.0f)), r->digits, cx, wy, 0, LV_TEXT_ALIGN_CENTER);
    { char b[40]; if (minmax_text(r, b, sizeof b)) draw_text(l, b, mfont(full ? 13 * s : 9 * R / 90.0f), theme_color(TC_UNITS), cx, cy + (int32_t)(R * 0.80f), full ? 1 : 0, LV_TEXT_ALIGN_CENTER); }

    // needle: thin, in the neutral colour whatever the zone, on a big dark hub
    float a = (a0 + span * r->frac) * PI_F / 180.0f, ca = cosf(a), sa = sinf(a);
    float L = rt1 - lmaj - R * 0.03f, hub = fmaxf(6, R * 0.13f), nw = fmaxf(1.5f, R * 0.016f);
    lv_color_t nc = r->dim ? theme_color(TC_STALE) : theme_color(TC_NEUTRAL);
    draw_tri(l, cx - sa * nw, cy + ca * nw, cx + sa * nw, cy - ca * nw, cx + L * ca, cy + L * sa, nc);
    draw_line(l, cx, cy, cx + (L - 2 * s) * ca, cy + (L - 2 * s) * sa, (int32_t)fmaxf(2, 2.4f * s), nc, true);
    draw_disc(l, cx, cy, (int32_t)hub, tick, LV_OPA_COVER);
    draw_arc(l, cx, cy, (int32_t)hub, (int32_t)fmaxf(1, 2 * s), 0, 360, lv_color_mix(lv_color_white(), tick, 70), LV_OPA_COVER, false);
    // min and max markers just inside the bezel
    for (int k = 0; k < 2; k++) {
        if (k == 0 ? !r->has_lo : !r->has_pk) continue;
        float am = (a0 + span * (k == 0 ? r->lofrac : r->pkfrac)) * PI_F / 180.0f, rr = R - bz - 1, ri = rr - 7 * s;
        draw_tri(l, cx + rr * cosf(am), cy + rr * sinf(am), cx + ri * cosf(am + 0.05f), cy + ri * sinf(am + 0.05f),
                 cx + ri * cosf(am - 0.05f), cy + ri * sinf(am - 0.05f), nc);
    }
}

// ---------------------------------------------------------------- numeric
// Digits as large as the cell allows, coloured by zone. No bar.
static void style_numeric(lv_layer_t *l, face_t *f, res_t *r)
{
    const ch_def_t *d = r->d;
    int32_t cx = f->cx, cy = f->cy, R = f->R;
    float s = R / 229.0f;
    bool full = f->full;
    int lab = full ? 20 : (int)(13 * R / 90.0f);
    draw_text(l, d->name, lfont(lab), r->dim ? theme_color(TC_STALE) : theme_color(TC_LABEL), cx, cy - (int32_t)(R * (full ? 0.5f : 0.56f)), lab / 5, LV_TEXT_ALIGN_CENTER);

    // Biggest digits that fit the cell width.
    const lv_font_t *vf = fit_digits(r->str, full ? 150 : R * 0.9f, (int32_t)(R * 1.7f));
    lv_color_t col = r->dim ? theme_color(TC_STALE) : r->flash ? theme_color(TC_BAD) : (d->zone_count ? r->bar : theme_color(TC_DIGITS));
    draw_text(l, r->str, vf, col, cx, cy + (int32_t)(R * (full ? 0.02f : 0.06f)), 0, LV_TEXT_ALIGN_CENTER);
    draw_text(l, units_for(d), ufont(full ? 20 : (int)(11 * R / 90.0f)), theme_color(TC_UNITS), cx, cy + (int32_t)(R * (full ? 0.36f : 0.62f)), 0, LV_TEXT_ALIGN_CENTER);
    if (!full) { char b[40]; if (minmax_text(r, b, sizeof b)) draw_text(l, b, mfont(9 * R / 90.0f), theme_color(TC_UNITS), cx, cy + (int32_t)(R * 0.84f), 0, LV_TEXT_ALIGN_CENTER); }
    if (full) {
        char b[40];
        if (minmax_text(r, b, sizeof b)) draw_text(l, b, mfont(13 * s), theme_color(TC_UNITS), cx, cy + (int32_t)(R * 0.62f), 1, LV_TEXT_ALIGN_CENTER);
    }
}

// ---------------------------------------------------------------- object
static void draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    face_t *f = lv_obj_get_user_data(obj);
    if (!f || !channels_def(f->ch)) return;
    lv_layer_t *l = lv_event_get_layer(e);
    lv_area_t co; lv_obj_get_coords(obj, &co);
    f->cx = (co.x1 + co.x2 + 1) / 2; f->cy = (co.y1 + co.y2 + 1) / 2;

    res_t r; resolve(f, &r);
    int style = f->style_override >= 0 ? f->style_override : g_theme.style;
    switch (style) {
    case STYLE_DIAL:    style_dial(l, f, &r); break;
    case STYLE_NUMERIC: style_numeric(l, f, &r); break;
    case STYLE_STOCK:   style_stock(l, f, &r); break;
    default:            style_ring(l, f, &r); break;
    }
    if (f->focused) draw_arc(l, f->cx, f->cy, f->R + 6, 2, 0, 360, theme_color(TC_DIGITS), 150, false);
}

static void delete_cb(lv_event_t *e) { lv_free(lv_event_get_user_data(e)); }

lv_obj_t *face_create(lv_obj_t *parent, ch_id_t ch, int32_t cx, int32_t cy, int32_t radius, bool full)
{
    face_t *f = lv_malloc_zeroed(sizeof(face_t));
    f->ch = ch; f->R = radius; f->full = full; f->style_override = -1;
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

void face_set_channel(lv_obj_t *face, ch_id_t ch) { face_t *f = lv_obj_get_user_data(face); if (f) { f->ch = ch; lv_obj_invalidate(face); } }
ch_id_t face_channel(lv_obj_t *face)             { face_t *f = lv_obj_get_user_data(face); return f ? f->ch : CH_RPM; }
void face_set_style(lv_obj_t *face, int style)   { face_t *f = lv_obj_get_user_data(face); if (f) { f->style_override = style; lv_obj_invalidate(face); } }
void face_set_focused(lv_obj_t *face, bool on)   { face_t *f = lv_obj_get_user_data(face); if (f && f->focused != on) { f->focused = on; lv_obj_invalidate(face); } }
void face_refresh(lv_obj_t *face, int64_t now_us){ face_t *f = lv_obj_get_user_data(face); if (f) { f->now_us = now_us; lv_obj_invalidate(face); } }
