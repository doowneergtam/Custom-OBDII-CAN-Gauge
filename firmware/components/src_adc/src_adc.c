// Analog sensor inputs on the rev C carrier: GPIO17 (AIN1, ADC2 ch 6) and
// GPIO18 (AIN2, ADC2 ch 7). On the carrier each input is sensor -> 1k ->
// GPIO, 1.5k GPIO -> GND, so the node voltage is 1/0.6 of the GPIO voltage.
// With the solder jumper closed a 1k pull-up from 5 V sits at the node for
// resistive (temperature) senders; the firmware then converts the node
// voltage back to the sender's resistance (the divider loads it with 2.5k).
// ADC2 is shared with the radio: reads that lose the arbitration return
// ESP_ERR_TIMEOUT and are simply retried.
#include "src_adc.h"

#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "channels.h"

static const char *TAG = "adc";
aux_cfg_t g_aux[AUX_N];

#define DIVIDER_GAIN   (1.0f / 0.6f)     // node volts per GPIO volt (1k / 1.5k)
#define PULLUP_OHMS    1000.0f
#define PULLUP_VOLTS   5.0f
#define LOAD_OHMS      2500.0f           // 1k + 1.5k across the node
#define SAMPLES        16
#define PERIOD_MS      50

static const adc_channel_t CHAN[AUX_N] = { ADC_CHANNEL_6, ADC_CHANNEL_7 };   // GPIO17, GPIO18
static const ch_id_t       CHID[AUX_N] = { CH_AUX1, CH_AUX2 };
static adc_oneshot_unit_handle_t s_unit;
static adc_cali_handle_t         s_cali;
static float s_volts[AUX_N] = { NAN, NAN };
static float s_ohms[AUX_N]  = { NAN, NAN };

void src_adc_defaults(void)
{
    memset(g_aux, 0, sizeof g_aux);
    for (int i = 0; i < AUX_N; i++) {
        snprintf(g_aux[i].label, sizeof g_aux[i].label, "AUX %d", i + 1);
        snprintf(g_aux[i].units, sizeof g_aux[i].units, "V");
        g_aux[i].decimals = 2;
        g_aux[i].npts = 2; g_aux[i].x[0] = 0; g_aux[i].y[0] = 0; g_aux[i].x[1] = 5; g_aux[i].y[1] = 5;
    }
}

void src_adc_apply(void)
{
    for (int i = 0; i < AUX_N; i++) {
        ch_def_t *d = channels_def_mut(CHID[i]);
        if (!d) continue;
        snprintf(d->name, CH_NAME_LEN, "%s", g_aux[i].label[0] ? g_aux[i].label : "AUX");
        snprintf(d->units, CH_UNITS_LEN, "%s", g_aux[i].units);
        d->decimals = g_aux[i].decimals > 3 ? 3 : g_aux[i].decimals;
        d->enabled = g_aux[i].enabled;
    }
}

// Piecewise-linear interpolation through the calibration points, clamped at the ends.
static float scale(const aux_cfg_t *a, float x)
{
    int n = a->npts;
    if (n == 0) return x;
    if (n == 1) return a->y[0];
    // points are kept sorted by x on apply; be tolerant anyway
    int lo = 0;
    for (int i = 1; i < n; i++) if (a->x[i] <= x) lo = i;
    if (lo >= n - 1) lo = n - 2;
    float x0 = a->x[lo], x1 = a->x[lo + 1], y0 = a->y[lo], y1 = a->y[lo + 1];
    if (x <= a->x[0]) return a->y[0];
    if (x >= a->x[n - 1]) return a->y[n - 1];
    if (x1 == x0) return y0;
    return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
}

static bool read_mv(int i, int *mv)
{
    for (int tries = 0; tries < 4; tries++) {
        int raw;
        esp_err_t e = adc_oneshot_read(s_unit, CHAN[i], &raw);
        if (e == ESP_ERR_TIMEOUT) { vTaskDelay(1); continue; }   // radio had the ADC
        if (e != ESP_OK) return false;
        if (s_cali) { if (adc_cali_raw_to_voltage(s_cali, raw, mv) != ESP_OK) *mv = raw * 3100 / 4095; }
        else *mv = raw * 3100 / 4095;
        return true;
    }
    return false;
}

static void adc_task(void *arg)
{
    (void)arg;
    for (;;) {
        for (int i = 0; i < AUX_N; i++) {
            if (!g_aux[i].enabled) { s_volts[i] = NAN; s_ohms[i] = NAN; continue; }
            int acc = 0, got = 0, mv;
            for (int k = 0; k < SAMPLES; k++) if (read_mv(i, &mv)) { acc += mv; got++; }
            if (!got) continue;
            float vg = (acc / (float)got) / 1000.0f;          // GPIO volts
            float vn = vg * DIVIDER_GAIN;                     // sensor node volts
            s_volts[i] = vn;
            float x = vn;
            if (g_aux[i].resistive) {
                // node = 5 V through 1k, loaded by Rt || 2.5k
                float rpar = vn >= PULLUP_VOLTS - 0.01f ? 1e9f : PULLUP_OHMS * vn / (PULLUP_VOLTS - vn);
                float rt = rpar >= LOAD_OHMS ? 1e9f : rpar * LOAD_OHMS / (LOAD_OHMS - rpar);
                s_ohms[i] = rt; x = rt;
            } else {
                s_ohms[i] = NAN;
            }
            channels_set_value(CHID[i], scale(&g_aux[i], x));
        }
        vTaskDelay(pdMS_TO_TICKS(PERIOD_MS));
    }
}

esp_err_t src_adc_start(void)
{
    adc_oneshot_unit_init_cfg_t uc = { .unit_id = ADC_UNIT_2, .ulp_mode = ADC_ULP_MODE_DISABLE };
    esp_err_t e = adc_oneshot_new_unit(&uc, &s_unit);
    if (e != ESP_OK) { ESP_LOGW(TAG, "ADC2 unit: %s", esp_err_to_name(e)); return e; }
    adc_oneshot_chan_cfg_t cc = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    for (int i = 0; i < AUX_N; i++) adc_oneshot_config_channel(s_unit, CHAN[i], &cc);
    adc_cali_curve_fitting_config_t cal = { .unit_id = ADC_UNIT_2, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    if (adc_cali_create_scheme_curve_fitting(&cal, &s_cali) != ESP_OK) { s_cali = NULL; ESP_LOGW(TAG, "no ADC calibration, using nominal"); }
    src_adc_apply();
    if (xTaskCreatePinnedToCore(adc_task, "adc", 3072, NULL, 4, NULL, 1) != pdPASS) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "analog inputs on GPIO17/18 (%s, %s)", g_aux[0].enabled ? "on" : "off", g_aux[1].enabled ? "on" : "off");
    return ESP_OK;
}

float src_adc_raw_volts(int i) { return (i >= 0 && i < AUX_N) ? s_volts[i] : NAN; }
float src_adc_raw_ohms(int i)  { return (i >= 0 && i < AUX_N) ? s_ohms[i]  : NAN; }
