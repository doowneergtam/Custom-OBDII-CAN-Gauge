// Included at the end of face.c: dial, stock and numeric styles plus the object
// plumbing. Split only to keep each file a readable size; shares statics.

// ---------------------------------------------------------------- dial
static void style_dial(lv_layer_t *l, face_t *f, res_t *r)
{
    const ch_def_t *d = r->d;
    int32_t cx = f->cx, cy = f->cy, R = f->R;
    float s = R / 229.0f;
    bool full = f->full;
    const float span = 270, a0 = sweep0(135);
    const theme_ticks_t *tk = &g_theme.ticks;
    int per = tk->min_n ? tk->min_n : (full ? 5 : 4), majors = tk->maj_n ? tk->maj_n : (full ? 10 : 5);
    int n = per * majors;
    int32_t bw = (int32_t)fmaxf(2, R * 0.032f * g_theme.bar_scale);

    if (el(EL_BAR)) draw_arc(l, cx, cy, R - 2, 2, a0, a0 + span, theme_color(TC_TRACK), LV_OPA_COVER, false);
    if (g_theme.bands && el(EL_BAR)) arc_bands(l, d, cx, cy, R - 4 - bw / 2, bw, a0, span, 230);
    for (int i = 0; i <= n && el(EL_TICKS); i++) {
        float a = (a0 + span * i / n) * PI_F / 180.0f;
        bool major = i % per == 0;
        float r1 = R - 6 - bw - 2 + dyp(EL_TICKS, R) * 0, r0 = r1 - (major ? R * 0.1f * tk->maj_len : R * 0.05f * tk->min_len);
        draw_line(l, cx + r0 * cosf(a), cy + r0 * sinf(a), cx + r1 * cosf(a), cy + r1 * sinf(a),
                  major ? (int32_t)fmaxf(1, 2.2f * s * tk->maj_w) : (int32_t)fmaxf(1, 1.0f * tk->min_w), major ? theme_color(TC_DIGITS) : theme_color(TC_TICK), false);
        if (major && (full || R > 70)) {
            float v = channels_to_display(d, d->min + (d->max - d->min) * i / n);
            char b[16];
            if (fabsf(channels_to_display(d, d->max)) >= 1000) snprintf(b, sizeof b, "%.0f", (double)(v / 1000)); else fmt_val(b, sizeof b, v, 0);
            float rt = r0 - R * 0.09f;
            draw_text(l, b, font_px(full ? 18 : (int)(10 * R / 90.0f)), theme_color(TC_TICK), cx + (int32_t)(rt * cosf(a)), cy + (int32_t)(rt * sinf(a)), 0, LV_TEXT_ALIGN_CENTER);
        }
    }
    int lab = full ? 18 : (int)(13 * R / 90.0f);
    if (el(EL_LABEL)) draw_text(l, d->name, lfont(lab), r->dim ? theme_color(TC_STALE) : theme_color(TC_LABEL), cx, cy - (int32_t)(R * 0.4f) + dyp(EL_LABEL, R), lab / 5, LV_TEXT_ALIGN_CENTER);
    int vpx = full ? 48 : (int)fmaxf(18, R * 0.3f);
    if (el(EL_DIGITS)) draw_text(l, r->str, fit_digits(r->str, vpx, (int32_t)(R * 1.1f)), r->digits, cx, cy + (int32_t)(R * 0.5f) + dyp(EL_DIGITS, R), 0, LV_TEXT_ALIGN_CENTER);
    if (el(EL_UNITS)) draw_text(l, units_for(d), ufont(full ? 16 : (int)(11 * R / 90.0f)), theme_color(TC_UNITS), cx, cy + (int32_t)(R * 0.5f) + (full ? vpx * 3 / 4 : (int32_t)(R * 0.2f)) + dyp(EL_UNITS, R), 0, LV_TEXT_ALIGN_CENTER);
    if (el(EL_MINMAX)) { char b[40]; if (minmax_text(r, b, sizeof b)) draw_text(l, b, mfont(full ? 13 * s : 9 * R / 90.0f), theme_color(TC_UNITS), cx, cy + (int32_t)(R * (full ? 0.86f : 0.74f)) + dyp(EL_MINMAX, R), 0, LV_TEXT_ALIGN_CENTER); }

    // needle: tapered triangle in the bar colour, hub ring
    float a = (a0 + span * r->frac) * PI_F / 180.0f, L = (R - 12 - bw - R * 0.1f) * g_theme.needle_len, hub = fmaxf(3, R * 0.06f * g_theme.hub_scale);
    float ca = cosf(a), sa = sinf(a);
    float bx = cx - hub * 1.3f * ca, by = cy - hub * 1.3f * sa;
    float px = -sa * hub * 0.45f * g_theme.needle_w, py = ca * hub * 0.45f * g_theme.needle_w;
    if (el(EL_NEEDLE)) {
        draw_tri(l, bx + px, by + py, bx - px, by - py, cx + L * ca, cy + L * sa, r->bar);
        draw_disc(l, cx, cy, (int32_t)hub, theme_color(TC_BG), LV_OPA_COVER);
        draw_arc(l, cx, cy, (int32_t)hub, (int32_t)fmaxf(2, 2.5f * s), 0, 360, r->bar, LV_OPA_COVER, false);
    }
    // min and max markers on the rim
    for (int k = 0; k < 2 && el(EL_MINMAX); k++) {
        if (k == 0 ? !r->has_lo : !r->has_pk) continue;
        float am = (a0 + span * (k == 0 ? r->lofrac : r->pkfrac)) * PI_F / 180.0f, rr = R - 1, ri = R - 1 - 7 * s;
        draw_tri(l, cx + rr * cosf(am), cy + rr * sinf(am), cx + ri * cosf(am + 0.04f), cy + ri * sinf(am + 0.04f), cx + ri * cosf(am - 0.04f), cy + ri * sinf(am - 0.04f), r->dim ? theme_color(TC_STALE) : theme_color(TC_DIGITS));
    }
}

