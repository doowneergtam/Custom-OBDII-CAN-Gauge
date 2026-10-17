idf_component_register(SRCS "alarms.c" "audio.c"
                       INCLUDE_DIRS "include"
                       REQUIRES channels hal_board esp_timer)
