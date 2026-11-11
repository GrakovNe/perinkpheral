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

/* The host's one queue. Whatever happens - a press, later a timer or a
 * response from the network - is posted here from its own task and taken
 * off by the host loop one at a time, so nothing in the host runs
 * concurrently with anything else. */
typedef enum {
    EVENT_BUTTON,
    EVENT_TIMER,      /* a timer of the scene on the screen ran out */
    EVENT_STATUS_CHANGED, /* what the header shows is different now */
    EVENT_DUMP_FRAME, /* diagnostics: print what the panel shows */
} event_kind_t;

/* What a button did. A press arrives as it happens (down, up) and as
 * what it meant (short, long). */
typedef enum {
    EVENT_PRESS_DOWN,
    EVENT_PRESS_UP,
    EVENT_PRESS_SHORT,
    EVENT_PRESS_LONG,
} event_press_t;

typedef struct {
    event_kind_t kind;
    struct {
        uint8_t       index; /* 0..3, as the buttons sit on the device */
        event_press_t press;
    } button;
    struct {
        uint8_t id;
    } timer;
} event_t;

void events_start(void);

/* From any task; never blocks. False: the queue is full, the event is lost. */
bool events_post(const event_t *event);

void events_post_button(uint8_t index, event_press_t press);

/* Blocks up to timeout_ms (UINT32_MAX: forever; 0: only looks). */
bool events_wait(event_t *event, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
