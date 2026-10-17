#include "gauge_config.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "theme.h"
#include "alarms.h"
#include "src_adc.h"

static const char *TAG = "config";
#define PATH     "/config/gauge.json"
#define PATH_TMP "/config/gauge.json.tmp"
#define SAVE_DELAY_US (5 * 1000 * 1000)

ui_cfg_t g_ui_cfg;
static esp_timer_handle_t s_save_timer;
static const char *str(cJSON *o, const char *k) { cJSON *v = o ? cJSON_GetObjectItem(o, k) : NULL; return cJSON_IsString(v) ? v->valuestring : NULL; }
// Floats go out rounded to 4 decimals as exact doubles, so 1.3f is "1.3" on the page, not 1.2999999523.
static double num4(float v) { return (double)llround((double)v * 10000.0) / 10000.0; }
static int user_preset_match(void);
static cJSON *s_presets, *s_screens;   // arrays owned here, duplicated into every build_json
#define USER_LIST_MAX 8

// Page ids in ch_id_t order (channels.h). The page keys cells, tones and
// channel settings by these.
static const char *CH_IDS[] = {
    "RPM", "BOOST", "MAP", "CLT", "OILP", "OILT", "AFR", "AFRTGT", "EGOCOR", "MAT", "EGT",
    "TPS", "ADV", "PW1", "DUTY", "BATT", "KNOCK", "VSS", "TCRET", "LAUNCH",
    "LOAD", "MAF", "STFT", "LTFT", "O2", "AUX1", "AUX2",
};
#define CH_IDS_N ((int)(sizeof CH_IDS / sizeof CH_IDS[0]))
static const char *THEME_KEYS[TC_COUNT] = { "bg", "digits", "label", "units", "track", "tick", "neutral", "good", "warn", "bad", "cold", "stale" };
static const char *STYLE_IDS[STYLE_COUNT] = { "ring", "dial", "numeric" };
static const char *FILL_IDS[FILL_COUNT]   = { "gradient", "segments", "solid" };
static const char *LEVEL_IDS[4]           = { "cold", "good", "warn", "bad" };

const char *config_channel_id(ch_id_t id) { return (int)id < CH_IDS_N ? CH_IDS[id] : "?"; }

static int ch_by_id(const char *s)
{
    if (!s) return -1;
    for (int i = 0; i < channels_count() && i < CH_IDS_N; i++) {
        if (strcasecmp(CH_IDS[i], s) == 0 || strcasecmp(channels_def((ch_id_t)i)->name, s) == 0) return i;
    }
    return -1;
}
static int index_of(const char *const *list, int n, const char *s, int def)
{
    if (!s) return def;
    for (int i = 0; i < n; i++) if (strcasecmp(list[i], s) == 0) return i;
    return def;
}
static int tone_by_name(const char *s, int def)
{
    if (!s) return def;
    for (int i = 0; i < TONE_COUNT; i++) if (strcasecmp(alarms_tone_name((alarm_tone_t)i), s) == 0) return i;
    return def;
}

void config_defaults(void)
{
    memset(&g_ui_cfg, 0, sizeof g_ui_cfg);
    g_ui_cfg.layout = 1;
    const uint8_t def_cells[UI_CELLS_MAX] = { CH_BOOST, CH_OILP, CH_CLT, CH_AFR, CH_BATT, CH_OILT, CH_RPM };
    memcpy(g_ui_cfg.cells, def_cells, sizeof def_cells);
    g_ui_cfg.brightness = 80;
    g_ui_cfg.sim = true; g_ui_cfg.source = SRC_MEGASQUIRT;
    g_ui_cfg.wifi_timeout_min = 0;
    g_ui_cfg.wifi_on = true;
    g_theme.style = STYLE_RING; g_theme.fill = FILL_GRADIENT; g_theme.bands = true;
    g_theme.digit_scale = 1.0f; g_theme.label_scale = 1.0f; g_theme.cell_scale = 1.0f;
    g_theme.units_scale = 1.0f; g_theme.minmax_scale = 1.0f; g_theme.bar_scale = 1.0f;
    theme_apply_preset(0);
    alarms_load_defaults();
    channels_set_units((ch_units_t){ false, false, false });
    src_adc_defaults(); src_adc_apply();
}

