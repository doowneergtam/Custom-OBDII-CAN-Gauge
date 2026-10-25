// hal_board for the Waveshare ESP32-S3-Touch-AMOLED-1.75 pod.
#include "hal_board.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_littlefs.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "bsp/display.h"
#include "bsp/touch.h"
#include "esp_lcd_touch.h"
#include "esp_lv_adapter.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "hal_board";

static lv_display_t *s_disp;
static lv_indev_t   *s_touch;
static esp_codec_dev_handle_t s_spk;
static bool s_spk_open;

#define DISP_BUF_LINES 48   // 466 x 48 x 2 bytes = 44 KB per buffer, two buffers, internal DMA RAM. Fewer render strips per frame: each strip costs ~2 ms of fixed overhead

// CO5300 wants even start and odd end coordinates (copied from the BSP).
static void rounder_event_cb(lv_event_t *e)
{
    lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

// ---- Whole-screen rotation ----
// LVGL keeps drawing the screen upright. With an angle set, each finished strip is
// copied into a PSRAM copy of the whole screen instead of going to the panel. After
// the last strip of a frame, every changed area is turned through the angle
// (bilinear samples from the copy) and sent to the panel in small chunks. The panel
// is round and the turn is about its centre, so the visible circle maps onto itself.
// Angle 0 sends LVGL's strips straight through, exactly as before.
#define ROT_CHUNK_PX  (HAL_DISPLAY_W * 10)   // pixels per transfer; two 9 KB buffers in internal DMA RAM
#define ROT_MAX_DIRTY 24
static volatile int s_rot_req;          // angle asked for, degrees, plus = clockwise
static int       s_rot;                 // angle on the panel now
static int32_t   s_rcos, s_rsin;        // its cosine and sine, Q16
static uint32_t *s_shadow;              // HAL_DISPLAY_W x HAL_DISPLAY_H upright screen, RGB565 spread as 0x07E0F81F
#define EXM 0x07E0F81Fu                 // spread fields have room for a weight of up to 32
static uint16_t *s_chunk[2];
static int       s_chunk_i;
static lv_area_t s_dirty[ROT_MAX_DIRTY];
static int       s_ndirty;
static bool      s_in_frame, s_rot_full;
static lv_indev_read_cb_t s_touch_read;

// Rows are shared with a helper task on the other core (LVGL runs on core 1).
typedef struct { uint16_t *buf; int32_t x1, x2, ybase, y0, y1, w; } rot_job_t;
static rot_job_t s_job;
static TaskHandle_t s_worker;
static SemaphoreHandle_t s_job_done;
static void rot_worker(void *arg);

static bool rot_buffers(void)
{
    if (!s_shadow) s_shadow = heap_caps_calloc((size_t)HAL_DISPLAY_W * HAL_DISPLAY_H, 4, MALLOC_CAP_SPIRAM);
    for (int i = 0; i < 2; i++)
        if (!s_chunk[i]) s_chunk[i] = heap_caps_malloc(ROT_CHUNK_PX * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_job_done) s_job_done = xSemaphoreCreateBinary();
    if (!s_worker && s_job_done) xTaskCreatePinnedToCore(rot_worker, "rot", 3072, NULL, 6, &s_worker, 0);
    return s_shadow && s_chunk[0] && s_chunk[1];
}

static void area_join(lv_area_t *p, const lv_area_t *a)
{
    p->x1 = LV_MIN(p->x1, a->x1); p->y1 = LV_MIN(p->y1, a->y1);
    p->x2 = LV_MAX(p->x2, a->x2); p->y2 = LV_MAX(p->y2, a->y2);
}

static void rot_add_dirty(const lv_area_t *a)
{
    if (s_ndirty) {   // the strips of one area arrive in order: grow it
        lv_area_t *p = &s_dirty[s_ndirty - 1];
        if (p->x1 == a->x1 && p->x2 == a->x2 && p->y2 + 1 == a->y1) { p->y2 = a->y2; return; }
    }
    if (s_ndirty < ROT_MAX_DIRTY) s_dirty[s_ndirty++] = *a;
    else area_join(&s_dirty[ROT_MAX_DIRTY - 1], a);
}

// Where an upright area lands on the panel: the box around its turned corners, two
// pixels wider for the bilinear neighbours, aligned the way the CO5300 wants.
static bool rot_box(const lv_area_t *a, lv_area_t *o)
{
    const float c = (HAL_DISPLAY_W - 1) * 0.5f, cs = s_rcos / 65536.0f, sn = s_rsin / 65536.0f;
    float x1 = 1e9f, y1 = 1e9f, x2 = -1e9f, y2 = -1e9f;
    for (int k = 0; k < 4; k++) {
        float dx = ((k & 1) ? a->x2 + 1 : a->x1 - 1) - c, dy = ((k & 2) ? a->y2 + 1 : a->y1 - 1) - c;
        float px = c + dx * cs - dy * sn, py = c + dx * sn + dy * cs;
        x1 = fminf(x1, px); x2 = fmaxf(x2, px); y1 = fminf(y1, py); y2 = fmaxf(y2, py);
    }
    o->x1 = LV_MAX(0, (int32_t)floorf(x1) - 1); o->y1 = LV_MAX(0, (int32_t)floorf(y1) - 1);
    o->x2 = LV_MIN(HAL_DISPLAY_W - 1, (int32_t)ceilf(x2) + 1); o->y2 = LV_MIN(HAL_DISPLAY_H - 1, (int32_t)ceilf(y2) + 1);
    if (o->x1 > o->x2 || o->y1 > o->y2) return false;
    o->x1 &= ~1; o->y1 &= ~1; o->x2 |= 1; o->y2 |= 1;
    return true;
}

// Bilinear sample of the upright screen at (u, v), Q16, four weights adding up to 32,
// swapped to the panel's byte order. (u, v) must be at least one pixel inside the
// right and bottom edges.
static inline __attribute__((always_inline)) uint16_t rot_sample(const uint32_t *sh, int32_t u, int32_t v)
{
    const int W = HAL_DISPLAY_W;
    const uint32_t *p = sh + (v >> 16) * W + (u >> 16);
    uint32_t fx = (u >> 11) & 31, fy = (v >> 11) & 31, w11 = (fx * fy) >> 5;
    uint32_t m = ((p[0] * (32 - fx - fy + w11) + p[1] * (fx - w11) + p[W] * (fy - w11) + p[W + 1] * w11) >> 5) & EXM;
    m |= m >> 16;
    return (uint16_t)((m << 8) | ((m >> 8) & 0xFF));
}

// Checked version for the ends of a row, where the sample may fall off the screen.
static __attribute__((noinline)) uint16_t rot_edge(int32_t u, int32_t v)
{
    const int W = HAL_DISPLAY_W, H = HAL_DISPLAY_H;
    if ((uint32_t)u < ((uint32_t)(W - 1) << 16) && (uint32_t)v < ((uint32_t)(H - 1) << 16)) return rot_sample(s_shadow, u, v);
    uint32_t m = s_shadow[LV_CLAMP(0, v >> 16, H - 1) * W + LV_CLAMP(0, u >> 16, W - 1)];
    m |= m >> 16;
    return (uint16_t)((m << 8) | ((m >> 8) & 0xFF));
}

// One panel row: each pixel samples the upright screen at its turned-back position.
// Pixels off the round panel are sent black; the run in the middle, where every
// sample is known to be on the screen, goes without checks.
static __attribute__((noinline)) void rot_row(uint16_t *out, int32_t x1, int32_t x2, int32_t y)
{
    const int W = HAL_DISPLAY_W, H = HAL_DISPLAY_H;
    const float c = (W - 1) * 0.5f, cs = s_rcos / 65536.0f, sn = s_rsin / 65536.0f, dy = y - c, rr = c + 1.5f;
    if (dy * dy >= rr * rr) { memset(out, 0, (size_t)(x2 - x1 + 1) * 2); return; }
    float half = sqrtf(rr * rr - dy * dy);
    int32_t a = LV_MAX(x1, (int32_t)floorf(c - half)), b = LV_MIN(x2, (int32_t)ceilf(c + half));
    if (a > b) { memset(out, 0, (size_t)(x2 - x1 + 1) * 2); return; }
    if (a > x1) { memset(out, 0, (size_t)(a - x1) * 2); out += a - x1; }
    // sample position at x = a, Q16; doubles keep it exact enough at 466 px
    const double dx = a - (double)c;
    int32_t u = (int32_t)lround((c + dx * cs + dy * sn) * 65536.0);
    int32_t v = (int32_t)lround((c - dx * sn + dy * cs) * 65536.0);
    const int32_t rc = s_rcos, rs = s_rsin, n = b - a + 1;
    // steps k with 0 <= u, v < (W-1)<<16, found in float, then pulled in by one for safety
    float klo = 0, khi = n - 1;
    const float lu = (float)((W - 1) << 16), lv = (float)((H - 1) << 16);
    if (rc > 0) { klo = fmaxf(klo, -u / (float)rc); khi = fminf(khi, (lu - u) / rc); }
    else if (rc < 0) { klo = fmaxf(klo, (lu - u) / (float)rc); khi = fminf(khi, -u / (float)rc); }
    else if (u < 0 || u >= lu) khi = -1;
    if (rs < 0) { klo = fmaxf(klo, v / (float)rs); khi = fminf(khi, (v - lv) / rs); }
    else if (rs > 0) { klo = fmaxf(klo, (v - lv) / (float)rs); khi = fminf(khi, v / (float)rs); }
    else if (v < 0 || v >= lv) khi = -1;
    int32_t k0 = (int32_t)ceilf(klo) + 1, k1 = (int32_t)floorf(khi) - 1, k = 0;
    if (k1 < k0) { k0 = n; k1 = n - 1; }   // no safe run: all checked
    const uint32_t *sh = s_shadow;
    for (; k < k0 && k < n; k++, u += rc, v -= rs) *out++ = rot_edge(u, v);
    for (; k <= k1; k++, u += rc, v -= rs) *out++ = rot_sample(sh, u, v);
    for (; k < n; k++, u += rc, v -= rs) *out++ = rot_edge(u, v);
    if (b < x2) memset(out, 0, (size_t)(x2 - b) * 2);
}

// Two chunk buffers in turn. Safe without a completion wait: the panel driver
// finishes the previous colour transfer before it sends the next window command.
static void rot_rows(const rot_job_t *j)
{
    for (int32_t yy = j->y0; yy <= j->y1; yy++) rot_row(j->buf + (yy - j->ybase) * j->w, j->x1, j->x2, yy);
}

static void rot_worker(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        rot_rows(&s_job);
        xSemaphoreGive(s_job_done);
    }
}

