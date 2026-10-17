// Bench MegaSquirt simulator: sends MS2/Extra dash broadcast frames
// (IDs 1512..1516, 11-bit, 500 kbps, big-endian) at 20 Hz with a slowly
// varying synthetic engine. Runs on any ESP32 with a CAN transceiver on
// CONFIG_SIM_CAN_TX / CONFIG_SIM_CAN_RX. See docs/ms2-can.md for the layout.

#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/twai.h"
#include "sdkconfig.h"

static const char *TAG = "can_sim";

#define BASE_ID 1512
#define RATE_HZ 20

static void put_u16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v & 0xFF; }
static void put_s16(uint8_t *p, int16_t v)  { put_u16(p, (uint16_t)v); }

static esp_err_t send(uint32_t id, const uint8_t *data)
{
    twai_message_t m = { .identifier = id, .data_length_code = 8 };
    memcpy(m.data, data, 8);
    return twai_transmit(&m, pdMS_TO_TICKS(20));
}

void app_main(void)
{
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CONFIG_SIM_CAN_TX, CONFIG_SIM_CAN_RX, TWAI_MODE_NORMAL);
    twai_timing_config_t  t = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t  f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    ESP_ERROR_CHECK(twai_driver_install(&g, &t, &f));
    ESP_ERROR_CHECK(twai_start());
    ESP_LOGI(TAG, "MS2 dash broadcast sim on TX=%d RX=%d, %d Hz", CONFIG_SIM_CAN_TX, CONFIG_SIM_CAN_RX, RATE_HZ);

    float ts = 0.0f;
    uint8_t d[8];
    for (;;) {
        ts += 1.0f / RATE_HZ;

        // Synthetic engine: idle, a pull to 6500, back down. 30 s cycle.
        float ph   = fmodf(ts, 30.0f) / 30.0f;
        float rpm  = 900.0f + 5600.0f * (ph < 0.6f ? ph / 0.6f : (1.0f - ph) / 0.4f);
        float tps  = ph < 0.6f ? 100.0f * ph / 0.6f : 0.0f;
        float map  = 30.0f + 70.0f * tps / 100.0f;
        float clt  = 170.0f + 30.0f * (1.0f - expf(-ts / 120.0f));   // warms up over 2 min
        float mat  = 95.0f + 10.0f * sinf(ts * 0.1f);
        float adv  = 12.0f + 20.0f * (rpm / 6500.0f);
        float pw   = 2.0f + 10.0f * map / 100.0f;
        float afr  = 14.7f - 2.0f * tps / 100.0f + 0.2f * sinf(ts * 3.0f);
        float batt = 13.8f + 0.3f * sinf(ts * 0.5f);
        float oilp = 15.0f + 55.0f * rpm / 6500.0f;                    // generic sensor 1, psi
        float oilt = clt + 20.0f;                                      // generic sensor 2, F
        float vss  = 30.0f * tps / 100.0f;                             // m/s

        memset(d, 0, 8);
        put_s16(&d[0], (int16_t)(map * 10)); put_u16(&d[2], (uint16_t)rpm);
        put_s16(&d[4], (int16_t)(clt * 10)); put_s16(&d[6], (int16_t)(tps * 10));
        send(BASE_ID + 0, d);

        memset(d, 0, 8);
        put_u16(&d[0], (uint16_t)(pw * 1000)); put_u16(&d[2], (uint16_t)(pw * 1000));
        put_s16(&d[4], (int16_t)(mat * 10));   put_s16(&d[6], (int16_t)(adv * 10));
        send(BASE_ID + 1, d);

        memset(d, 0, 8);
        d[0] = (uint8_t)(14.7f * 10); d[1] = (uint8_t)(afr * 10);
        put_s16(&d[2], 1000); put_s16(&d[4], 0); put_s16(&d[6], (int16_t)(pw * 1000));
        send(BASE_ID + 2, d);

        memset(d, 0, 8);
        put_s16(&d[0], (int16_t)(batt * 10)); put_s16(&d[2], (int16_t)(oilp * 10));
        put_s16(&d[4], (int16_t)(oilt * 10)); d[6] = 0;
        send(BASE_ID + 3, d);

        memset(d, 0, 8);
        put_u16(&d[0], (uint16_t)(vss * 10));
        send(BASE_ID + 4, d);

        vTaskDelay(pdMS_TO_TICKS(1000 / RATE_HZ));
    }
}
