/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_code.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_partition.h"

#include "app_manifest.h"
#include "app_room.h"
#include "app_store.h"

static const char *TAG = "app_code";

#define PARTITION_LABEL   "appcode"
#define PARTITION_SUBTYPE 0x40 /* the "appcode" type of partitions.csv: one of the application's own */
/* The first four bytes of a module's first sector: "PKAP" as they lie in
 * the flash. An erased sector begins with 0xFFFFFFFF, and a module whose
 * writing was cut short has no mark: the mark is written last. */
#define SLOT_MAGIC        0x50414b50u
#define COPY_CHUNK_BYTES  1024
#define SECTOR_BYTES      4096
/* A module takes a sector at least: the partition is 80 of them. */
#define EXTENTS_MAX       96

/* The first bytes of a module's piece. Written last, so that a piece that
 * was being written when the power went is not taken for a module. */
typedef struct {
    uint32_t magic;
    uint32_t size;
    char     name[APP_NAME_MAX + 1];
    uint8_t  unused[APP_CODE_HEADER_BYTES - 2 * sizeof(uint32_t) - (APP_NAME_MAX + 1)];
} slot_header_t;
_Static_assert(sizeof(slot_header_t) == APP_CODE_HEADER_BYTES, "the header is its bytes");

static const esp_partition_t *s_partition;
static const uint8_t         *s_mapped; /* the whole partition, for good */
static int                    s_sectors;

/* What lies in the partition now, by start: the host loop's, which alone
 * writes there. */
static app_room_extent_t s_extents[EXTENTS_MAX];
static int               s_extent_count;

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
    s_sectors = (int)(s_partition->size / SECTOR_BYTES);
    ESP_LOGI(TAG, "%d sectors", s_sectors);
}

static int sectors_for(size_t module_bytes)
{
    return (int)((APP_CODE_HEADER_BYTES + module_bytes + SECTOR_BYTES - 1) / SECTOR_BYTES);
}

static const slot_header_t *header_at(int sector)
{
    return (const slot_header_t *)(s_mapped + (size_t)sector * SECTOR_BYTES);
}

/* A module begins at this sector, and the partition holds all of it. */
static bool is_module_at(int sector)
{
    const slot_header_t *header = header_at(sector);
    return header->magic == SLOT_MAGIC && header->size >= 1 &&
           header->size <= MANIFEST_MODULE_MAX_BYTES &&
           sector + sectors_for(header->size) <= s_sectors &&
           memchr(header->name, '\0', sizeof header->name) != NULL &&
           app_manifest_is_valid_name(header->name);
}

/* Reads what lies in the partition: a module at every sector that begins
 * one, and after it the sectors it takes. */
static void scan(void)
{
    s_extent_count = 0;
    for (int sector = 0; sector < s_sectors && s_extent_count < EXTENTS_MAX;) {
        if (!is_module_at(sector)) {
            sector++;
            continue;
        }
        int sectors = sectors_for(header_at(sector)->size);
        s_extents[s_extent_count++] = (app_room_extent_t){ sector, sectors };
        sector += sectors;
    }
}

/* The extent of the application's module, or -1. */
static int extent_of(const char *app_name)
{
    for (int i = 0; i < s_extent_count; i++) {
        if (strcmp(header_at(s_extents[i].start)->name, app_name) == 0) {
            return i;
        }
    }
    return -1;
}

const uint8_t *app_code_find(const char *app_name, size_t *size)
{
    if (s_partition == NULL) {
        return NULL;
    }
    scan();
    int i = extent_of(app_name);
    if (i < 0) {
        return NULL;
    }
    const slot_header_t *header = header_at(s_extents[i].start);
    *size = header->size;
    return (const uint8_t *)header + sizeof(slot_header_t);
}

bool app_code_has_room(const char *app_name, size_t module_bytes)
{
    if (s_partition == NULL) {
        return true; /* it will run from its file */
    }
    /* Asked from another task than the one that writes: the partition is
     * walked here, and nothing of the host loop's is touched. */
    int free_sectors = s_sectors;
    for (int sector = 0; sector < s_sectors;) {
        if (!is_module_at(sector)) {
            sector++;
            continue;
        }
        int sectors = sectors_for(header_at(sector)->size);
        if (strcmp(header_at(sector)->name, app_name) != 0) {
            free_sectors -= sectors;
        }
        sector += sectors;
    }
    return sectors_for(module_bytes) <= free_sectors;
}

size_t app_code_free_bytes(void)
{
    if (s_partition == NULL) {
        return 0;
    }
    /* Walked here, as for the room: asked from another task. */
    int free_sectors = s_sectors;
    for (int sector = 0; sector < s_sectors;) {
        if (!is_module_at(sector)) {
            sector++;
            continue;
        }
        int sectors = sectors_for(header_at(sector)->size);
        free_sectors -= sectors;
        sector += sectors;
    }
    return (size_t)free_sectors * SECTOR_BYTES;
}

/* Its mark goes: the sectors are free, and erased when they are taken. */
static void forget(int extent)
{
    esp_partition_erase_range(s_partition, (size_t)s_extents[extent].start * SECTOR_BYTES,
                              SECTOR_BYTES);
}

