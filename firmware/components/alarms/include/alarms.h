// Alarms: per-channel tone per zone level, master volume, and the engine
// that watches every channel. On-screen channels sound; off-screen channels
// raise a banner instead (ui registers the banner callback).
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "channels.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { TONE_OFF = 0, TONE_CHIRP, TONE_DOUBLE, TONE_BUZZ, TONE_RISING, TONE_SIREN, TONE_COUNT } alarm_tone_t;

typedef struct {
    uint8_t volume;            // 0..100
    bool    warn_on;
    bool    bad_on;
    bool    mute;
    uint8_t repeat_s;          // 0 = once, else re-alarm interval while in a bad zone
    uint8_t tone_warn[CH_MAX]; // alarm_tone_t per channel
    uint8_t tone_bad[CH_MAX];
} alarm_cfg_t;

extern alarm_cfg_t g_alarms;

typedef void (*alarm_banner_cb_t)(ch_id_t id, ch_zone_level_t level);

void        alarms_init(void);                    // defaults + audio
void        alarms_load_defaults(void);
const char *alarms_tone_name(alarm_tone_t t);
void        alarms_set_banner_cb(alarm_banner_cb_t cb);

// Call every UI tick. `visible` lists the channel ids currently on screen.
void        alarms_tick(int64_t now_us, const ch_id_t *visible, int n_visible);

// Audio
esp_err_t   audio_init(void);
void        audio_play(alarm_tone_t tone, uint8_t volume);   // non-blocking

#ifdef __cplusplus
}
#endif
