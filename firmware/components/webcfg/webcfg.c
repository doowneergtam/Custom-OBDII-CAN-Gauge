#include "webcfg.h"

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_http_server.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "cJSON.h"
#include "gauge_config.h"
#include "channels.h"
#include "src_adc.h"
#include <math.h>
#include "alarms.h"

static const char *TAG = "web";

extern const uint8_t page_gz_start[] asm("_binary_page_html_gz_start");
extern const uint8_t page_gz_end[]   asm("_binary_page_html_gz_end");

static webcfg_hooks_t s_hooks;
static httpd_handle_t s_httpd;
static esp_netif_t   *s_ap_netif;
static TaskHandle_t   s_dns_task;
static bool           s_on, s_wifi_inited;
static char           s_ssid[33], s_pass[65];
static int64_t        s_last_activity_us;
static esp_timer_handle_t s_idle_timer;

// ---------------------------------------------------------------- helpers
static void touch_activity(void) { s_last_activity_us = esp_timer_get_time(); }

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t r = httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
    cJSON_free(text);
    return r;
}

static esp_err_t send_ok(httpd_req_t *req)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    return send_json(req, o);
}

// ---------------------------------------------------------------- handlers
static esp_err_t h_page(httpd_req_t *req)
{
    touch_activity();
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)page_gz_start, page_gz_end - page_gz_start);
}

// Captive portal probes: answer with a redirect to the page.
static esp_err_t h_redirect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t h_get_config(httpd_req_t *req)
{
    touch_activity();
    char *text = config_to_json();
    if (!text) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t r = httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
    free(text);
    return r;
}