void app_code_erase_all(void)
{
    if (s_partition == NULL) {
        return;
    }
    scan();
    for (int i = 0; i < s_extent_count; i++) {
        forget(i);
    }
}

/* Copies the file into the lowest free run that holds it, the module the
 * application had there forgotten first. False when there is no such
 * run, or the flash refused: the file stays, and is taken next time. */
static bool place(const char *app_name, const char *path)
{
    scan();
    int own = extent_of(app_name);
    if (own >= 0) {
        forget(own);
        scan();
    }
    struct stat found;
    if (stat(path, &found) != 0 || found.st_size < 1 ||
        found.st_size > MANIFEST_MODULE_MAX_BYTES) {
        return false;
    }
    int sectors = sectors_for((size_t)found.st_size);
    int sector = app_room_first_fit(s_extents, s_extent_count, s_sectors, sectors);
    FILE *file = sector >= 0 ? fopen(path, "rb") : NULL;
    if (file == NULL) {
        return false;
    }
    size_t   start = (size_t)sector * SECTOR_BYTES;
    bool     is_stored = esp_partition_erase_range(s_partition, start,
                                                   (size_t)sectors * SECTOR_BYTES) == ESP_OK;
    uint32_t size = 0;
    static uint8_t chunk[COPY_CHUNK_BYTES]; /* only the host loop stores */
    size_t   count;
    while (is_stored && (count = fread(chunk, 1, sizeof chunk, file)) > 0) {
        is_stored = size + count <= (uint32_t)found.st_size &&
                    esp_partition_write(s_partition, start + sizeof(slot_header_t) + size, chunk,
                                        count) == ESP_OK;
        size += (uint32_t)count;
    }
    is_stored = is_stored && !ferror(file) && size == (uint32_t)found.st_size;
    fclose(file);
    if (is_stored) {
        slot_header_t header = { .magic = SLOT_MAGIC, .size = size };
        strlcpy(header.name, app_name, sizeof header.name);
        memset(header.unused, 0xFF, sizeof header.unused);
        is_stored = esp_partition_write(s_partition, start, &header, sizeof header) == ESP_OK;
    }
    return is_stored;
}

/* Writes a module that lies in the partition back into its folder, as
 * the file it came as. */
static bool write_back(int extent, const char *path)
{
    const slot_header_t *header = header_at(s_extents[extent].start);
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    bool is_written = fwrite((const uint8_t *)header + sizeof(slot_header_t), 1, header->size,
                             file) == header->size;
    return fclose(file) == 0 && is_written;
}

/* Moves the modules down, one at a time, until they lie packed from the
 * start and the free sectors are one run at the end. A module on its way
 * is a file in its folder first: if the power goes, the next start takes
 * it in again as it takes any module that came as a file. */
static void pack(const char *apps_root)
{
    for (int moved = 0; moved < EXTENTS_MAX; moved++) {
        scan();
        int extent = app_room_to_move(s_extents, s_extent_count);
        if (extent < 0) {
            return;
        }
        char name[APP_NAME_MAX + 1];
        char path[APP_STORE_PATH_MAX];
        strlcpy(name, header_at(s_extents[extent].start)->name, sizeof name);
        if (snprintf(path, sizeof path, "%s/%s/" APP_FILE_MODULE, apps_root, name) >=
                (int)sizeof path ||
            !write_back(extent, path)) {
            remove(path);
            ESP_LOGW(TAG, "\"%s\" cannot be moved: no room for it as a file", name);
            return;
        }
        if (!place(name, path)) {
            ESP_LOGW(TAG, "\"%s\" was not moved back: it runs from its file", name);
            return;
        }
        remove(path);
    }
}

/* A module whose application has no folder any more is nobody's. */
static void forget_orphans(const char *apps_root)
{
    scan();
    for (int i = 0; i < s_extent_count; i++) {
        char        path[APP_STORE_PATH_MAX];
        struct stat folder;
        if (snprintf(path, sizeof path, "%s/%s", apps_root, header_at(s_extents[i].start)->name) <
                (int)sizeof path &&
            stat(path, &folder) != 0) {
            forget(i);
        }
    }
}

/* Into the partition, the others moved together when the room is there
 * but in pieces. */
static bool store(const char *app_name, const char *path, const char *apps_root)
{
    if (place(app_name, path)) {
        return true;
    }
    struct stat found;
    scan();
    if (stat(path, &found) != 0 ||
        app_room_free(s_extents, s_extent_count, s_sectors) < sectors_for((size_t)found.st_size)) {
        return false;
    }
    pack(apps_root);
    return place(app_name, path);
}

void app_code_adopt(const char *apps_root)
{
    if (s_partition == NULL) {
        return;
    }
    forget_orphans(apps_root);
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
        struct stat module;
        if (stat(path, &module) != 0) {
            continue; /* in the partition already, or not an application */
        }
        if (store(entry->d_name, path, apps_root)) {
            remove(path);
            ESP_LOGI(TAG, "\"%s\" is in the partition", entry->d_name);
        } else {
            ESP_LOGW(TAG, "\"%s\" found no room: it runs from its file", entry->d_name);
        }
    }
    closedir(directory);
}
