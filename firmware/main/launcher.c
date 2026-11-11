/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "launcher.h"

#include <string.h>

#include "esp_log.h"

#include "app_host.h"
#include "app_scene.h"
#include "app_store.h"
#include "storage.h"

static const char *TAG = "launcher";

#define BUILT_IN_PAGES     2 /* Device, and the system applications: About */
#define INSTALLED_APPS_MAX (LAUNCHER_ENTRIES_MAX - BUILT_IN_PAGES)

/* The titles in here are what the menu points at. */
static app_manifest_t s_installed[INSTALLED_APPS_MAX];
static scene_t       *s_app_scenes[INSTALLED_APPS_MAX];
static int            s_installed_count;

static menu_entry_t s_entries[LAUNCHER_ENTRIES_MAX];
static int          s_entry_count;

/* The system applications: part of the firmware, built from
 * firmware/system. Their names begin with "sys.", which no installed
 * application's name can. */
#define SYSTEM_NAME_PREFIX "sys."

extern const uint8_t system_about_wasm_start[] asm("_binary_system_about_wasm_start");
extern const uint8_t system_about_wasm_end[] asm("_binary_system_about_wasm_end");
extern const char system_about_manifest_start[] asm("_binary_system_about_manifest_start");

static app_manifest_t s_about;

/* NULL when the manifest it was built with is not one this host reads:
 * a mistake of the build, said in the log. */
static scene_t *system_app_scene(app_manifest_t *manifest, const char *manifest_text,
                                 const uint8_t *wasm, size_t wasm_size)
{
    app_manifest_result_t result = app_manifest_parse(manifest_text, manifest);
    if (result != APP_MANIFEST_OK ||
        strlen(SYSTEM_NAME_PREFIX) + strlen(manifest->name) > APP_NAME_MAX) {
        ESP_LOGE(TAG, "a system application's manifest: %s", app_manifest_result_text(result));
        return NULL;
    }
    memmove(manifest->name + strlen(SYSTEM_NAME_PREFIX), manifest->name,
            strlen(manifest->name) + 1);
    memcpy(manifest->name, SYSTEM_NAME_PREFIX, strlen(SYSTEM_NAME_PREFIX));
    return app_scene_create_built_in(manifest, wasm, wasm_size, app_host_services());
}

void launcher_start(void)
{
    s_installed_count = app_store_list(STORAGE_APPS_ROOT, s_installed, INSTALLED_APPS_MAX);
    ESP_LOGI(TAG, "%d applications installed", s_installed_count);

    s_entry_count = 0;
    for (int i = 0; i < s_installed_count; i++) {
        s_app_scenes[i] =
            app_scene_create(&s_installed[i], STORAGE_APPS_ROOT, app_host_services());
        if (s_app_scenes[i] != NULL) {
            s_entries[s_entry_count++] =
                (menu_entry_t){ s_installed[i].title, s_installed[i].has_icon ? s_installed[i].icon : NULL,
                                s_app_scenes[i] };
        }
    }
    s_entries[s_entry_count++] = (menu_entry_t){ "Device", NULL, scene_device() };
    scene_t *about = system_app_scene(&s_about, system_about_manifest_start,
                                      system_about_wasm_start,
                                      (size_t)(system_about_wasm_end - system_about_wasm_start));
    if (about != NULL) {
        s_entries[s_entry_count++] =
            (menu_entry_t){ s_about.title, s_about.has_icon ? s_about.icon : NULL, about };
    }
}

const menu_entry_t *launcher_entries(int *count)
{
    *count = s_entry_count;
    return s_entries;
}

const app_manifest_t *launcher_installed(int *count)
{
    *count = s_installed_count;
    return s_installed;
}

scene_t *launcher_app_scene(const char *app_name)
{
    for (int i = 0; i < s_installed_count; i++) {
        if (strcmp(s_installed[i].name, app_name) == 0) {
            return s_app_scenes[i];
        }
    }
    return NULL;
}

const char *launcher_app_name(const scene_t *scene)
{
    for (int i = 0; i < s_installed_count; i++) {
        if (s_app_scenes[i] == scene) {
            return s_installed[i].name;
        }
    }
    return NULL;
}
