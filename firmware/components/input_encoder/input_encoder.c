#include "input_encoder.h"

#include <stdatomic.h>
#include "esp_log.h"
#include "esp_check.h"
#include "iot_knob.h"
#include "iot_button.h"
#include "button_gpio.h"

static const char *TAG = "enc";

static knob_handle_t   s_knob;
static button_handle_t s_btn;
static lv_indev_t     *s_indev;
static lv_group_t     *s_group;

// Written from the knob/button component timer task, read from the LVGL task.
static atomic_int  s_steps;     // +1 per detent clockwise, -1 anticlockwise
static atomic_bool s_pressed;
static bool        s_reverse;

// read_cb state machine: each rotation step is reported as one press +
// one release of LEFT/RIGHT so LVGL sees distinct key events.
static uint32_t s_rot_key;
static bool     s_rot_down;
static bool     s_enter_down;

static void knob_left_cb(void *h, void *arg)  { (void)h; (void)arg; atomic_fetch_add(&s_steps, s_reverse ? +1 : -1); }
static void knob_right_cb(void *h, void *arg) { (void)h; (void)arg; atomic_fetch_add(&s_steps, s_reverse ? -1 : +1); }
static void btn_down_cb(void *h, void *arg)   { (void)h; (void)arg; atomic_store(&s_pressed, true); }
static void btn_up_cb(void *h, void *arg)     { (void)h; (void)arg; atomic_store(&s_pressed, false); }

static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;

    // Finish a rotation key before anything else.
    if (s_rot_down) {
        data->key   = s_rot_key;
        data->state = LV_INDEV_STATE_RELEASED;
        s_rot_down  = false;
        return;
    }

    // Push button: ENTER follows the physical level so LVGL times long press.
    bool pressed = atomic_load(&s_pressed);
    if (pressed || s_enter_down) {
        data->key    = LV_KEY_ENTER;
        data->state  = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        s_enter_down = pressed;
        return;
    }

    // Rotation: consume one detent per read.
    int steps = atomic_load(&s_steps);
    if (steps != 0) {
        int dir = steps > 0 ? 1 : -1;
        atomic_fetch_sub(&s_steps, dir);
        s_rot_key   = dir > 0 ? LV_KEY_RIGHT : LV_KEY_LEFT;
        s_rot_down  = true;
        data->key   = s_rot_key;
        data->state = LV_INDEV_STATE_PRESSED;
        return;
    }

    data->state = LV_INDEV_STATE_RELEASED;
}

esp_err_t input_encoder_init(const input_encoder_config_t *cfg, lv_display_t *disp)
{
    ESP_RETURN_ON_FALSE(cfg && disp, ESP_ERR_INVALID_ARG, TAG, "bad args");
    s_reverse = cfg->reverse;

    knob_config_t kc = {
        .default_direction = 0,
        .gpio_encoder_a    = cfg->gpio_a,
        .gpio_encoder_b    = cfg->gpio_b,
        .enable_power_save = false,
    };
    s_knob = iot_knob_create(&kc);
    ESP_RETURN_ON_FALSE(s_knob, ESP_FAIL, TAG, "knob create");
    iot_knob_register_cb(s_knob, KNOB_LEFT,  knob_left_cb,  NULL);
    iot_knob_register_cb(s_knob, KNOB_RIGHT, knob_right_cb, NULL);

    button_config_t bc = { .long_press_time = 0, .short_press_time = 0 };
    button_gpio_config_t gc = {
        .gpio_num     = cfg->gpio_push,
        .active_level = 0,
        .enable_power_save = false,
        .disable_pull = false,
    };
    ESP_RETURN_ON_ERROR(iot_button_new_gpio_device(&bc, &gc, &s_btn), TAG, "button create");
    iot_button_register_cb(s_btn, BUTTON_PRESS_DOWN, NULL, btn_down_cb, NULL);
    iot_button_register_cb(s_btn, BUTTON_PRESS_UP,   NULL, btn_up_cb,   NULL);

    s_indev = lv_indev_create();
    ESP_RETURN_ON_FALSE(s_indev, ESP_FAIL, TAG, "indev create");
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(s_indev, read_cb);
    lv_indev_set_display(s_indev, disp);
    lv_indev_set_long_press_time(s_indev, cfg->long_press_ms ? cfg->long_press_ms : 600);
    lv_timer_set_period(lv_indev_get_read_timer(s_indev), 10);

    s_group = lv_group_create();
    lv_group_set_default(s_group);
    lv_indev_set_group(s_indev, s_group);

    ESP_LOGI(TAG, "encoder A=%d B=%d push=%d", cfg->gpio_a, cfg->gpio_b, cfg->gpio_push);
    return ESP_OK;
}

lv_indev_t *input_encoder_indev(void) { return s_indev; }
lv_group_t *input_encoder_group(void) { return s_group; }
