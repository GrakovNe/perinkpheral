/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STATUS_DATE_MAX 31 /* "Fri 13 Nov", in letters of two bytes too */

/* What the header shows, as data. */
typedef struct {
    bool wifi_connected; /* the device is on its network now */
    bool date_known;
    char date[STATUS_DATE_MAX + 1];
} status_t;

/* Watches for what the header shows to change - the network coming or
 * going, the date turning - and posts EVENT_STATUS_CHANGED when it does. */
void status_start(void);

void status_read(status_t *status);

/* Something the header shows may have changed: look, and tell the host
 * if it did. Safe from any task. */
void status_check(void);

#ifdef __cplusplus
}
#endif
