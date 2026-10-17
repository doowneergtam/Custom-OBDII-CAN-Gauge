// UI layer: screen composition, encoder handling, menus, alarm banner.
// Reads channel records, theme, alarm and ui config; never touches CAN/ADC.
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Build the screen from g_ui_cfg / g_theme and start the refresh timer.
// Call once after hal_board_init(), config_load() and input_encoder_init().
// `group` is the encoder's group (may be NULL for touch-only bring-up).
void ui_init(lv_group_t *group);

// Re-apply g_ui_cfg / g_theme / alarms after they changed outside the menu
// (web interface). Safe from any task; takes the LVGL lock.
void ui_reload(void);
// Flash the display white twice so the user can tell which gauge this is.
void ui_identify(void);

// Bench console hooks: drive the UI like touch/encoder would.
// what: "tap" (a=x, b=y), "hold", "click", "rot" (a=steps), "swipe" (a='l'/'r'/'u'/'d')
void ui_test_input(const char *what, int a, int b);
int  ui_test_level(void);   // 0 = gauge screen, else menu level

#ifdef __cplusplus
}
#endif
