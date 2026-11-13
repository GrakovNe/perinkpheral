# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
#
# Applications as a CMake project, for an IDE that reads one (CLion, VS Code,
# Qt Creator): the code is understood - the SDK's headers, strings.h and
# pictures.h - and building, running on a device and the live page are
# targets. tools/mkapp still does the building; nothing here replaces it.
#
#     cmake_minimum_required(VERSION 3.20)
#     include("<perinkpheral>/sdk/cmake/perinkpheral.cmake")   # before project()
#     project(my-app C)
#     perinkpheral_app(.)
#
# Needs WASI_SDK_PATH. The device's address for the run targets is
# PERINKPHERAL_HOST, in the environment or as a CMake variable.
#
# Targets of an application `name`:
#     name           builds the package into <build>/apps/name, with mkapp's report
#     name-run       builds, uploads and restarts the device into it
#     name-serve     the page with the live screen, the buttons, the log and Run
#     name-code      the sources as the compiler sees them: for the IDE, never built
include_guard(GLOBAL)

get_filename_component(PERINKPHERAL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/wasm32-toolchain.cmake")
endif()

function(perinkpheral_app app_dir)
    cmake_parse_arguments(PARSE_ARGV 1 app "NO_RUN" "" "")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    get_filename_component(app_dir "${app_dir}" ABSOLUTE)
    get_filename_component(name "${app_dir}" NAME)
    set(mkapp "${PERINKPHERAL_ROOT}/tools/mkapp/mkapp.py")
    set(devtool "${PERINKPHERAL_ROOT}/tools/devtool/devtool.py")
    set(generated "${CMAKE_BINARY_DIR}/generated/${name}")

    # strings.h and pictures.h, made now so that the code reads from the
    # first moment, and again by every build.
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${mkapp}" "${app_dir}" --headers "${generated}"
                    RESULT_VARIABLE made ERROR_VARIABLE said)
    if(NOT made EQUAL 0)
        message(WARNING "${name}: ${said}")
    endif()

    file(GLOB sources CONFIGURE_DEPENDS "${app_dir}/*.c" "${app_dir}/*.h")
    file(GLOB library CONFIGURE_DEPENDS "${PERINKPHERAL_ROOT}/sdk/lib/*.c")
    file(GLOB made_sources "${generated}/*.c")
    file(GLOB inputs CONFIGURE_DEPENDS "${app_dir}/manifest.json" "${app_dir}/strings/*"
                                       "${app_dir}/pictures/*")
    add_library(${name}-code OBJECT EXCLUDE_FROM_ALL ${sources} ${library} ${made_sources})
    target_include_directories(${name}-code PRIVATE "${PERINKPHERAL_ROOT}/sdk"
                               "${PERINKPHERAL_ROOT}/sdk/lib" "${generated}" "${app_dir}")
    target_compile_options(${name}-code PRIVATE -nostdlib -ffreestanding -fno-builtin -Oz
                           -Wall -Wextra)
    set_target_properties(${name}-code PROPERTIES C_STANDARD 11)

    set(environment "${CMAKE_COMMAND}" -E env "WASI_SDK_PATH=$ENV{WASI_SDK_PATH}")
    add_custom_target(${name} ALL
                      COMMAND ${environment} "${Python3_EXECUTABLE}" "${mkapp}" "${app_dir}"
                              --headers "${generated}"
                      COMMAND ${environment} "${Python3_EXECUTABLE}" "${mkapp}" "${app_dir}"
                              --out "${CMAKE_BINARY_DIR}/apps/${name}"
                      SOURCES ${inputs}
                      USES_TERMINAL VERBATIM)
    if(app_NO_RUN)
        return()
    endif()

    # The device: a CMake variable, or the environment when the target runs.
    set(PERINKPHERAL_HOST "$ENV{PERINKPHERAL_HOST}" CACHE STRING
        "The device's address, for the run targets")
    set(device "")
    if(PERINKPHERAL_HOST)
        set(device --host "${PERINKPHERAL_HOST}")
    endif()
    add_custom_target(${name}-run
                      COMMAND ${environment} "${Python3_EXECUTABLE}" "${devtool}" ${device}
                              run "${app_dir}"
                      USES_TERMINAL VERBATIM)
    add_custom_target(${name}-serve
                      COMMAND ${environment} "${Python3_EXECUTABLE}" "${devtool}" ${device}
                              serve "${app_dir}"
                      USES_TERMINAL VERBATIM)
endfunction()
