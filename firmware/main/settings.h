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

/* The application that was on the screen last, to return to after a
 * restart. name_size is at least 16; the name is empty when there was
 * none. Saving the name that is stored already writes nothing. */
void settings_load_last_app(char *name, size_t name_size);
void settings_save_last_app(const char *name);

#ifdef __cplusplus
}
#endif
