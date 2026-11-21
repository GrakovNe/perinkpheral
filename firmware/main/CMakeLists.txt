set(srcs "main.c" "settings.c" "reset.c" "crash_note.c" "flash_budget.c" "deadline_budget.c" "events.c" "timers.c" "host.c" "host_notices.c" "host_apps.c" "app_host.c" "app_system.c" "app_kv.c" "app_code.c" "glyphs.c"
         "storage.c" "sysinfo.c" "status.c" "screens.c" "header.c" "menu_order.c" "ota.c" "web_server.c" "web_util.c" "web_apps.c" "log_ring.c" "log_capture.c"
         "scene_menu.c" "scene_splash.c" "scene_storage.c" "scene_confirm.c" "scene_notify.c" "scene_working.c" "launcher.c" "poller.c" "poller_net.c" "image_layout.c" "power.c" "memory.c"
         "app_install.c" "app_update.c")
if(CONFIG_APP_DEV_CONSOLE)
    list(APPEND srcs "devconsole.c" "devtools.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "."
                       PRIV_INCLUDE_DIRS "../../sdk"
                       REQUIRES appconf appstore appsys board deadlines epd gfx input net qr ui wasm3 words
                              nvs_flash esp_partition esp_hw_support esp_system esp_timer esp_pm
                              app_update esp_app_format esp_driver_usb_serial_jtag
                              esp_http_server esp_http_client mbedtls)

# The page is made of its parts (web/page.html, style.css, js/), put together
# by tools/perinkpheral page, and goes into the firmware packed - and to the
# browser as it is: every browser unpacks gzip.
set(web_dir "${CMAKE_CURRENT_LIST_DIR}/web")
file(GLOB_RECURSE web_parts CONFIGURE_DEPENDS "${web_dir}/*.html" "${web_dir}/*.css" "${web_dir}/*.js" "${web_dir}/*.woff2"
                                              "${CMAKE_CURRENT_LIST_DIR}/../../tools/cli/src/main/kotlin/dev/perinkpheral/cli/FirmwareTools.kt"
                                              "${CMAKE_CURRENT_LIST_DIR}/../system/settings/cities.json"
                                              "${CMAKE_CURRENT_LIST_DIR}/../components/appstore/include/catalog.h"
                                              "${CMAKE_CURRENT_LIST_DIR}/../../sdk/lib/manifest_limits.h")
set(page "${CMAKE_BINARY_DIR}/index.html")
set(packed_page "${CMAKE_BINARY_DIR}/index.html.gz")
add_custom_command(OUTPUT "${page}" "${packed_page}"
                   COMMAND "${CMAKE_CURRENT_LIST_DIR}/../../tools/perinkpheral" page "${web_dir}" "${page}" "${packed_page}"
                   DEPENDS ${web_parts}
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
# Every partition's size as PARTITION_<NAME>_BYTES: flash_budget.c holds what
# is promised of the flash against what the table gives, at compile time.
foreach(partition_line IN LISTS partition_lines)
    string(REPLACE "," ";" partition_fields "${partition_line}")
    list(GET partition_fields 0 partition_name)
    list(GET partition_fields 4 partition_size)
    string(TOUPPER "${partition_name}" partition_name)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE "PARTITION_${partition_name}_BYTES=${partition_size}u")
endforeach()
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u image_layout")
# crash_note.c records a panic before the system handler runs.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_panic_handler")

# The image of the "apps" partition, which only a first flash writes: the
# applications firmware/preinstalled.properties names, as the catalog
# repository has them committed at the commit it pins - checked against the
# catalog's sizes and SHA-256 and laid out as an installation lays them out,
# so the device takes them for installed ones. The packages are kept under
# the build folder by the pin; a build with neither them nor the network
# needs a checkout of the catalog repository (PERINKPHERAL_APPS).
set(repo_dir "${CMAKE_CURRENT_LIST_DIR}/../..")
set(apps_image_dir "${CMAKE_BINARY_DIR}/apps_image")
set(preinstalled_done "")
if(NOT CONFIG_APP_BUNDLE_APPS)
    # Or the applications of an earlier build would still be packed.
    file(REMOVE_RECURSE "${apps_image_dir}")
endif()
file(MAKE_DIRECTORY "${apps_image_dir}")
if(CONFIG_APP_BUNDLE_APPS)
    get_filename_component(preinstalled_list "${CMAKE_CURRENT_LIST_DIR}/../preinstalled.properties" ABSOLUTE)
    # Beside the image's folder, not in it: whatever is in it is flashed.
    set(preinstalled_done "${CMAKE_BINARY_DIR}/preinstalled.done")
    idf_build_get_property(firmware_version PROJECT_VER)
    # What the command holds the packages to: the device's limits and the tool itself.
    file(GLOB_RECURSE preinstalled_inputs CONFIGURE_DEPENDS "${repo_dir}/tools/core/src/main/*" "${repo_dir}/tools/cli/src/main/*")
    list(APPEND preinstalled_inputs "${preinstalled_list}" "${partitions_csv}"
         "${repo_dir}/sdk/lib/manifest_limits.h" "${CMAKE_CURRENT_LIST_DIR}/app_code.h" "${CMAKE_CURRENT_LIST_DIR}/app_code.c"
         "${CMAKE_CURRENT_LIST_DIR}/../components/appstore/include/catalog.h"
         "${CMAKE_CURRENT_LIST_DIR}/../components/appstore/include/app_room.h"
         "${CMAKE_CURRENT_LIST_DIR}/../components/appstore/include/app_abi.h")
    add_custom_command(OUTPUT "${preinstalled_done}"
                       COMMAND "${repo_dir}/tools/perinkpheral" preinstalled build "${preinstalled_list}"
                               --out "${apps_image_dir}" --cache "${CMAKE_BINARY_DIR}/preinstalled_cache"
                               --firmware "${firmware_version}"
                       COMMAND "${CMAKE_COMMAND}" -E touch "${preinstalled_done}"
                       DEPENDS ${preinstalled_inputs}
                       VERBATIM)
endif()
add_custom_target(preinstalled_apps DEPENDS ${preinstalled_done})

# The system applications in firmware/system are built with the SDK
# (tools/perinkpheral build) and are part of the firmware itself: an update
# over the network brings them along, so they always speak the API of the
# host they run on.
if(NOT DEFINED ENV{WASI_SDK_PATH})
    message(FATAL_ERROR "WASI_SDK_PATH is not set. The firmware carries applications of its "
                        "own, and building them needs wasi-sdk.")
endif()
set(system_apps_dir "${CMAKE_BINARY_DIR}/system_apps")
file(GLOB_RECURSE system_sdk_sources CONFIGURE_DEPENDS "${repo_dir}/sdk/lib/*" "${repo_dir}/sdk/host_api.h" "${repo_dir}/sdk/host/*"
                                                       "${repo_dir}/tools/core/src/*"
                                                    "${repo_dir}/tools/cli/src/*")
foreach(system_app about settings setup)
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
                                   "${repo_dir}/tools/perinkpheral" build "${system_app_dir}"
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
littlefs_create_partition_image(apps "${apps_image_dir}" FLASH_IN_PROJECT DEPENDS preinstalled_apps)
