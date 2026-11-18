# SPDX-License-Identifier: MIT
idf_component_register(SRCS "main.c" "ota.c" "settings.c" "screen.c" "fonts.c"
    "${REPOSITORY}/firmware/components/net/ota_stream.c"
    INCLUDE_DIRS "." "${REPOSITORY}/firmware/components/net/include"
    REQUIRES board appconf gfx ui words epd deadlines esp_wifi esp_netif esp_event esp_http_server
    app_update esp_timer nvs_flash esp_driver_gpio esp_system mdns bootloader_support)
file(STRINGS "${REPOSITORY}/firmware/partitions.csv" partition_lines REGEX "^[^#]")
string(REPLACE " " "" partition_lines "${partition_lines}")
string(SHA256 partition_hash "${partition_lines}")
string(SUBSTRING "${partition_hash}" 0 8 partition_table_number)
target_compile_definitions(${COMPONENT_LIB} PRIVATE "RECOVERY_LAYOUT=0x${partition_table_number}u")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${REPOSITORY}/firmware/partitions.csv")
