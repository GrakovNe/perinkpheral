set(srcs "main.c" "settings.c" "reset.c" "events.c" "timers.c" "host.c" "app_host.c"
         "sysinfo.c" "status.c" "screens.c"
         "scene_menu.c" "scene_device.c" "scene_splash.c")
if(CONFIG_APP_DEV_CONSOLE)
    list(APPEND srcs "devconsole.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "."
                       PRIV_INCLUDE_DIRS "../../sdk"
                       REQUIRES appconf appsys board epd gfx input ui
                              nvs_flash esp_hw_support esp_system esp_timer
                              app_update esp_app_format esp_driver_usb_serial_jtag)

# The sample application is built from source with the SDK and linked in.
if(NOT DEFINED ENV{WASI_SDK_PATH})
    message(FATAL_ERROR "WASI_SDK_PATH is not set: the firmware carries wasm applications, "
                        "and building them needs wasi-sdk")
endif()
set(repo_dir "${CMAKE_CURRENT_LIST_DIR}/../..")
set(hello_dir "${repo_dir}/sdk/apps/hello")
set(hello_out "${CMAKE_BINARY_DIR}/apps/hello")
file(GLOB app_sources "${hello_dir}/*.c" "${repo_dir}/sdk/lib/*" "${repo_dir}/sdk/host_api.h")
add_custom_command(OUTPUT "${hello_out}/app.wasm"
                   COMMAND "${repo_dir}/tools/mkapp/mkapp.py" "${hello_dir}" --out "${hello_out}"
                   DEPENDS ${app_sources} "${repo_dir}/tools/mkapp/mkapp.py"
                   VERBATIM)
add_custom_target(hello_app DEPENDS "${hello_out}/app.wasm")
target_add_binary_data(${COMPONENT_LIB} "${hello_out}/app.wasm" BINARY DEPENDS hello_app)
