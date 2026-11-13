/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdint.h>

#include "appconf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The system API as the host carries it out: what sdk/host_api.h promises
 * under sys_* to an application that asked for it. The functions are the
 * ones app_services_t names. */

int32_t app_system_info(int what, char *text, int32_t capacity);
int32_t app_system_number_get(int what, int32_t *value, int32_t *least, int32_t *most);
int32_t app_system_number_set(int what, int32_t value);
int32_t app_system_text_set(int what, const char *text);
int32_t app_system_time_set(int64_t unix_seconds);
int32_t app_system_app_list(uint32_t *ids, int32_t capacity);
int32_t app_system_app_title(uint32_t id, char *text, int32_t capacity);
int32_t app_system_app_remove(uint32_t id);
uint32_t app_system_app_default(void);
int32_t app_system_app_set_default(uint32_t id);
int32_t app_system_do(int action);

#ifdef __cplusplus
}
#endif
