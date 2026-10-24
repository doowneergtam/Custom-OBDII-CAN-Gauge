// Screen composition, encoder handling, menu tree, alarm banner.
//
// Gauge screen: rotate = move focus between cells, click = reset that cell's
// peak, hold = menu. Menu: a roller; rotate scrolls, click selects, hold
// backs out. Style picker shows a full-screen sample of each style.
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "face.h"
#include "hal_board.h"
#include "gauge_config.h"
#include "theme.h"
#include "alarms.h"
#include "src_can.h"
#include "webcfg.h"

static const char *TAG = "ui";
#define UI_REFRESH_MS 40
#define C 233

typedef enum { M_CLOSED = 0, M_ROOT, M_CHANNEL, M_LAYOUT, M_THEME, M_STYLE, M_ALARMS, M_ALTONE, M_ALTLVL, M_SYSTEM, M_BRIGHT, M_SCREENS } level_t;
static const uint8_t LAYOUT_IDS[] = { 1, 3, 4, 6, 7 };
static const uint8_t BRIGHT[] = { 20, 40, 60, 80, 100 };
static const uint8_t VOLS[] = { 0, 20, 40, 60, 80, 100 };
static const uint8_t REPEATS[] = { 0, 5, 10, 30 };

static lv_group_t *s_group;
static lv_obj_t   *s_scr, *s_proxy;
static lv_obj_t   *s_faces[UI_CELLS_MAX];
static int         s_nfaces, s_focus;
static int64_t     s_focus_until;      // selection ring fades 3 s after the last move
static lv_obj_t   *s_touch;            // full-screen transparent touch target
#define FOCUS_HOLD_US (3000 * 1000)
static lv_obj_t   *s_menu, *s_title;
static lv_obj_t   *s_preview, *s_pill;
static lv_obj_t   *s_banner, *s_banner_lbl;
static lv_obj_t   *s_flash;
static int         s_flash_count;
static int64_t     s_banner_until;
static level_t     s_level;
static int         s_alt_ch, s_style_sel;
static int         s_user_preset = -1;   // which user preset is showing, -1 = none/built-in
static char        s_opts[1024];

static void menu_open(level_t level);
static void menu_close(void);
static void menu_select(uint32_t i);
static void preview_step(int dir);
static void preview_select(void);
static level_t parent_of(level_t level);

// ---------------------------------------------------------------- layouts
typedef struct { int16_t cx, cy, r; } cell_t;
static cell_t s_cells[UI_CELLS_MAX];
static int layout_cells_base(uint8_t layout, cell_t *out);
static int layout_cells(uint8_t layout, cell_t *out)
{
    int n = layout_cells_base(layout, out);
    // Outer cells grow with the theme's cell scale (centre cell stays).
    float cs = g_theme.cell_scale > 1.0f ? g_theme.cell_scale : 1.0f;
    for (int i = (layout >= 6 ? 1 : 0); i < n; i++) out[i].r = (int16_t)(out[i].r * cs);
    return n;
}
static int layout_cells_base(uint8_t layout, cell_t *out)
{
    static const cell_t L3[] = { {233, 136, 90}, {138, 306, 90}, {328, 306, 90} };
    static const cell_t L4[] = { {138, 138, 88}, {328, 138, 88}, {138, 328, 88}, {328, 328, 88} };
    switch (layout) {
    case 3: memcpy(out, L3, sizeof L3); return 3;
    case 4: memcpy(out, L4, sizeof L4); return 4;
    case 6: case 7: {
        int n = layout - 1; float R = layout == 6 ? 160 : 162; int16_t r = layout == 6 ? 66 : 64;
        out[0] = (cell_t){ C, C, layout == 6 ? 78 : 76 };
        for (int i = 0; i < n; i++) {
            float a = (-90.0f + 360.0f * i / n) * 3.14159265f / 180.0f;
            out[i + 1] = (cell_t){ (int16_t)lroundf(C + R * cosf(a)), (int16_t)lroundf(C + R * sinf(a)), r };
        }
        return layout;
    }
    default: out[0] = (cell_t){ C, C, 229 }; return 1;
    }
}