static bool rot_send(esp_lcd_panel_handle_t panel, const lv_area_t *a)
{
    int32_t w = a->x2 - a->x1 + 1, rows = (ROT_CHUNK_PX / w) & ~1;
    bool sent = false;
    for (int32_t y = a->y1; y <= a->y2; y += rows) {
        int32_t y2 = LV_MIN(a->y2, y + rows - 1);
        uint16_t *buf = s_chunk[s_chunk_i]; s_chunk_i ^= 1;
        int32_t mid = y + (y2 - y + 1) / 2;
        bool split = s_worker && y2 > y;
        if (split) { s_job = (rot_job_t){ buf, a->x1, a->x2, y, mid, y2, w }; xTaskNotifyGive(s_worker); }
        rot_rows(&(rot_job_t){ buf, a->x1, a->x2, y, y, split ? mid - 1 : y2, w });
        if (split) xSemaphoreTake(s_job_done, portMAX_DELAY);
        if (esp_lcd_panel_draw_bitmap(panel, a->x1, y, a->x2 + 1, y2 + 1, buf) == ESP_OK) sent = true;
    }
    return sent;
}

static esp_err_t rot_draw_bitmap(lv_display_t *disp, esp_lcd_panel_handle_t panel,
                                 int xs, int ys, int xe, int ye, const void *map, void *ctx)
{
    (void)ctx;
    if (!s_in_frame) {   // first strip of a frame: a new angle starts here, never part way through
        s_in_frame = true;
        int want = s_rot_req;
        if (want != s_rot) {
            if (want && !rot_buffers()) { ESP_LOGW(TAG, "rotation: no memory"); want = 0; s_rot_req = 0; }
            float r = want * (float)M_PI / 180.0f;
            s_rot = want; s_rcos = (int32_t)lroundf(cosf(r) * 65536.0f); s_rsin = (int32_t)lroundf(sinf(r) * 65536.0f);
            s_rot_full = true; s_ndirty = 0;
        }
    }
    bool last = lv_display_flush_is_last(disp);
    if (last) s_in_frame = false;
    if (!s_rot) return esp_lcd_panel_draw_bitmap(panel, xs, ys, xe, ye, map);

    const uint16_t *src = map;
    for (int y = ys; y < ye; y++) {
        uint32_t *d = s_shadow + y * HAL_DISPLAY_W;
        for (int x = xs; x < xe; x++) { uint32_t c = __builtin_bswap16(*src++); d[x] = (c | (c << 16)) & EXM; }
    }
    rot_add_dirty(&(lv_area_t){ xs, ys, xe - 1, ye - 1 });
    if (!last) return ESP_ERR_NOT_ALLOWED;   // nothing sent yet: the adapter releases LVGL at once

    if (s_rot_full) { s_dirty[0] = (lv_area_t){ 0, 0, HAL_DISPLAY_W - 1, HAL_DISPLAY_H - 1 }; s_ndirty = 1; s_rot_full = false; }
    lv_area_t out[ROT_MAX_DIRTY]; int n = 0;
    for (int i = 0; i < s_ndirty; i++) {
        lv_area_t o;
        if (!rot_box(&s_dirty[i], &o)) continue;
        int j = 0;
        for (; j < n; j++)   // overlapping boxes go out as one
            if (o.x1 <= out[j].x2 && o.x2 >= out[j].x1 && o.y1 <= out[j].y2 && o.y2 >= out[j].y1) break;
        if (j < n) area_join(&out[j], &o); else out[n++] = o;
    }
    s_ndirty = 0;
    bool sent = false;
    for (int i = 0; i < n; i++) sent |= rot_send(panel, &out[i]);
    return sent ? ESP_OK : ESP_ERR_NOT_ALLOWED;
}

