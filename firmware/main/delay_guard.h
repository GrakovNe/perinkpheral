/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Number of busy waits of more than a second that were cut to a
 * millisecond since the start. See delay_guard.c. */
uint32_t delay_guard_shortened(void);

#ifdef __cplusplus
}
#endif
