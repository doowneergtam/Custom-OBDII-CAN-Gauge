#include "src_can.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "driver/twai.h"
#include "channels.h"

static const char *TAG = "can";

static src_can_stats_t s_stats;
static portMUX_TYPE    s_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile src_can_mode_t s_mode;
static volatile bool   s_feed = true;

// ---------------------------------------------------------------- OBD-II
// Mode 01 PIDs we poll, in rotation. Single-frame replies only.
static const uint8_t OBD_PIDS[] = { 0x0C, 0x0B, 0x05, 0x0D, 0x11, 0x0E, 0x0F, 0x04, 0x10, 0x06, 0x07, 0x14 };
#define OBD_REQ_ID   0x7DF
#define OBD_PERIOD_MS 50

static void obd_parse(const twai_message_t *m)
{
    if (m->data_length_code < 4 || m->data[1] != 0x41) return;
    uint8_t pid = m->data[2], A = m->data[3], B = m->data_length_code > 4 ? m->data[4] : 0;
    switch (pid) {
    case 0x0C: channels_set_value(CH_RPM,  (A * 256 + B) / 4.0f); break;
    case 0x0B: channels_set_value(CH_MAP,  (float)A);
               channels_set_value(CH_BOOST, (A - 101.3f) / 6.895f); break;
    case 0x05: channels_set_value(CH_CLT,  (A - 40) * 1.8f + 32.0f); break;
    case 0x0D: channels_set_value(CH_VSS,  A * 0.621371f); break;
    case 0x11: channels_set_value(CH_TPS,  A * 100.0f / 255.0f); break;
    case 0x0E: channels_set_value(CH_ADV,  A / 2.0f - 64.0f); break;
    case 0x0F: channels_set_value(CH_MAT,  (A - 40) * 1.8f + 32.0f); break;
    case 0x04: channels_set_value(CH_LOAD, A * 100.0f / 255.0f); break;
    case 0x10: channels_set_value(CH_MAF,  (A * 256 + B) / 100.0f); break;
    case 0x06: channels_set_value(CH_STFT, (A - 128) * 100.0f / 128.0f); break;
    case 0x07: channels_set_value(CH_LTFT, (A - 128) * 100.0f / 128.0f); break;
    case 0x14: channels_set_value(CH_O2,   A / 200.0f); break;
    default: break;
    }
}