// ---------------------------------------------------------------- stock
// Factory NB1 cluster looks, proportions measured off the real faces (R is the
// face radius). Stock 1 is the tach and speedo: chrome bezel, ticks on the rim
// with the numerals inside, a thin red needle on a big dark hub, the value in an
// odometer-style window. Stock 2 is the small fuel, oil and temperature gauge:
// the pivot sits below centre, the needle sweeps 90 degrees across the top over
// seven ticks, and the range ends are lettered past the sweep like E and F.
// Both are black and white like the factory faces: the zone colour (blocks in
// the tick ring, the min/max pointers) only appears with the bands switch, which
// choosing a Stock style turns off. Slots: BG face, DIGITS numerals and major
// ticks, TICK minor ticks and hub, TRACK bezel, NEUTRAL needle, LABEL name,
// UNITS units, zone colours for the bands. Both mirror styleStock() and
// styleStock2() in design/pod-preview/pod-preview.html.

// Smallest "nice" major step that gives at most 10 majors over the range, and
// the minor divisions it carries (stock tach: 1000 x 5, speedo: 20 x 4).
static float nice_step(float range, int *minors)
{
    static const float M[] = { 1, 2, 2.5f, 5 };
    static const int   D[] = { 10, 10, 10, 10 };   // factory faces: ten minors per major, the fifth drawn medium
    for (int e = -2; e <= 6; e++) {
        float p = powf(10.0f, (float)e);
        for (int i = 0; i < 4; i++) if (range <= (10.0f + 1e-3f) * M[i] * p) { *minors = D[i]; return M[i] * p; }
    }
    *minors = 5;
    return range / 10;
}

// Chrome bezel, 0.04R wide: a flat ring with a soft sheen on top and a fainter
// one below, so there is no dark half on any preset. Returns its width.
static int32_t stock_bezel(lv_layer_t *l, int32_t cx, int32_t cy, int32_t R, float s)
{
    lv_color_t chrome = theme_color(TC_TRACK);
    int32_t bz = (int32_t)fmaxf(3, R * 0.04f), hw = (int32_t)fmaxf(1, bz * 0.4f);
    draw_arc(l, cx, cy, R, bz, 0, 360, chrome, LV_OPA_COVER, false);
    draw_arc(l, cx, cy, (int32_t)(R - bz * 0.15f), hw, 200, 340, lv_color_mix(lv_color_white(), chrome, 90), LV_OPA_COVER, false);
    draw_arc(l, cx, cy, (int32_t)(R - bz * 0.15f), hw, 20, 160, lv_color_mix(lv_color_white(), chrome, 45), LV_OPA_COVER, false);
    draw_arc(l, cx, cy, R - bz, (int32_t)fmaxf(1, 1.5f * s), 0, 360, lv_color_mix(lv_color_black(), chrome, 110), LV_OPA_COVER, false);
    return bz;
}

