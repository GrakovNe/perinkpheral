/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include "esp_err.h"
#include "appconf.h"
#include "refresh_policy.h"

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

/* How the panel is to be treated, as the display service takes it. */
refresh_policy_cfg_t settings_refresh_policy(const appconf_t *c);

/* Where applications are installed from: the addresses of the catalogs,
 * a line each (see catalog.h). Loading gives the official one first and
 * then the ones the user added; saving takes only the added ones.
 * sources_size is at least CATALOG_SOURCES_TEXT_MAX. */
void      settings_load_sources(char *sources, size_t sources_size);
esp_err_t settings_save_sources(const char *added_sources);

#ifdef __cplusplus
}
#endif
