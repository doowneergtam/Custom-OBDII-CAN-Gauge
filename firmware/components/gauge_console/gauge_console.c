#include "gauge_console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_check.h"
#include "channels.h"
#include "src_adc.h"
#include "gauge_config.h"
#include "theme.h"
#include "alarms.h"
#include "src_can.h"
#include "webcfg.h"
#include "ui.h"

static const char *TAG = "console";

// Every command prints exactly one "ok ..." or "err ..." line last, so a
// script can tell where the reply ends.
static int cmd_stats(int argc, char **argv)
{
    (void)argc; (void)argv;
    src_can_stats_t cs; src_can_stats(&cs);
    printf("stats up=%lld internal=%u min=%u largest=%u psram=%u psram_min=%u wifi=%d clients=%d can_rx=%lu can_match=%lu obd_req=%lu layout=%d style=%d sim=%d src=%d\n",
           (long long)(esp_timer_get_time() / 1000000),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
           webcfg_is_on(), webcfg_clients(), (unsigned long)cs.frames_rx, (unsigned long)cs.frames_matched, (unsigned long)cs.obd_requests,
           g_ui_cfg.layout, g_theme.style, g_ui_cfg.sim, g_ui_cfg.source);
    int fps, avg, mx; ui_perf(&fps, &avg, &mx);
    int kpx, nr; ui_perf_inv(&kpx, &nr);
    int dus, dn, wus, fl; ui_perf_more(&dus, &dn, &wus, &fl);
    int pr[9]; ui_perf_prim(pr);
    printf("perf3 label=%d arc=%d line=%d rect=%d tri=%d image=%d resolve=%d poly=%d circle=%d\n", pr[0], pr[1], pr[2], pr[3], pr[4], pr[5], pr[6], pr[7], pr[8]);
    printf("perf2 draw_us=%d draw_calls=%d flush_cb_us=%d flushes=%d\n", dus, dn, wus, fl);
    printf("perf fps=%d frame_avg_us=%d frame_max_us=%d inv_kpx=%d inv_rects=%d\n", fps, avg, mx, kpx, nr);
    printf("ok\n");
    return 0;
}

static int cmd_tap(int argc, char **argv)
{
    if (argc < 3) { printf("err usage: tap x y\n"); return 1; }
    ui_test_input("tap", atoi(argv[1]), atoi(argv[2]));
    printf("ok\n"); return 0;
}
static int cmd_hold(int argc, char **argv)  { (void)argc; (void)argv; ui_test_input("hold", 0, 0); printf("ok\n"); return 0; }
static int cmd_click(int argc, char **argv) { (void)argc; (void)argv; ui_test_input("click", 0, 0); printf("ok\n"); return 0; }
static int cmd_rot(int argc, char **argv)   { ui_test_input("rot", argc > 1 ? atoi(argv[1]) : 1, 0); printf("ok\n"); return 0; }
static int cmd_swipe(int argc, char **argv)
{
    if (argc < 2) { printf("err usage: swipe left|right|up|down\n"); return 1; }
    ui_test_input("swipe", argv[1][0], 0);
    printf("ok\n"); return 0;
}
static int cmd_level(int argc, char **argv) { (void)argc; (void)argv; printf("level %d\nok\n", ui_test_level()); return 0; }

static int cmd_getcfg(int argc, char **argv)
{
    (void)argc; (void)argv;
    char *j = config_to_json();
    if (!j) { printf("err nomem\n"); return 1; }
    printf("cfg %s\nok\n", j);
    free(j);
    return 0;
}

// cfg <json>: merge the given JSON into the config, like the phone page does.
static int cmd_cfg(int argc, char **argv)
{
    if (argc < 2) { printf("err usage: cfg <json>\n"); return 1; }
    // argv is split on spaces; re-join so JSON with spaces works
    size_t len = 0; for (int i = 1; i < argc; i++) len += strlen(argv[i]) + 1;
    char *buf = malloc(len + 1); if (!buf) { printf("err nomem\n"); return 1; }
    buf[0] = 0; for (int i = 1; i < argc; i++) { strcat(buf, argv[i]); if (i + 1 < argc) strcat(buf, " "); }
    esp_err_t e = config_apply_json(buf);
    free(buf);
    if (e != ESP_OK) { printf("err bad json\n"); return 1; }
    config_mark_dirty();
    ui_reload();
    printf("ok\n"); return 0;
}

