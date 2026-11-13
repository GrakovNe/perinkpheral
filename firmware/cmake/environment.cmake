# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
#
# The firmware configures in whatever environment it is given. From a shell
# where ESP-IDF's export.sh was run there is nothing to do. From anywhere
# else - an IDE that runs CMake itself - this finds ESP-IDF and wasi-sdk and
# sets what export.sh would have: the tools on PATH and ESP-IDF's Python.
#
# Where they are, the first that is there:
#   ESP-IDF   the IDF_PATH environment variable, -DIDF_PATH=, ~/esp/esp-idf
#   wasi-sdk  the WASI_SDK_PATH environment variable, -DWASI_SDK_PATH=,
#             /opt/wasi-sdk, ~/opt/wasi-sdk

function(first_that_is_there out what marker)
    foreach(candidate ${ARGN})
        if(candidate AND EXISTS "${candidate}/${marker}")
            file(TO_CMAKE_PATH "${candidate}" candidate)
            set(${out} "${candidate}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR "${what} was not found. Set ${out} - in the environment, or as "
                        "-D${out}=<where it is> among the CMake options.")
endfunction()

if(CMAKE_HOST_WIN32)
    set(home "$ENV{USERPROFILE}")
else()
    set(home "$ENV{HOME}")
endif()

first_that_is_there(IDF_PATH "ESP-IDF" "tools/cmake/project.cmake"
                    "$ENV{IDF_PATH}" "${IDF_PATH}" "${home}/esp/esp-idf")
first_that_is_there(WASI_SDK_PATH "wasi-sdk" "bin/clang"
                    "$ENV{WASI_SDK_PATH}" "${WASI_SDK_PATH}" "/opt/wasi-sdk" "${home}/opt/wasi-sdk")
set(IDF_PATH "${IDF_PATH}" CACHE PATH "ESP-IDF")
set(WASI_SDK_PATH "${WASI_SDK_PATH}" CACHE PATH "wasi-sdk")
set(ENV{IDF_PATH} "${IDF_PATH}")
set(ENV{WASI_SDK_PATH} "${WASI_SDK_PATH}")

# export.sh was run: the tools are on PATH already.
if(DEFINED ENV{IDF_PYTHON_ENV_PATH} AND DEFINED ENV{ESP_IDF_VERSION})
    return()
endif()

# ESP-IDF's Python environment: install.sh made it, named after both versions.
set(tools_path "$ENV{IDF_TOOLS_PATH}")
if(NOT tools_path)
    set(tools_path "${home}/.espressif")
endif()
include("${IDF_PATH}/tools/cmake/version.cmake")
file(GLOB python_envs "${tools_path}/python_env/idf${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}_py*_env")
list(SORT python_envs COMPARE NATURAL ORDER DESCENDING)
set(idf_python "")
foreach(python_env ${python_envs})
    foreach(binary "bin/python" "Scripts/python.exe")
        if(NOT idf_python AND EXISTS "${python_env}/${binary}")
            set(idf_python "${python_env}/${binary}")
            set(idf_python_env "${python_env}")
        endif()
    endforeach()
endforeach()
if(NOT idf_python)
    message(FATAL_ERROR "ESP-IDF's tools are not installed (no Python environment in "
                        "${tools_path}/python_env): run install.sh of ${IDF_PATH}.")
endif()

execute_process(COMMAND "${idf_python}" "${IDF_PATH}/tools/idf_tools.py" --idf-path "${IDF_PATH}"
                        export --format key-value
                OUTPUT_VARIABLE exported ERROR_VARIABLE said RESULT_VARIABLE failed)
if(failed)
    message(FATAL_ERROR "ESP-IDF's tools could not be found: ${said}")
endif()
if(CMAKE_HOST_WIN32)
    set(path_separator ";")
else()
    set(path_separator ":")
endif()
get_filename_component(idf_python_bin "${idf_python}" DIRECTORY)
string(REPLACE "\n" ";" exported_lines "${exported}")
foreach(line ${exported_lines})
    if(line MATCHES "^([A-Za-z_][A-Za-z0-9_]*)=(.*)$")
        set(name "${CMAKE_MATCH_1}")
        set(value "${CMAKE_MATCH_2}")
        if(name STREQUAL "PATH")
            # Where the compiler is: the bootloader is a project of its own,
            # configured while this one is built, and looks for it again.
            string(REPLACE "${path_separator}" ";" tool_dirs "${value}")
            foreach(tool_dir ${tool_dirs})
                if(EXISTS "${tool_dir}/riscv32-esp-elf-gcc" OR EXISTS "${tool_dir}/riscv32-esp-elf-gcc.exe")
                    set(IDF_COMPILER_DIR "${tool_dir}")
                endif()
            endforeach()
            string(REPLACE "$PATH" "$ENV{PATH}" value "${value}")
            string(REPLACE "%PATH%" "$ENV{PATH}" value "${value}")
            set(value "${idf_python_bin}${path_separator}${value}")
        endif()
        if(NOT name STREQUAL "IDF_DEACTIVATE_FILE_PATH")
            set(ENV{${name}} "${value}")
        endif()
    endif()
endforeach()
set(ENV{IDF_PYTHON_ENV_PATH} "${idf_python_env}")
# The build's steps call Python by its name unless told which: they run
# later, in an environment this file does not reach.
if(NOT PYTHON)
    set(PYTHON "${idf_python}" CACHE FILEPATH "ESP-IDF's Python")
endif()
message(STATUS "ESP-IDF ${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR} at ${IDF_PATH}, its tools from ${tools_path}")
