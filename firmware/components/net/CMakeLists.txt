idf_component_register(SRCS "wifi_plan.c" "wifi.c" "net_time.c" "ota_stream.c" "ota_health.c"
                            "captive_dns.c" "captive_dns_task.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "../../../sdk"
                       REQUIRES deadlines board
                       PRIV_REQUIRES esp_wifi esp_netif esp_event esp_timer lwip)
