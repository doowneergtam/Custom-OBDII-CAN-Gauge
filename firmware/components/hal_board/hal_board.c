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

static const char *TAG = "hal_board";

static lv_display_t *s_disp;
static lv_indev_t   *s_touch;
static esp_codec_dev_handle_t s_spk;
static bool s_spk_open;

#define DISP_BUF_LINES 24   // 466 x 24 x 2 bytes = 22 KB per buffer, two buffers, internal DMA RAM

// CO5300 wants even start and odd end coordinates (copied from the BSP).
static void rounder_event_cb(lv_event_t *e)
{
    lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

static esp_err_t display_start_internal_buffers(void)
{
    esp_lv_adapter_config_t acfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
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
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
    };
    s_disp = esp_lv_adapter_register_display(&disp_cfg);
    ESP_RETURN_ON_FALSE(s_disp, ESP_FAIL, TAG, "register display");
    lv_display_add_event_cb(s_disp, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    bsp_display_cfg_t tcfg = { .touch_flags = { 0 } };
    esp_lcd_touch_handle_t tp = NULL;
    if (bsp_touch_new(&tcfg, &tp) == ESP_OK && tp) {
        const esp_lv_adapter_touch_config_t touch_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(s_disp, tp);
        s_touch = esp_lv_adapter_register_touch(&touch_cfg);
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