// ---------------------------------------------------------------- to JSON
static cJSON *build_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "layout", g_ui_cfg.layout);
    cJSON *cells = cJSON_AddArrayToObject(root, "cells");
    for (int i = 0; i < UI_CELLS_MAX; i++) cJSON_AddItemToArray(cells, cJSON_CreateString(config_channel_id((ch_id_t)g_ui_cfg.cells[i])));
    cJSON_AddNumberToObject(root, "brightness", g_ui_cfg.brightness);
    cJSON_AddBoolToObject(root, "sim", g_ui_cfg.sim);
    cJSON_AddStringToObject(root, "source", g_ui_cfg.source == SRC_OBD ? "obd" : "ms");

    ch_units_t u = channels_get_units();
    cJSON *units = cJSON_AddObjectToObject(root, "units");
    cJSON_AddBoolToObject(units, "temp_c", u.temp_c);
    cJSON_AddBoolToObject(units, "press_bar", u.press_bar);
    cJSON_AddBoolToObject(units, "speed_kmh", u.speed_kmh);

    cJSON *theme = cJSON_AddObjectToObject(root, "theme");
    cJSON_AddStringToObject(theme, "preset", g_theme.preset >= 0 ? theme_preset_name(g_theme.preset) : (config_preset_count() && user_preset_match() >= 0 ? config_preset_name(user_preset_match()) : "Custom"));
    cJSON_AddStringToObject(theme, "style", STYLE_IDS[g_theme.style < STYLE_COUNT ? g_theme.style : 0]);
    cJSON_AddStringToObject(theme, "fill", FILL_IDS[g_theme.fill < FILL_COUNT ? g_theme.fill : 0]);
    cJSON_AddBoolToObject(theme, "bands", g_theme.bands);
    cJSON_AddNumberToObject(theme, "digit_scale", num4(g_theme.digit_scale));
    cJSON_AddNumberToObject(theme, "label_scale", num4(g_theme.label_scale));
    cJSON_AddNumberToObject(theme, "cell_scale", num4(g_theme.cell_scale));
    cJSON_AddNumberToObject(theme, "units_scale", num4(g_theme.units_scale));
    cJSON_AddNumberToObject(theme, "minmax_scale", num4(g_theme.minmax_scale));
    cJSON_AddNumberToObject(theme, "bar_scale", num4(g_theme.bar_scale));
    cJSON *rgb = cJSON_AddObjectToObject(theme, "rgb");
    for (int i = 0; i < TC_COUNT; i++) { char hex[9]; snprintf(hex, sizeof hex, "#%06lx", (unsigned long)g_theme.rgb[i]); cJSON_AddStringToObject(rgb, THEME_KEYS[i], hex); }

    cJSON *al = cJSON_AddObjectToObject(root, "alarms");
    cJSON_AddNumberToObject(al, "volume", g_alarms.volume);
    cJSON_AddBoolToObject(al, "warn_on", g_alarms.warn_on);
    cJSON_AddBoolToObject(al, "bad_on", g_alarms.bad_on);
    cJSON_AddBoolToObject(al, "mute", g_alarms.mute);
    cJSON_AddNumberToObject(al, "repeat_s", g_alarms.repeat_s);
    cJSON *tw = cJSON_AddObjectToObject(al, "tone_warn"), *tb = cJSON_AddObjectToObject(al, "tone_bad");
    for (int i = 0; i < channels_count(); i++) {
        const char *n = config_channel_id((ch_id_t)i);
        cJSON_AddStringToObject(tw, n, alarms_tone_name((alarm_tone_t)g_alarms.tone_warn[i]));
        cJSON_AddStringToObject(tb, n, alarms_tone_name((alarm_tone_t)g_alarms.tone_bad[i]));
    }

    cJSON *chs = cJSON_AddObjectToObject(root, "channels");
    for (int i = 0; i < channels_count(); i++) {
        const ch_def_t *d = channels_def((ch_id_t)i);
        cJSON *c = cJSON_AddObjectToObject(chs, config_channel_id((ch_id_t)i));
        cJSON_AddNumberToObject(c, "min", num4(d->min));
        cJSON_AddNumberToObject(c, "max", num4(d->max));
        cJSON_AddBoolToObject(c, "show_units", d->show_units);
        cJSON_AddBoolToObject(c, "show_min", d->show_min);
        cJSON_AddBoolToObject(c, "show_max", d->show_max);
        cJSON *z = cJSON_AddArrayToObject(c, "zones");
        for (int k = 0; k < d->zone_count; k++) {
            cJSON *zz = cJSON_CreateArray();
            cJSON_AddItemToArray(zz, cJSON_CreateNumber(num4(d->zones[k].from)));
            cJSON_AddItemToArray(zz, cJSON_CreateNumber(num4(d->zones[k].to)));
            cJSON_AddItemToArray(zz, cJSON_CreateString(LEVEL_IDS[d->zones[k].level & 3]));
            cJSON_AddItemToArray(z, zz);
        }
    }

    cJSON *aux = cJSON_AddObjectToObject(root, "aux");
    for (int i = 0; i < AUX_N; i++) {
        char key[2] = { (char)('1' + i), 0 };
        cJSON *a = cJSON_AddObjectToObject(aux, key);
        cJSON_AddBoolToObject(a, "enabled", g_aux[i].enabled);
        cJSON_AddBoolToObject(a, "resistive", g_aux[i].resistive);
        cJSON_AddStringToObject(a, "label", g_aux[i].label);
        cJSON_AddStringToObject(a, "units", g_aux[i].units);
        cJSON_AddNumberToObject(a, "decimals", g_aux[i].decimals);
        cJSON *pts = cJSON_AddArrayToObject(a, "pts");
        for (int k = 0; k < g_aux[i].npts; k++) {
            cJSON *p = cJSON_CreateArray();
            cJSON_AddItemToArray(p, cJSON_CreateNumber(num4(g_aux[i].x[k])));
            cJSON_AddItemToArray(p, cJSON_CreateNumber(num4(g_aux[i].y[k])));
            cJSON_AddItemToArray(pts, p);
        }
    }

    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    cJSON_AddStringToObject(wifi, "ssid", g_ui_cfg.wifi_ssid);
    cJSON_AddStringToObject(wifi, "password", g_ui_cfg.wifi_password);
    cJSON_AddNumberToObject(wifi, "timeout_min", g_ui_cfg.wifi_timeout_min);
    cJSON_AddBoolToObject(wifi, "on", g_ui_cfg.wifi_on);
    cJSON_AddItemToObject(root, "theme_presets", s_presets ? cJSON_Duplicate(s_presets, 1) : cJSON_CreateArray());
    cJSON_AddItemToObject(root, "screens", s_screens ? cJSON_Duplicate(s_screens, 1) : cJSON_CreateArray());
    return root;
}

