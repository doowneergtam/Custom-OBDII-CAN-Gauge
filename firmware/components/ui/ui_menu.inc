// Included at the end of ui.c: menu actions, style picker, roller, init.

// ---------------------------------------------------------------- style picker
// Browsing onto Stock shows it in its own colours (the factory cream face), so
// the sample is the real thing; stepping off or backing out restores the colours
// that were there, and choosing Stock keeps them. Both Stock styles are needle-only by design.
static uint32_t s_pick_rgb[TC_COUNT];
static int8_t   s_pick_preset;
static bool     s_pick_swapped, s_pick_bands;

static void preview_swap_in(void)
{
    int stock = theme_preset_find("Stock");
    if (s_pick_swapped || stock < 0) return;
    memcpy(s_pick_rgb, g_theme.rgb, sizeof s_pick_rgb);
    s_pick_preset = g_theme.preset; s_pick_bands = g_theme.bands;
    theme_apply_preset(stock);
    g_theme.bands = false;   // the factory faces are black and white: no zone colour until asked for
    s_pick_swapped = true;
    apply_theme();
}
static void preview_swap_out(void)
{
    if (!s_pick_swapped) return;
    memcpy(g_theme.rgb, s_pick_rgb, sizeof g_theme.rgb);
    g_theme.preset = s_pick_preset; g_theme.bands = s_pick_bands;
    s_pick_swapped = false;
    apply_theme();
}
static void preview_update_pill(void)
{
    char b[32];
    snprintf(b, sizeof b, "<  %s  %d/%d  >", theme_style_name((gauge_style_t)s_style_sel), s_style_sel + 1, STYLE_COUNT);
    lv_label_set_text(lv_obj_get_child(s_pill, 0), b);
}

static void preview_step(int dir)
{
    if (!s_preview) return;
    s_style_sel = (s_style_sel + dir + STYLE_COUNT) % STYLE_COUNT;
    if (s_style_sel == STYLE_STOCK || s_style_sel == STYLE_STOCK2) preview_swap_in(); else preview_swap_out();
    face_set_style(s_preview, s_style_sel);
    preview_update_pill();
}
static void preview_select(void)
{
    g_theme.style = (gauge_style_t)s_style_sel;
    if (s_pick_swapped) { s_pick_swapped = false; s_user_preset = -1; }   // Stock chosen: its colours stay
    config_mark_dirty();
    menu_open(M_THEME);
}
static void preview_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        int dir = key == LV_KEY_RIGHT ? 1 : key == LV_KEY_LEFT ? -1 : 0;
        if (dir) preview_step(dir);
    } else if (code == LV_EVENT_SHORT_CLICKED) {
        preview_select();
    } else if (code == LV_EVENT_LONG_PRESSED) {
        preview_swap_out(); menu_open(M_THEME);
    }
}

static void preview_open(void)
{
    s_style_sel = g_theme.style;
    s_preview = face_create(s_scr, face_channel(s_faces[s_focus]), C, C, 229, true);
    face_set_style(s_preview, s_style_sel);
    lv_obj_add_event_cb(s_preview, preview_event_cb, LV_EVENT_ALL, NULL);

    s_pill = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_pill);
    lv_obj_set_size(s_pill, 180, 34);
    lv_obj_set_pos(s_pill, C - 90, 18);
    lv_obj_set_style_radius(s_pill, 17, 0);
    lv_obj_set_style_bg_opa(s_pill, 190, 0);
    lv_obj_set_style_border_width(s_pill, 2, 0);
    lv_obj_t *lbl = lv_label_create(s_pill);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl);
    ui_restyle();
    if (s_style_sel == STYLE_STOCK || s_style_sel == STYLE_STOCK2) preview_swap_in();
    preview_update_pill();

    if (s_group) { lv_group_remove_all_objs(s_group); lv_group_add_obj(s_group, s_preview); lv_group_focus_obj(s_preview); }
    if (s_touch) lv_obj_move_foreground(s_touch);
}

static void preview_close(void)
{
    if (s_preview) { lv_obj_delete(s_preview); s_preview = NULL; }
    if (s_pill) { lv_obj_delete(s_pill); s_pill = NULL; }
}

// ---------------------------------------------------------------- selection
static uint8_t cycle_u8(const uint8_t *list, int n, uint8_t cur)
{
    for (int i = 0; i < n; i++) if (list[i] == cur) return list[(i + 1) % n];
    return list[0];
}

