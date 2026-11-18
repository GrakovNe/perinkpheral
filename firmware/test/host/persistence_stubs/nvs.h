/* SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>
#include "esp_err.h"
#define NVS_DEFAULT_PART_NAME "nvs"
#define NVS_NS_NAME_MAX_SIZE 16
#define NVS_KEY_NAME_MAX_SIZE 16
#define ESP_ERR_NVS_NOT_FOUND 1
#define ESP_ERR_NVS_INVALID_LENGTH 2
#define ESP_ERR_NVS_NOT_ENOUGH_SPACE 3
#define ESP_ERR_NVS_NO_FREE_PAGES 4
#define ESP_ERR_NVS_NEW_VERSION_FOUND 5

typedef unsigned nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
typedef enum { NVS_TYPE_BLOB, NVS_TYPE_ANY } nvs_type_t;
typedef struct nvs_opaque_iterator_t *nvs_iterator_t;
typedef struct {
    char namespace_name[NVS_NS_NAME_MAX_SIZE];
    char key[NVS_KEY_NAME_MAX_SIZE];
    nvs_type_t type;
} nvs_entry_info_t;
typedef struct { size_t free_entries; } nvs_stats_t;
esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle);
esp_err_t nvs_open_from_partition(const char *partition, const char *name, nvs_open_mode_t mode,
                                 nvs_handle_t *handle);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value, size_t *length);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length);
esp_err_t nvs_erase_all(nvs_handle_t handle);
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key);
esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t nvs_entry_find(const char *partition, const char *name, nvs_type_t type, nvs_iterator_t *entry);
esp_err_t nvs_entry_next(nvs_iterator_t *entry);
esp_err_t nvs_entry_info(nvs_iterator_t entry, nvs_entry_info_t *info);
void nvs_release_iterator(nvs_iterator_t entry);
esp_err_t nvs_get_stats(const char *partition, nvs_stats_t *stats);
