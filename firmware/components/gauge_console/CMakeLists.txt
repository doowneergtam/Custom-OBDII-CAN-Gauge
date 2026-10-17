idf_component_register(SRCS "gauge_console.c"
                       INCLUDE_DIRS "include"
                       REQUIRES console channels config theme alarms src_can webcfg ui esp_timer src_adc)