static int cmd_sim(int argc, char **argv)
{
    if (argc > 1) { g_ui_cfg.sim = strcmp(argv[1], "on") == 0; src_can_set_feed(!g_ui_cfg.sim); config_mark_dirty(); }
    printf("sim %d\nok\n", g_ui_cfg.sim); return 0;
}
static int cmd_wifi(int argc, char **argv)
{
    if (argc > 1) { if (strcmp(argv[1], "on") == 0) webcfg_start(); else webcfg_stop(); }
    printf("wifi %d\nok\n", webcfg_is_on()); return 0;
}
static int cmd_tone(int argc, char **argv) { audio_play((alarm_tone_t)(argc > 1 ? atoi(argv[1]) : 1), g_alarms.volume); printf("ok\n"); return 0; }
static int cmd_screen(int argc, char **argv)
{
    if (argc < 3) { printf("err usage: screen save|load <name|index>\n"); return 1; }
    if (strcmp(argv[1], "save") == 0) { esp_err_t e = config_save_screen(argv[2]); config_mark_dirty(); printf("%s\n", e == ESP_OK ? "ok" : "err save"); return e == ESP_OK ? 0 : 1; }
    esp_err_t e = config_apply_screen(atoi(argv[2]));
    if (e == ESP_OK) { config_mark_dirty(); ui_reload(); }
    printf("%s\n", e == ESP_OK ? "ok" : "err load"); return e == ESP_OK ? 0 : 1;
}
static int cmd_save(int argc, char **argv)   { (void)argc; (void)argv; printf("%s\n", config_save_now() == ESP_OK ? "ok" : "err save"); return 0; }
static int cmd_reboot(int argc, char **argv) { (void)argc; (void)argv; printf("ok\n"); vTaskDelay(pdMS_TO_TICKS(100)); esp_restart(); return 0; }
static int cmd_aux(int argc, char **argv)
{
    (void)argc; (void)argv;
    for (int i = 0; i < AUX_N; i++) {
        ch_id_t id = i == 0 ? CH_AUX1 : CH_AUX2;
        printf("aux%d %s %s: %s raw %.3f V", i + 1, g_aux[i].label, g_aux[i].enabled ? "on" : "off", g_aux[i].resistive ? "resistive" : "0-5V", src_adc_raw_volts(i));
        if (g_aux[i].resistive) printf(" (%.0f ohm)", src_adc_raw_ohms(i));
        printf(" -> %.3f %s (%d pts)\n", channels_value(id), g_aux[i].units, g_aux[i].npts);
    }
    return 0;
}
static int cmd_snap(int argc, char **argv) { ui_snapshot(argc > 1 ? atoi(argv[1]) : 2); printf("ok\n"); return 0; }
static int cmd_vals(int argc, char **argv)
{
    (void)argc; (void)argv;
    int64_t now = esp_timer_get_time();
    printf("vals");
    for (int i = 0; i < channels_count(); i++) printf(" %s=%.2f/%d", config_channel_id((ch_id_t)i), (double)channels_value((ch_id_t)i), (int)channels_freshness((ch_id_t)i, now));
    printf("\nok\n"); return 0;
}

// One heartbeat line a minute so a long log shows memory drift.
static void soak_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        ESP_LOGI(TAG, "soak up=%lld internal=%u min=%u psram=%u psram_min=%u wifi=%d",
                 (long long)(esp_timer_get_time() / 1000000),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM), webcfg_is_on());
    }
}

esp_err_t gauge_console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t rc = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    rc.prompt = "gauge>";
    rc.max_cmdline_length = 2048;
    rc.task_stack_size = 8192;
    esp_console_dev_usb_serial_jtag_config_t hw = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    esp_err_t e = esp_console_new_repl_usb_serial_jtag(&hw, &rc, &repl);
    if (e != ESP_OK) { ESP_LOGW(TAG, "repl: %s", esp_err_to_name(e)); return e; }

    const esp_console_cmd_t cmds[] = {
        { .command = "stats",  .help = "memory, uptime, sources", .func = cmd_stats },
        { .command = "tap",    .help = "tap x y", .func = cmd_tap },
        { .command = "hold",   .help = "long press", .func = cmd_hold },
        { .command = "click",  .help = "encoder click", .func = cmd_click },
        { .command = "rot",    .help = "rot n (encoder steps, +/-)", .func = cmd_rot },
        { .command = "swipe",  .help = "swipe left|right|up|down", .func = cmd_swipe },
        { .command = "level",  .help = "current menu level", .func = cmd_level },
        { .command = "getcfg", .help = "print config JSON", .func = cmd_getcfg },
        { .command = "cfg",    .help = "cfg <json>: merge config", .func = cmd_cfg },
        { .command = "sim",    .help = "sim on|off", .func = cmd_sim },
        { .command = "wifi",   .help = "wifi on|off", .func = cmd_wifi },
        { .command = "tone",   .help = "tone n", .func = cmd_tone },
        { .command = "screen", .help = "screen save <name> | load <index>", .func = cmd_screen },
        { .command = "save",   .help = "save config now", .func = cmd_save },
        { .command = "vals",   .help = "channel values", .func = cmd_vals },
        { .command = "snap",   .help = "snap [step]: dump the first face as hex rows", .func = cmd_snap },
        { .command = "aux",    .help = "analog inputs: raw volts/ohms and value", .func = cmd_aux },
        { .command = "reboot", .help = "restart", .func = cmd_reboot },
    };
    for (size_t i = 0; i < sizeof cmds / sizeof cmds[0]; i++) esp_console_cmd_register(&cmds[i]);
    esp_console_register_help_command();
    ESP_RETURN_ON_ERROR(esp_console_start_repl(repl), TAG, "start repl");
    xTaskCreate(soak_task, "soak", 3072, NULL, 1, NULL);
    ESP_LOGI(TAG, "console ready");
    return ESP_OK;
}