// Touch arrives in panel coordinates; turn it back to where the upright screen has it.
static void touch_read_rot(lv_indev_t *indev, lv_indev_data_t *data)
{
    s_touch_read(indev, data);
    if (!s_rot) return;
    const float c = (HAL_DISPLAY_W - 1) * 0.5f, cs = s_rcos / 65536.0f, sn = s_rsin / 65536.0f;
    float dx = data->point.x - c, dy = data->point.y - c;
    data->point.x = LV_CLAMP(0, (int32_t)lroundf(c + dx * cs + dy * sn), HAL_DISPLAY_W - 1);
    data->point.y = LV_CLAMP(0, (int32_t)lroundf(c - dx * sn + dy * cs), HAL_DISPLAY_H - 1);
}

void hal_board_set_rotation(int deg)
{
    if (deg < -180 || deg > 180) deg = 0;
    if (deg == -180) deg = 180;
    if (deg == s_rot_req) return;
    s_rot_req = deg;
    if (s_disp) lv_obj_invalidate(lv_display_get_screen_active(s_disp));   // next frame is whole
}

static esp_err_t display_start_internal_buffers(void)
{
    esp_lv_adapter_config_t acfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    acfg.task_core_id = 1;   // core 0 keeps Wi-Fi and the rotation helper
    ESP_RETURN_ON_ERROR(esp_lv_adapter_init(&acfg), TAG, "lv adapter");

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t io = NULL;
    const bsp_display_config_t dcfg = { .max_transfer_sz = HAL_DISPLAY_W * DISP_BUF_LINES * 2 };
    ESP_RETURN_ON_ERROR(bsp_display_new(&dcfg, &panel, &io), TAG, "panel");

    esp_lv_adapter_display_config_t disp_cfg = {
        .panel = panel,
        .panel_io = io,
        .profile = {
            .interface = ESP_LV_ADAPTER_PANEL_IF_OTHER,
            .rotation = ESP_LV_ADAPTER_ROTATE_0,
            .hor_res = HAL_DISPLAY_W,
            .ver_res = HAL_DISPLAY_H,
            .buffer_height = DISP_BUF_LINES,
            .use_psram = false,
            .enable_ppa_accel = false,
            .require_double_buffer = true,
        },
        // Tear-effect sync: the CO5300's TE line is on GPIO13. The adapter times each
        // flush into the panel's vertical blanking from the TE edge, which removes the
        // horizontal tear across the digits that free-running stripe flushes produced.
        // TE sync needs a full 434 KB frame buffer, which does not fit internal RAM next to
        // Wi-Fi (the adapter aborted at boot). Stays off until a PSRAM frame buffer is proven.
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
        .te_sync = { .gpio_num = -1 },
    };
    s_disp = esp_lv_adapter_register_display(&disp_cfg);
    ESP_RETURN_ON_FALSE(s_disp, ESP_FAIL, TAG, "register display");
    lv_display_add_event_cb(s_disp, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    const esp_lv_adapter_draw_bitmap_callbacks_t dcbs = { .custom_draw_bitmap = rot_draw_bitmap };
    ESP_RETURN_ON_ERROR(esp_lv_adapter_set_draw_bitmap_callbacks(s_disp, &dcbs, NULL), TAG, "draw hook");

    bsp_display_cfg_t tcfg = { .touch_flags = { 0 } };
    esp_lcd_touch_handle_t tp = NULL;
    if (bsp_touch_new(&tcfg, &tp) == ESP_OK && tp) {
        const esp_lv_adapter_touch_config_t touch_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(s_disp, tp);
        s_touch = esp_lv_adapter_register_touch(&touch_cfg);
        if (s_touch) { s_touch_read = lv_indev_get_read_cb(s_touch); lv_indev_set_read_cb(s_touch, touch_read_rot); }
    } else {
        ESP_LOGW(TAG, "touch init failed");
    }
    ESP_RETURN_ON_ERROR(esp_lv_adapter_start(), TAG, "lv start");
    return ESP_OK;
}

esp_err_t hal_board_init(void)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "i2c init");

    // Same sequence as the BSP's bsp_display_start(), except the LVGL draw
    // buffers live in internal DMA memory in small chunks instead of PSRAM.
    // The BSP's 50-line PSRAM buffers make the SPI driver bounce every flush
    // through a 46 KB internal buffer, which Wi-Fi leaves no room for.
    ESP_RETURN_ON_ERROR(display_start_internal_buffers(), TAG, "display start failed");

    // CO5300 brightness is a panel command, not a backlight PWM.
    if (bsp_display_brightness_init() == ESP_OK) {
        bsp_display_brightness_set(80);
    } else {
        ESP_LOGW(TAG, "brightness control unavailable");
    }

    ESP_LOGI(TAG, "display %dx%d up, touch %s", HAL_DISPLAY_W, HAL_DISPLAY_H,
             s_touch ? "present" : "absent");
    return ESP_OK;
}