char *config_to_json(void)
{
    cJSON *root = build_json();
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) return NULL;
    char *copy = strdup(text);   // caller frees with free()
    cJSON_free(text);
    return copy;
}

// ---------------------------------------------------------------- from JSON
static double num(cJSON *o, const char *k, double def) { cJSON *v = cJSON_GetObjectItem(o, k); return cJSON_IsNumber(v) ? v->valuedouble : def; }
static bool   bln(cJSON *o, const char *k, bool def)   { cJSON *v = cJSON_GetObjectItem(o, k); return cJSON_IsBool(v) ? cJSON_IsTrue(v) : def; }
static double zval(cJSON *v, double def)               { return cJSON_IsNumber(v) ? v->valuedouble : def; }

esp_err_t config_apply_json(const char *text)
{
    cJSON *root = cJSON_Parse(text);
    if (!root) return ESP_ERR_INVALID_ARG;

    int layout = (int)num(root, "layout", g_ui_cfg.layout);
    if (layout == 1 || layout == 3 || layout == 4 || layout == 6 || layout == 7) g_ui_cfg.layout = layout;
    cJSON *cells = cJSON_GetObjectItem(root, "cells");
    if (cJSON_IsArray(cells)) { int i = 0; cJSON *it; cJSON_ArrayForEach(it, cells) { if (i >= UI_CELLS_MAX) break; int id = cJSON_IsString(it) ? ch_by_id(it->valuestring) : -1; if (id >= 0) g_ui_cfg.cells[i] = id; i++; } }
    int br = (int)num(root, "brightness", g_ui_cfg.brightness); if (br >= 5 && br <= 100) g_ui_cfg.brightness = br;
    g_ui_cfg.sim = bln(root, "sim", g_ui_cfg.sim);
    const char *src = str(root, "source"); if (src) g_ui_cfg.source = strcasecmp(src, "obd") == 0 ? SRC_OBD : SRC_MEGASQUIRT;

    cJSON *units = cJSON_GetObjectItem(root, "units");
    if (units) channels_set_units((ch_units_t){ bln(units, "temp_c", false), bln(units, "press_bar", false), bln(units, "speed_kmh", false) });

    cJSON *theme = cJSON_GetObjectItem(root, "theme");
    if (theme) {
        const char *p = str(theme, "preset");
        g_theme.preset = -1;
        if (p) for (int i = 0; i < theme_preset_count(); i++) if (strcasecmp(theme_preset_name(i), p) == 0) g_theme.preset = i;
        g_theme.style = (gauge_style_t)index_of(STYLE_IDS, STYLE_COUNT, str(theme, "style"), g_theme.style);
        g_theme.fill  = (bar_fill_t)index_of(FILL_IDS, FILL_COUNT, str(theme, "fill"), g_theme.fill);
        g_theme.bands = bln(theme, "bands", g_theme.bands);
        float ds = (float)num(theme, "digit_scale", g_theme.digit_scale), ls = (float)num(theme, "label_scale", g_theme.label_scale);
        if (ds >= 0.3f && ds <= 5.1f) g_theme.digit_scale = ds;
        if (ls >= 0.3f && ls <= 4.1f) g_theme.label_scale = ls;
        float cs = (float)num(theme, "cell_scale", g_theme.cell_scale);
        if (cs >= 1.0f && cs <= 2.6f) g_theme.cell_scale = cs;
        float us = (float)num(theme, "units_scale", g_theme.units_scale), ms = (float)num(theme, "minmax_scale", g_theme.minmax_scale);
        if (us >= 0.3f && us <= 4.1f) g_theme.units_scale = us;
        if (ms >= 0.3f && ms <= 4.1f) g_theme.minmax_scale = ms;
        float bs = (float)num(theme, "bar_scale", g_theme.bar_scale);
        if (bs >= 0.3f && bs <= 3.1f) g_theme.bar_scale = bs;
        cJSON *rgb = cJSON_GetObjectItem(theme, "rgb");
        if (cJSON_IsObject(rgb)) for (int i = 0; i < TC_COUNT; i++) { const char *h = str(rgb, THEME_KEYS[i]); if (h && h[0] == '#') g_theme.rgb[i] = (uint32_t)strtoul(h + 1, NULL, 16) & 0xFFFFFF; }
    }

    cJSON *al = cJSON_GetObjectItem(root, "alarms");
    if (al) {
        int vol = (int)num(al, "volume", g_alarms.volume); if (vol >= 0 && vol <= 100) g_alarms.volume = vol;
        g_alarms.warn_on = bln(al, "warn_on", g_alarms.warn_on);
        g_alarms.bad_on  = bln(al, "bad_on", g_alarms.bad_on);
        g_alarms.mute    = bln(al, "mute", g_alarms.mute);
        g_alarms.repeat_s = (uint8_t)num(al, "repeat_s", g_alarms.repeat_s);
        cJSON *tw = cJSON_GetObjectItem(al, "tone_warn"), *tb = cJSON_GetObjectItem(al, "tone_bad"), *it;
        if (tw) cJSON_ArrayForEach(it, tw) { int id = ch_by_id(it->string); if (id >= 0 && cJSON_IsString(it)) g_alarms.tone_warn[id] = tone_by_name(it->valuestring, g_alarms.tone_warn[id]); }
        if (tb) cJSON_ArrayForEach(it, tb) { int id = ch_by_id(it->string); if (id >= 0 && cJSON_IsString(it)) g_alarms.tone_bad[id] = tone_by_name(it->valuestring, g_alarms.tone_bad[id]); }
    }

    cJSON *chs = cJSON_GetObjectItem(root, "channels"), *c;
    if (chs) cJSON_ArrayForEach(c, chs) {
        int id = ch_by_id(c->string);
        if (id < 0) continue;
        ch_def_t *d = channels_def_mut((ch_id_t)id);
        float mn = (float)num(c, "min", d->min), mx = (float)num(c, "max", d->max);
        if (mx > mn) { d->min = mn; d->max = mx; }
        d->show_units = bln(c, "show_units", d->show_units);
        d->show_min = bln(c, "show_min", d->show_min);
        d->show_max = bln(c, "show_max", d->show_max);
        cJSON *z = cJSON_GetObjectItem(c, "zones");
        if (cJSON_IsArray(z)) {
            d->zone_count = 0; cJSON *zz;
            cJSON_ArrayForEach(zz, z) {
                if (d->zone_count >= CH_ZONES_MAX || !cJSON_IsArray(zz) || cJSON_GetArraySize(zz) < 3) continue;
                cJSON *lv = cJSON_GetArrayItem(zz, 2);
                int level = cJSON_IsString(lv) ? index_of(LEVEL_IDS, 4, lv->valuestring, CH_ZONE_GOOD) : cJSON_IsNumber(lv) ? (lv->valueint & 3) : CH_ZONE_GOOD;
                d->zones[d->zone_count++] = (ch_zone_t){ (float)zval(cJSON_GetArrayItem(zz, 0), -1e9), (float)zval(cJSON_GetArrayItem(zz, 1), 1e9), (ch_zone_level_t)level };
            }
        }
    }

    cJSON *aux = cJSON_GetObjectItem(root, "aux");
    if (cJSON_IsObject(aux)) {
        for (int i = 0; i < AUX_N; i++) {
            char key[2] = { (char)('1' + i), 0 };
            cJSON *a = cJSON_GetObjectItem(aux, key);
            if (!cJSON_IsObject(a)) continue;
            aux_cfg_t *g = &g_aux[i];
            g->enabled = bln(a, "enabled", g->enabled);
            g->resistive = bln(a, "resistive", g->resistive);
            const char *l = str(a, "label"), *u = str(a, "units");
            if (l) snprintf(g->label, sizeof g->label, "%s", l);
            if (u) snprintf(g->units, sizeof g->units, "%s", u);
            int dp = (int)num(a, "decimals", g->decimals); if (dp >= 0 && dp <= 3) g->decimals = (uint8_t)dp;
            cJSON *pts = cJSON_GetObjectItem(a, "pts");
            if (cJSON_IsArray(pts)) {
                g->npts = 0; cJSON *p;
                cJSON_ArrayForEach(p, pts) {
                    if (g->npts >= AUX_PTS_MAX || !cJSON_IsArray(p) || cJSON_GetArraySize(p) < 2) continue;
                    g->x[g->npts] = (float)zval(cJSON_GetArrayItem(p, 0), 0); g->y[g->npts] = (float)zval(cJSON_GetArrayItem(p, 1), 0); g->npts++;
                }
                for (int m = 1; m < g->npts; m++) for (int k = m; k > 0 && g->x[k] < g->x[k - 1]; k--) {   // keep sorted by x
                    float tx = g->x[k], ty = g->y[k]; g->x[k] = g->x[k - 1]; g->y[k] = g->y[k - 1]; g->x[k - 1] = tx; g->y[k - 1] = ty;
                }
            }
        }
        src_adc_apply();
    }

    cJSON *wifi = cJSON_GetObjectItem(root, "wifi");
    if (wifi) {
        const char *s = str(wifi, "ssid"), *p = str(wifi, "password");
        if (s) snprintf(g_ui_cfg.wifi_ssid, sizeof g_ui_cfg.wifi_ssid, "%s", s);
        if (p && (strlen(p) == 0 || strlen(p) >= 8)) snprintf(g_ui_cfg.wifi_password, sizeof g_ui_cfg.wifi_password, "%s", p);
        g_ui_cfg.wifi_timeout_min = (uint8_t)num(wifi, "timeout_min", g_ui_cfg.wifi_timeout_min);
        g_ui_cfg.wifi_on = bln(wifi, "on", g_ui_cfg.wifi_on);
    }
    cJSON *tp = cJSON_GetObjectItem(root, "theme_presets");
    if (cJSON_IsArray(tp)) { if (s_presets) cJSON_Delete(s_presets); s_presets = cJSON_Duplicate(tp, 1); while (cJSON_GetArraySize(s_presets) > USER_LIST_MAX) cJSON_DeleteItemFromArray(s_presets, USER_LIST_MAX); }
    cJSON *sc = cJSON_GetObjectItem(root, "screens");
    if (cJSON_IsArray(sc)) { if (s_screens) cJSON_Delete(s_screens); s_screens = cJSON_Duplicate(sc, 1); while (cJSON_GetArraySize(s_screens) > USER_LIST_MAX) cJSON_DeleteItemFromArray(s_screens, USER_LIST_MAX); }
    cJSON_Delete(root);
    return ESP_OK;
}