static int alarm_channel_at(int k)
{
    for (int i = 0; i < channels_count(); i++) { const ch_def_t *d = channels_def((ch_id_t)i); if (!d->zone_count || !d->enabled) continue; if (k-- == 0) return i; }
    return 0;
}
static int visible_channel_at(int k)
{
    for (int i = 0; i < channels_count(); i++) { if (!channels_def((ch_id_t)i)->enabled) continue; if (k-- == 0) return i; }
    return 0;
}

static void menu_select(uint32_t i)
{
    switch (s_level) {
    case M_ROOT:
        switch (i) {
        case 0: menu_open(M_CHANNEL); break;
        case 1: menu_open(M_LAYOUT); break;
        case 2: menu_open(M_SCREENS); break;
        case 3: menu_open(M_THEME); break;
        case 4: menu_open(M_ALARMS); break;
        case 5: menu_open(M_SYSTEM); break;
        case 6: menu_open(M_BRIGHT); break;
        case 7: channels_reset_peaks(); menu_close(); break;
        default: menu_close(); break;
        }
        break;
    case M_SYSTEM:
        if (i == 0) { g_ui_cfg.sim = !g_ui_cfg.sim; src_can_set_feed(!g_ui_cfg.sim); }
        else if (i == 1) { g_ui_cfg.source = g_ui_cfg.source == SRC_OBD ? SRC_MEGASQUIRT : SRC_OBD; src_can_set_mode(g_ui_cfg.source == SRC_OBD ? SRC_CAN_MODE_OBD : SRC_CAN_MODE_MS); }
        else if (i == 2) { if (webcfg_is_on()) { webcfg_stop(); g_ui_cfg.wifi_on = false; } else if (webcfg_start() == ESP_OK) g_ui_cfg.wifi_on = true; else ESP_LOGW(TAG, "hotspot failed"); config_mark_dirty(); menu_open(M_SYSTEM); lv_roller_set_selected(s_menu, i, LV_ANIM_OFF); break; }
        else { menu_open(M_ROOT); break; }
        config_mark_dirty(); menu_open(M_SYSTEM); lv_roller_set_selected(s_menu, i, LV_ANIM_OFF);
        break;
    case M_CHANNEL:
        { int id = visible_channel_at((int)i); g_ui_cfg.cells[s_focus] = id; face_set_channel(s_faces[s_focus], (ch_id_t)id); config_mark_dirty(); }
        menu_close();
        break;
    case M_LAYOUT:
        if (i < 5) { g_ui_cfg.layout = LAYOUT_IDS[i]; build_faces(); config_mark_dirty(); }
        menu_close();
        break;
    case M_SCREENS: {
        int n = config_screen_count();
        if ((int)i < n) { if (config_apply_screen((int)i) == ESP_OK) { apply_theme(); build_faces(); hal_board_set_brightness(g_ui_cfg.brightness); config_mark_dirty(); } menu_close(); }
        else if ((int)i == n) { char name[20]; snprintf(name, sizeof name, "Screen %d", n + 1); config_save_screen(name); config_mark_dirty(); menu_open(M_SCREENS); lv_roller_set_selected(s_menu, i, LV_ANIM_OFF); }
        else menu_open(M_ROOT);
        break;
    }
    case M_THEME:
        if (i == 0) { cycle_preset(); apply_theme(); config_mark_dirty(); menu_open(M_THEME); }
        else if (i == 1) { g_theme.fill = (bar_fill_t)((g_theme.fill + 1) % FILL_COUNT); config_mark_dirty(); menu_open(M_THEME); lv_roller_set_selected(s_menu, 1, LV_ANIM_OFF); }
        else if (i == 2) { menu_open(M_STYLE); }
        else if (i == 3) { g_theme.bands = !g_theme.bands; config_mark_dirty(); menu_open(M_THEME); lv_roller_set_selected(s_menu, 3, LV_ANIM_OFF); }
        else menu_open(M_ROOT);
        break;
    case M_ALARMS:
        if (i == 0) { g_alarms.volume = cycle_u8(VOLS, 6, g_alarms.volume); audio_play(TONE_CHIRP, g_alarms.volume); }
        else if (i == 1) g_alarms.warn_on = !g_alarms.warn_on;
        else if (i == 2) g_alarms.bad_on = !g_alarms.bad_on;
        else if (i == 3) g_alarms.repeat_s = cycle_u8(REPEATS, 4, g_alarms.repeat_s);
        else if (i == 4) { s_alt_ch = alarm_channel_at(0); menu_open(M_ALTONE); break; }
        else { menu_open(M_ROOT); break; }
        config_mark_dirty(); menu_open(M_ALARMS); lv_roller_set_selected(s_menu, i, LV_ANIM_OFF);
        break;
    case M_ALTONE:
        s_alt_ch = alarm_channel_at((int)i); menu_open(M_ALTLVL);
        break;
    case M_ALTLVL:
        if (i == 0) { g_alarms.tone_warn[s_alt_ch] = (g_alarms.tone_warn[s_alt_ch] + 1) % TONE_COUNT; audio_play((alarm_tone_t)g_alarms.tone_warn[s_alt_ch], g_alarms.volume); }
        else if (i == 1) { g_alarms.tone_bad[s_alt_ch] = (g_alarms.tone_bad[s_alt_ch] + 1) % TONE_COUNT; audio_play((alarm_tone_t)g_alarms.tone_bad[s_alt_ch], g_alarms.volume); }
        else { menu_open(M_ALTONE); break; }
        config_mark_dirty(); menu_open(M_ALTLVL); lv_roller_set_selected(s_menu, i, LV_ANIM_OFF);
        break;
    case M_BRIGHT:
        if (i < 5) { g_ui_cfg.brightness = BRIGHT[i]; hal_board_set_brightness(g_ui_cfg.brightness); config_mark_dirty(); }
        menu_close();
        break;
    default: menu_close(); break;
    }
}