lv_display_t *hal_board_display(void) { return s_disp; }
lv_indev_t   *hal_board_touch(void)   { return s_touch; }

bool hal_board_lock(uint32_t timeout_ms) { return bsp_display_lock(timeout_ms) == ESP_OK; }
void hal_board_unlock(void)              { bsp_display_unlock(); }

esp_err_t hal_board_set_brightness(uint8_t percent)
{
    if (percent > 100) percent = 100;
    return bsp_display_brightness_set(percent);
}

// ---------------------------------------------------------------- audio
esp_err_t hal_board_audio_init(uint32_t sample_rate)
{
    // BSP default I2S config; esp_codec_dev reclocks it to the sample rate on open.
    ESP_RETURN_ON_ERROR(bsp_audio_init(NULL), TAG, "i2s");
    s_spk = bsp_audio_codec_speaker_init();
    ESP_RETURN_ON_FALSE(s_spk, ESP_FAIL, TAG, "speaker codec");
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16, .channel = 1, .channel_mask = 0, .sample_rate = sample_rate, .mclk_multiple = 0,
    };
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(s_spk, &fs) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG, "codec open");
    esp_codec_dev_set_out_vol(s_spk, 70);
    s_spk_open = true;
    ESP_LOGI(TAG, "speaker ready, %lu Hz", (unsigned long)sample_rate);
    return ESP_OK;
}

void hal_board_audio_write_begin(void) { if (s_spk_open) esp_codec_dev_set_out_mute(s_spk, false); }
void hal_board_audio_write(const void *pcm, size_t bytes) { if (s_spk_open) esp_codec_dev_write(s_spk, (void *)pcm, (int)bytes); }
void hal_board_audio_write_end(void)   { if (s_spk_open) esp_codec_dev_set_out_mute(s_spk, true); }

// ---------------------------------------------------------------- storage
esp_err_t hal_board_storage_init(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/config",
        .partition_label = "config",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    ESP_RETURN_ON_ERROR(err, TAG, "littlefs mount");
    size_t total = 0, used = 0;
    esp_littlefs_info("config", &total, &used);
    ESP_LOGI(TAG, "config fs %u/%u bytes used", (unsigned)used, (unsigned)total);
    return ESP_OK;
}
