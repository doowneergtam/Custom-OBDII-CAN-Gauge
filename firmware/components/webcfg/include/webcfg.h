// Phone setup page over the gauge's own Wi-Fi hotspot.
//
// SoftAP + captive DNS + mDNS (gauge.local) + HTTP server. Serves the page
// embedded at build time (tools/build_web.py) and the JSON interface the
// page uses: GET/PUT /api/config, GET /api/state, POST /api/<action>.
// Switches itself off after an idle timeout so the radio is not running
// while driving. The ui component owns start/stop from its System menu.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*reload)(void);          // config changed on the page: re-apply everything
    void (*reset_minmax)(void);
    void (*identify)(void);        // flash the display
    void (*test_tone)(int tone);   // alarm_tone_t
} webcfg_hooks_t;

void        webcfg_init(const webcfg_hooks_t *hooks);   // once, before start
esp_err_t   webcfg_start(void);
void        webcfg_stop(void);
bool        webcfg_is_on(void);
const char *webcfg_ssid(void);      // valid while on
const char *webcfg_password(void);
int         webcfg_clients(void);

#ifdef __cplusplus
}
#endif
