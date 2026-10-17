idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES hal_board channels ui input_encoder src_can config alarms nvs_flash webcfg gauge_console src_adc)
