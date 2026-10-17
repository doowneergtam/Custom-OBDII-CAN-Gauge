idf_component_register(SRCS "webcfg.c"
                       INCLUDE_DIRS "include"
                       EMBED_FILES "page.html.gz"
                       REQUIRES esp_wifi esp_netif esp_event esp_http_server json config channels alarms esp_timer src_adc)
