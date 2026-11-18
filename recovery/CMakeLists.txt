# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.16)
get_filename_component(REPOSITORY "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(EXTRA_COMPONENT_DIRS
    "${REPOSITORY}/firmware/components/board"
    "${REPOSITORY}/firmware/components/appconf"
    "${REPOSITORY}/firmware/components/gfx"
    "${REPOSITORY}/firmware/components/ui"
    "${REPOSITORY}/firmware/components/words"
    "${REPOSITORY}/firmware/components/epd"
    "${REPOSITORY}/firmware/components/deadlines")
set(COMPONENTS main)
set(PROJECT_VER "1")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(perink_recovery)
if((RECOVERY_ANTENNA STREQUAL "y" AND NOT CONFIG_APP_ANTENNA_EXTERNAL) OR
   (RECOVERY_ANTENNA STREQUAL "n" AND CONFIG_APP_ANTENNA_EXTERNAL))
    message(FATAL_ERROR "Recovery antenna differs from main. Remove recovery's generated sdkconfig and rebuild.")
endif()
partition_table_add_check_size_target(recovery_check_size
    DEPENDS gen_project_binary BINARY_PATH "${CMAKE_BINARY_DIR}/perink_recovery.bin"
    PARTITION_TYPE app PARTITION_SUBTYPE factory)
add_dependencies(app recovery_check_size)