static void menu_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SHORT_CLICKED) menu_select(lv_roller_get_selected(s_menu));
    else if (code == LV_EVENT_LONG_PRESSED) { if (s_level == M_ROOT) menu_close(); else menu_open(parent_of(s_level)); }
}

// ---------------------------------------------------------------- roller
static void menu_build(void)
{
    s_title = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_letter_space(s_title, 3, 0);
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, -150);

    s_menu = lv_roller_create(s_scr);
    lv_obj_set_width(s_menu, 320);
    lv_roller_set_visible_row_count(s_menu, 5);
    lv_obj_center(s_menu);
    lv_obj_set_style_bg_opa(s_menu, 240, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_menu, 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_menu, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(s_menu, 22, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_menu, 46, LV_PART_SELECTED);
    lv_obj_set_style_border_width(s_menu, 2, LV_PART_SELECTED);
    lv_obj_set_style_radius(s_menu, 14, LV_PART_SELECTED);
    lv_obj_set_style_text_font(s_menu, &lv_font_montserrat_28, LV_PART_SELECTED);
    lv_obj_set_style_outline_width(s_menu, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(s_menu, 0, LV_STATE_FOCUS_KEY);
    lv_obj_add_event_cb(s_menu, menu_event_cb, LV_EVENT_ALL, NULL);
    ui_restyle();
}

static void menu_open(level_t level)
{
    if (!s_menu) menu_build();
    preview_close();
    s_level = level;
    for (int i = 0; i < s_nfaces; i++) lv_obj_set_hidden(s_faces[i], true);
    if (level == M_STYLE) {
        lv_obj_set_hidden(s_menu, true); lv_obj_set_hidden(s_title, true);
        preview_open();
        return;
    }
    int sel = 0;
    const char *opts = build_options(level, &sel);
    lv_label_set_text(s_title, menu_title(level));
    lv_roller_set_options(s_menu, opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(s_menu, sel, LV_ANIM_OFF);
    lv_obj_set_hidden(s_menu, false); lv_obj_set_hidden(s_title, false);
    if (s_touch) lv_obj_move_foreground(s_touch);
    if (s_group) { lv_group_remove_all_objs(s_group); lv_group_add_obj(s_group, s_menu); lv_group_focus_obj(s_menu); }
}

static void menu_close(void)
{
    s_level = M_CLOSED;
    preview_close();
    if (s_menu) { lv_obj_set_hidden(s_menu, true); lv_obj_set_hidden(s_title, true); }
    for (int i = 0; i < s_nfaces; i++) lv_obj_set_hidden(s_faces[i], false);
    focus_gauge();
}

// ---------------------------------------------------------------- reload / identify
void ui_reload(void)
{
    // Rebuild the face objects only when their geometry changed; a colour or
    // scale change just needs a redraw, and a rebuild stalls the data feed.
    static uint8_t last_layout = 0, last_cells[UI_CELLS_MAX]; static float last_zoom = -1, last_cs = -1;
    bool geom = last_layout != g_ui_cfg.layout || last_zoom != g_ui_cfg.zoom || last_cs != g_theme.cell_scale
                || memcmp(last_cells, g_ui_cfg.cells, sizeof last_cells) != 0;
    if (!hal_board_lock(2000)) return;
    apply_theme();
    if (geom) { build_faces(); last_layout = g_ui_cfg.layout; last_zoom = g_ui_cfg.zoom; last_cs = g_theme.cell_scale; memcpy(last_cells, g_ui_cfg.cells, sizeof last_cells); }
    else for (int i = 0; i < s_nfaces; i++) lv_obj_invalidate(s_faces[i]);
    hal_board_set_brightness(g_ui_cfg.brightness);
    if (s_level != M_CLOSED) { for (int i = 0; i < s_nfaces; i++) lv_obj_set_hidden(s_faces[i], true); }
    hal_board_unlock();
    src_can_set_feed(!g_ui_cfg.sim);
    src_can_set_mode(g_ui_cfg.source == SRC_OBD ? SRC_CAN_MODE_OBD : SRC_CAN_MODE_MS);
    ESP_LOGI(TAG, "reload%s", geom ? ", faces rebuilt" : "");
}

static void flash_timer_cb(lv_timer_t *t)
{
    s_flash_count--;
    lv_obj_set_hidden(s_flash, (s_flash_count & 1) == 0);
    if (s_flash_count <= 0) { lv_obj_set_hidden(s_flash, true); lv_timer_delete(t); }
}
void ui_identify(void)
{
    if (!hal_board_lock(500)) return;
    if (!s_flash) {
        s_flash = lv_obj_create(s_scr); lv_obj_remove_style_all(s_flash);
        lv_obj_set_size(s_flash, HAL_DISPLAY_W, HAL_DISPLAY_H); lv_obj_set_pos(s_flash, 0, 0);
        lv_obj_set_style_bg_color(s_flash, lv_color_white(), 0); lv_obj_set_style_bg_opa(s_flash, 220, 0);
    }
    lv_obj_move_foreground(s_flash); lv_obj_set_hidden(s_flash, false);
    s_flash_count = 4;
    lv_timer_create(flash_timer_cb, 150, NULL);
    hal_board_unlock();
}

static void hook_reload(void)       { ui_reload(); }
static void hook_reset(void)        { channels_reset_peaks(); }
static void hook_identify(void)     { ui_identify(); }
static void hook_tone(int tone)     { audio_play((alarm_tone_t)tone, g_alarms.volume); }

// ---------------------------------------------------------------- init
extern int64_t g_face_inv_px, g_face_inv_rects, g_face_draw_us, g_face_draw_n;
static int64_t s_wait_t0, s_wait_sum, s_flush_n, s_fl_t0, s_fl_sum; static int s_fl_out; static int s_draw_us_out, s_draw_n_out, s_wait_us_out, s_flush_n_out;
static int s_inv_kpx_out, s_inv_rects_out;
extern int64_t g_prim_us[9];
static int s_prim_out[9];
// Frame counter for the console: refreshes per second and how long each took.
static int64_t s_refr_t0, s_perf_win;
static int s_perf_n, s_perf_fps; static int64_t s_perf_sum, s_perf_max, s_perf_avg_out, s_perf_max_out;
static void perf_cb(lv_event_t *e)
{
    int64_t now = esp_timer_get_time();
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FLUSH_WAIT_START) { s_wait_t0 = now; return; }
    if (code == LV_EVENT_FLUSH_WAIT_FINISH) { s_wait_sum += now - s_wait_t0; return; }
    if (code == LV_EVENT_FLUSH_START) { s_flush_n++; s_fl_t0 = now; return; }
    if (code == LV_EVENT_FLUSH_FINISH) { s_fl_sum += now - s_fl_t0; return; }
    if (code == LV_EVENT_REFR_START) { s_refr_t0 = now; return; }
    int64_t dt = now - s_refr_t0;
    s_perf_n++; s_perf_sum += dt; if (dt > s_perf_max) s_perf_max = dt;
    if (now - s_perf_win >= 1000000) {
        s_perf_fps = s_perf_n; s_perf_avg_out = s_perf_n ? s_perf_sum / s_perf_n : 0; s_perf_max_out = s_perf_max;
        s_draw_us_out = s_perf_n ? (int)(g_face_draw_us / s_perf_n) : 0; s_draw_n_out = s_perf_n ? (int)(g_face_draw_n / s_perf_n) : 0; g_face_draw_us = 0; g_face_draw_n = 0;
        s_wait_us_out = s_perf_n ? (int)(s_wait_sum / s_perf_n) : 0; s_flush_n_out = s_perf_n ? (int)(s_flush_n / s_perf_n) : 0; s_wait_sum = 0; s_flush_n = 0; s_fl_out = s_perf_n ? (int)(s_fl_sum / s_perf_n) : 0; s_fl_sum = 0;
        for (int i = 0; i < 9; i++) { s_prim_out[i] = s_perf_n ? (int)(g_prim_us[i] / s_perf_n) : 0; g_prim_us[i] = 0; }
        s_inv_kpx_out = s_perf_n ? (int)(g_face_inv_px / s_perf_n / 1000) : 0; s_inv_rects_out = s_perf_n ? (int)(g_face_inv_rects / s_perf_n) : 0; g_face_inv_px = 0; g_face_inv_rects = 0;
        s_perf_n = 0; s_perf_sum = 0; s_perf_max = 0; s_perf_win = now;
    }
}
void ui_snapshot(int step) { if (s_nfaces < 1 || !hal_board_lock(3000)) return; face_snapshot_dump(s_faces[0], step); hal_board_unlock(); }
void ui_perf(int *fps, int *avg_us, int *max_us) { *fps = s_perf_fps; *avg_us = (int)s_perf_avg_out; *max_us = (int)s_perf_max_out; }
void ui_perf_more(int *draw_us, int *draw_calls, int *wait_us, int *flushes) { *draw_us = s_draw_us_out; *draw_calls = s_draw_n_out; *wait_us = s_wait_us_out * 0 + s_fl_out; *flushes = s_flush_n_out; }
void ui_perf_prim(int *out6) { for (int i = 0; i < 9; i++) out6[i] = s_prim_out[i]; }
void ui_perf_inv(int *kpx_per_frame, int *rects_per_frame) { *kpx_per_frame = s_inv_kpx_out; *rects_per_frame = s_inv_rects_out; }

void ui_init(lv_group_t *group)
{
    if (!hal_board_lock(1000)) { ESP_LOGE(TAG, "could not take LVGL lock"); return; }
    s_group = group;
    s_scr = lv_screen_active();
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(s_scr, false);
    apply_theme();

    // Invisible focus target for gauge-mode encoder input.
    s_proxy = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_proxy);
    lv_obj_set_size(s_proxy, 1, 1);
    lv_obj_add_event_cb(s_proxy, proxy_event_cb, LV_EVENT_ALL, NULL);

    build_touch();
    lv_obj_add_event_cb(s_scr, scr_event_cb, LV_EVENT_PRESSED, NULL);
    build_banner();
    build_faces();
    webcfg_init(&(webcfg_hooks_t){ .reload = hook_reload, .reset_minmax = hook_reset, .identify = hook_identify, .test_tone = hook_tone });
    alarms_set_banner_cb(banner_cb);
    hal_board_set_brightness(g_ui_cfg.brightness);
    focus_gauge();
    lv_timer_create(ui_tick, UI_REFRESH_MS, NULL);
    lv_display_add_event_cb(lv_display_get_default(), perf_cb, LV_EVENT_REFR_START, NULL);
    lv_display_add_event_cb(lv_display_get_default(), perf_cb, LV_EVENT_REFR_READY, NULL);
    lv_display_add_event_cb(lv_display_get_default(), perf_cb, LV_EVENT_FLUSH_WAIT_START, NULL);
    lv_display_add_event_cb(lv_display_get_default(), perf_cb, LV_EVENT_FLUSH_WAIT_FINISH, NULL);
    lv_display_add_event_cb(lv_display_get_default(), perf_cb, LV_EVENT_FLUSH_START, NULL);
    lv_display_add_event_cb(lv_display_get_default(), perf_cb, LV_EVENT_FLUSH_FINISH, NULL);
    hal_board_unlock();
    ESP_LOGI(TAG, "ui ready: layout %d, style %s", g_ui_cfg.layout, theme_style_name(g_theme.style));
}
