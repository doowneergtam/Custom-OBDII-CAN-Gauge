// Included by face.c after the primitives: a small anti-aliased rasteriser that
// writes the needle and hub straight into LVGL's strip buffer.
//
// Why: LVGL's software triangles evaluate edge masks over the whole bounding
// box of each triangle, which made the needle about 6 ms of every frame. The
// needle outline is one convex polygon, so per scan line the covered span is
// solved from its edges: the interior is a plain fill and only the one-pixel
// band at each end is anti-aliased. Discs use squared distances, no sqrt.
//
// Safe because, with no OS, LVGL renders every draw task inside the call that
// creates it: by the time a style reaches the needle, the cached background has
// already been blitted into the buffer, and anything drawn after (min/max
// pointers, text) lands on top. Used only for RGB565 layers; anything else falls
// back to the LVGL primitives.

static bool fast_ok(lv_layer_t *l)
{
    return l && s_pass != P_MEASURE && gate() && l->draw_buf && l->draw_buf->data && l->color_format == LV_COLOR_FORMAT_RGB565;
}

#define FMIN(a, b) ((a) < (b) ? (a) : (b))
#define FMAX(a, b) ((a) > (b) ? (a) : (b))
static inline int32_t ifloor(float v) { int32_t i = (int32_t)v; return (v < i) ? i - 1 : i; }

static inline uint16_t px565(lv_color_t c) { return (uint16_t)(((c.red & 0xF8) << 8) | ((c.green & 0xFC) << 3) | (c.blue >> 3)); }
static inline void blend565(uint16_t *d, uint16_t s, int32_t a)   // a = 0..256
{
    if (a >= 256) { *d = s; return; }
    if (a <= 0) return;
    uint32_t dv = *d, ia = 256 - a;
    uint32_t r = (((s >> 11) & 31) * a + ((dv >> 11) & 31) * ia) >> 8;
    uint32_t g = (((s >> 5) & 63) * a + ((dv >> 5) & 63) * ia) >> 8;
    uint32_t b = ((s & 31) * a + (dv & 31) * ia) >> 8;
    *d = (uint16_t)((r << 11) | (g << 5) | b);
}

static bool fast_window(lv_layer_t *l, int32_t *x1, int32_t *y1, int32_t *x2, int32_t *y2)
{
    const lv_area_t *c = &l->_clip_area, *b = &l->buf_area;
    *x1 = LV_MAX(c->x1, b->x1); *y1 = LV_MAX(c->y1, b->y1); *x2 = LV_MIN(c->x2, b->x2); *y2 = LV_MIN(c->y2, b->y2);
    return *x1 <= *x2 && *y1 <= *y2;
}
static inline uint16_t *fast_row(lv_layer_t *l, int32_t y)
{
    return (uint16_t *)(l->draw_buf->data + (size_t)(y - l->buf_area.y1) * l->draw_buf->header.stride) - l->buf_area.x1;
}

