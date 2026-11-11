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

/* The live settings: the services read from them what the user chose. */
void app_host_use(const appconf_t *config);

#ifdef __cplusplus
}
#endif
