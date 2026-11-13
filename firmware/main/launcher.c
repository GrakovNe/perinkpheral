/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "launcher.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mbedtls/sha256.h"

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_code.h"
#include "app_host.h"
#include "app_kv.h"
#include "app_scene.h"
#include "app_store.h"
#include "settings.h"
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

/* What the host calls an application: a number made of its code and its
 * manifest, so that another version is another application to whatever
 * was waiting for the old one, and the same version installed again is
 * the same. Never 0, and never negative as an int. */
#define HASH_CHUNK_BYTES 512

static uint32_t id_of(const char *app_name, const uint8_t *code, size_t code_size)
{
    mbedtls_sha256_context hash;
    uint8_t                digest[32];
    mbedtls_sha256_init(&hash);
    mbedtls_sha256_starts(&hash, 0);
    if (code != NULL) {
        mbedtls_sha256_update(&hash, code, code_size);
    } else {
        /* It has no slot: the module is a file. */
        char path[APP_STORE_PATH_MAX];
        snprintf(path, sizeof path, "%s/%s/" APP_FILE_MODULE, STORAGE_APPS_ROOT, app_name);
        FILE *module = fopen(path, "rb");
        static uint8_t chunk[HASH_CHUNK_BYTES]; /* only the host loop lists applications */
        size_t count;
        while (module != NULL && (count = fread(chunk, 1, sizeof chunk, module)) > 0) {
            mbedtls_sha256_update(&hash, chunk, count);
        }
        if (module != NULL) {
            fclose(module);
        }
    }
    char *manifest = app_store_read_manifest(STORAGE_APPS_ROOT, app_name);
    if (manifest != NULL) {
        mbedtls_sha256_update(&hash, (const uint8_t *)manifest, strlen(manifest));
        free(manifest);
    }
    mbedtls_sha256_finish(&hash, digest);
    mbedtls_sha256_free(&hash);
    uint32_t id = ((uint32_t)digest[0] << 24 | (uint32_t)digest[1] << 16 |
                   (uint32_t)digest[2] << 8 | digest[3]) & 0x7FFFFFFFu;
    return id != 0 ? id : 1;
}

/* The title the application has for the device's language, when it has
 * one that fits: what the menu, the page and Settings then call it. */
static void take_translated_title(app_manifest_t *manifest)
{
    appconf_t settings;
    settings_read(&settings);
    if (strcmp(settings.language, "en") == 0) {
        return;
    }
    char  *text = app_store_read_translation(STORAGE_APPS_ROOT, manifest->name, settings.language);
    cJSON *translation = text != NULL ? cJSON_Parse(text) : NULL;
    free(text);
    const cJSON *title = cJSON_GetObjectItemCaseSensitive(translation, "title");
    if (cJSON_IsString(title) && title->valuestring[0] != '\0' &&
        strlen(title->valuestring) < sizeof manifest->title) {
        strcpy(manifest->title, title->valuestring);
    }
    cJSON_Delete(translation);
}

/* Reads the store and makes a scene of every installed application. */
static void read_installed(void)
{
    /* A module that came as a file goes into its slot first. */
    app_code_adopt(STORAGE_APPS_ROOT);
    s_installed_count = app_store_list(STORAGE_APPS_ROOT, s_installed, INSTALLED_APPS_MAX);
    ESP_LOGI(TAG, "%d applications installed", s_installed_count);
    for (int i = 0; i < s_installed_count; i++) {
        take_translated_title(&s_installed[i]);
    }
    for (int i = 0; i < s_installed_count; i++) {
        /* Run from its slot, in place; one that has none is read from its
         * file into memory, as all of them used to be. */
        size_t         size = 0;
        const uint8_t *code = app_code_find(s_installed[i].name, &size);
        s_installed[i].id = id_of(s_installed[i].name, code, size);
        s_app_scenes[i] = code != NULL
                              ? app_scene_create_built_in(&s_installed[i], code, size,
                                                          app_host_services())
                              : app_scene_create(&s_installed[i], STORAGE_APPS_ROOT,
                                                 app_host_services());
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

bool launcher_remove(const char *app_name, bool forgets)
{
    if (!app_store_remove(STORAGE_APPS_ROOT, app_name)) {
        return false;
    }
    /* Its code stays in its slot until the list is read anew: an instance
     * may still be running from it, with its last handlers to come. */
    if (forgets) {
        app_kv_erase(app_name);
    }
    return true;
}

int launcher_read_installed(app_manifest_t *copy, int capacity)
{
    for (;;) {
        uint32_t version = s_installed_version;
        if ((version & 1) != 0) {
            /* The host loop is reading the list anew - it reads flash, for
             * milliseconds - and every reader outranks it: to spin here
             * would keep it from ever finishing. */
            vTaskDelay(1);
            continue;
        }
        int count = s_installed_count < capacity ? s_installed_count : capacity;
        memcpy(copy, s_installed, (size_t)count * sizeof *copy);
        if (version == s_installed_version) {
            return count;
        }
    }
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
    /* An install the power stopped is finished, or cleared away, first. */
    app_store_recover(STORAGE_APPS_ROOT);
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
