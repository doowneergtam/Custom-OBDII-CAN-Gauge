// NB1 Miata Gauge — pod firmware entry point.

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "hal_board.h"
#include "channels.h"
#include "gauge_config.h"
#include "alarms.h"
#include "input_encoder.h"
#include "src_can.h"
#include "src_adc.h"
#include "webcfg.h"
#include "gauge_console.h"
#include "ui.h"

static const char *TAG = "main";

#if CONFIG_NB1_SIM_DATA
// Fake data source for bench bring-up: a 30 s idle / pull / settle cycle on a
// turbo engine, same as tools/can_sim. Disable in menuconfig once a CAN
// source is connected. Feeds the raw CAN frames so the decoder path is
// exercised too.
static void put_u16(uint8_t *p, int v) { p[0] = (v >> 8) & 0xFF; p[1] = v & 0xFF; }
static void sim_task(void *arg)
{
    (void)arg;
    float t = 0.0f;
    uint8_t d[8];
    for (;;) {
        if (!g_ui_cfg.sim) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }   // runtime toggle, menu or web
        t += 0.05f;
        float ph = fmodf(t, 30.0f) / 30.0f;
        float pull = ph < 0.6f ? ph / 0.6f : (1.0f - ph) / 0.4f;
        float rpm = 900.0f + 5600.0f * pull + 60.0f * sinf(t * 9.0f);
        float tps = ph < 0.6f ? 100.0f * ph / 0.6f : 0.0f;
        float spool = fmaxf(0.0f, fminf(1.0f, (rpm - 2500.0f) / 2000.0f));
        float boost = tps / 100.0f * spool * 13.0f;
        float map = boost > 0.3f ? 101.3f + boost * 6.895f : 35.0f + 66.0f * tps / 100.0f;
        float clt = 170.0f + 30.0f * (1.0f - expf(-t / 120.0f));
        float pw = 1.8f + 12.0f * map / 200.0f;
        float afr = 14.7f - 3.2f * spool * tps / 100.0f + 0.25f * sinf(t * 3.0f);

        put_u16(&d[0], (int)(map * 10)); put_u16(&d[2], (int)rpm); put_u16(&d[4], (int)(clt * 10)); put_u16(&d[6], (int)(tps * 10));
        channels_feed_can(0x5E8, d, 8);
        put_u16(&d[0], (int)(pw * 1000)); put_u16(&d[2], (int)(pw * 1000)); put_u16(&d[4], (int)((95.0f + 25.0f * spool) * 10)); put_u16(&d[6], (int)((24.0f - 14.0f * spool) * 10));
        channels_feed_can(0x5E9, d, 8);
        d[0] = (uint8_t)(147 - (int)(32 * spool * tps / 100)); d[1] = (uint8_t)(afr * 10); put_u16(&d[2], 1000); put_u16(&d[4], (int)((800.0f + 800.0f * tps / 100.0f) * 10)); put_u16(&d[6], (int)(pw * 1000));
        channels_feed_can(0x5EA, d, 8);
        put_u16(&d[0], (int)((13.8f + 0.3f * sinf(t * 0.5f)) * 10)); put_u16(&d[2], (int)((15.0f + 55.0f * rpm / 6500.0f) * 10)); put_u16(&d[4], (int)((clt + 20.0f) * 10)); d[6] = 0; d[7] = 0;
        channels_feed_can(0x5EB, d, 8);
        put_u16(&d[0], (int)(30.0f * tps / 100.0f * 10)); put_u16(&d[2], 0); put_u16(&d[4], 0); put_u16(&d[6], 0);
        channels_feed_can(0x5EC, d, 8);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
#endif

void app_main(void)
{
    ESP_LOGI(TAG, "NB1 gauge starting");

    // NVS is needed by the Wi-Fi driver even though we keep nothing in it.
    esp_err_t nv = nvs_flash_init();
    if (nv == ESP_ERR_NVS_NO_FREE_PAGES || nv == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); nvs_flash_init(); }

    channels_init();
    channels_load_preset_megasquirt();

    ESP_ERROR_CHECK(hal_board_init());

    config_defaults();
    if (hal_board_storage_init() == ESP_OK) config_load();
    alarms_init();   // after config: keeps loaded tones, brings up the codec

    lv_group_t *group = NULL;
#if CONFIG_NB1_ENCODER
    input_encoder_config_t enc = {
        .gpio_a = HAL_PIN_ENC_A, .gpio_b = HAL_PIN_ENC_B, .gpio_push = HAL_PIN_ENC_PUSH,
        .reverse = false, .long_press_ms = 600,
    };
    if (hal_board_lock(1000)) {
        if (input_encoder_init(&enc, hal_board_display()) == ESP_OK) group = input_encoder_group();
        else ESP_LOGW(TAG, "encoder unavailable, touch only");
        hal_board_unlock();
    }
#else
    if (src_adc_start() != ESP_OK) ESP_LOGW(TAG, "analog inputs unavailable");
#endif

    ui_init(group);
    if (g_ui_cfg.wifi_on && webcfg_start() != ESP_OK) ESP_LOGW(TAG, "hotspot failed to start");

    src_can_config_t can = {
        .gpio_tx = HAL_PIN_CAN_TX, .gpio_rx = HAL_PIN_CAN_RX, .bitrate = 500000,
        .mode = g_ui_cfg.source == SRC_OBD ? SRC_CAN_MODE_OBD : SRC_CAN_MODE_MS,
#ifdef CONFIG_NB1_CAN_LISTEN_ONLY
        .listen_only = true,
#else
        .listen_only = false,
#endif
    };
    if (src_can_start(&can) != ESP_OK) ESP_LOGW(TAG, "CAN unavailable; channels will show dashes");
    src_can_set_feed(!g_ui_cfg.sim);

#if CONFIG_NB1_SIM_DATA
    xTaskCreate(sim_task, "sim", 4096, NULL, 6, NULL);   // above httpd and LVGL so a config write never starves the feed
#endif
    gauge_console_start();
}
