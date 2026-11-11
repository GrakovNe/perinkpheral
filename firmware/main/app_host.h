/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include "app.h"
#include "appconf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What this device gives an application beyond drawing: the clock, the
 * log, the timers, a store of its own. */
const app_services_t *app_host_services(void);

/* News for the applications that asked for the kind (see "intents" in
 * host_api.h): from any task. One that does not fit the queue is lost,
 * and said so in the log. */
void app_host_tell_time_changed(void);
void app_host_tell_network_is_up(void);

/* Whether applications' greys are dithered: a setting, told here when it
 * changes. */
void app_host_set_dithering(bool dithers);

#ifdef __cplusplus
}
#endif
