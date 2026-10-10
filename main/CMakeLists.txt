idf_component_register(SRCS "main.c" "app_common.c" "app_hwtest.c" "app_loopback.c" "app_radiotest.c"
                            "app_walkie.c" "walkie_ui.c"
                       INCLUDE_DIRS "."
                       REQUIRES board drivers link proto nvs_flash esp_timer esp_wifi)
