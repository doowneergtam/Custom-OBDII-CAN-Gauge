// Persistent settings: one JSON file on LittleFS (/config/gauge.json), in the
// same schema the phone page speaks (see design/web-config/index.html), so
// the page can GET and PUT it unchanged.
// Writes are debounced (5 s after the last change) and atomic (tmp + rename).
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "channels.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_CELLS_MAX 7

typedef enum { SRC_MEGASQUIRT = 0, SRC_OBD = 1 } data_source_t;

typedef struct {
    uint8_t layout;                 // 1, 3, 4, 6, 7
    uint8_t cells[UI_CELLS_MAX];    // ch_id_t per cell
    uint8_t brightness;             // 0..100
    bool    sim;                    // synthetic engine instead of the bus
    data_source_t source;           // what the CAN listener expects
    char    wifi_ssid[33];          // empty = NB1-Gauge-<mac>
    char    wifi_password[65];      // empty = default
    uint8_t wifi_timeout_min;       // 0 = never
    bool    wifi_on;                // start the hotspot at boot
} ui_cfg_t;

extern ui_cfg_t g_ui_cfg;

void      config_defaults(void);     // g_ui_cfg + theme preset 0 + alarm defaults
esp_err_t config_load(void);         // apply file over defaults; ESP_ERR_NOT_FOUND if none
void      config_mark_dirty(void);   // schedule a save
esp_err_t config_save_now(void);

// Whole config as JSON text (caller frees), and the reverse. Used by the
// file and by the web interface.
char     *config_to_json(void);
esp_err_t config_apply_json(const char *text);

// Short channel id the page uses ("OILP"), in ch_id_t order.
const char *config_channel_id(ch_id_t id);

// User colour presets and saved screens (layout + cells + theme), kept as
// JSON subtrees of the config so the page and the gauge see the same list.
int         config_preset_count(void);
const char *config_preset_name(int i);
esp_err_t   config_apply_preset(int i);          // colours only
int         config_screen_count(void);
const char *config_screen_name(int i);
esp_err_t   config_apply_screen(int i);          // layout, cells, theme
esp_err_t   config_save_screen(const char *name); // snapshot of the current ones

#ifdef __cplusplus
}
#endif
