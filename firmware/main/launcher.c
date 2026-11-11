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

#define BUILT_IN_PAGES     3 /* the system applications: Settings, Device and About */
#define INSTALLED_APPS_MAX LAUNCHER_INSTALLED_MAX
_Static_assert(LAUNCHER_INSTALLED_MAX == LAUNCHER_ENTRIES_MAX - BUILT_IN_PAGES,
               "the menu has a row for every application and every system one");

/* The titles in here are what the menu points at. */
static app_manifest_t s_installed[INSTALLED_APPS_MAX];
static scene_t       *s_app_scenes[INSTALLED_APPS_MAX];
static int            s_installed_count;
static volatile uint32_t s_installed_version; /* odd while the list is being read anew */

static menu_entry_t s_entries[LAUNCHER_ENTRIES_MAX];
static int          s_entry_count;

/* The system applications: part of the firmware, built from
 * firmware/system. Their names begin with "sys.", which no installed
 * application's name can. */
#define SYSTEM_NAME_PREFIX "sys."

#define SYSTEM_APP(name)                                                                \
    extern const uint8_t system_##name##_wasm_start[] asm("_binary_system_" #name          \
                                                           "_wasm_start");                  \
    extern const uint8_t system_##name##_wasm_end[] asm("_binary_system_" #name "_wasm_end"); \
    extern const char system_##name##_manifest_start[] asm("_binary_system_" #name          \
                                                             "_manifest_start")
SYSTEM_APP(setup);
SYSTEM_APP(settings);
SYSTEM_APP(device);
SYSTEM_APP(about);

/* In the order the menu lists them, after the installed ones. */
enum { SYSTEM_SETTINGS, SYSTEM_DEVICE, SYSTEM_ABOUT, SYSTEM_IN_MENU, SYSTEM_SETUP = SYSTEM_IN_MENU,
       SYSTEM_APP_COUNT };

static app_manifest_t s_system[SYSTEM_APP_COUNT];
static scene_t       *s_system_scenes[SYSTEM_APP_COUNT];

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

/* Reads the store and makes a scene of every installed application. */
static void read_installed(void)
{
    s_installed_count = app_store_list(STORAGE_APPS_ROOT, s_installed, INSTALLED_APPS_MAX);
    ESP_LOGI(TAG, "%d applications installed", s_installed_count);
    for (int i = 0; i < s_installed_count; i++) {
        s_app_scenes[i] =
            app_scene_create(&s_installed[i], STORAGE_APPS_ROOT, app_host_services());
    }
}

/* What the menu lists: the installed applications, then the system ones. */
static void list_entries(void)
{
    s_entry_count = 0;
    for (int i = 0; i < s_installed_count; i++) {
        if (s_app_scenes[i] != NULL) {
            s_entries[s_entry_count++] =
                (menu_entry_t){ s_installed[i].title, s_installed[i].has_icon ? s_installed[i].icon : NULL,
                                s_app_scenes[i] };
        }
    }
    for (int i = 0; i < SYSTEM_IN_MENU; i++) {
        if (s_system_scenes[i] != NULL) {
            s_entries[s_entry_count++] = (menu_entry_t){
                s_system[i].title, s_system[i].has_icon ? s_system[i].icon : NULL,
                s_system_scenes[i] };
        }
    }
}

int launcher_read_installed(app_manifest_t *copy, int capacity)
{
    uint32_t version;
    int      count;
    do {
        version = s_installed_version;
        count = s_installed_count < capacity ? s_installed_count : capacity;
        memcpy(copy, s_installed, (size_t)count * sizeof *copy);
    } while ((version & 1) != 0 || version != s_installed_version);
    return count;
}

void launcher_reload(launcher_drop_fn drop, void *context)
{
    for (int i = 0; i < s_installed_count; i++) {
        if (s_app_scenes[i] != NULL) {
            drop(s_app_scenes[i], context);
            app_scene_destroy(s_app_scenes[i]);
            s_app_scenes[i] = NULL;
        }
    }
    s_installed_version++;
    read_installed();
    s_installed_version++;
    list_entries();
}

void launcher_start(void)
{
    read_installed();
    s_system_scenes[SYSTEM_SETTINGS] =
        system_app_scene(&s_system[SYSTEM_SETTINGS], system_settings_manifest_start,
                         system_settings_wasm_start,
                         (size_t)(system_settings_wasm_end - system_settings_wasm_start));
    s_system_scenes[SYSTEM_DEVICE] =
        system_app_scene(&s_system[SYSTEM_DEVICE], system_device_manifest_start,
                         system_device_wasm_start,
                         (size_t)(system_device_wasm_end - system_device_wasm_start));
    s_system_scenes[SYSTEM_ABOUT] =
        system_app_scene(&s_system[SYSTEM_ABOUT], system_about_manifest_start,
                         system_about_wasm_start,
                         (size_t)(system_about_wasm_end - system_about_wasm_start));
    /* Shown when the device has no network, and not something to open. */
    s_system_scenes[SYSTEM_SETUP] =
        system_app_scene(&s_system[SYSTEM_SETUP], system_setup_manifest_start,
                         system_setup_wasm_start,
                         (size_t)(system_setup_wasm_end - system_setup_wasm_start));
    list_entries();
}

scene_t *launcher_setup(void)
{
    return s_system_scenes[SYSTEM_SETUP];
}

scene_t *launcher_scene(const char *name)
{
    for (int i = 0; i < SYSTEM_APP_COUNT; i++) {
        if (s_system_scenes[i] != NULL && strcmp(s_system[i].name, name) == 0) {
            return s_system_scenes[i];
        }
    }
    return launcher_app_scene(name);
}

scene_t *launcher_about(void)
{
    return s_system_scenes[SYSTEM_ABOUT];
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
