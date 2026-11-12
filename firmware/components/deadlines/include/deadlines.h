/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

#include "deadline_table.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Every deadline of the host is kept here, and this module does nothing
 * else: set one, take it back, be told that some have come. The scale is
 * the clock - the user's, the network's or 1970, it always has a time and
 * runs through sleep. Safe from any task.
 *
 * Whoever has deadlines has a kind here. */
typedef enum {
    DEADLINE_SCENE_TIMER,     /* number: the timer of the scene on the screen */
    DEADLINE_ALARM,           /* owner: the application's id; number: its alarm */
    DEADLINE_AWAKE_WINDOW,    /* the while after a press is over */
    DEADLINE_WIFI_RETRY,
    DEADLINE_STOP,            /* the device stops: see host_stop_soon() */
    DEADLINE_DATE,            /* the day in the header is another */
    DEADLINE_PANEL_SLEEP,     /* the panel into its deep sleep */
    DEADLINE_PANEL_KEEPALIVE, /* the refresh that is due when nothing else was */
    DEADLINE_POLL,            /* owner: the application's id; number: its poll */
    DEADLINE_POLLER_LOOK,     /* the poller has something to look at again */
    DEADLINE_CONFIRM,         /* nobody answered the question on the screen */
} deadline_kind_t;

/* `some_are_due` is called from the timer's task whenever a deadline has
 * come and nobody was told yet; it says whether the news was taken - if
 * not, it is told again in a moment. Whoever is told calls
 * deadlines_take_due() until there is none. */
void deadlines_start(bool (*some_are_due)(void));

/* The clock, in milliseconds since 1970. */
int64_t deadlines_now_ms(void);

/* "In so long": moves with the clock when the clock is set. */
bool deadlines_set_in(deadline_kind_t kind, uint32_t owner, int number, int64_t in_ms);

/* "At such a time", milliseconds since 1970: stays where it is. */
bool deadlines_set_at(deadline_kind_t kind, uint32_t owner, int number, int64_t at_ms);

void deadlines_cancel(deadline_kind_t kind, uint32_t owner, int number);

/* The owners that are not among these are no more: their deadlines of
 * the kind go. */
void deadlines_keep_only(deadline_kind_t kind, const uint32_t *owners, int owner_count);

/* Whether the deadline is set. */
bool deadlines_has(deadline_kind_t kind, uint32_t owner, int number);

/* One that has come, taken out; false when none has. */
bool deadlines_take_due(deadline_id_t *id);

/* Whoever takes them stopped before there was none: tell again. */
void deadlines_look_again(void);

/* The one place the clock is set: the deadlines "in so long" move with
 * it in the same breath. False when the clock would not be set. Returns
 * through `moved_ms` how far it went. */
bool deadlines_set_clock(const struct timeval *to, int64_t *moved_ms);

#ifdef __cplusplus
}
#endif