// ---------------------------------------------------------------- user presets and screens
static int user_preset_match(void)
{
    for (int i = 0; i < config_preset_count(); i++) {
        cJSON *rgb = cJSON_GetObjectItem(cJSON_GetArrayItem(s_presets, i), "rgb");
        bool same = cJSON_IsObject(rgb);
        for (int k = 0; same && k < TC_COUNT; k++) { const char *h = str(rgb, THEME_KEYS[k]); same = h && h[0] == '#' && ((uint32_t)strtoul(h + 1, NULL, 16) & 0xFFFFFF) == g_theme.rgb[k]; }
        if (same) return i;
    }
    return -1;
}
int config_preset_count(void) { return s_presets ? cJSON_GetArraySize(s_presets) : 0; }
const char *config_preset_name(int i) { const char *n = s_presets ? str(cJSON_GetArrayItem(s_presets, i), "name") : NULL; return n ? n : "?"; }
esp_err_t config_apply_preset(int i)
{
    cJSON *p = s_presets ? cJSON_GetArrayItem(s_presets, i) : NULL;
    cJSON *rgb = p ? cJSON_GetObjectItem(p, "rgb") : NULL;
    if (!cJSON_IsObject(rgb)) return ESP_ERR_NOT_FOUND;
    for (int k = 0; k < TC_COUNT; k++) { const char *h = str(rgb, THEME_KEYS[k]); if (h && h[0] == '#') g_theme.rgb[k] = (uint32_t)strtoul(h + 1, NULL, 16) & 0xFFFFFF; }
    g_theme.preset = -1;
    return ESP_OK;
}
int config_screen_count(void) { return s_screens ? cJSON_GetArraySize(s_screens) : 0; }
const char *config_screen_name(int i) { const char *n = s_screens ? str(cJSON_GetArrayItem(s_screens, i), "name") : NULL; return n ? n : "?"; }
esp_err_t config_apply_screen(int i)
{
    cJSON *s = s_screens ? cJSON_GetArrayItem(s_screens, i) : NULL;
    if (!s) return ESP_ERR_NOT_FOUND;
    // Re-use the full parser on a copy that carries only the screen's fields.
    cJSON *tmp = cJSON_CreateObject();
    cJSON *l = cJSON_GetObjectItem(s, "layout"), *c = cJSON_GetObjectItem(s, "cells"), *t = cJSON_GetObjectItem(s, "theme");
    if (l) cJSON_AddItemToObject(tmp, "layout", cJSON_Duplicate(l, 1));
    if (c) cJSON_AddItemToObject(tmp, "cells", cJSON_Duplicate(c, 1));
    if (t) cJSON_AddItemToObject(tmp, "theme", cJSON_Duplicate(t, 1));
    char *text = cJSON_PrintUnformatted(tmp);
    cJSON_Delete(tmp);
    if (!text) return ESP_ERR_NO_MEM;
    esp_err_t err = config_apply_json(text);
    cJSON_free(text);
    return err;
}
esp_err_t config_save_screen(const char *name)
{
    if (!s_screens) s_screens = cJSON_CreateArray();
    cJSON *full = build_json();
    cJSON *snap = cJSON_CreateObject();
    cJSON_AddStringToObject(snap, "name", name);
    cJSON_AddItemToObject(snap, "layout", cJSON_Duplicate(cJSON_GetObjectItem(full, "layout"), 1));
    cJSON_AddItemToObject(snap, "cells", cJSON_Duplicate(cJSON_GetObjectItem(full, "cells"), 1));
    cJSON_AddItemToObject(snap, "theme", cJSON_Duplicate(cJSON_GetObjectItem(full, "theme"), 1));
    cJSON_Delete(full);
    for (int i = 0; i < cJSON_GetArraySize(s_screens); i++) {
        const char *n = str(cJSON_GetArrayItem(s_screens, i), "name");
        if (n && strcasecmp(n, name) == 0) { cJSON_ReplaceItemInArray(s_screens, i, snap); return ESP_OK; }
    }
    if (cJSON_GetArraySize(s_screens) >= USER_LIST_MAX) { cJSON_Delete(snap); return ESP_ERR_NO_MEM; }
    cJSON_AddItemToArray(s_screens, snap);
    return ESP_OK;
}

