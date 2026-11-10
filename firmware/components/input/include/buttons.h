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

/* Called from the buttons task for every press: index 0..3, BTN_SHORT or
 * BTN_LONG. It must not block. */
typedef void (*buttons_sink_fn)(uint8_t index, btn_event_t event);

void buttons_start(buttons_sink_fn sink);

#ifdef __cplusplus
}
#endif
