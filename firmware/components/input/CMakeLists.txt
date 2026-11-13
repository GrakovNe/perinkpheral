idf_component_register(SRCS "btn_core.c" "buttons.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_gpio esp_hw_support esp_timer esp_system)
