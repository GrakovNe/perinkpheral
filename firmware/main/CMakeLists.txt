idf_component_register(SRCS "main.c" "settings.c" "reset.c" "demo.c" "screens.c"
                       INCLUDE_DIRS "."
                       REQUIRES appconf board epd gfx input
                              nvs_flash esp_hw_support esp_system
                              app_update esp_app_format)