// Thin needle (0.026R at the base, 0.013R stem) from the pivot (px, py) at angle a (radians),
// with the big dark hub on top of it.
static void stock_needle(lv_layer_t *l, float px, float py, float a, float L, float hub, int32_t R, float s, lv_color_t nc)
{
    L *= g_theme.needle_len; hub *= g_theme.hub_scale;
    float ca = cosf(a), sa = sinf(a);
    float wb = fmaxf(2.0f, R * 0.036f * g_theme.needle_w);      // base half-width where it leaves the hub
    float wt = fmaxf(1.0f, R * 0.011f * g_theme.needle_w);      // stem half-width
    float Lb = L * 0.52f;                                        // the thick section ends about half way out
    lv_color_t tick = theme_color(TC_TICK);
    // tail behind the hub, then the thick section tapering to the stem, then the stem to the tip
    float tl = fmaxf(hub * 1.5f, R * 0.2f);
    draw_tri(l, px - sa * wb, py + ca * wb, px + sa * wb, py - ca * wb, px - tl * ca, py - tl * sa, nc);
    draw_tri(l, px - sa * wb, py + ca * wb, px + sa * wb, py - ca * wb, px + Lb * ca - sa * wt, py + Lb * sa + ca * wt, nc);
    draw_tri(l, px - sa * wb, py + ca * wb, px + Lb * ca - sa * wt, py + Lb * sa + ca * wt, px + Lb * ca + sa * wt, py + Lb * sa - ca * wt, nc);
    draw_tri(l, px + Lb * ca - sa * wt, py + Lb * sa + ca * wt, px + Lb * ca + sa * wt, py + Lb * sa - ca * wt, px + L * ca, py + L * sa, nc);
    draw_line(l, px + Lb * ca, py + Lb * sa, px + (L - 2 * s) * ca, py + (L - 2 * s) * sa, (int32_t)fmaxf(1, 2 * wt), nc, true);
    draw_disc(l, (int32_t)px, (int32_t)py, (int32_t)hub, tick, LV_OPA_COVER);
    draw_arc(l, (int32_t)px, (int32_t)py, (int32_t)hub, (int32_t)fmaxf(1, 2 * s), 0, 360, lv_color_mix(lv_color_white(), tick, 70), LV_OPA_COVER, false);
}

// Small min/max pointer: tip at radius rr from (px, py), angle am.
static void stock_marker(lv_layer_t *l, float px, float py, float am, float rr, float s, lv_color_t c)
{
    float ri = rr - 7 * s;
    draw_tri(l, px + rr * cosf(am), py + rr * sinf(am), px + ri * cosf(am + 0.05f), py + ri * sinf(am + 0.05f),
             px + ri * cosf(am - 0.05f), py + ri * sinf(am - 0.05f), c);
}

// The value in an odometer-style window centred at (x, y), min/max under it.
static void stock_window(lv_layer_t *l, face_t *f, res_t *r, int32_t x, int32_t y, int32_t R, float s)
{
    bool full = f->full;
    int32_t ww = (int32_t)(R * 0.46f * g_theme.digit_scale), wh = (int32_t)(R * 0.17f * g_theme.digit_scale);
    int32_t yd = y + dyp(EL_DIGITS, R);
    if (el(EL_WINDOW) && el(EL_DIGITS)) draw_rect(l, x - ww / 2, yd - wh / 2, ww, wh, wh / 4, lv_color_mix(theme_color(TC_TICK), theme_color(TC_BG), 28), LV_OPA_COVER);
    if (el(EL_DIGITS)) draw_text(l, r->str, dfont(full ? 30 : fmaxf(12, 13 * R / 90.0f)), r->digits, x, yd, 0, LV_TEXT_ALIGN_CENTER);
    char b[40];
    if (el(EL_MINMAX) && minmax_text(r, b, sizeof b)) draw_text(l, b, mfont(full ? 13 * s : 9 * R / 90.0f), theme_color(TC_UNITS), x, y + (int32_t)(R * 0.16f) + dyp(EL_MINMAX, R), full ? 1 : 0, LV_TEXT_ALIGN_CENTER);
}

