# SPDX-License-Identifier: MIT
# Called after project(), once IDF has created the flash targets.
if(NOT CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE OR NOT CONFIG_BOOTLOADER_FACTORY_RESET OR
   NOT CONFIG_BOOTLOADER_OTA_DATA_ERASE OR NOT CONFIG_BOOTLOADER_NUM_PIN_FACTORY_RESET EQUAL 21 OR
   NOT CONFIG_BOOTLOADER_FACTORY_RESET_PIN_LOW OR NOT CONFIG_BOOTLOADER_HOLD_TIME_GPIO EQUAL 3 OR
   NOT CONFIG_BOOTLOADER_DATA_FACTORY_RESET STREQUAL "")
    message(FATAL_ERROR "Recovery requires rollback, BACK GPIO21 entry and erasing only otadata. Regenerate sdkconfig from sdkconfig.defaults.")
endif()
partition_table_get_partition_info(main_offset "--partition-name ota_0" "offset")
partition_table_get_partition_info(recovery_offset "--partition-name factory" "offset")
# IDF chooses factory for its one app by default. Move only that image, in
# every representation used by esptool and flasher_args.json.
foreach(flash_target flash app-flash)
    foreach(property FLASH_FILE FLASH_ENTRY IMAGES)
        get_property(entries TARGET ${flash_target} PROPERTY ${property})
        string(REPLACE "${recovery_offset}" "${main_offset}" entries "${entries}")
        set_property(TARGET ${flash_target} PROPERTY ${property} "${entries}")
    endforeach()
endforeach()
partition_table_add_check_size_target(main_check_size
    DEPENDS gen_project_binary BINARY_PATH "${CMAKE_BINARY_DIR}/perinkpheral.bin"
    PARTITION_TYPE app PARTITION_SUBTYPE ota_0)
add_dependencies(app main_check_size)

get_filename_component(recovery_source "${CMAKE_CURRENT_LIST_DIR}/../../recovery" ABSOLUTE)
set(recovery_build "${CMAKE_BINARY_DIR}/recovery")
if(CONFIG_APP_ANTENNA_EXTERNAL)
    set(recovery_antenna y)
else()
    set(recovery_antenna n)
endif()
# Use a separate generated config for each antenna choice because IDF's
# defaults cannot override a value already kept in sdkconfig.
set(recovery_antenna_defaults "${recovery_build}/sdkconfig.antenna.${recovery_antenna}")
file(CONFIGURE OUTPUT "${recovery_antenna_defaults}" CONTENT "CONFIG_APP_ANTENNA_EXTERNAL=${recovery_antenna}
")
add_custom_target(recovery ALL
    COMMAND "${PYTHON}" "$ENV{IDF_PATH}/tools/idf.py" -C "${recovery_source}"
            -B "${recovery_build}" -D "SDKCONFIG=${recovery_build}/sdkconfig.${recovery_antenna}"
            -D "SDKCONFIG_DEFAULTS=${recovery_source}/sdkconfig.defaults;${recovery_antenna_defaults}"
            -D "RECOVERY_ANTENNA=${recovery_antenna}" build
    BYPRODUCTS "${recovery_build}/perink_recovery.bin"
    USES_TERMINAL VERBATIM)
esptool_py_flash_target_image(flash recovery "${recovery_offset}" "${recovery_build}/perink_recovery.bin")
add_dependencies(flash recovery main_check_size)
# Empty boot metadata deliberately boots recovery. BACK explicitly tries the
# installed main with probation, both after first install and a USB app-flash.
esptool_py_flash_target_image(app-flash otadata "0xf000" "${CMAKE_BINARY_DIR}/ota_data_initial.bin")
add_dependencies(app-flash main_check_size blank_ota_data)

# Migration uses the first-install flash settings with an exact list that
# omits the file system, the application code cache and both NVS partitions.
idf_component_get_property(migration_args esptool_py FLASH_ARGS)
idf_component_get_property(migration_sub_args esptool_py FLASH_SUB_ARGS)
esptool_py_flash_target(migration-flash "${migration_args}" "${migration_sub_args}")
get_property(install_images TARGET flash PROPERTY IMAGES)
foreach(image IN ITEMS "0x0 bootloader/bootloader.bin" "0x8000 partition_table/partition-table.bin"
        "0xf000 ota_data_initial.bin" "0x20000 recovery/perink_recovery.bin")
    if(NOT image IN_LIST install_images)
        message(FATAL_ERROR "Required migration image is absent from the flash target: ${image}")
    endif()
    set_property(TARGET migration-flash APPEND PROPERTY IMAGES "${image}")
endforeach()
# IDF stores the main binary's name as a generator expression until generation.
list(FILTER install_images INCLUDE REGEX "^0x100000 ")
list(LENGTH install_images main_entries)
if(NOT main_entries EQUAL 1)
    message(FATAL_ERROR "Expected one main image at 0x100000 in the flash target.")
endif()
set_property(TARGET migration-flash APPEND PROPERTY IMAGES "${install_images}")
add_dependencies(migration-flash recovery main_check_size bootloader partition_table_bin blank_ota_data)
