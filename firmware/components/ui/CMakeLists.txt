idf_component_register(SRCS "ui.c" "face.c"
                       INCLUDE_DIRS "include"
                       REQUIRES channels hal_board theme alarms config src_can webcfg fonts esp_timer lvgl__lvgl)
