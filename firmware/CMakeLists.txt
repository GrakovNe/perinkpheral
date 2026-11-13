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

# ESP-IDF and wasi-sdk are found, and their tools put on PATH, when the
# environment does not have them already: an IDE needs no setting up.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/environment.cmake")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# What the bootloader's own project cannot find by itself when the build
# runs outside ESP-IDF's environment.
if(CMAKE_MAKE_PROGRAM)
    idf_build_set_property(EXTRA_CMAKE_ARGS "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}" APPEND)
endif()
if(IDF_COMPILER_DIR)
    idf_build_set_property(EXTRA_CMAKE_ARGS "-DCMAKE_PROGRAM_PATH=${IDF_COMPILER_DIR}" APPEND)
endif()
project(perinkpheral)
