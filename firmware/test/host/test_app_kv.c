/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include <stdbool.h>
#include <stdlib.h>
#include "nvs.h"
#include "tinytest.h"
#include "../../main/settings.h"
#include "app_store.h"

typedef enum {
    FAIL_NONE, FAIL_SOURCE_OPEN, FAIL_DESTINATION_OPEN, FAIL_SOURCE_GET, FAIL_DESTINATION_GET,
    FAIL_SET, FAIL_COMMIT, FAIL_FIND, FAIL_NEXT, FAIL_ERASE, FAIL_ERASE_COMMIT, FAIL_GET_MISSING,
    FAIL_MARKER_READ_OPEN, FAIL_MARKER_OPEN, FAIL_MARKER_GET, FAIL_MARKER_SET, FAIL_MARKER_COMMIT,
} failure_t;
static failure_t failure;
static int durable[3][2], pending[3][2];
static int open_handles, iterators, erases, destination_commits;
static const char *keys[] = { "first", "second" };
struct nvs_opaque_iterator_t { int partition; int key; bool all; };

esp_err_t nvs_open_from_partition(const char *partition, const char *name, nvs_open_mode_t mode,
                                 nvs_handle_t *handle)
{
    CHECK(strcmp(name, "a.clock") == 0 || strcmp(name, "migration") == 0);
    int index = strcmp(partition, "nvs") == 0 ? 0 : strcmp(name, "migration") == 0 ? 2 : 1;
    if ((index == 0 && failure == FAIL_SOURCE_OPEN) ||
        (index == 1 && failure == FAIL_DESTINATION_OPEN) ||
        (index == 2 && failure == FAIL_MARKER_OPEN && mode == NVS_READWRITE) ||
        (index == 2 && failure == FAIL_MARKER_READ_OPEN && mode == NVS_READONLY)) {
        return ESP_FAIL;
    }
    *handle = (nvs_handle_t)index;
    memcpy(pending[index], durable[index], sizeof pending[index]);
    open_handles++;
    return ESP_OK;
}

esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle)
{
    return nvs_open_from_partition("nvs", name, mode, handle);
}

void nvs_close(nvs_handle_t handle)
{
    (void)handle;
    open_handles--;
}

static int key_index(const char *key)
{
    return strcmp(key, keys[0]) == 0 ? 0 : 1;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value, size_t *length)
{
    if (handle == 2) {
        CHECK_STR(key, "a.clock");
        if (failure == FAIL_MARKER_GET) return ESP_FAIL;
        if (pending[2][0] == 0) return ESP_ERR_NVS_NOT_FOUND;
        CHECK(*length >= 1);
        if (value != NULL) *(uint8_t *)value = (uint8_t)pending[2][0];
        *length = 1;
        return ESP_OK;
    }
    int index = key_index(key);
    if (handle == 0 && index == 1 && failure == FAIL_GET_MISSING) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if ((handle == 0 && index == 1 && failure == FAIL_SOURCE_GET) ||
        (handle == 1 && failure == FAIL_DESTINATION_GET)) {
        return ESP_FAIL;
    }
    if (pending[handle][index] == 0) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (value != NULL) {
        CHECK(*length >= sizeof(int));
        memcpy(value, &pending[handle][index], sizeof(int));
    }
    *length = sizeof(int);
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length)
{
    if (handle == 2) {
        CHECK_STR(key, "a.clock");
        CHECK(length == 1);
        if (failure == FAIL_MARKER_SET) return ESP_FAIL;
        pending[2][0] = *(const uint8_t *)value;
        return ESP_OK;
    }
    CHECK(length == sizeof(int));
    int index = key_index(key);
    if (index == 1 && failure == FAIL_SET) {
        return ESP_FAIL;
    }
    memcpy(&pending[handle][index], value, length);
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    if ((handle == 1 && failure == FAIL_COMMIT) ||
        (handle == 0 && failure == FAIL_ERASE_COMMIT) ||
        (handle == 2 && failure == FAIL_MARKER_COMMIT)) {
        return ESP_FAIL;
    }
    memcpy(durable[handle], pending[handle], sizeof durable[handle]);
    destination_commits += handle == 1;
    return ESP_OK;
}

