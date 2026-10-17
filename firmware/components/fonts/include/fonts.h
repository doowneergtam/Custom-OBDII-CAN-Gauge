// Large digits. LVGL's bundled Montserrat tops out at 48 px; above that we
// render Montserrat-Light.ttf at runtime with LVGL's TinyTTF and cache one
// font object per size step. Falls back to the 48 px bitmap font when the
// TTF is not compiled in (components/fonts/Montserrat-Light.ttf missing).
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Font for digits at roughly `px` pixels tall. Never returns NULL.
const lv_font_t *fonts_digits(int px);
bool             fonts_have_ttf(void);

#ifdef __cplusplus
}
#endif
