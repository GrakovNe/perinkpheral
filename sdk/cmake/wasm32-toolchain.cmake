# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
#
# The compiler applications are built with: clang of wasi-sdk, for wasm32
# with no system under it. tools/mkapp does the building; this is for a
# CMake project to describe the same to an IDE.
if(NOT DEFINED ENV{WASI_SDK_PATH})
    message(FATAL_ERROR "WASI_SDK_PATH is not set. Applications are built with wasi-sdk: "
                        "unpack one and point WASI_SDK_PATH at it (in an IDE: the environment "
                        "of the CMake profile).")
endif()
file(TO_CMAKE_PATH "$ENV{WASI_SDK_PATH}" wasi_sdk)
if(NOT EXISTS "${wasi_sdk}/bin/clang")
    message(FATAL_ERROR "No bin/clang in WASI_SDK_PATH (${wasi_sdk}).")
endif()

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR wasm32)
set(CMAKE_C_COMPILER "${wasi_sdk}/bin/clang")
set(CMAKE_C_COMPILER_TARGET wasm32)
set(CMAKE_AR "${wasi_sdk}/bin/llvm-ar")
set(CMAKE_RANLIB "${wasi_sdk}/bin/llvm-ranlib")
# Nothing links without the host: a check of the compiler builds a library.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