static void obd_poll_task(void *arg)
{
    (void)arg;
    size_t i = 0;
    for (;;) {
        if (s_mode == SRC_CAN_MODE_OBD && s_feed) {
            twai_message_t req = { .identifier = OBD_REQ_ID, .data_length_code = 8, .data = { 0x02, 0x01, OBD_PIDS[i], 0, 0, 0, 0, 0 } };
            if (twai_transmit(&req, pdMS_TO_TICKS(20)) == ESP_OK) {
                portENTER_CRITICAL(&s_lock); s_stats.obd_requests++; portEXIT_CRITICAL(&s_lock);
            }
            i = (i + 1) % (sizeof OBD_PIDS / sizeof OBD_PIDS[0]);
            vTaskDelay(pdMS_TO_TICKS(OBD_PERIOD_MS));
        } else {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

// ---------------------------------------------------------------- receive
static void rx_task(void *arg)
{
    (void)arg;
    twai_message_t msg;
    for (;;) {
        esp_err_t err = twai_receive(&msg, pdMS_TO_TICKS(1000));
        if (err == ESP_OK) {
            if (msg.rtr) continue;
            portENTER_CRITICAL(&s_lock);
            s_stats.frames_rx++;
            s_stats.last_frame_us = esp_timer_get_time();
            portEXIT_CRITICAL(&s_lock);
            if (!s_feed) continue;
            int n = 0;
            if (s_mode == SRC_CAN_MODE_OBD) {
                if (msg.identifier >= 0x7E8 && msg.identifier <= 0x7EF) { obd_parse(&msg); n = 1; }
            } else {
                n = channels_feed_can(msg.identifier, msg.data, msg.data_length_code);
            }
            if (n > 0) { portENTER_CRITICAL(&s_lock); s_stats.frames_matched++; portEXIT_CRITICAL(&s_lock); }
            continue;
        }

        // Idle second: check bus health and recover if needed.
        twai_status_info_t st;
        if (twai_get_status_info(&st) == ESP_OK) {
            if (st.state == TWAI_STATE_BUS_OFF) {
                portENTER_CRITICAL(&s_lock); s_stats.bus_off_count++; portEXIT_CRITICAL(&s_lock);
                ESP_LOGW(TAG, "bus off, recovering");
                twai_initiate_recovery();
            } else if (st.state == TWAI_STATE_STOPPED) {
                twai_start();
            }
            if (st.bus_error_count) {
                portENTER_CRITICAL(&s_lock); s_stats.bus_errors = st.bus_error_count; portEXIT_CRITICAL(&s_lock);
            }
        }
    }
}

// The display, touch, I2S and I2C drivers use up core 0's level-1 interrupt
// slots, so the TWAI driver is installed from a task pinned to core 1 and may
// take any low or medium priority level.
typedef struct { const src_can_config_t *cfg; esp_err_t err; SemaphoreHandle_t done; } install_job_t;

static esp_err_t install_driver(const src_can_config_t *cfg)
{
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
        cfg->gpio_tx, cfg->gpio_rx,
        cfg->listen_only ? TWAI_MODE_LISTEN_ONLY : TWAI_MODE_NORMAL);
    g.rx_queue_len = 32;
    g.tx_queue_len = 4;
    g.intr_flags = ESP_INTR_FLAG_LOWMED;

    twai_timing_config_t t;
    switch (cfg->bitrate) {
    case 1000000: t = (twai_timing_config_t)TWAI_TIMING_CONFIG_1MBITS();   break;
    case 250000:  t = (twai_timing_config_t)TWAI_TIMING_CONFIG_250KBITS(); break;
    case 125000:  t = (twai_timing_config_t)TWAI_TIMING_CONFIG_125KBITS(); break;
    case 500000:
    default:      t = (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS(); break;
    }
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    ESP_RETURN_ON_ERROR(twai_driver_install(&g, &t, &f), TAG, "driver install");
    ESP_RETURN_ON_ERROR(twai_start(), TAG, "start");
    return ESP_OK;
}

static void install_task(void *arg)
{
    install_job_t *job = arg;
    job->err = install_driver(job->cfg);
    xSemaphoreGive(job->done);
    vTaskDelete(NULL);
}

esp_err_t src_can_start(const src_can_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, TAG, "no config");
    s_mode = cfg->mode;

    install_job_t job = { .cfg = cfg, .err = ESP_FAIL, .done = xSemaphoreCreateBinary() };
    ESP_RETURN_ON_FALSE(job.done, ESP_ERR_NO_MEM, TAG, "sem");
    if (xTaskCreatePinnedToCore(install_task, "can_init", 3072, &job, 8, NULL, 1) != pdPASS) { vSemaphoreDelete(job.done); return ESP_ERR_NO_MEM; }
    xSemaphoreTake(job.done, pdMS_TO_TICKS(2000));
    vSemaphoreDelete(job.done);
    ESP_RETURN_ON_ERROR(job.err, TAG, "driver install on core 1");

    memset(&s_stats, 0, sizeof s_stats);
    ESP_RETURN_ON_FALSE(xTaskCreatePinnedToCore(rx_task, "can_rx", 3072, NULL, 8, NULL, 1) == pdPASS, ESP_ERR_NO_MEM, TAG, "rx task");
    ESP_RETURN_ON_FALSE(xTaskCreatePinnedToCore(obd_poll_task, "obd_poll", 2560, NULL, 5, NULL, 1) == pdPASS, ESP_ERR_NO_MEM, TAG, "poll task");

    ESP_LOGI(TAG, "TWAI up: TX=%d RX=%d %lu bps %s, mode %s", cfg->gpio_tx, cfg->gpio_rx,
             (unsigned long)cfg->bitrate, cfg->listen_only ? "listen-only" : "normal",
             s_mode == SRC_CAN_MODE_OBD ? "OBD-II" : "MegaSquirt");
    return ESP_OK;
}

void src_can_set_mode(src_can_mode_t mode) { if (s_mode != mode) ESP_LOGI(TAG, "mode %s", mode == SRC_CAN_MODE_OBD ? "OBD-II" : "MegaSquirt"); s_mode = mode; }
void src_can_set_feed(bool enabled)        { s_feed = enabled; }

void src_can_stats(src_can_stats_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_stats;
    portEXIT_CRITICAL(&s_lock);
}