// Menu, title, style pill and alarm banner colours follow the face: the
// background is the face colour, and the text and selection bubble keep the
// theme's own colours while they contrast with it, flipping to near-black or
// near-white when they would not (a light face with light type, say).
static bool contrasts(lv_color_t a, lv_color_t b)
{
    int d = (int)lv_color_brightness(a) - (int)lv_color_brightness(b);
    return d > 90 || d < -90;
}
static lv_color_t legible(theme_color_t slot, lv_color_t bg)
{
    lv_color_t c = theme_color(slot);
    if (contrasts(c, bg)) return c;
    return lv_color_brightness(bg) > 127 ? lv_color_hex(0x15181C) : lv_color_hex(0xF4F6F8);
}
static void ui_restyle(void)
{
    lv_color_t bg = theme_color(TC_BG), fg = legible(TC_LABEL, bg), sel = legible(TC_DIGITS, bg), accent = legible(TC_NEUTRAL, bg);
    if (s_menu) {
        lv_obj_set_style_bg_color(s_menu, bg, LV_PART_MAIN);
        lv_obj_set_style_text_color(s_menu, fg, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_menu, accent, LV_PART_SELECTED);
        lv_obj_set_style_border_color(s_menu, accent, LV_PART_SELECTED);
        lv_obj_set_style_text_color(s_menu, sel, LV_PART_SELECTED);
    }
    if (s_title) lv_obj_set_style_text_color(s_title, fg, 0);
    if (s_pill) {
        lv_obj_set_style_bg_color(s_pill, bg, 0);
        lv_obj_set_style_border_color(s_pill, accent, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(s_pill, 0), sel, 0);
    }
    if (s_banner_lbl) lv_obj_set_style_text_color(s_banner_lbl, sel, 0);
}

static void apply_theme(void)
{
    lv_obj_set_style_bg_color(s_scr, theme_color(TC_BG), 0);
    ui_restyle();
    for (int i = 0; i < s_nfaces; i++) lv_obj_invalidate(s_faces[i]);
}

static void build_faces(void)
{
    for (int i = 0; i < s_nfaces; i++) lv_obj_delete(s_faces[i]);
    s_nfaces = layout_cells(g_ui_cfg.layout, s_cells);
    for (int i = 0; i < s_nfaces; i++) {
        s_faces[i] = face_create(s_scr, (ch_id_t)g_ui_cfg.cells[i], s_cells[i].cx, s_cells[i].cy, s_cells[i].r, s_nfaces == 1);
    }
    if (s_focus >= s_nfaces) s_focus = 0;
    s_focus_until = 0;
    if (s_touch) lv_obj_move_foreground(s_touch);
    if (s_banner) lv_obj_move_foreground(s_banner);
}

// ---------------------------------------------------------------- banner
static void banner_cb(ch_id_t id, ch_zone_level_t level)
{
    const ch_def_t *d = channels_def(id);
    char buf[48];
    snprintf(buf, sizeof buf, "%s  %.*f %s", d->name, channels_display_decimals(d),
             (double)channels_to_display(d, channels_value(id)), channels_units_label(d));
    lv_label_set_text(s_banner_lbl, buf);
    lv_obj_set_style_border_color(s_banner, theme_zone_color(level), 0);
    lv_obj_set_style_bg_color(s_banner, theme_zone_color(level), 0);
    lv_obj_set_hidden(s_banner, false);
    lv_obj_move_foreground(s_banner);
    s_banner_until = esp_timer_get_time() + 3000000;
}

static void build_banner(void)
{
    s_banner = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_banner);
    lv_obj_set_size(s_banner, 300, 40);
    lv_obj_set_pos(s_banner, C - 150, 380);
    lv_obj_set_style_radius(s_banner, 20, 0);
    lv_obj_set_style_bg_opa(s_banner, 56, 0);
    lv_obj_set_style_border_width(s_banner, 2, 0);
    lv_obj_set_style_border_opa(s_banner, LV_OPA_COVER, 0);
    s_banner_lbl = lv_label_create(s_banner);
    lv_obj_set_style_text_font(s_banner_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_banner_lbl, theme_color(TC_DIGITS), 0);
    lv_obj_center(s_banner_lbl);
    lv_obj_set_hidden(s_banner, true);
}

