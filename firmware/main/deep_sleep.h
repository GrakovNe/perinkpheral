/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The chip's deep sleep: everything but its clock and a little memory is
 * off, and waking is a cold start that knows why it woke. */

typedef enum {
    WOKE_NOT_FROM_SLEEP, /* powered up, restarted, reset */
    WOKE_BY_CLOCK,       /* the moment it was told to wake at came */
    WOKE_BY_BUTTON,      /* somebody pressed one of the three that can wake it */
} wake_reason_t;

wake_reason_t deep_sleep_woke_by(void);

/* Sleeps until `wake_at_ms` of the clock (milliseconds since 1970;
 * INT64_MAX: for no moment) or a press of button 1, 2 or 3 - button 4 is
 * not on a line that can wake the chip. Does not return: the device
 * starts anew. Whoever calls this has put everything away first. */
void deep_sleep_until(int64_t wake_at_ms) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
