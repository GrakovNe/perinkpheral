cmake_minimum_required(VERSION 3.16)

# A release build is told its version. Any other build is the version the
# project is at and the commit it was made from: "0.1.0-0adbfb4".
set(APP_BASE_VERSION "0.1.0")
if(DEFINED ENV{APP_VERSION})
    set(PROJECT_VER "$ENV{APP_VERSION}")
else()
    execute_process(COMMAND git rev-parse --short HEAD
                    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
                    OUTPUT_VARIABLE app_commit OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET)
    if(app_commit)
        set(PROJECT_VER "${APP_BASE_VERSION}-${app_commit}")
    else()
        set(PROJECT_VER "${APP_BASE_VERSION}")
    endif()
endif()

if(NOT DEFINED ENV{IDF_PATH})
    message(FATAL_ERROR "IDF_PATH is not set. The firmware is an ESP-IDF 5.5 project: build it "
                        "from a shell where ESP-IDF's export.sh was run, or give an IDE that "
                        "environment (AGENTS.md).")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(perinkpheral)
