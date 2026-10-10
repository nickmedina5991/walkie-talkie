idf_component_register(SRCS "oled.c" "audio_io.c" "input.c" "battery.c"
                       INCLUDE_DIRS "include"
                       REQUIRES board esp_driver_i2s esp_driver_i2c esp_driver_pcnt esp_driver_gpio esp_adc
                       PRIV_REQUIRES log)