// ---------------------------------------------------------------- tick
static void ui_tick(lv_timer_t *t)
{
    (void)t;
    int64_t now = esp_timer_get_time();
    for (int i = 0; i < s_nfaces; i++) face_refresh(s_faces[i], now);
    if (s_preview) face_refresh(s_preview, now);
    ch_id_t vis[UI_CELLS_MAX];
    for (int i = 0; i < s_nfaces; i++) vis[i] = face_channel(s_faces[i]);
    alarms_tick(now, vis, s_nfaces);
    if (s_banner_until && now > s_banner_until) { lv_obj_set_hidden(s_banner, true); s_banner_until = 0; }
    if (s_focus_until && now > s_focus_until) { for (int i = 0; i < s_nfaces; i++) face_set_focused(s_faces[i], false); s_focus_until = 0; }
}

// ---------------------------------------------------------------- gauge input
static void show_focus(void)
{
    for (int i = 0; i < s_nfaces; i++) face_set_focused(s_faces[i], s_nfaces > 1 && i == s_focus);
    s_focus_until = esp_timer_get_time() + FOCUS_HOLD_US;
}
static void move_focus(int dir)
{
    if (s_nfaces > 1) s_focus = (s_focus + dir + s_nfaces) % s_nfaces;
    show_focus();
}
static void proxy_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        int dir = key == LV_KEY_RIGHT ? 1 : key == LV_KEY_LEFT ? -1 : 0;
        if (!dir) return;
        move_focus(dir);
    } else if (code == LV_EVENT_SHORT_CLICKED) {
        channels_reset_peak(face_channel(s_faces[s_focus]));
        show_focus();
    } else if (code == LV_EVENT_LONG_PRESSED) {
        menu_open(M_ROOT);
    }
}

static void focus_gauge(void)
{
    if (!s_group) return;
    lv_group_remove_all_objs(s_group);
    lv_group_add_obj(s_group, s_proxy);
    lv_group_focus_obj(s_proxy);
}

// ---------------------------------------------------------------- menu options
static int preset_index(void) { return g_theme.preset; }
// Built-in presets first, then the user's saved ones.
static const char *current_preset_name(void)
{
    if (g_theme.preset >= 0) return theme_preset_name(g_theme.preset);
    if (s_user_preset >= 0 && s_user_preset < config_preset_count()) return config_preset_name(s_user_preset);
    return "Custom";
}
static void cycle_preset(void)
{
    int nb = theme_preset_count(), nu = config_preset_count();
    int cur = g_theme.preset >= 0 ? g_theme.preset : nb + (s_user_preset >= 0 ? s_user_preset : -1);
    int next = (cur + 1) % (nb + nu);
    if (next < nb) { theme_apply_preset(next); s_user_preset = -1; }
    else { config_apply_preset(next - nb); s_user_preset = next - nb; }
}

static const char *build_options(level_t level, int *selected)
{
    char *p = s_opts; size_t n = sizeof s_opts; int sel = 0; s_opts[0] = 0;
    #define ADD(...) do { int w = snprintf(p, n, __VA_ARGS__); if (w > 0 && (size_t)w < n) { p += w; n -= w; } } while (0)
    switch (level) {
    case M_ROOT:
        ADD("Channel\nLayout\nScreens\nTheme\nAlarms\nSystem\nBrightness\nReset min/max\nExit"); break;
    case M_SCREENS:
        for (int i = 0; i < config_screen_count(); i++) ADD("%s\n", config_screen_name(i));
        ADD("Save as Screen %d\nBack", config_screen_count() + 1);
        break;
    case M_SYSTEM:
        ADD("Sim data: %s\nSource: %s\nWi-Fi setup: %s\nBack", g_ui_cfg.sim ? "On" : "Off",
            g_ui_cfg.source == SRC_OBD ? "OBD-II CAN" : "MegaSquirt", webcfg_is_on() ? "On" : "Off");
        break;
    case M_CHANNEL:
        { int k = 0; for (int i = 0; i < channels_count(); i++) { if (!channels_def((ch_id_t)i)->enabled) continue; ADD("%s%s", k ? "\n" : "", channels_def((ch_id_t)i)->name); if ((ch_id_t)i == face_channel(s_faces[s_focus])) sel = k; k++; } }
        break;
    case M_LAYOUT:
        for (int i = 0; i < 5; i++) { ADD("%s%d cell%s", i ? "\n" : "", LAYOUT_IDS[i], LAYOUT_IDS[i] == 1 ? "" : "s"); if (LAYOUT_IDS[i] == g_ui_cfg.layout) sel = i; }
        break;
    case M_THEME:
        ADD("Preset: %s\nBar fill: %s\nStyle: %s\nZone bands: %s\nBack", current_preset_name(), theme_fill_name(g_theme.fill), theme_style_name(g_theme.style), g_theme.bands ? "On" : "Off");
        break;
    case M_ALARMS:
        ADD("Volume: %d%%\nWarn alarms: %s\nBad alarms: %s\nBad repeat: ", g_alarms.volume, g_alarms.warn_on ? "On" : "Off", g_alarms.bad_on ? "On" : "Off");
        if (g_alarms.repeat_s) ADD("%d s", g_alarms.repeat_s); else ADD("Once");
        ADD("\nChannel tones\nBack");
        break;
    case M_ALTONE: {
        int k = 0;
        for (int i = 0; i < channels_count(); i++) { const ch_def_t *d = channels_def((ch_id_t)i); if (!d->zone_count || !d->enabled) continue; ADD("%s%s", k ? "\n" : "", d->name); if (i == s_alt_ch) sel = k; k++; }
        break;
    }
    case M_ALTLVL:
        ADD("Warn: %s\nBad: %s\nBack", alarms_tone_name((alarm_tone_t)g_alarms.tone_warn[s_alt_ch]), alarms_tone_name((alarm_tone_t)g_alarms.tone_bad[s_alt_ch]));
        break;
    case M_BRIGHT:
        for (int i = 0; i < 5; i++) { ADD("%s%d%%", i ? "\n" : "", BRIGHT[i]); if (BRIGHT[i] == g_ui_cfg.brightness) sel = i; }
        break;
    default: break;
    }
    #undef ADD
    *selected = sel;
    return s_opts;
}

