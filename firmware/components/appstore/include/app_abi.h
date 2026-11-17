/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* An explicit set, never an ordering test. Each generation retained here
 * needs its own contract and runtime support. ABI 2 was never frozen. */
static const uint32_t APP_SUPPORTED_ABIS[] = { 3 };

static inline bool app_abi_supported(uint32_t abi)
{
    for (unsigned i = 0; i < sizeof APP_SUPPORTED_ABIS / sizeof APP_SUPPORTED_ABIS[0]; i++) {
        if (APP_SUPPORTED_ABIS[i] == abi) {
            return true;
        }
    }
    return false;
}
