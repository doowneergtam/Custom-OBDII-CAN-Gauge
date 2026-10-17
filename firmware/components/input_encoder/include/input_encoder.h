// Rotary encoder with push button, exposed to LVGL as a keypad input device.
//
// Rotation -> LV_KEY_LEFT / LV_KEY_RIGHT key events on the focused object.
// Push     -> LV_KEY_ENTER held for the real press duration, so LVGL itself
//             produces SHORT_CLICKED / LONG_PRESSED on the focused object.
//
// Keypad semantics were chosen over LVGL's encoder indev because they are
// unambiguous with a single focused object (the gauge face) and work the
// same for rollers in menus.
#pragma once

#include "lvgl.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int  gpio_a;
    int  gpio_b;
    int  gpio_push;          // active low, internal pull-up
    bool reverse;            // swap rotation direction
    uint16_t long_press_ms;  // default 600
} input_encoder_config_t;

// Must be called with the LVGL lock held. Creates the indev and a group and
// makes that group the default (lv_group_get_default()).
esp_err_t   input_encoder_init(const input_encoder_config_t *cfg, lv_display_t *disp);
lv_indev_t *input_encoder_indev(void);
lv_group_t *input_encoder_group(void);

#ifdef __cplusplus
}
#endif