static const char *menu_title(level_t level)
{
    static char t[24];
    switch (level) {
    case M_CHANNEL: snprintf(t, sizeof t, "CELL %d", s_focus + 1); return t;
    case M_LAYOUT: return "LAYOUT";
    case M_SCREENS: return "SCREENS";
    case M_THEME: return "THEME";
    case M_ALARMS: return "ALARMS";
    case M_ALTONE: return "TONES";
    case M_ALTLVL: return channels_def((ch_id_t)s_alt_ch)->name;
    case M_SYSTEM: return "SYSTEM";
    case M_BRIGHT: return "BRIGHTNESS";
    default: return "MENU";
    }
}

static level_t parent_of(level_t level)
{
    switch (level) {
    case M_STYLE:  return M_THEME;
    case M_ALTONE: return M_ALARMS;
    case M_ALTLVL: return M_ALTONE;
    default:       return M_ROOT;
    }
}

// ---------------------------------------------------------------- touch
// Tap a cell = select (tap again = reset min/max), hold = menu, swipe = move.
// Menu: tap the middle row = choose, tap above/below or swipe = scroll, hold = back.
static void touch_tap(lv_point_t p)
{
    if (s_level == M_STYLE) {
        // left third = previous style, right third = next, middle = keep
        if (p.x < HAL_DISPLAY_W / 3) preview_step(-1);
        else if (p.x > HAL_DISPLAY_W * 2 / 3) preview_step(1);
        else preview_select();
        return;
    }
    if (s_level != M_CLOSED) {
        int dy = p.y - C;
        if (dy > -34 && dy < 34) { menu_select(lv_roller_get_selected(s_menu)); return; }
        int sel = (int)lv_roller_get_selected(s_menu) + (dy > 0 ? 1 : -1);
        if (sel >= 0) lv_roller_set_selected(s_menu, (uint32_t)sel, LV_ANIM_ON);
        return;
    }
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < s_nfaces; i++) {
        float dx = p.x - s_cells[i].cx, dyy = p.y - s_cells[i].cy, d = sqrtf(dx * dx + dyy * dyy);
        if (d < s_cells[i].r + 10 && d < bd) { bd = d; best = i; }
    }
    if (best < 0) return;
    if (best == s_focus && s_focus_until) channels_reset_peak(face_channel(s_faces[s_focus]));
    s_focus = best;
    show_focus();
}

