// Command console on the USB serial port, for bench testing and scripted
// soak runs (tools/soak.py). Drives the UI the way the encoder, touch and
// the phone page do, and reports memory and source statistics.
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t gauge_console_start(void);

#ifdef __cplusplus
}
#endif
