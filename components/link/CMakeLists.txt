idf_component_register(SRCS "crypto.c" "espnow_link.c"
                       INCLUDE_DIRS "include"
                       REQUIRES proto esp_wifi esp_event mbedtls
                       PRIV_REQUIRES esp_netif log)
