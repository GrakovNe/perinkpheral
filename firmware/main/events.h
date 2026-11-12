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

/* The host's one queue. Whatever happens - a press, a timer, a request of
 * another task - is posted here from its own task and taken
 * off by the host loop one at a time, so nothing in the host runs
 * concurrently with anything else. */
typedef enum {
    EVENT_BUTTON,
    EVENT_DEADLINES,  /* some deadlines have come: see deadlines.h */
    EVENT_STATUS_CHANGED, /* what the header shows is different now */
    EVENT_DUMP_FRAME, /* diagnostics: print what the panel shows */
    EVENT_DEV_REQUEST, /* development tools ask the host loop for something */
    EVENT_APPS_CHANGED, /* one was installed or removed: the list is to be read again */
    EVENT_CALL,       /* another task asks the host loop to do something, and waits */
    EVENT_INTENT,     /* news for the applications that asked for the kind */
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
    void *dev_request; /* a devtools request, see devtools.h */
    void *call;        /* of events_call() */
    int   intent;      /* a HOST_INTENT_* */
} event_t;

void events_start(void);

/* From any task; never blocks. False: the queue is full, the event is lost. */
bool events_post(const event_t *event);

void events_post_button(uint8_t index, event_press_t press);

/* The host loop owns the host's state: whoever else wants it changed asks
 * here. `run` is called in the host loop with `argument`, and this returns
 * when it has returned - at once when asked from the host loop itself.
 * False, with nothing done, when the queue is full. */
typedef void (*events_call_fn)(void *argument);
bool events_call(events_call_fn run, void *argument);

/* The same for a caller that has nothing better to do than wait for room
 * in the queue: returns when the call was made. Not from the host loop's
 * own callers that must answer somebody soon. */
void events_call_patiently(events_call_fn run, void *argument);

/* For the host loop: marks the task that is one, and answers a call. */
void events_this_is_the_host_loop(void);
void events_answer_call(void *call);

/* Something happened that applications may have asked to be told of (a
 * HOST_INTENT_*). Safe from any task; false when the queue is full. */
bool events_post_intent(int intent);

/* An application was installed or removed. Safe from any task. */
bool events_post_apps_changed(void);

/* Blocks up to timeout_ms (UINT32_MAX: forever; 0: only looks). */
bool events_wait(event_t *event, uint32_t timeout_ms);

/* Whether anything is in the queue now. */
bool events_are_waiting(void);

/* Says in the log how many events did not fit since it last said so; for
 * the host loop. */
void events_report_dropped(void);

#ifdef __cplusplus
}
#endif