static esp_err_t h_put_config(httpd_req_t *req)
{
    touch_activity();
    size_t len = req->content_len;
    if (len == 0 || len > 48 * 1024) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad length"); return ESP_FAIL; }
    char *buf = malloc(len + 1);
    if (!buf) return httpd_resp_send_500(req);
    size_t got = 0;
    while (got < len) {
        int r = httpd_req_recv(req, buf + got, len - got);
        if (r <= 0) { free(buf); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv"); return ESP_FAIL; }
        got += r;
    }
    buf[len] = 0;
    esp_err_t err = config_apply_json(buf);
    free(buf);
    if (err != ESP_OK) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid config"); return ESP_FAIL; }
    config_mark_dirty();
    if (s_hooks.reload) s_hooks.reload();
    return send_ok(req);
}

static esp_err_t h_state(httpd_req_t *req)
{
    touch_activity();
    cJSON *root = cJSON_CreateObject();
    cJSON *vals = cJSON_AddObjectToObject(root, "values");
    int64_t now = esp_timer_get_time();
    for (int i = 0; i < channels_count(); i++) {
        const ch_def_t *d = channels_def((ch_id_t)i);
        if (channels_freshness((ch_id_t)i, now) == CH_DEAD) continue;
        cJSON_AddNumberToObject(vals, d->name, channels_value((ch_id_t)i));
    }
    cJSON *scr = cJSON_AddArrayToObject(root, "screen");
    for (int i = 0; i < g_ui_cfg.layout && i < UI_CELLS_MAX; i++) cJSON_AddItemToArray(scr, cJSON_CreateString(channels_def((ch_id_t)g_ui_cfg.cells[i])->name));
    cJSON_AddBoolToObject(root, "sim", g_ui_cfg.sim);
    cJSON *ar = cJSON_AddArrayToObject(root, "aux_raw");
    for (int i = 0; i < AUX_N; i++) {
        cJSON *a = cJSON_CreateObject();
        float v = src_adc_raw_volts(i), r = src_adc_raw_ohms(i);
        if (!isnan(v)) cJSON_AddNumberToObject(a, "volts", (double)llround(v * 1000.0) / 1000.0);
        if (!isnan(r) && r < 1e8f) cJSON_AddNumberToObject(a, "ohms", (double)llround(r));
        cJSON_AddItemToArray(ar, a);
    }
    return send_json(req, root);
}

static esp_err_t h_action(httpd_req_t *req)
{
    touch_activity();
    const char *uri = req->uri;
    char body[256] = {0};
    int n = httpd_req_recv(req, body, sizeof body - 1);
    if (n < 0) n = 0;
    body[n] = 0;
    if (strcmp(uri, "/api/reset-peaks") == 0) { if (s_hooks.reset_minmax) s_hooks.reset_minmax(); }
    else if (strcmp(uri, "/api/identify") == 0) { if (s_hooks.identify) s_hooks.identify(); }
    else if (strcmp(uri, "/api/test-alarm") == 0) {
        int tone = TONE_CHIRP;
        cJSON *j = cJSON_Parse(body);
        if (j) { cJSON *t = cJSON_GetObjectItem(j, "tone"); if (cJSON_IsString(t)) { for (int k = 0; k < TONE_COUNT; k++) if (strcasecmp(alarms_tone_name((alarm_tone_t)k), t->valuestring) == 0) tone = k; } cJSON_Delete(j); }
        if (s_hooks.test_tone) s_hooks.test_tone(tone);
    }
    else if (strcmp(uri, "/api/reboot") == 0) { send_ok(req); vTaskDelay(pdMS_TO_TICKS(300)); esp_restart(); return ESP_OK; }
    else { httpd_resp_send_404(req); return ESP_FAIL; }
    return send_ok(req);
}

// Channel names in the page's JSON are the same as the firmware's, except
// the page id form (e.g. "OILP") used in cells/tones. config_apply_json
// accepts both; see config.c.

// ---------------------------------------------------------------- captive DNS
// Answers every A query with 192.168.4.1 so phones open the page on join.
static void dns_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (sock < 0 || bind(sock, (struct sockaddr *)&addr, sizeof addr) < 0) { ESP_LOGW(TAG, "dns bind failed"); if (sock >= 0) close(sock); s_dns_task = NULL; vTaskDelete(NULL); return; }
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    uint8_t buf[512];
    while (s_on) {
        struct sockaddr_in from; socklen_t fl = sizeof from;
        int n = recvfrom(sock, buf, sizeof buf, 0, (struct sockaddr *)&from, &fl);
        if (n < 12) continue;
        // walk the question name to find its end
        int i = 12; while (i < n && buf[i]) i += buf[i] + 1;
        int qend = i + 5;   // null + type(2) + class(2)
        if (qend > n) continue;
        buf[2] = 0x81; buf[3] = 0x80;   // standard response, recursion available
        buf[6] = 0; buf[7] = 1;         // 1 answer
        buf[8] = buf[9] = buf[10] = buf[11] = 0;
        int o = qend;
        if (o + 16 > (int)sizeof buf) continue;
        buf[o++] = 0xC0; buf[o++] = 0x0C;                 // name pointer
        buf[o++] = 0; buf[o++] = 1; buf[o++] = 0; buf[o++] = 1;   // A, IN
        buf[o++] = 0; buf[o++] = 0; buf[o++] = 0; buf[o++] = 30;  // TTL 30 s
        buf[o++] = 0; buf[o++] = 4;
        buf[o++] = 192; buf[o++] = 168; buf[o++] = 4; buf[o++] = 1;
        sendto(sock, buf, o, 0, (struct sockaddr *)&from, fl);
    }
    close(sock);
    s_dns_task = NULL;
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------- lifecycle
static void idle_cb(void *arg)
{
    (void)arg;
    if (!s_on || g_ui_cfg.wifi_timeout_min == 0) return;
    wifi_sta_list_t sta;
    bool clients = esp_wifi_ap_get_sta_list(&sta) == ESP_OK && sta.num > 0;
    int64_t idle = esp_timer_get_time() - s_last_activity_us;
    if (!clients && idle > (int64_t)g_ui_cfg.wifi_timeout_min * 60 * 1000000) {
        ESP_LOGI(TAG, "idle %d min, hotspot off", g_ui_cfg.wifi_timeout_min);
        webcfg_stop();
    }
}

void webcfg_init(const webcfg_hooks_t *hooks)
{
    if (hooks) s_hooks = *hooks;
}

static esp_err_t wifi_once(void)
{
    if (s_wifi_inited) return ESP_OK;
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    esp_err_t e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;
    s_ap_netif = esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "storage");
    s_wifi_inited = true;
    return ESP_OK;
}

