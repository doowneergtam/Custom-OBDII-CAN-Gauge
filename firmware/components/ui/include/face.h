// A gauge face: one custom-drawn LVGL object showing one channel in the
// current theme's style (ring, dial, numeric) at any radius.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"
#include "channels.h"
#include "theme.h"

#ifdef __cplusplus
extern "C" {
#endif

// Creates the object centred at (cx, cy) with the given radius. `full` is
// the single-cell face (min/max, peak text, bigger type).
lv_obj_t *face_create(lv_obj_t *parent, ch_id_t ch, int32_t cx, int32_t cy, int32_t radius, bool full);
void      face_set_channel(lv_obj_t *face, ch_id_t ch);
ch_id_t   face_channel(lv_obj_t *face);
// Override the theme style for this face (-1 = follow g_theme.style).
void      face_set_style(lv_obj_t *face, int style);
void      face_set_focused(lv_obj_t *face, bool focused);
// Redraw with the current channel value; call from the LVGL tick.
void      face_refresh(lv_obj_t *face, int64_t now_us);
// Theme, units or ranges changed: every face re-renders its cached background on its next refresh.
void      face_restyle_all(void);
void      face_snapshot_dump(lv_obj_t *face, int step);   // console: print the face as hex rows

#ifdef __cplusplus
}
#endif
