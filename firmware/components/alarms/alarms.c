#include "alarms.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "alarms";

alarm_cfg_t g_alarms;
static alarm_banner_cb_t s_banner;

typedef struct { ch_zone_level_t level; int64_t last_fire_us; } ch_alarm_t;
static ch_alarm_t s_st[CH_MAX];

static const char *TONE_NAMES[TONE_COUNT] = { "Off", "Chirp", "Double", "Buzz", "Rising", "Siren" };
const char *alarms_tone_name(alarm_tone_t t) { return t < TONE_COUNT ? TONE_NAMES[t] : "?"; }

void alarms_load_defaults(void)
{
    memset(&g_alarms, 0, sizeof g_alarms);
    g_alarms.volume = 60; g_alarms.warn_on = true; g_alarms.bad_on = true; g_alarms.repeat_s = 10;
    // Shipped defaults: every channel with zones chirps on warn; bad escalates.
    for (int i = 0; i < channels_count(); i++) {
        const ch_def_t *d = channels_def((ch_id_t)i);
        if (!d || d->zone_count == 0) continue;
        g_alarms.tone_warn[i] = TONE_CHIRP;
        g_alarms.tone_bad[i]  = TONE_DOUBLE;
    }
    g_alarms.tone_bad[CH_OILP] = TONE_SIREN;
    g_alarms.tone_bad[CH_KNOCK] = TONE_SIREN;
    g_alarms.tone_bad[CH_EGT] = TONE_SIREN;
    g_alarms.tone_bad[CH_RPM] = TONE_BUZZ;
    g_alarms.tone_warn[CH_EGOCOR] = TONE_OFF;
}

void alarms_init(void)
{
    alarms_load_defaults();
    memset(s_st, 0, sizeof s_st);
    if (audio_init() != ESP_OK) ESP_LOGW(TAG, "audio unavailable, alarms are banner-only");
}

void alarms_set_banner_cb(alarm_banner_cb_t cb) { s_banner = cb; }

static int severity(ch_zone_level_t l) { return l == CH_ZONE_BAD ? 2 : l == CH_ZONE_WARN ? 1 : 0; }

void alarms_tick(int64_t now_us, const ch_id_t *visible, int n_visible)
{
    for (int i = 0; i < channels_count(); i++) {
        ch_id_t id = (ch_id_t)i;
        if (channels_freshness(id, now_us) != CH_FRESH) { s_st[i].level = CH_ZONE_GOOD; continue; }
        ch_zone_level_t level = channels_zone(id);
        ch_alarm_t *st = &s_st[i];
        int sev = severity(level), psev = severity(st->level);
        bool fire = false;
        if (sev > psev) fire = true;
        else if (sev == 2 && g_alarms.repeat_s && now_us - st->last_fire_us > (int64_t)g_alarms.repeat_s * 1000000) fire = true;
        if (fire) {
            bool enabled = (level == CH_ZONE_WARN && g_alarms.warn_on) || (level == CH_ZONE_BAD && g_alarms.bad_on);
            alarm_tone_t tone = (alarm_tone_t)(level == CH_ZONE_BAD ? g_alarms.tone_bad[i] : g_alarms.tone_warn[i]);
            if (enabled && tone != TONE_OFF) {
                bool on_screen = false;
                for (int k = 0; k < n_visible; k++) if (visible[k] == id) { on_screen = true; break; }
                if (on_screen) { if (!g_alarms.mute) audio_play(tone, g_alarms.volume); }
                else if (s_banner) s_banner(id, level);
            }
            st->last_fire_us = now_us;
        }
        st->level = level;
    }
}