// Stock 1: the tach and speedo. Majors 0.115R long on the rim, numerals 0.14R
// tall on a 0.69R circle, hub 0.145R, needle to 0.87R, label where the factory
// prints "x1000r/min".
static void style_stock(lv_layer_t *l, face_t *f, res_t *r)
{
    const ch_def_t *d = r->d;
    int32_t cx = f->cx, cy = f->cy, R = f->R;
    float s = R / 229.0f;
    bool full = f->full;
    const float span = 270, a0 = sweep0(135);
    lv_color_t ink = theme_color(TC_DIGITS), tick = theme_color(TC_TICK);
    const theme_ticks_t *tk = &g_theme.ticks;
    int32_t bz = stock_bezel(l, cx, cy, R, s);

    // scale in display units: majors at nice multiples, minors between them
    float dmin = channels_to_display(d, d->min), dmax = channels_to_display(d, d->max);
    if (dmax < dmin) { float t = dmin; dmin = dmax; dmax = t; }
    if (dmax - dmin < 1e-6f) dmax = dmin + 1;
    int minors; float step = nice_step(dmax - dmin, &minors);
    if (tk->maj_n) step = (dmax - dmin) / tk->maj_n;
    if (tk->min_n) minors = tk->min_n;
    float ms = step / minors;
    uint8_t dec = fabsf(step - roundf(step)) < 1e-3f ? 0 : fabsf(step * 10 - roundf(step * 10)) < 1e-3f ? 1 : 2;
    bool thousands = fabsf(dmax) >= 1000;
    float rt1 = R - bz - 2 * s, lmaj = R * 0.115f * tk->maj_len, lmid = R * 0.085f * tk->maj_len, lmin = R * 0.05f * tk->min_len;
    int j0 = (int)ceilf(dmin / ms - 1e-3f), j1 = (int)floorf(dmax / ms + 1e-3f);
    if (j1 - j0 > 160) j1 = j0 + 160;

    // zone blocks in the tick ring (bands switch only), one per minor division, a hairline of face between them
    if (g_theme.bands && d->zone_count) {
        for (int j = j0; j < j1; j++) {
            float v0 = j * ms, v1 = (j + 1) * ms;
            ch_zone_level_t z = channels_zone_of(d, channels_from_display(d, (v0 + v1) * 0.5f));
            if (z == CH_ZONE_GOOD) continue;
            draw_arc(l, cx, cy, (int32_t)rt1, (int32_t)lmaj, a0 + span * (v0 - dmin) / (dmax - dmin) + 0.5f,
                     a0 + span * (v1 - dmin) / (dmax - dmin) - 0.5f, theme_zone_color(z), LV_OPA_COVER, false);
        }
    }
    // ticks: majors 0.115R long and 0.03R wide in ink, minors 0.06R and 0.012R; minors inside a zone block are left out
    for (int j = j0; j <= j1 && el(EL_TICKS); j++) {
        float v = j * ms, fr = (v - dmin) / (dmax - dmin);
        bool major = ((j % minors) + minors) % minors == 0;
        if (!major && g_theme.bands && d->zone_count && channels_zone_of(d, channels_from_display(d, v)) != CH_ZONE_GOOD) continue;
        bool medium = !major && minors % 2 == 0 && ((j % minors) + minors) % minors == minors / 2;
        float a = (a0 + span * fr) * PI_F / 180.0f, ca = cosf(a), sa = sinf(a), r0 = rt1 - (major ? lmaj : medium ? lmid : lmin);
        draw_line(l, cx + r0 * ca, cy + r0 * sa, cx + rt1 * ca, cy + rt1 * sa,
                  major ? (int32_t)fmaxf(1, R * 0.03f * tk->maj_w) : medium ? (int32_t)fmaxf(1, R * 0.02f * tk->maj_w) : (int32_t)fmaxf(1, R * 0.012f * tk->min_w), major || medium ? ink : tick, false);
        if (major && (full || R > 70)) {
            char b[16];
            if (thousands) snprintf(b, sizeof b, step >= 1000 ? "%.0f" : "%.1f", (double)(v / 1000)); else fmt_val(b, sizeof b, v, dec);
            float rn = r0 - R * 0.17f;
            draw_text(l, b, nfont(full ? 50 : 17 * R / 90.0f), ink, cx + (int32_t)(rn * ca), cy + (int32_t)(rn * sa), 0, LV_TEXT_ALIGN_CENTER);
        }
    }

    // name and units where the factory prints "x1000r/min", the value window and min/max below them
    int lab = full ? 22 : (int)(12 * R / 90.0f);
    if (el(EL_LABEL)) draw_text(l, d->name, nfont(lab), r->dim ? theme_color(TC_STALE) : theme_color(TC_LABEL), cx, cy + (int32_t)(R * 0.44f) + dyp(EL_LABEL, R), 1, LV_TEXT_ALIGN_CENTER);
    char ub[32]; const char *u = units_for(d);
    if (thousands) snprintf(ub, sizeof ub, "x1000%s%s", u[0] ? " " : "", u); else snprintf(ub, sizeof ub, "%s", u);
    if (ub[0] && el(EL_UNITS)) draw_text(l, ub, nfont(full ? 17 : 10 * R / 90.0f), theme_color(TC_UNITS), cx, cy + (int32_t)(R * 0.55f) + dyp(EL_UNITS, R), 0, LV_TEXT_ALIGN_CENTER);
    stock_window(l, f, r, cx, cy + (int32_t)(R * 0.70f), R, s);

    // needle to just inside the tick ring, hub like the factory's; min/max pointers only with the bands
    float a = (a0 + span * r->frac) * PI_F / 180.0f;
    lv_color_t nc = r->dim ? theme_color(TC_STALE) : theme_color(TC_NEUTRAL);
    if (el(EL_NEEDLE)) stock_needle(l, cx, cy, a, R * 0.87f, fmaxf(6, R * 0.145f), R, s, nc);
    if (g_theme.bands && el(EL_MINMAX)) for (int k = 0; k < 2; k++) {
        if (k == 0 ? !r->has_lo : !r->has_pk) continue;
        stock_marker(l, cx, cy, (a0 + span * (k == 0 ? r->lofrac : r->pkfrac)) * PI_F / 180.0f, R - bz - 1, s, nc);
    }
}

