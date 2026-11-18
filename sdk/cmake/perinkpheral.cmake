# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
#
# Applications as a CMake project, for an IDE that reads one (CLion, VS Code,
# Qt Creator): the code is understood - the SDK's headers, strings.h and
# pictures.h - and compiled by clang of wasi-sdk with the flags of the build.
#
#     cmake_minimum_required(VERSION 3.20)
#     include("<perinkpheral>/sdk/cmake/perinkpheral.cmake")   # before project()
#     project(my-app C)
#     perinkpheral_app(.)
#
# Needs WASI_SDK_PATH. The flags of the compiler are the build's own, asked of
# tools/perinkpheral (Java), and so are strings.h and pictures.h, made into the
# build folder. The CLion plugin gives all of it in its toolchain file instead:
# PERINKPHERAL_COMPILE_FLAGS, PERINKPHERAL_LINK_FLAGS and PERINKPHERAL_GENERATED,
# the folder of the headers it makes itself.
#
# Targets of an application `name`:
#     name           compiles the code into app.wasm
#     name-package   builds and checks the package as the device takes it, with the report
#     name-run       builds, uploads, restarts the device and opens it (PERINKPHERAL_HOST)
#     name-serve     the page with the live screen, the buttons, the log and Run
include_guard(GLOBAL)

get_filename_component(PERINKPHERAL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/wasm32-toolchain.cmake")
endif()

function(perinkpheral_app app_dir)
    get_filename_component(app_dir "${app_dir}" ABSOLUTE)
    get_filename_component(name "${app_dir}" NAME)
    set(tools "${PERINKPHERAL_ROOT}/tools/perinkpheral")
    set(environment "${CMAKE_COMMAND}" -E env "WASI_SDK_PATH=$ENV{WASI_SDK_PATH}")

    # The flags are written once, in the tools: what CMake compiles is what the device gets.
    foreach(kind compile link)
        string(TOUPPER "${kind}" KIND)
        if(NOT DEFINED PERINKPHERAL_${KIND}_FLAGS)
            execute_process(COMMAND "${tools}" flags ${kind} OUTPUT_VARIABLE flags
                            OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE asked ERROR_VARIABLE said)
            if(NOT asked EQUAL 0)
                message(FATAL_ERROR "tools/perinkpheral flags ${kind}: ${said}")
            endif()
            set(PERINKPHERAL_${KIND}_FLAGS "${flags}")
        endif()
    endforeach()

    if(PERINKPHERAL_GENERATED)
        if(PERINKPHERAL_GENERATED_BY_PATH)
            file(REAL_PATH "${app_dir}" canonical_app_dir)
            string(SHA256 app_path_hash "${canonical_app_dir}")
            set(generated "${PERINKPHERAL_GENERATED}/${app_path_hash}/${name}")
        else()
            set(generated "${PERINKPHERAL_GENERATED}/${name}")
        endif()
    else()
        # Made now, so that the code reads from the first moment.
        set(generated "${CMAKE_BINARY_DIR}/generated/${name}")
        execute_process(COMMAND ${environment} "${tools}" headers "${app_dir}" "${generated}"
                        RESULT_VARIABLE made ERROR_VARIABLE said)
        if(NOT made EQUAL 0)
            message(WARNING "${name}: ${said}")
        endif()
    endif()

    file(GLOB sources CONFIGURE_DEPENDS "${app_dir}/*.c" "${app_dir}/*.h")
    file(GLOB library CONFIGURE_DEPENDS "${PERINKPHERAL_ROOT}/sdk/lib/*.c")
    file(GLOB made_sources CONFIGURE_DEPENDS "${generated}/*.c")
    add_executable(${name} ${sources} ${library} ${made_sources})
    # Every application's module is app.wasm: each in a folder of its own.
    set_target_properties(${name} PROPERTIES OUTPUT_NAME app SUFFIX ".wasm" C_STANDARD 11
                          RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/modules/${name}")
    target_include_directories(${name} PRIVATE "${PERINKPHERAL_ROOT}/sdk" "${PERINKPHERAL_ROOT}/sdk/lib"
                               "${generated}" "${app_dir}")
    target_compile_options(${name} PRIVATE ${PERINKPHERAL_COMPILE_FLAGS})
    target_link_options(${name} PRIVATE ${PERINKPHERAL_LINK_FLAGS})

    file(GLOB inputs CONFIGURE_DEPENDS "${app_dir}/manifest.json" "${app_dir}/strings/*"
                                       "${app_dir}/pictures/*")
    add_custom_target(${name}-package
                      COMMAND ${environment} "${tools}" build "${app_dir}"
                              --out "${CMAKE_BINARY_DIR}/apps/${name}"
                      SOURCES ${inputs}
                      USES_TERMINAL VERBATIM)

    # The device: a CMake variable, or the environment when the target runs.
    set(PERINKPHERAL_HOST "$ENV{PERINKPHERAL_HOST}" CACHE STRING
        "The device's address, for the run targets")
    set(device "")
    if(PERINKPHERAL_HOST)
        set(device --host "${PERINKPHERAL_HOST}")
    endif()
    add_custom_target(${name}-run
                      COMMAND ${environment} "${tools}" ${device} run "${app_dir}"
                      USES_TERMINAL VERBATIM)
    add_custom_target(${name}-serve
                      COMMAND ${environment} "${tools}" ${device} serve "${app_dir}"
                      USES_TERMINAL VERBATIM)
endfunction()
