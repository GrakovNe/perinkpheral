idf_component_register(SRCS "epd.c" "refresh_policy.c" "frame_box.c" "display.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_gpio esp_timer hal soc)
