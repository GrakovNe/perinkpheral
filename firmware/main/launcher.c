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

#define BUILT_IN_PAGES     2
#define INSTALLED_APPS_MAX (LAUNCHER_ENTRIES_MAX - BUILT_IN_PAGES)

/* The titles in here are what the menu points at. */
static app_manifest_t s_installed[INSTALLED_APPS_MAX];
static scene_t       *s_app_scenes[INSTALLED_APPS_MAX];
static int            s_installed_count;

static menu_entry_t s_entries[LAUNCHER_ENTRIES_MAX];
static int          s_entry_count;

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
                (menu_entry_t){ s_installed[i].title, s_app_scenes[i] };
        }
    }
    s_entries[s_entry_count++] = (menu_entry_t){ "Device", scene_device() };
    s_entries[s_entry_count++] = (menu_entry_t){ "About", scene_splash() };
}

const menu_entry_t *launcher_entries(int *count)
{
    *count = s_entry_count;
    return s_entries;
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
