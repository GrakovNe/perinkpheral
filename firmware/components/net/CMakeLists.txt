idf_component_register(SRCS "wifi_plan.c" "wifi.c" "net_time.c" "ota_stream.c" "ota_health.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_wifi esp_netif esp_event esp_timer lwip)
