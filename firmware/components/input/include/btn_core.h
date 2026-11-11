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

/* One active-low button fed with periodic samples: debounce, then what
 * happened to it. A press is told twice over: as it happens (down, up)
 * for whoever follows the finger, and as what it meant (short, long) for
 * whoever only wants the meaning. Pure, host-tested. */
typedef enum {
    BTN_NONE,
    BTN_DOWN,  /* pressed, past the debounce */
    BTN_UP,    /* released */
    BTN_SHORT, /* released before long_ms; comes right after the BTN_UP */
    BTN_LONG,  /* still held at long_ms; the release then is only a BTN_UP */
} btn_event_t;

/* The most that one sample can produce: a release is an up and a short. */
#define BTN_EVENTS_PER_SAMPLE 2

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

/* level: 1 idle, 0 pressed. now_ms is any monotonic clock. Writes what
 * this sample produced into `events`, in the order it happened, and
 * returns how many. */
int btn_core_feed(btn_core_t *b, int level, uint32_t now_ms, const btn_core_cfg_t *cfg,
                  btn_event_t events[BTN_EVENTS_PER_SAMPLE]);

#ifdef __cplusplus
}
#endif
