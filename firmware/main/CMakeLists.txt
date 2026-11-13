set(srcs "main.c" "settings.c" "reset.c" "events.c" "timers.c" "host.c" "app_host.c" "app_system.c" "app_kv.c" "app_code.c" "glyphs.c"
         "storage.c" "sysinfo.c" "status.c" "screens.c" "ota.c" "web_server.c" "web_apps.c" "log_ring.c" "log_capture.c"
         "scene_menu.c" "scene_splash.c" "scene_storage.c" "scene_confirm.c" "scene_notify.c" "scene_working.c" "launcher.c" "poller.c" "poller_net.c" "image_layout.c" "power.c" "memory.c")
if(CONFIG_APP_DEV_CONSOLE)
    list(APPEND srcs "devconsole.c" "devtools.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "."
                       PRIV_INCLUDE_DIRS "../../sdk"
                       REQUIRES appconf appsys board deadlines epd gfx input net qr ui wasm3
                              nvs_flash esp_partition esp_hw_support esp_system esp_timer esp_pm
                              app_update esp_app_format esp_driver_usb_serial_jtag
                              esp_http_server esp_http_client mbedtls json)

# The page goes into the firmware packed - 17 KB of its 55 - and to the
# browser as it is: every browser unpacks gzip.
idf_build_get_property(python PYTHON)
set(page "${CMAKE_CURRENT_LIST_DIR}/web/index.html")
set(packed_page "${CMAKE_BINARY_DIR}/index.html.gz")
add_custom_command(OUTPUT "${packed_page}"
                   COMMAND "${python}" -c
                           "import gzip, sys; open(sys.argv[2], 'wb').write(gzip.compress(open(sys.argv[1], 'rb').read(), 9, mtime=0))"
                           "${page}" "${packed_page}"
                   DEPENDS "${page}"
                   VERBATIM)
add_custom_target(packed_page DEPENDS "${packed_page}")
target_add_binary_data(${COMPONENT_LIB} "${packed_page}" BINARY DEPENDS packed_page)

# A number that is another for another partition table: the lines of
# partitions.csv that say something, without the spaces. An image carries
# it, and a device takes no update built for another table.
# The path without "..": a build with Ninja takes two spellings of one file
# for two files, and stops.
get_filename_component(partitions_csv "${CMAKE_CURRENT_LIST_DIR}/../partitions.csv" ABSOLUTE)
file(STRINGS "${partitions_csv}" partition_lines REGEX "^[^#]")
string(REPLACE " " "" partition_lines "${partition_lines}")
string(SHA256 partition_hash "${partition_lines}")
string(SUBSTRING "${partition_hash}" 0 8 partition_table_number)
target_compile_definitions(${COMPONENT_LIB} PRIVATE "APP_PARTITION_TABLE=0x${partition_table_number}u")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${partitions_csv}")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u image_layout")

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
    # What an earlier build packed and sdk/apps has no more must not be
    # packed again: an application that moved out would be flashed for ever.
    file(GLOB packed_dirs "${apps_image_dir}/*")
    foreach(packed_dir ${packed_dirs})
        get_filename_component(packed_name "${packed_dir}" NAME)
        if(NOT EXISTS "${repo_dir}/sdk/apps/${packed_name}")
            file(REMOVE_RECURSE "${packed_dir}")
        endif()
    endforeach()
    file(GLOB sdk_sources CONFIGURE_DEPENDS "${repo_dir}/sdk/lib/*" "${repo_dir}/sdk/host_api.h"
                                            "${repo_dir}/tools/mkapp/mkapp.py")
    foreach(app_dir ${app_dirs})
        get_filename_component(app_name "${app_dir}" NAME)
        file(GLOB app_sources CONFIGURE_DEPENDS "${app_dir}/*")
        add_custom_command(OUTPUT "${apps_image_dir}/${app_name}/app.wasm"
                           COMMAND "${CMAKE_COMMAND}" -E env "WASI_SDK_PATH=$ENV{WASI_SDK_PATH}"
                                   "${repo_dir}/tools/mkapp/mkapp.py" "${app_dir}"
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
foreach(system_app about device settings setup)
    set(system_app_dir "${CMAKE_CURRENT_LIST_DIR}/../system/${system_app}")
    set(system_app_out "${system_apps_dir}/${system_app}")
    # What is embedded is named after the application: the file's name is
    # what the build names the embedded data by.
    set(system_app_wasm "${system_apps_dir}/system_${system_app}.wasm")
    set(system_app_manifest "${system_apps_dir}/system_${system_app}.manifest")
    # A system application has no files of its own on the device: its words
    # in another language are embedded beside it. Russian is the one there is.
    set(system_app_strings "${system_apps_dir}/system_${system_app}.strings_ru")
    set(system_app_words "${system_apps_dir}/system_${system_app}.manifest_ru")
    file(GLOB system_app_sources CONFIGURE_DEPENDS "${system_app_dir}/*")
    add_custom_command(OUTPUT "${system_app_wasm}" "${system_app_manifest}"
                              "${system_app_strings}" "${system_app_words}"
                       COMMAND "${CMAKE_COMMAND}" -E env "WASI_SDK_PATH=$ENV{WASI_SDK_PATH}"
                                   "${repo_dir}/tools/mkapp/mkapp.py" "${system_app_dir}"
                               --out "${system_app_out}"
                       COMMAND "${CMAKE_COMMAND}" -E copy "${system_app_out}/app.wasm"
                               "${system_app_wasm}"
                       COMMAND "${CMAKE_COMMAND}" -E copy "${system_app_out}/manifest.json"
                               "${system_app_manifest}"
                       COMMAND "${CMAKE_COMMAND}" -E copy "${system_app_out}/strings.ru"
                               "${system_app_strings}"
                       COMMAND "${CMAKE_COMMAND}" -E copy "${system_app_out}/manifest.ru"
                               "${system_app_words}"
                       DEPENDS ${system_app_sources} ${system_sdk_sources}
                       VERBATIM)
    add_custom_target(system_app_${system_app}
                      DEPENDS "${system_app_wasm}" "${system_app_manifest}"
                              "${system_app_strings}" "${system_app_words}")
    target_add_binary_data(${COMPONENT_LIB} "${system_app_wasm}" BINARY
                           DEPENDS system_app_${system_app})
    target_add_binary_data(${COMPONENT_LIB} "${system_app_manifest}" TEXT
                           DEPENDS system_app_${system_app})
    target_add_binary_data(${COMPONENT_LIB} "${system_app_strings}" BINARY
                           DEPENDS system_app_${system_app})
    target_add_binary_data(${COMPONENT_LIB} "${system_app_words}" TEXT
                           DEPENDS system_app_${system_app})
endforeach()
littlefs_create_partition_image(apps "${apps_image_dir}" FLASH_IN_PROJECT DEPENDS bundled_apps)