static void touch_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *ind = lv_indev_active();
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_LONG_PRESSED || code == LV_EVENT_GESTURE) {
        lv_point_t p = { -1, -1 }; if (ind) lv_indev_get_point(ind, &p);
        ESP_LOGI(TAG, "touch ev %d indev %p type %d at %d,%d level %d", (int)code, (void *)ind, ind ? (int)lv_indev_get_type(ind) : -1, (int)p.x, (int)p.y, (int)s_level);
    }
    if (!ind || lv_indev_get_type(ind) != LV_INDEV_TYPE_POINTER) return;
    if (code == LV_EVENT_SHORT_CLICKED) {
        lv_point_t p; lv_indev_get_point(ind, &p); touch_tap(p);
    } else if (code == LV_EVENT_LONG_PRESSED) {
        if (s_level == M_CLOSED) menu_open(M_ROOT);
        else if (s_level == M_ROOT) menu_close();
        else menu_open(parent_of(s_level));
    } else if (code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(ind);
        if (s_level == M_STYLE) { if (dir == LV_DIR_LEFT) preview_step(1); else if (dir == LV_DIR_RIGHT) preview_step(-1); }
        else if (s_level != M_CLOSED) {
            int sel = (int)lv_roller_get_selected(s_menu) + (dir == LV_DIR_TOP ? 1 : dir == LV_DIR_BOTTOM ? -1 : 0);
            if (sel >= 0) lv_roller_set_selected(s_menu, (uint32_t)sel, LV_ANIM_ON);
        } else if (dir == LV_DIR_LEFT) move_focus(-1);
        else if (dir == LV_DIR_RIGHT) move_focus(1);
    }
}

static void build_touch(void)
{
    s_touch = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_touch);
    lv_obj_set_size(s_touch, HAL_DISPLAY_W, HAL_DISPLAY_H);
    lv_obj_set_pos(s_touch, 0, 0);
    lv_obj_add_flag(s_touch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_touch, touch_event_cb, LV_EVENT_ALL, NULL);
}
// Diagnostics: see whether the touch indev delivers anything to the screen at all.
static void scr_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) { lv_point_t p = { -1, -1 }; lv_indev_t *ind = lv_indev_active(); if (ind) lv_indev_get_point(ind, &p); ESP_LOGI(TAG, "screen pressed at %d,%d", (int)p.x, (int)p.y); }
}

// ---------------------------------------------------------------- bench console hooks
void ui_test_input(const char *what, int a, int b)
{
    if (!hal_board_lock(2000)) return;
    if (strcmp(what, "tap") == 0) { lv_point_t p = { a, b }; touch_tap(p); }
    else if (strcmp(what, "hold") == 0) {
        if (s_level == M_CLOSED) menu_open(M_ROOT);
        else if (s_level == M_ROOT) menu_close();
        else menu_open(parent_of(s_level));
    } else if (strcmp(what, "click") == 0) {
        if (s_level == M_STYLE) preview_select();
        else if (s_level != M_CLOSED) menu_select(lv_roller_get_selected(s_menu));
        else { channels_reset_peak(face_channel(s_faces[s_focus])); show_focus(); }
    } else if (strcmp(what, "rot") == 0) {
        int dir = a >= 0 ? 1 : -1, n = a >= 0 ? a : -a;
        for (int i = 0; i < n; i++) {
            if (s_level == M_STYLE) preview_step(dir);
            else if (s_level != M_CLOSED) { int sel = (int)lv_roller_get_selected(s_menu) + dir; if (sel >= 0) lv_roller_set_selected(s_menu, (uint32_t)sel, LV_ANIM_OFF); }
            else move_focus(dir);
        }
    } else if (strcmp(what, "swipe") == 0) {
        lv_dir_t dir = a == 'l' ? LV_DIR_LEFT : a == 'r' ? LV_DIR_RIGHT : a == 'u' ? LV_DIR_TOP : LV_DIR_BOTTOM;
        if (s_level == M_STYLE) { if (dir == LV_DIR_LEFT) preview_step(1); else if (dir == LV_DIR_RIGHT) preview_step(-1); }
        else if (s_level != M_CLOSED) { int sel = (int)lv_roller_get_selected(s_menu) + (dir == LV_DIR_TOP ? 1 : dir == LV_DIR_BOTTOM ? -1 : 0); if (sel >= 0) lv_roller_set_selected(s_menu, (uint32_t)sel, LV_ANIM_ON); }
        else if (dir == LV_DIR_LEFT) move_focus(-1);
        else if (dir == LV_DIR_RIGHT) move_focus(1);
    }
    hal_board_unlock();
}
int ui_test_level(void) { return (int)s_level; }

#include "ui_menu.inc"