// Convex polygon, any winding, up to 8 vertices.
static void fast_poly_(lv_layer_t *l, const float *xs, const float *ys, int n, lv_color_t col);
static void fast_poly(lv_layer_t *l, const float *xs, const float *ys, int n, lv_color_t col) { PT(7, fast_poly_(l, xs, ys, n, col)); }
static void fast_poly_(lv_layer_t *l, const float *xs, const float *ys, int n, lv_color_t col)
{
    int32_t wx1, wy1, wx2, wy2;
    if (n < 3 || n > 8 || !fast_window(l, &wx1, &wy1, &wx2, &wy2)) return;
    float area = 0, miny = 1e9f, maxy = -1e9f, minx = 1e9f, maxx = -1e9f;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n; area += xs[i] * ys[j] - xs[j] * ys[i];
        miny = FMIN(miny, ys[i]); maxy = FMAX(maxy, ys[i]); minx = FMIN(minx, xs[i]); maxx = FMAX(maxx, xs[i]);
    }
    if (maxx < wx1 - 1 || minx > wx2 + 1 || maxy < wy1 - 1 || miny > wy2 + 1) return;
    float sg = area > 0 ? 1.0f : -1.0f;
    float A[8], B[8], C[8]; int m = 0;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float dx = xs[j] - xs[i], dy = ys[j] - ys[i], len2 = dx * dx + dy * dy;
        if (len2 < 1e-6f) continue;
        float inv = 1.0f / sqrtf(len2);
        A[m] = -dy * inv * sg; B[m] = dx * inv * sg; C[m] = -(A[m] * xs[i] + B[m] * ys[i]); m++;
    }
    int32_t y1 = LV_MAX(wy1, ifloor(miny) - 1), y2 = LV_MIN(wy2, ifloor(maxy) + 1);
    uint16_t s = px565(col);
    for (int32_t y = y1; y <= y2; y++) {
        float fy = y + 0.5f;
        // outer span: every edge d >= -0.5; inner span: every edge d >= 0.5 (fully covered)
        float ol = (float)wx1, orr = (float)wx2 + 1.0f, il = -1e9f, ir = 1e9f;
        bool empty = false;
        for (int i = 0; i < m; i++) {
            float base = B[i] * fy + C[i];
            if (A[i] > 1e-6f)       { ol = FMAX(ol, (-0.5f - base) / A[i]); il = FMAX(il, (0.5f - base) / A[i]); }
            else if (A[i] < -1e-6f) { orr = FMIN(orr, (-0.5f - base) / A[i]); ir = FMIN(ir, (0.5f - base) / A[i]); }
            else if (base < -0.5f)  { empty = true; break; }
            else if (base < 0.5f)   { il = 1e9f; }   // horizontal edge cuts through this row: no fully covered pixels
        }
        if (empty || ol >= orr) continue;
        int32_t xa = LV_MAX(wx1, ifloor(ol - 0.5f)), xe = LV_MIN(wx2, ifloor(orr - 0.5f) + 1);
        int32_t ia = ifloor(il - 0.5f) + 1, ie = ifloor(ir - 0.5f);   // pixel centres fully inside
        if (ia < xa) ia = xa;
        if (ie > xe) ie = xe;
        uint16_t *row = fast_row(l, y);
        for (int32_t x = xa; x <= xe; x++) {
            if (x >= ia && x <= ie) {   // interior run: plain store
                for (; x <= ie; x++) row[x] = s;
                x = ie;
                continue;
            }
            float fx = x + 0.5f, d = 1e9f;
            for (int i = 0; i < m; i++) { float v = A[i] * fx + B[i] * fy + C[i]; d = v < d ? v : d; }
            if (d > -0.5f) blend565(&row[x], s, d >= 0.5f ? 256 : (int32_t)((d + 0.5f) * 256.0f));
        }
    }
}

// Filled disc, or a ring when w > 0 (outer radius r, width w). Coverage from
// squared distance: d ~ (r^2 - dist^2) / 2r near the edge.
static void fast_circle_(lv_layer_t *l, float cx, float cy, float r, float w, lv_color_t col);
static void fast_circle(lv_layer_t *l, float cx, float cy, float r, float w, lv_color_t col) { PT(8, fast_circle_(l, cx, cy, r, w, col)); }
static void fast_circle_(lv_layer_t *l, float cx, float cy, float r, float w, lv_color_t col)
{
    int32_t wx1, wy1, wx2, wy2;
    if (r <= 0 || !fast_window(l, &wx1, &wy1, &wx2, &wy2)) return;
    if (cx + r < wx1 - 1 || cx - r > wx2 + 1 || cy + r < wy1 - 1 || cy - r > wy2 + 1) return;
    float ri = w > 0 ? r - w : 0;
    float ro2 = (r + 0.5f) * (r + 0.5f), rof2 = (r - 0.5f) * (r - 0.5f), inv2r = 1.0f / (2.0f * r);
    float ri2 = ri > 0.5f ? (ri - 0.5f) * (ri - 0.5f) : -1.0f, rif2 = ri > 0 ? (ri + 0.5f) * (ri + 0.5f) : -1.0f, inv2ri = ri > 0 ? 1.0f / (2.0f * ri) : 0;
    int32_t y1 = LV_MAX(wy1, ifloor(cy - r) - 1), y2 = LV_MIN(wy2, ifloor(cy + r) + 1);
    uint16_t s = px565(col);
    for (int32_t y = y1; y <= y2; y++) {
        float dy = y + 0.5f - cy, dy2 = dy * dy;
        if (dy2 >= ro2) continue;
        float hw = sqrtf(ro2 - dy2);   // one sqrt per row
        int32_t xa = LV_MAX(wx1, ifloor(cx - hw)), xe = LV_MIN(wx2, ifloor(cx + hw));
        uint16_t *row = fast_row(l, y);
        for (int32_t x = xa; x <= xe; x++) {
            float dx = x + 0.5f - cx, d2 = dx * dx + dy2;
            if (d2 >= ro2) continue;
            if (ri > 0 && d2 <= ri2) continue;   // inside the hole of a ring
            int32_t a = 256;
            if (d2 > rof2) a = (int32_t)(((r * r - d2) * inv2r + 0.5f) * 256.0f);
            if (ri > 0 && d2 < rif2) { int32_t b = (int32_t)(((d2 - ri * ri) * inv2ri + 0.5f) * 256.0f); a = b < a ? b : a; }
            blend565(&row[x], s, a);
        }
    }
}
