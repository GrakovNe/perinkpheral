/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>

/* What the header's host part is made of; header_compose() puts it into
 * words. */
typedef struct {
    bool      wifi_connected; /* the last use of the network worked */
    bool      date_known;
    bool      shows_clock;    /* the time where the date stands */
    struct tm local;          /* the user's local time, when it is known */
} status_t;

/* Watches for what the header shows to change - the network coming or
 * going, the date turning - and posts EVENT_STATUS_CHANGED when it does. */
void status_start(void);

void status_read(status_t *status);

/* Something the header shows may have changed: look, and tell the host
 * if it did. Safe from any task. */
void status_check(void);

/* The same after somebody set the clock or saved a setting: a change is shown at once, not
 * when the panel's pace for data allows - they wait for it. */
void status_check_set(void);

/* Before the clock is set by hand: the next check is shown at once, whoever makes it. Taken
 * back with false when the clock was not set after all. */
void status_expect_set(bool expected);

#ifdef __cplusplus
}
#endif
