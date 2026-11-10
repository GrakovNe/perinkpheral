/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include "app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What this device gives an application beyond drawing: the clock, the
 * log, the timers. */
const app_services_t *app_host_services(void);

#ifdef __cplusplus
}
#endif
