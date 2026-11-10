set(srcs "main.c" "settings.c" "reset.c" "events.c" "host.c"
         "sysinfo.c" "status.c" "screens.c"
         "scene_menu.c" "scene_device.c" "scene_splash.c")
if(CONFIG_APP_DEV_CONSOLE)
    list(APPEND srcs "devconsole.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "."
                       REQUIRES appconf board epd gfx input ui
                              nvs_flash esp_hw_support esp_system esp_timer
                              app_update esp_app_format esp_driver_usb_serial_jtag)
