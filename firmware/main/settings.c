/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "words.h"
#include "settings.h"

#include <limits.h>
#include <stdlib.h>

#include "catalog.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "events.h"
#include "host.h"
#include "gfx.h"
#include "app_host.h"
#include "display.h"
#include "net_time.h"
#include "poller.h"
#include "power.h"
#include "status.h"
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
#define KEY_FONT           "font"
#define KEY_REFRESH_FAST   "refresh_fast"
#define KEY_TIME_AUTO      "time_auto"
#define KEY_POWER_SAVING   "power_saving"
#define KEY_DEFAULT_APP    "default_app"
#define KEY_MENU_IDLE      "menu_idle"
#define KEY_HEADER_CLOCK   "header_clock"
#define KEY_LANGUAGE       "language"

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

static void load(appconf_t *c, const char device_id[5])
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
    nvs_get_u8(h, KEY_FONT, &c->font);
    if (c->font >= APPCONF_FONT_COUNT) {
        /* Stored by a firmware that had more of them: not a reason to throw
         * every setting away, the network's among them. */
        c->font = 0;
    }
    nvs_get_u8(h, KEY_REFRESH_FAST, &c->refresh_fast);
    nvs_get_u8(h, KEY_TIME_AUTO, &c->time_auto);
    nvs_get_u8(h, KEY_POWER_SAVING, &c->power_saving);
    if (c->power_saving > APPCONF_POWER_AUTO) {
        /* Stored by a firmware that had more ways of saving: not a reason
         * to throw every setting away, the network's among them. */
        c->power_saving = APPCONF_POWER_SAVING;
    }
    get_str(h, KEY_DEFAULT_APP, c->default_app, sizeof c->default_app);
    nvs_get_u8(h, KEY_MENU_IDLE, &c->menu_idle_min);
    nvs_get_u8(h, KEY_HEADER_CLOCK, &c->header_clock);
    get_str(h, KEY_LANGUAGE, c->language, sizeof c->language);
    nvs_close(h);

    appconf_derive(c, device_id);
    char why[128];
    if (!appconf_validate(c, why, sizeof why)) {
        ESP_LOGW(TAG, "stored settings invalid (%s), using factory defaults", why);
        appconf_defaults(c, device_id);
    }
}

static esp_err_t save(const appconf_t *c)
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
    PUT(nvs_set_u8(h, KEY_FONT, c->font));
    PUT(nvs_set_u8(h, KEY_REFRESH_FAST, c->refresh_fast));
    PUT(nvs_set_u8(h, KEY_TIME_AUTO, c->time_auto));
    PUT(nvs_set_u8(h, KEY_POWER_SAVING, c->power_saving));
    PUT(nvs_set_str(h, KEY_DEFAULT_APP, c->default_app));
    PUT(nvs_set_u8(h, KEY_MENU_IDLE, c->menu_idle_min));
    PUT(nvs_set_u8(h, KEY_HEADER_CLOCK, c->header_clock));
    PUT(nvs_set_str(h, KEY_LANGUAGE, c->language));
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
    /* The official source first, whatever is stored; then what was added. */
    int used = snprintf(sources, sources_size, "%s\n", CATALOG_OFFICIAL_SOURCE);
    nvs_handle_t h;
    if (used < 0 || (size_t)used >= sources_size || nvs_open(CFG_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t length = sources_size - (size_t)used;
    /* An older firmware stored the official source with the rest. */
    static const char STORED_OFFICIAL[] = CATALOG_OFFICIAL_SOURCE "\n";
    char *added = sources + used;
    if (nvs_get_str(h, KEY_SOURCES, added, &length) != ESP_OK ||
        !catalog_sources_are_valid(sources)) {
        added[0] = '\0';
    } else if (strncmp(added, STORED_OFFICIAL, sizeof STORED_OFFICIAL - 1) == 0) {
        memmove(added, added + sizeof STORED_OFFICIAL - 1,
                strlen(added + sizeof STORED_OFFICIAL - 1) + 1);
    }
    nvs_close(h);
}

esp_err_t settings_save_sources(const char *sources)
{
    if (!catalog_sources_are_valid(sources) ||
        catalog_source_count(sources) > CATALOG_ADDED_SOURCES_MAX) {
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
        .full_is_fast = c->refresh_fast != 0,
    };
    return policy;
}

/* ---- the settings in effect ---- */

static appconf_t         s_live;
static volatile uint32_t s_version; /* odd while s_live is being rewritten */
static char              s_device_id[5];

static void publish(const appconf_t *settings)
{
    s_version++;
    s_live = *settings;
    appconf_derive(&s_live, s_device_id);
    s_version++;
}

void settings_start(const char device_id[5])
{
    memcpy(s_device_id, device_id, sizeof s_device_id);
    appconf_t stored;
    load(&stored, device_id);
    publish(&stored);
}

void settings_read(appconf_t *copy)
{
    uint32_t version;
    for (;;) {
        version = s_version;
        if ((version & 1) != 0) {
            /* Caught the owner in the middle of a change. It is the host
             * loop, which every reader outranks: spinning here would keep
             * it from ever finishing. */
            vTaskDelay(1);
            continue;
        }
        *copy = s_live;
        if (version == s_version) {
            return;
        }
    }
}

settings_outcome_t settings_change(const appconf_t *wanted, char *problem, size_t problem_size,
                                   bool *needs_restart)
{
    if (!appconf_validate(wanted, problem, problem_size)) {
        return SETTINGS_INVALID;
    }
    if (save(wanted) != ESP_OK) {
        return SETTINGS_NOT_STORED;
    }
    bool power_changed = wanted->power_saving != s_live.power_saving;
    bool language_changed = strcmp(wanted->language, s_live.language) != 0;
    bool zone_changed = strcmp(wanted->tz, s_live.tz) != 0;
    if (needs_restart != NULL) {
        *needs_restart = strcmp(wanted->device_name, s_live.device_name) != 0 ||
                         strcmp(wanted->ntp_server, s_live.ntp_server) != 0 ||
                         strcmp(wanted->wifi_ssid, s_live.wifi_ssid) != 0 ||
                         strcmp(wanted->wifi_password, s_live.wifi_password) != 0;
    }
    publish(wanted);

    net_time_set_zone(s_live.tz);
    net_time_set_automatic(s_live.time_auto != 0);
    app_host_set_dithering(s_live.dither != 0);
    if (gfx_font_family() != (gfx_family_t)s_live.font) {
        /* Every letter on the screen is another one now. */
        gfx_font_use((gfx_family_t)s_live.font);
        host_redraw();
    }
    /* The host's own words follow the language at once; an application
     * speaks it from the next time it is loaded. */
    words_use(s_live.language);
    host_redraw();
    if (language_changed) {
        host_reload_apps_later(); /* their titles are other words now */
    }
    refresh_policy_cfg_t policy = settings_refresh_policy(&s_live);
    display_set_policy(&policy);
    if (power_changed) {
        /* Only then: the polls are another distance apart, and scheduling
         * them anew is not something to do for a change of the time zone. */
        power_set_mode((power_mode_t)s_live.power_saving);
        poller_reschedule();
    }
    if (zone_changed) {
        app_host_tell_time_changed();
    }
    status_check(); /* the date in the header may be another now */
    return SETTINGS_CHANGED;
}

