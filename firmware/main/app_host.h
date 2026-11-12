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

/* An alarm was handed to its application: one that comes again is set for
 * its next time. Before the handler runs, so that it may take it back. */
void app_host_alarm_rang(uint32_t app_id, int alarm);
/* The applications that are still there; the recurrences of the others go. */
void app_host_keep_recurring_of(const uint32_t *app_ids, int count);
/* The device was powered up or restarted - not woken from its sleep. */
void app_host_tell_started(void);

/* Whether applications' greys are dithered: a setting, told here when it
 * changes. */
void app_host_set_dithering(bool dithers);

#ifdef __cplusplus
}
#endif
