/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "btn_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUTTONS_COUNT 4

typedef struct {
    uint8_t     index; /* 0..3 */
    btn_event_t event; /* BTN_SHORT or BTN_LONG */
} buttons_event_t;

void buttons_start(void);

/* Blocks up to timeout_ms (UINT32_MAX: forever) for the next event. */
bool buttons_wait(buttons_event_t *ev, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
