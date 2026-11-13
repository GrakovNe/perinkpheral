# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
#
# The repository as one project for an IDE that reads CMake: the SDK's
# sample and the system applications with their code understood, and the
# rest as targets that run the project's own tools. Needs WASI_SDK_PATH.
#
# The firmware is an ESP-IDF project of its own: open the `firmware` folder
# for its code (AGENTS.md says how). Here it is only built and sent.
cmake_minimum_required(VERSION 3.20)
include("${CMAKE_CURRENT_LIST_DIR}/sdk/cmake/perinkpheral.cmake")
project(perinkpheral C)

perinkpheral_app(sdk/apps/hello)
foreach(system_app about device settings setup)
    perinkpheral_app(firmware/system/${system_app} NO_RUN)
endforeach()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(firmware_dir "${CMAKE_CURRENT_LIST_DIR}/firmware")
set(environment "${CMAKE_COMMAND}" -E env "WASI_SDK_PATH=$ENV{WASI_SDK_PATH}")

add_custom_target(host-tests
                  COMMAND ${environment} make -C "${firmware_dir}/test/host" test
                  USES_TERMINAL VERBATIM)
add_custom_target(guide
                  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/docs/guide/build.py"
                  USES_TERMINAL VERBATIM)
if(DEFINED ENV{IDF_PATH})
    add_custom_target(firmware
                      COMMAND ${environment} sh -c ". \"$ENV{IDF_PATH}/export.sh\" >/dev/null && idf.py build"
                      WORKING_DIRECTORY "${firmware_dir}"
                      USES_TERMINAL VERBATIM)
    add_custom_target(firmware-send
                      COMMAND "${Python3_EXECUTABLE}"
                              "${CMAKE_CURRENT_LIST_DIR}/tools/devtool/devtool.py" flash
                      WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
                      USES_TERMINAL VERBATIM)
    add_dependencies(firmware-send firmware)
else()
    message(STATUS "IDF_PATH is not set: no targets for the firmware")
endif()
