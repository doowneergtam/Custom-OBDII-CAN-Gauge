// Board abstraction. The only component allowed to know which board we run
// on. Pod build wraps the Waveshare ESP32-S3-Touch-AMOLED-1.75 BSP; the P4
// cluster build will provide a second implementation of this header.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Pod header pin assignment (docs/pod-pinout.md)
#define HAL_PIN_CAN_TX      44   // U0RXD: input during boot, pulled up = CAN recessive
#define HAL_PIN_CAN_RX      43   // U0TXD: ROM boot log here for ~300 ms; carrier has 1k in series
#define HAL_PIN_ENC_A       18
#define HAL_PIN_ENC_B       17
#define HAL_PIN_ENC_PUSH    16

#define HAL_DISPLAY_W       466
#define HAL_DISPLAY_H       466

// Bring up I2C, PMIC/expander, display and LVGL. Returns when LVGL is
// running and lv_screen_active() is usable (under hal_board_lock).
esp_err_t     hal_board_init(void);
lv_display_t *hal_board_display(void);
lv_indev_t   *hal_board_touch(void);     // may be NULL

// Any LVGL call from outside the LVGL task must be wrapped in these.
bool hal_board_lock(uint32_t timeout_ms);
void hal_board_unlock(void);

// 0..100. AMOLED has no backlight; this drives the panel's own brightness.
esp_err_t hal_board_set_brightness(uint8_t percent);

// Speaker: 16-bit mono PCM at the given sample rate through the codec.
esp_err_t hal_board_audio_init(uint32_t sample_rate);
void      hal_board_audio_write_begin(void);          // unmute / enable amp
void      hal_board_audio_write(const void *pcm, size_t bytes);
void      hal_board_audio_write_end(void);            // mute again

// Config storage: mount the "config" LittleFS partition at /config.
esp_err_t hal_board_storage_init(void);

#ifdef __cplusplus
}
#endif
