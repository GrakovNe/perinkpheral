/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include "esp_err.h"
#include "appconf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The only module that touches NVS. */
void settings_nvs_init(void);

/* Never fails: defaults when nothing is stored or what is stored does
 * not validate. */
void settings_load(appconf_t *c, const char device_id[5]);

esp_err_t settings_save(const appconf_t *c);

#ifdef __cplusplus
}
#endif