// ---------------------------------------------------------------- file
esp_err_t config_save_now(void)
{
    char *text = config_to_json();
    if (!text) return ESP_ERR_NO_MEM;
    FILE *f = fopen(PATH_TMP, "w");
    if (!f) { ESP_LOGE(TAG, "open %s failed", PATH_TMP); free(text); return ESP_FAIL; }
    size_t n = strlen(text), w = fwrite(text, 1, n, f);
    fclose(f);
    free(text);
    if (w != n) { unlink(PATH_TMP); return ESP_FAIL; }
    unlink(PATH);
    if (rename(PATH_TMP, PATH) != 0) { ESP_LOGE(TAG, "rename failed"); return ESP_FAIL; }
    ESP_LOGI(TAG, "saved %u bytes", (unsigned)n);
    return ESP_OK;
}

static void save_timer_cb(void *arg) { (void)arg; config_save_now(); }

void config_mark_dirty(void)
{
    if (!s_save_timer) {
        const esp_timer_create_args_t a = { .callback = save_timer_cb, .name = "cfg_save" };
        if (esp_timer_create(&a, &s_save_timer) != ESP_OK) return;
    }
    esp_timer_stop(s_save_timer);
    esp_timer_start_once(s_save_timer, SAVE_DELAY_US);
}

esp_err_t config_load(void)
{
    FILE *f = fopen(PATH, "r");
    if (!f) { ESP_LOGI(TAG, "no config file, using defaults"); return ESP_ERR_NOT_FOUND; }
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > 65536) { fclose(f); return ESP_ERR_INVALID_SIZE; }
    char *text = malloc(len + 1);
    if (!text) { fclose(f); return ESP_ERR_NO_MEM; }
    size_t r = fread(text, 1, len, f); fclose(f); text[r] = 0;
    esp_err_t err = config_apply_json(text);
    free(text);
    if (err != ESP_OK) ESP_LOGW(TAG, "config parse failed, using defaults"); else ESP_LOGI(TAG, "config loaded");
    return err;
}
