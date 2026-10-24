// Runtime fonts. LVGL's bundled Montserrat tops out at 48 px; above that we
// render Montserrat-Light.ttf with LVGL's TinyTTF and cache one font object
// per size step. The Stock style's numerals and labels use
// LiberationSans-BoldItalic.ttf (the factory cluster's bold italic look) at
// every size. Both TTFs are optional: drop them into components/fonts to
// compile them in; the bitmap Montserrat stands in when one is missing.
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Font for digits at roughly `px` pixels tall. Never returns NULL.
const lv_font_t *fonts_digits(int px);
// Bold italic font for the Stock style's numerals and labels. Never returns NULL.
const lv_font_t *fonts_numerals(int px);
bool             fonts_have_ttf(void);
bool             fonts_have_italic(void);

#ifdef __cplusplus
}
#endif
