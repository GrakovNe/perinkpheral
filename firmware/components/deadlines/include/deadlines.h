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

/* Deadlines of the host: set, cancel, notification of due ones.
 * Time base: the system clock (set by the user, by the network, or
 * counting from 1970); it runs through sleep. Thread-safe.
 *
 * Deadline kinds: */
typedef enum {
    DEADLINE_SCENE_TIMER,     /* number: timer id of the current scene */
    DEADLINE_ALARM,           /* owner: application id; number: alarm id */
    DEADLINE_AWAKE_WINDOW,    /* end of the period after a press */
    DEADLINE_POWER_LOOK,      /* check of the USB host for the power profile */
    DEADLINE_MEMORY_LOOK,     /* heap check, see memory.h */
    DEADLINE_WIFI_RETRY,
    DEADLINE_STOP,            /* stop of the device, see host_stop_soon() */
    DEADLINE_DATE,            /* change of the date in the header */
    DEADLINE_PANEL_SLEEP,     /* deep sleep of the panel */
    DEADLINE_PANEL_KEEPALIVE, /* keep-alive refresh */
    DEADLINE_POLL,            /* owner: application id; number: poll index */
    DEADLINE_POLLER_LOOK,     /* wake-up of the poller */
    DEADLINE_CONFIRM,         /* timeout of the question on the screen */
    DEADLINE_MENU_IDLE,       /* idle timeout of the menu: the default application opens */
} deadline_kind_t;

/* `some_are_due` is called from the timer task when a deadline is due
 * and not yet reported. It returns true when the notification was
 * accepted; on false the call is repeated. The receiver calls
 * deadlines_take_due() until it returns false. */
void deadlines_start(bool (*some_are_due)(void));

/* System clock, milliseconds since 1970. */
int64_t deadlines_now_ms(void);

/* Relative deadline, `in_ms` from now. Shifted when the clock is set. */
bool deadlines_set_in(deadline_kind_t kind, uint32_t owner, int number, int64_t in_ms);

/* Absolute deadline, milliseconds since 1970. Not shifted when the clock is set. */
bool deadlines_set_at(deadline_kind_t kind, uint32_t owner, int number, int64_t at_ms);

void deadlines_cancel(deadline_kind_t kind, uint32_t owner, int number);

/* Cancels the deadlines of `kind` whose owner is not in `owners`. */
void deadlines_keep_only(deadline_kind_t kind, const uint32_t *owners, int owner_count);

/* True when the deadline is set. */
bool deadlines_has(deadline_kind_t kind, uint32_t owner, int number);

/* Removes one due deadline and returns it in `id`. False: none is due. */
bool deadlines_take_due(deadline_id_t *id);

/* Requests the notification again. For a receiver that stopped before deadlines_take_due() returned false. */
void deadlines_reschedule(void);

/* Sets the system clock and shifts the relative deadlines by the same
 * amount. The only place where the clock is set. `moved_ms`: the shift.
 * False: the clock was not set. */
bool deadlines_set_clock(const struct timeval *to, int64_t *moved_ms);

#ifdef __cplusplus
}
#endif
