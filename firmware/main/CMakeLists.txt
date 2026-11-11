set(srcs "main.c" "settings.c" "reset.c" "events.c" "timers.c" "host.c" "app_host.c" "app_system.c" "app_kv.c"
         "storage.c" "sysinfo.c" "status.c" "screens.c" "ota.c" "web_server.c" "web_apps.c" "log_ring.c" "log_capture.c"
         "scene_menu.c" "scene_splash.c" "scene_setup.c" "launcher.c" "poller.c")
if(CONFIG_APP_DEV_CONSOLE)
    list(APPEND srcs "devconsole.c" "devtools.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "."
                       PRIV_INCLUDE_DIRS "../../sdk"
                       EMBED_TXTFILES "web/index.html"
                       REQUIRES appconf appsys board epd gfx input net qr ui wasm3
                              nvs_flash esp_hw_support esp_system esp_timer
                              app_update esp_app_format esp_driver_usb_serial_jtag
                              esp_http_server esp_http_client mbedtls json)

# The applications in sdk/apps are built with the SDK and packed into the
# image of the "apps" partition, which is flashed with the firmware.
set(repo_dir "${CMAKE_CURRENT_LIST_DIR}/../..")
set(apps_image_dir "${CMAKE_BINARY_DIR}/apps_image")
set(built_apps "")
if(NOT CONFIG_APP_BUNDLE_APPS)
    # Or the applications of an earlier build would still be packed.
    file(REMOVE_RECURSE "${apps_image_dir}")
endif()
file(MAKE_DIRECTORY "${apps_image_dir}")
if(CONFIG_APP_BUNDLE_APPS)
    if(NOT DEFINED ENV{WASI_SDK_PATH})
        message(FATAL_ERROR "WASI_SDK_PATH is not set. Building the applications in sdk/apps "
                            "needs wasi-sdk; set it, or turn APP_BUNDLE_APPS off in menuconfig "
                            "to build the firmware without applications.")
    endif()
    # CONFIGURE_DEPENDS: a new application or source file is picked up by
    # the next build without anyone remembering to reconfigure.
    file(GLOB app_dirs CONFIGURE_DEPENDS "${repo_dir}/sdk/apps/*")
    file(GLOB sdk_sources CONFIGURE_DEPENDS "${repo_dir}/sdk/lib/*" "${repo_dir}/sdk/host_api.h"
                                            "${repo_dir}/tools/mkapp/mkapp.py")
    foreach(app_dir ${app_dirs})
        get_filename_component(app_name "${app_dir}" NAME)
        file(GLOB app_sources CONFIGURE_DEPENDS "${app_dir}/*")
        add_custom_command(OUTPUT "${apps_image_dir}/${app_name}/app.wasm"
                           COMMAND "${repo_dir}/tools/mkapp/mkapp.py" "${app_dir}"
                                   --out "${apps_image_dir}/${app_name}"
                           DEPENDS ${app_sources} ${sdk_sources}
                           VERBATIM)
        list(APPEND built_apps "${apps_image_dir}/${app_name}/app.wasm")
    endforeach()
endif()
add_custom_target(bundled_apps DEPENDS ${built_apps})

# The system applications in firmware/system are built the same way and are
# part of the firmware itself: an update over the network brings them along,
# so they always speak the API of the host they run on.
if(NOT DEFINED ENV{WASI_SDK_PATH})
    message(FATAL_ERROR "WASI_SDK_PATH is not set. The firmware carries applications of its "
                        "own, and building them needs wasi-sdk.")
endif()
set(system_apps_dir "${CMAKE_BINARY_DIR}/system_apps")
file(GLOB system_sdk_sources CONFIGURE_DEPENDS "${repo_dir}/sdk/lib/*" "${repo_dir}/sdk/host_api.h"
                                               "${repo_dir}/tools/mkapp/mkapp.py")
foreach(system_app about device)
    set(system_app_dir "${CMAKE_CURRENT_LIST_DIR}/../system/${system_app}")
    set(system_app_out "${system_apps_dir}/${system_app}")
    # What is embedded is named after the application: the file's name is
    # what the build names the embedded data by.
    set(system_app_wasm "${system_apps_dir}/system_${system_app}.wasm")
    set(system_app_manifest "${system_apps_dir}/system_${system_app}.manifest")
    file(GLOB system_app_sources CONFIGURE_DEPENDS "${system_app_dir}/*")
    add_custom_command(OUTPUT "${system_app_wasm}" "${system_app_manifest}"
                       COMMAND "${repo_dir}/tools/mkapp/mkapp.py" "${system_app_dir}"
                               --out "${system_app_out}"
                       COMMAND "${CMAKE_COMMAND}" -E copy "${system_app_out}/app.wasm"
                               "${system_app_wasm}"
                       COMMAND "${CMAKE_COMMAND}" -E copy "${system_app_out}/manifest.json"
                               "${system_app_manifest}"
                       DEPENDS ${system_app_sources} ${system_sdk_sources}
                       VERBATIM)
    add_custom_target(system_app_${system_app}
                      DEPENDS "${system_app_wasm}" "${system_app_manifest}")
    target_add_binary_data(${COMPONENT_LIB} "${system_app_wasm}" BINARY
                           DEPENDS system_app_${system_app})
    target_add_binary_data(${COMPONENT_LIB} "${system_app_manifest}" TEXT
                           DEPENDS system_app_${system_app})
endforeach()
littlefs_create_partition_image(apps "${apps_image_dir}" FLASH_IN_PROJECT DEPENDS bundled_apps)
