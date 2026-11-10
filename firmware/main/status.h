/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STATUS_DATE_MAX 15 /* "Wed 11 Nov" */

/* What the header shows, as data. */
typedef struct {
    bool wifi_connected;             /* the last connection attempt succeeded */
    bool date_known;
    char date[STATUS_DATE_MAX + 1];
} status_t;

/* There is no network and no clock yet: Wi-Fi is down and the date is
 * unknown until those services exist and report here. */
void status_read(status_t *status);

#ifdef __cplusplus
}
#endif