static void log_heap(const char *when)
{
    ESP_LOGI(TAG, "%s: internal free %u (largest %u), dma free %u (largest %u), psram free %u", when,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

esp_err_t webcfg_start(void)
{
    if (s_on) return ESP_OK;
    log_heap("before wifi");
    ESP_RETURN_ON_ERROR(wifi_once(), TAG, "wifi");

    // SSID from config, with the MAC tail appended when the config one is the default.
    uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if (g_ui_cfg.wifi_ssid[0]) snprintf(s_ssid, sizeof s_ssid, "%s", g_ui_cfg.wifi_ssid);
    else snprintf(s_ssid, sizeof s_ssid, "NB1-Gauge-%02X%02X", mac[4], mac[5]);
    snprintf(s_pass, sizeof s_pass, "%s", g_ui_cfg.wifi_password[0] ? g_ui_cfg.wifi_password : "miata2000");

    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.ap.ssid, s_ssid, sizeof wc.ap.ssid);
    wc.ap.ssid_len = strlen(s_ssid);
    strlcpy((char *)wc.ap.password, s_pass, sizeof wc.ap.password);
    wc.ap.channel = 6;
    wc.ap.max_connection = 3;
    wc.ap.authmode = strlen(s_pass) >= 8 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wc.ap.pmf_cfg.required = false;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &wc), TAG, "config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");

    if (mdns_init() == ESP_OK) { mdns_hostname_set("gauge"); mdns_instance_name_set("NB1 Gauge"); mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0); }

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.stack_size = 8192;
    hc.max_uri_handlers = 12;
    hc.lru_purge_enable = true;
    hc.uri_match_fn = httpd_uri_match_wildcard;
    ESP_RETURN_ON_ERROR(httpd_start(&s_httpd, &hc), TAG, "httpd");
    const httpd_uri_t routes[] = {
        { .uri = "/",                   .method = HTTP_GET,  .handler = h_page },
        { .uri = "/index.html",         .method = HTTP_GET,  .handler = h_page },
        { .uri = "/api/config",         .method = HTTP_GET,  .handler = h_get_config },
        { .uri = "/api/config",         .method = HTTP_PUT,  .handler = h_put_config },
        { .uri = "/api/state",          .method = HTTP_GET,  .handler = h_state },
        { .uri = "/api/*",              .method = HTTP_POST, .handler = h_action },
        { .uri = "/generate_204",       .method = HTTP_GET,  .handler = h_redirect },   // Android
        { .uri = "/hotspot-detect.html",.method = HTTP_GET,  .handler = h_redirect },   // iOS / macOS
        { .uri = "/connecttest.txt",    .method = HTTP_GET,  .handler = h_redirect },   // Windows
        { .uri = "/*",                  .method = HTTP_GET,  .handler = h_redirect },
    };
    for (size_t i = 0; i < sizeof routes / sizeof routes[0]; i++) httpd_register_uri_handler(s_httpd, &routes[i]);

    s_on = true;
    touch_activity();
    xTaskCreate(dns_task, "captive_dns", 3072, NULL, 4, &s_dns_task);
    if (!s_idle_timer) { const esp_timer_create_args_t a = { .callback = idle_cb, .name = "web_idle" }; esp_timer_create(&a, &s_idle_timer); }
    esp_timer_start_periodic(s_idle_timer, 15 * 1000000);
    ESP_LOGI(TAG, "hotspot %s up, http://192.168.4.1 and http://gauge.local", s_ssid);
    log_heap("after wifi");
    return ESP_OK;
}

void webcfg_stop(void)
{
    if (!s_on) return;
    s_on = false;
    if (s_idle_timer) esp_timer_stop(s_idle_timer);
    if (s_httpd) { httpd_stop(s_httpd); s_httpd = NULL; }
    mdns_free();
    esp_wifi_stop();
    ESP_LOGI(TAG, "hotspot off");
}

bool        webcfg_is_on(void)    { return s_on; }
const char *webcfg_ssid(void)     { return s_ssid; }
const char *webcfg_password(void) { return s_pass; }
int webcfg_clients(void)
{
    if (!s_on) return 0;
    wifi_sta_list_t sta;
    return esp_wifi_ap_get_sta_list(&sta) == ESP_OK ? sta.num : 0;
}
