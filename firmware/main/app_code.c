/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_code.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"

#include "app_manifest.h"
#include "app_store.h"

static const char *TAG = "app_code";

#define PARTITION_LABEL   "appcode"
#define PARTITION_SUBTYPE 0x40
#define SLOT_MAGIC        0x50414b50u /* "PKAP" */
#define COPY_CHUNK_BYTES  1024

/* The first bytes of a slot. Written last, so that a slot that was being
 * written when the power went is not taken for a module. */
typedef struct {
    uint32_t magic;
    uint32_t size;
    char     name[APP_NAME_MAX + 1];
    uint8_t  unused[32 - 8 - (APP_NAME_MAX + 1)];
} slot_header_t;
_Static_assert(sizeof(slot_header_t) == APP_CODE_SLOT_BYTES - APP_CODE_MODULE_BYTES,
               "the header is what a slot has over a module");

static const esp_partition_t *s_partition;
static const uint8_t         *s_mapped; /* the whole partition, for good */
static int                    s_slot_count;

void app_code_start(void)
{
    s_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, PARTITION_SUBTYPE,
                                           PARTITION_LABEL);
    if (s_partition == NULL) {
        ESP_LOGW(TAG, "no partition for the code of applications: they run from their files");
        return;
    }
    esp_partition_mmap_handle_t handle;
    const void *mapped;
    if (esp_partition_mmap(s_partition, 0, s_partition->size, ESP_PARTITION_MMAP_DATA, &mapped,
                           &handle) != ESP_OK) {
        ESP_LOGE(TAG, "the partition cannot be mapped");
        s_partition = NULL;
        return;
    }
    s_mapped = mapped;
    s_slot_count = (int)(s_partition->size / APP_CODE_SLOT_BYTES);
    ESP_LOGI(TAG, "%d slots", s_slot_count);
}

static const slot_header_t *header_of(int slot)
{
    return (const slot_header_t *)(s_mapped + (size_t)slot * APP_CODE_SLOT_BYTES);
}

static bool is_taken(int slot)
{
    const slot_header_t *header = header_of(slot);
    return header->magic == SLOT_MAGIC && header->size >= 1 &&
           header->size <= APP_CODE_MODULE_BYTES &&
           memchr(header->name, '\0', sizeof header->name) != NULL &&
           app_manifest_is_valid_name(header->name);
}

/* The slot the application has, or -1. */
static int slot_of(const char *app_name)
{
    for (int slot = 0; slot < s_slot_count; slot++) {
        if (is_taken(slot) && strcmp(header_of(slot)->name, app_name) == 0) {
            return slot;
        }
    }
    return -1;
}

const uint8_t *app_code_find(const char *app_name, size_t *size)
{
    int slot = slot_of(app_name);
    if (slot < 0) {
        return NULL;
    }
    *size = header_of(slot)->size;
    return (const uint8_t *)header_of(slot) + sizeof(slot_header_t);
}

void app_code_forget(const char *app_name)
{
    int slot = slot_of(app_name);
    if (slot >= 0) {
        /* The first sector has the header: without it the slot is nobody's. */
        esp_partition_erase_range(s_partition, (size_t)slot * APP_CODE_SLOT_BYTES,
                                  s_partition->erase_size);
    }
}

/* Copies the file into the application's slot, or into a free one. */
static bool store(const char *app_name, const char *path)
{
    int slot = slot_of(app_name);
    for (int free_slot = 0; slot < 0 && free_slot < s_slot_count; free_slot++) {
        if (!is_taken(free_slot)) {
            slot = free_slot;
        }
    }
    FILE *file = slot >= 0 ? fopen(path, "rb") : NULL;
    if (file == NULL) {
        return false;
    }
    size_t   start = (size_t)slot * APP_CODE_SLOT_BYTES;
    bool     is_stored = esp_partition_erase_range(s_partition, start, APP_CODE_SLOT_BYTES) == ESP_OK;
    uint32_t size = 0;
    static uint8_t chunk[COPY_CHUNK_BYTES]; /* only the host loop stores */
    size_t   count;
    while (is_stored && (count = fread(chunk, 1, sizeof chunk, file)) > 0) {
        is_stored = size + count <= APP_CODE_MODULE_BYTES &&
                    esp_partition_write(s_partition, start + sizeof(slot_header_t) + size, chunk,
                                        count) == ESP_OK;
        size += (uint32_t)count;
    }
    is_stored = is_stored && !ferror(file) && size >= 1;
    fclose(file);
    if (is_stored) {
        slot_header_t header = { .magic = SLOT_MAGIC, .size = size };
        strlcpy(header.name, app_name, sizeof header.name);
        memset(header.unused, 0xFF, sizeof header.unused);
        is_stored = esp_partition_write(s_partition, start, &header, sizeof header) == ESP_OK;
    }
    return is_stored;
}

void app_code_adopt(const char *apps_root)
{
    if (s_partition == NULL) {
        return;
    }
    DIR *directory = opendir(apps_root);
    if (directory == NULL) {
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        char path[APP_STORE_PATH_MAX];
        if (!app_manifest_is_valid_name(entry->d_name) ||
            snprintf(path, sizeof path, "%s/%s/" APP_FILE_MODULE, apps_root, entry->d_name) >=
                (int)sizeof path) {
            continue;
        }
        FILE *module = fopen(path, "rb");
        if (module == NULL) {
            continue; /* in its slot already, or not an application */
        }
        fclose(module);
        if (store(entry->d_name, path)) {
            remove(path);
            ESP_LOGI(TAG, "\"%s\" is in a slot", entry->d_name);
        } else {
            ESP_LOGW(TAG, "\"%s\" found no slot: it runs from its file", entry->d_name);
        }
    }
    closedir(directory);
}