esp_err_t nvs_erase_all(nvs_handle_t handle)
{
    CHECK(handle < 2);
    if (handle == 0) CHECK(destination_commits > 0 && durable[2][0] == 1);
    if (handle == 1 && (durable[0][0] != 0 || durable[0][1] != 0)) CHECK(durable[2][0] == 1);
    erases++;
    if (failure == FAIL_ERASE) {
        return ESP_FAIL;
    }
    memset(pending[handle], 0, sizeof pending[handle]);
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    pending[handle][key_index(key)] = 0;
    return ESP_OK;
}

esp_err_t nvs_entry_find(const char *partition, const char *name, nvs_type_t type, nvs_iterator_t *entry)
{
    (void)type;
    if (name != NULL && failure == FAIL_FIND) {
        return ESP_FAIL;
    }
    int index = strcmp(partition, "nvs") == 0 ? 0 : name != NULL && strcmp(name, "migration") == 0 ? 2 : 1;
    if (index == 1 && name == NULL && durable[1][0] == 0 && durable[1][1] == 0) index = 2;
    if (durable[index][0] == 0 && durable[index][1] == 0) {
        *entry = NULL;
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *entry = malloc(sizeof **entry);
    **entry = (struct nvs_opaque_iterator_t){ index, durable[index][0] == 0 ? 1 : 0, name == NULL };
    iterators++;
    return ESP_OK;
}

esp_err_t nvs_entry_next(nvs_iterator_t *entry)
{
    if (failure == FAIL_NEXT) {
        nvs_release_iterator(*entry);
        *entry = NULL;
        return ESP_FAIL;
    }
    if (++(*entry)->key == 2 || durable[(*entry)->partition][(*entry)->key] == 0) {
        if ((*entry)->all && (*entry)->partition == 1 && durable[2][0] != 0) {
            (*entry)->partition = 2;
            (*entry)->key = 0;
            return ESP_OK;
        }
        nvs_release_iterator(*entry);
        *entry = NULL;
        return ESP_ERR_NVS_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t nvs_entry_info(nvs_iterator_t entry, nvs_entry_info_t *info)
{
    strcpy(info->namespace_name, entry->partition == 2 ? "migration" : "a.clock");
    strcpy(info->key, entry->partition == 2 ? "a.clock" : keys[entry->key]);
    return ESP_OK;
}

void nvs_release_iterator(nvs_iterator_t entry)
{
    if (entry != NULL) {
        iterators--;
        free(entry);
    }
}

esp_err_t nvs_get_stats(const char *partition, nvs_stats_t *stats)
{
    (void)partition;
    stats->free_entries = 10;
    return ESP_OK;
}
esp_err_t nvs_flash_init_partition(const char *partition) { (void)partition; return ESP_OK; }
esp_err_t nvs_flash_erase_partition(const char *partition) { (void)partition; return ESP_OK; }
void settings_load_app_record(const char *name, settings_app_record_t *record)
{
    (void)name;
    memset(record, 0, sizeof *record);
}
bool settings_save_app_record(const char *name, const settings_app_record_t *record)
{
    (void)name; (void)record;
    return true;
}
void settings_forget_app_record(const char *name) { (void)name; }
char *app_store_read_manifest(const char *root, const char *name)
{
    (void)root; (void)name;
    return NULL;
}

#include "../../main/app_kv.c"

int main(void)
{
    for (failure = FAIL_SOURCE_OPEN; failure <= FAIL_MARKER_COMMIT; failure++) {
        memset(durable, 0, sizeof durable);
        durable[0][0] = 11;
        durable[0][1] = 22;
        erases = destination_commits = 0;
        migrate_from_settings_partition();
        CHECK(durable[0][0] == 11 && durable[0][1] == 22);
        CHECK(open_handles == 0 && iterators == 0);
        if (failure != FAIL_ERASE && failure != FAIL_ERASE_COMMIT) {
            CHECK(erases == 0);
        } else {
            CHECK(durable[1][0] == 11 && durable[1][1] == 22);
            /* A later successful application write must survive a migration retry. */
            durable[1][0] = 33;
        }
        failure_t saved = failure;
        failure = FAIL_NONE;
        migrate_from_settings_partition();
        CHECK(durable[0][0] == 0 && durable[0][1] == 0);
        CHECK(durable[1][0] == (saved == FAIL_ERASE || saved == FAIL_ERASE_COMMIT ? 33 : 11));
        CHECK(durable[1][1] == 22);
        CHECK(open_handles == 0 && iterators == 0);
        failure = saved;
    }
    /* Once copying is durable, interrupted cleanup must not resurrect deletions. */
    for (failure_t failed = FAIL_ERASE; failed <= FAIL_ERASE_COMMIT; failed++) {
        memset(durable, 0, sizeof durable);
        durable[0][0] = 11;
        durable[0][1] = 22;
        failure = failed;
        migrate_from_settings_partition();
        CHECK(durable[2][0] == 1 && durable[0][0] == 11);
        failure = FAIL_NONE;
        CHECK(app_kv_set("clock", "first", NULL, 0) == HOST_OK);
        CHECK(durable[1][0] == 0);
        forget_host_namespaces();
        CHECK(durable[2][0] == 1);
        migrate_from_settings_partition();
        CHECK(durable[1][0] == 0 && durable[1][1] == 22);
        CHECK(durable[0][0] == 0 && durable[0][1] == 0);
    }
    /* A failed marker write cannot expose successful deletes to a copy retry. */
    const failure_t marker_failures[] = { FAIL_MARKER_OPEN, FAIL_MARKER_SET, FAIL_MARKER_COMMIT };
    for (size_t index = 0; index < sizeof marker_failures / sizeof marker_failures[0]; index++) {
        failure_t failed = marker_failures[index];
        memset(durable, 0, sizeof durable);
        durable[0][0] = 11;
        durable[0][1] = 22;
        failure = failed;
        migrate_from_settings_partition();
        CHECK(durable[1][0] == 11 && durable[2][0] == 0 && durable[0][0] == 11);
        CHECK(app_kv_set("clock", "first", NULL, 0) == HOST_ERR_FAILED);
        CHECK(durable[1][0] == 11);
        failure = FAIL_NONE;
        migrate_from_settings_partition();
        CHECK(app_kv_set("clock", "first", NULL, 0) == HOST_OK);
        migrate_from_settings_partition();
        CHECK(durable[1][0] == 0);
    }
    /* Explicitly forgetting a store cancels even an incomplete migration. */
    memset(durable, 0, sizeof durable);
    durable[0][0] = 11;
    durable[0][1] = 22;
    failure = FAIL_SET;
    migrate_from_settings_partition();
    failure = FAIL_NONE;
    app_kv_erase("clock");
    migrate_from_settings_partition();
    CHECK(durable[1][0] == 0 && durable[1][1] == 0);
    CHECK(durable[0][0] == 0 && durable[0][1] == 0);
    /* Ordinary stores need no migration marker, including after legacy cleanup. */
    memset(durable, 0, sizeof durable);
    failure = FAIL_MARKER_READ_OPEN;
    int value = 44;
    CHECK(app_kv_set("clock", "first", &value, sizeof value) == HOST_OK);
    CHECK(durable[1][0] == 44);
    app_kv_erase("clock");
    CHECK(durable[1][0] == 0 && durable[2][0] == 0);
    CHECK(open_handles == 0 && iterators == 0);
    REPORT();
}
