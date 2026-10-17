#include "fonts.h"
#include "esp_log.h"

static const char *TAG = "fonts";

#if FONTS_HAVE_TTF
extern const uint8_t ttf_start[] asm("_binary_Montserrat_Light_ttf_start");
extern const uint8_t ttf_end[]   asm("_binary_Montserrat_Light_ttf_end");
#endif

// Size steps above the bitmap fonts. Each is created on first use and kept.
static const int STEPS[] = { 56, 64, 72, 80, 96, 112, 128, 144, 160, 180, 200, 240, 280, 320, 400 };
#define NSTEPS ((int)(sizeof STEPS / sizeof STEPS[0]))
static lv_font_t *s_fonts[NSTEPS];

bool fonts_have_ttf(void)
{
#if FONTS_HAVE_TTF
    return true;
#else
    return false;
#endif
}

const lv_font_t *fonts_digits(int px)
{
    if (px <= 48 || !fonts_have_ttf()) {
        if (px >= 48) return &lv_font_montserrat_48;
        if (px >= 40) return &lv_font_montserrat_40;
        if (px >= 32) return &lv_font_montserrat_32;
        if (px >= 28) return &lv_font_montserrat_28;
        if (px >= 24) return &lv_font_montserrat_24;
        if (px >= 20) return &lv_font_montserrat_20;
        if (px >= 18) return &lv_font_montserrat_18;
        if (px >= 16) return &lv_font_montserrat_16;
        if (px >= 14) return &lv_font_montserrat_14;
        return &lv_font_montserrat_12;
    }
#if FONTS_HAVE_TTF
    int i = 0;
    while (i + 1 < NSTEPS && STEPS[i] < px) i++;
    if (!s_fonts[i]) {
        // TinyTTF's size is the line height; Montserrat digits are about 0.72 of it.
        s_fonts[i] = lv_tiny_ttf_create_data(ttf_start, ttf_end - ttf_start, STEPS[i]);
        if (!s_fonts[i]) { ESP_LOGW(TAG, "ttf %d px failed", STEPS[i]); return &lv_font_montserrat_48; }
        ESP_LOGI(TAG, "ttf font %d px ready", STEPS[i]);
    }
    return s_fonts[i];
#else
    return &lv_font_montserrat_48;
#endif
}
