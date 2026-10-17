// Analog sensor inputs (carrier rev C): two 0-5 V or resistive senders on
// GPIO17 / GPIO18, scaled by user calibration points into channels AUX1/AUX2.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUX_N        2
#define AUX_PTS_MAX  8

typedef struct {
    bool    enabled;        // off = channel hidden, no reads
    bool    resistive;      // solder jumper closed: x axis is sender ohms, else sensor volts
    char    label[9];       // channel name shown on the gauge
    char    units[8];
    uint8_t decimals;       // 0..3
    uint8_t npts;           // calibration points, sorted by x
    float   x[AUX_PTS_MAX]; // volts at the sensor node (or ohms)
    float   y[AUX_PTS_MAX]; // value in the channel's units
} aux_cfg_t;

extern aux_cfg_t g_aux[AUX_N];

void      src_adc_defaults(void);
void      src_adc_apply(void);           // push label/units/decimals/enabled into the channel defs
esp_err_t src_adc_start(void);
float     src_adc_raw_volts(int i);      // latest sensor-node volts, NAN if off
float     src_adc_raw_ohms(int i);       // latest sender ohms in resistive mode, NAN otherwise

#ifdef __cplusplus
}
#endif