// Stock 2: the small fuel, oil pressure and temperature gauges. Pivot 0.30R
// below centre, 90-degree sweep across the top from left to right (E on the
// left, F on the right), seven ticks on a ring 0.92R from the pivot (thick ends,
// a medium middle, four short ones), the range ends lettered past the sweep, hub
// 0.22R, name between the sweep and the hub where the factory puts its icon,
// units under the name, the value window below the hub.
static void style_stock2(lv_layer_t *l, face_t *f, res_t *r)
{
    const ch_def_t *d = r->d;
    int32_t cx = f->cx, cy = f->cy, R = f->R;
    float s = R / 229.0f;
    bool full = f->full;
    const float span = 90, a0 = sweep0(225);   // upper left to upper right, clockwise over the top
    lv_color_t ink = theme_color(TC_DIGITS), tick = theme_color(TC_TICK);
    const theme_ticks_t *tk = &g_theme.ticks;
    stock_bezel(l, cx, cy, R, s);
    float px = cx, py = cy + R * 0.30f, rt = R * 0.92f;
    float lend = R * 0.14f * tk->maj_len, lmid = R * 0.11f * tk->maj_len, lmin = R * 0.07f * tk->min_len;
    int nt = tk->maj_n ? tk->maj_n * (tk->min_n ? tk->min_n : 3) : 6;   // ticks between the ends

    // zone bands as solid arcs in the tick ring (bands switch only), like the red block at H on the factory temperature gauge
    if (g_theme.bands && d->zone_count) arc_bands(l, d, (int32_t)px, (int32_t)py, (int32_t)rt, (int32_t)lend, a0, span, LV_OPA_COVER);
    // seven ticks, all radial to the pivot
    for (int j = 0; j <= nt && el(EL_TICKS); j++) {
        float a = (a0 + span * j / (float)nt) * PI_F / 180.0f, ca = cosf(a), sa = sinf(a);
        int per = tk->maj_n ? (tk->min_n ? tk->min_n : 3) : 3;
        bool end = j == 0 || j == nt, mid = !end && j % per == 0;
        float len = end ? lend : mid ? lmid : lmin;
        int32_t w = end ? (int32_t)fmaxf(1, R * 0.04f * tk->maj_w) : mid ? (int32_t)fmaxf(1, R * 0.028f * tk->maj_w) : (int32_t)fmaxf(1, R * 0.016f * tk->min_w);
        draw_line(l, px + (rt - len) * ca, py + (rt - len) * sa, px + rt * ca, py + rt * sa, w, end || mid ? ink : tick, false);
    }
    // range ends lettered just past the sweep, like E and F
    if (full || R > 70) {
        for (int k = 0; k < 2; k++) {
            float a = (a0 + (k ? span + 12 : -12)) * PI_F / 180.0f;
            char b[16]; fmt_val(b, sizeof b, channels_to_display(d, k ? d->max : d->min), 0);
            draw_text(l, b, nfont(full ? 40 : 15 * R / 90.0f), ink, (int32_t)(px + rt * cosf(a)), (int32_t)(py + rt * sinf(a)), 0, LV_TEXT_ALIGN_CENTER);
        }
    }

    // name between the sweep and the hub, units under it, value window below the hub
    int lab = full ? 22 : (int)(12 * R / 90.0f);
    if (el(EL_LABEL)) draw_text(l, d->name, nfont(lab), r->dim ? theme_color(TC_STALE) : theme_color(TC_LABEL), cx, (int32_t)(py - R * 0.44f) + dyp(EL_LABEL, R), 1, LV_TEXT_ALIGN_CENTER);
    const char *u = units_for(d);
    if (u[0] && el(EL_UNITS)) draw_text(l, u, nfont(full ? 17 : 10 * R / 90.0f), theme_color(TC_UNITS), cx, (int32_t)(py - R * 0.33f) + dyp(EL_UNITS, R), 0, LV_TEXT_ALIGN_CENTER);
    stock_window(l, f, r, cx, cy + (int32_t)(R * 0.64f), R, s);

    // needle from the pivot to just inside the ticks; min/max pointers only with the bands
    float a = (a0 + span * r->frac) * PI_F / 180.0f;
    lv_color_t nc = r->dim ? theme_color(TC_STALE) : theme_color(TC_NEUTRAL);
    if (el(EL_NEEDLE)) stock_needle(l, px, py, a, rt - lend - R * 0.02f, fmaxf(6, R * 0.22f), R, s, nc);
    if (g_theme.bands && el(EL_MINMAX)) for (int k = 0; k < 2; k++) {
        if (k == 0 ? !r->has_lo : !r->has_pk) continue;
        stock_marker(l, px, py, (a0 + span * (k == 0 ? r->lofrac : r->pkfrac)) * PI_F / 180.0f, rt + 8 * s, s, nc);
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
    if (el(EL_LABEL)) draw_text(l, d->name, lfont(lab), r->dim ? theme_color(TC_STALE) : theme_color(TC_LABEL), cx, cy - (int32_t)(R * (full ? 0.5f : 0.56f)) + dyp(EL_LABEL, R), lab / 5, LV_TEXT_ALIGN_CENTER);

    // Biggest digits that fit the cell width.
    const lv_font_t *vf = fit_digits(r->str, full ? 150 : R * 0.9f, (int32_t)(R * 1.7f));
    lv_color_t col = r->dim ? theme_color(TC_STALE) : r->flash ? theme_color(TC_BAD) : (d->zone_count ? r->bar : theme_color(TC_DIGITS));
    if (el(EL_DIGITS)) draw_text(l, r->str, vf, col, cx, cy + (int32_t)(R * (full ? 0.02f : 0.06f)) + dyp(EL_DIGITS, R), 0, LV_TEXT_ALIGN_CENTER);
    if (el(EL_UNITS)) draw_text(l, units_for(d), ufont(full ? 20 : (int)(11 * R / 90.0f)), theme_color(TC_UNITS), cx, cy + (int32_t)(R * (full ? 0.36f : 0.62f)) + dyp(EL_UNITS, R), 0, LV_TEXT_ALIGN_CENTER);
    if (el(EL_MINMAX)) {
        char b[40];
        if (minmax_text(r, b, sizeof b)) draw_text(l, b, mfont(full ? 13 * s : 9 * R / 90.0f), theme_color(TC_UNITS), cx, cy + (int32_t)(R * (full ? 0.62f : 0.84f)) + dyp(EL_MINMAX, R), full ? 1 : 0, LV_TEXT_ALIGN_CENTER);
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
    case STYLE_STOCK2:  style_stock2(l, f, &r); break;
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
// Redraw only what moved. A face is re-rendered 25 times a second; rendering
// the whole 466 px disc each time is what made the needle stutter and the
// stripes tear. Now a tick that changes nothing redraws nothing, and a value
// change on a needle style redraws just the needle sweep and the reading.
static void inval(lv_obj_t *face, int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    lv_area_t co; lv_obj_get_coords(face, &co);
    lv_area_t a = { co.x1 + x1, co.y1 + y1, co.x1 + x2, co.y1 + y2 };
    lv_obj_invalidate_area(face, &a);
}
void face_refresh(lv_obj_t *face, int64_t now_us)
{
    face_t *f = lv_obj_get_user_data(face);
    if (!f) return;
    f->now_us = now_us;
    const ch_def_t *d = channels_def(f->ch);
    const ch_state_t *st = channels_state(f->ch);
    ch_freshness_t fr = channels_freshness(f->ch, now_us);
    float frac = (fr == CH_DEAD) ? 0 : channels_frac(d, st->value);
    bool bad = fr == CH_FRESH && channels_zone_of(d, st->value) == CH_ZONE_BAD;
    bool flash = bad && ((now_us / FLASH_US) & 1) == 0;
    int style = f->style_override >= 0 ? f->style_override : g_theme.style;
    bool needle = style == STYLE_DIAL || style == STYLE_STOCK || style == STYLE_STOCK2;
    bool changed = frac != f->last_frac || st->value != f->last_value || fr != f->last_fresh || flash != f->last_flash
                   || st->peak != f->last_peak || st->floor != f->last_floor || f->focused != f->last_focus;
    if (!changed && now_us - f->last_full_us < 1000000) return;    // nothing to show; a full pass once a second as a safety net
    if (!needle || !changed) { lv_obj_invalidate(face); f->last_full_us = now_us; }
    else {
        // needle sweep between the old and new angle, from the pivot out to the rim, plus the hub and the reading window
        int32_t pad = 10, R = f->R, ox = R + pad, oy = R + pad;         // object-local centre
        float span = style == STYLE_STOCK2 ? 90 : 270, a0 = g_theme.sweep_rot + (style == STYLE_STOCK2 ? 225 : 135);
        float py = style == STYLE_STOCK2 ? oy + R * 0.30f : oy;
        float fa = fminf(f->last_frac, frac), fb = fmaxf(f->last_frac, frac);
        float Lr = R * 1.02f;
        int32_t x1 = (int32_t)ox, x2 = (int32_t)ox, y1 = (int32_t)py, y2 = (int32_t)py;
        int steps = (int)fmaxf(2, (fb - fa) * span / 10) + 1;
        for (int i = 0; i <= steps; i++) {
            float a = (a0 + span * (fa + (fb - fa) * i / steps)) * PI_F / 180.0f;
            for (int k = -1; k <= 1; k += 2) {
                int32_t x = (int32_t)(ox + k * Lr * cosf(a)), y = (int32_t)(py + k * Lr * sinf(a));
                if (k < 0) { x = (int32_t)(ox - R * 0.3f * cosf(a)); y = (int32_t)(py - R * 0.3f * sinf(a)); }
                x1 = x < x1 ? x : x1; x2 = x > x2 ? x : x2; y1 = y < y1 ? y : y1; y2 = y > y2 ? y : y2;
            }
        }
        int32_t hub = (int32_t)(R * 0.3f);
        inval(face, x1 - 8, y1 - 8, x2 + 8, y2 + 8);
        inval(face, (int32_t)ox - hub, (int32_t)py - hub, (int32_t)ox + hub, (int32_t)py + hub);
        inval(face, (int32_t)(ox - R * 0.6f), (int32_t)(oy + R * 0.25f), (int32_t)(ox + R * 0.6f), (int32_t)(oy + R * 1.0f));   // reading, units, min/max
        if (style == STYLE_DIAL) inval(face, 0, 0, 2 * (R + pad), 2 * (R + pad));   // dial min/max markers sit on the rim: keep it simple
    }
    f->last_frac = frac; f->last_value = st->value; f->last_fresh = fr; f->last_flash = flash;
    f->last_peak = st->peak; f->last_floor = st->floor; f->last_focus = f->focused;
}
