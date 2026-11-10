/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One active-low button fed with periodic samples: debounce, then a
 * short or a long press. Pure, host-tested. */
typedef enum {
    BTN_NONE,
    BTN_SHORT, /* released before long_ms */
    BTN_LONG,  /* still held at long_ms; the release then says nothing */
} btn_event_t;

typedef struct {
    uint8_t  debounce; /* consecutive low samples that make a press */
    uint32_t long_ms;
} btn_core_cfg_t;

typedef struct {
    bool     down;
    bool     long_sent;
    uint8_t  lows;
    uint32_t down_at_ms;
} btn_core_t;

void btn_core_init(btn_core_t *b);

/* level: 1 idle, 0 pressed. now_ms is any monotonic clock. */
btn_event_t btn_core_feed(btn_core_t *b, int level, uint32_t now_ms,
                          const btn_core_cfg_t *cfg);

#ifdef __cplusplus
}
#endif
