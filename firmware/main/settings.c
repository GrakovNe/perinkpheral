/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "settings.h"

#include <limits.h>
#include <stdlib.h>

#include "catalog.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "settings";

#define CFG_NS "cfg"

#define KEY_NAME    "name"
#define KEY_WSSID   "wssid"
#define KEY_WPASS   "wpass"
#define KEY_NTP     "ntp"
#define KEY_TZ      "tz"
#define KEY_LAST_APP "last_app"
#define KEY_SOURCES  "sources"
#define KEY_REFRESH_DATA   "rf_data"
#define KEY_REFRESH_BUTTON "rf_button"
#define KEY_REFRESH_FULL   "rf_full"
#define KEY_REFRESH_KEEP   "rf_keep"
#define KEY_DITHER         "dither"

void settings_nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "nvs: %s, erasing once", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

/* nvs_get_str() fills the buffer before it checks the data CRC, so a
 * corrupt entry read straight into *out would wipe the default. */
static void get_str(nvs_handle_t h, const char *key, char *out, size_t out_sz)
{
    char *tmp = malloc(out_sz);
    if (tmp == NULL) {
        ESP_LOGW(TAG, "key %s: no memory, using default", key);
        return;
    }
    size_t sz = out_sz;
    esp_err_t err = nvs_get_str(h, key, tmp, &sz);
    if (err == ESP_OK && sz > 0) {
        memcpy(out, tmp, sz);
        out[sz - 1] = '\0';
    } else if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "key %s: %s, using default", key, esp_err_to_name(err));
    }
    free(tmp);
}

void settings_load(appconf_t *c, const char device_id[5])
{
    appconf_defaults(c, device_id);

    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no saved settings, using factory defaults");
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s, using factory defaults", esp_err_to_name(err));
        return;
    }

    get_str(h, KEY_NAME, c->device_name, sizeof c->device_name);
    get_str(h, KEY_WSSID, c->wifi_ssid, sizeof c->wifi_ssid);
    get_str(h, KEY_WPASS, c->wifi_password, sizeof c->wifi_password);
    get_str(h, KEY_NTP, c->ntp_server, sizeof c->ntp_server);
    get_str(h, KEY_TZ, c->tz, sizeof c->tz);
    /* A key that is not there leaves the default in place. */
    nvs_get_u16(h, KEY_REFRESH_DATA, &c->refresh_min_data_s);
    nvs_get_u16(h, KEY_REFRESH_BUTTON, &c->refresh_min_button_ms);
    nvs_get_u8(h, KEY_REFRESH_FULL, &c->refresh_full_every);
    nvs_get_u8(h, KEY_REFRESH_KEEP, &c->refresh_keepalive_h);
    nvs_get_u8(h, KEY_DITHER, &c->dither);
    nvs_close(h);

    appconf_derive(c, device_id);
    char why[128];
    if (!appconf_validate(c, why, sizeof why)) {
        ESP_LOGW(TAG, "stored settings invalid (%s), using factory defaults", why);
        appconf_defaults(c, device_id);
    }
}

esp_err_t settings_save(const appconf_t *c)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err));
        return err;
    }

#define PUT(call)              \
    do {                       \
        if (err == ESP_OK) {   \
            err = (call);      \
        }                      \
    } while (0)

    PUT(nvs_set_str(h, KEY_NAME, c->device_name));
    PUT(nvs_set_str(h, KEY_WSSID, c->wifi_ssid));
    PUT(nvs_set_str(h, KEY_WPASS, c->wifi_password));
    PUT(nvs_set_str(h, KEY_NTP, c->ntp_server));
    PUT(nvs_set_str(h, KEY_TZ, c->tz));
    PUT(nvs_set_u16(h, KEY_REFRESH_DATA, c->refresh_min_data_s));
    PUT(nvs_set_u16(h, KEY_REFRESH_BUTTON, c->refresh_min_button_ms));
    PUT(nvs_set_u8(h, KEY_REFRESH_FULL, c->refresh_full_every));
    PUT(nvs_set_u8(h, KEY_REFRESH_KEEP, c->refresh_keepalive_h));
    PUT(nvs_set_u8(h, KEY_DITHER, c->dither));
    PUT(nvs_commit(h));
#undef PUT

    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
    }
    return err;
}

void settings_load_last_app(char *name, size_t name_size)
{
    name[0] = '\0';
    nvs_handle_t h;
    if (nvs_open(CFG_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    get_str(h, KEY_LAST_APP, name, name_size);
    nvs_close(h);
}

void settings_save_last_app(const char *name)
{
    char stored[32];
    settings_load_last_app(stored, sizeof stored);
    if (strcmp(stored, name) == 0) {
        return; /* nothing to wear the flash for */
    }
    nvs_handle_t h;
    if (nvs_open(CFG_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    if (nvs_set_str(h, KEY_LAST_APP, name) == ESP_OK) {
        nvs_commit(h);
    }
    nvs_close(h);
}

void settings_load_sources(char *sources, size_t sources_size)
{
    snprintf(sources, sources_size, "%s", CATALOG_DEFAULT_SOURCES);
    nvs_handle_t h;
    if (nvs_open(CFG_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    /* Left at the default when nothing is stored or it does not fit. */
    size_t length = sources_size;
    char  *stored = malloc(sources_size);
    if (stored != NULL && nvs_get_str(h, KEY_SOURCES, stored, &length) == ESP_OK &&
        catalog_sources_are_valid(stored)) {
        snprintf(sources, sources_size, "%s", stored);
    }
    free(stored);
    nvs_close(h);
}

esp_err_t settings_save_sources(const char *sources)
{
    if (!catalog_sources_are_valid(sources)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, KEY_SOURCES, sources);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

refresh_policy_cfg_t settings_refresh_policy(const appconf_t *c)
{
    refresh_policy_cfg_t policy = {
        .min_data_ms = c->refresh_min_data_s * 1000u,
        .min_button_ms = c->refresh_min_button_ms,
        /* "Never" is a count no row of partial refreshes reaches. */
        .full_every = c->refresh_full_every == APPCONF_REFRESH_FULL_NEVER ? INT_MAX
                                                                         : c->refresh_full_every,
        .keepalive_ms = c->refresh_keepalive_h * 3600u * 1000u,
    };
    return policy;
}
