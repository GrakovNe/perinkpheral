/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_code.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_partition.h"

#include "app_manifest.h"
#include "app_room.h"
#include "app_store.h"

static const char *TAG = "app_code";

/* A module being written back into its folder, until it is whole. */
#define PART_SUFFIX ".part"

#define PARTITION_LABEL   "appcode"
#define PARTITION_SUBTYPE 0x40 /* the "appcode" type of partitions.csv: one of the application's own */
/* The first four bytes of a module's first sector: "PKAP" as they lie in
 * the flash. An erased sector begins with 0xFFFFFFFF, and a module whose
 * writing was cut short has no mark: the mark is written last, after the
 * size and the name, which go first. */
#define SLOT_MAGIC        0x50414b50u
#define COPY_CHUNK_BYTES  1024
#define SECTOR_BYTES      4096
/* A module takes a sector at least: the partition is 80 of them. */
#define EXTENTS_MAX       96

/* The first bytes of a module's piece. The size and the name are written
 * before the body, the mark after it: a piece that was being written when
 * the power went is not taken for a module, and what its body holds is not
 * read as the header of another. */
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

/* How many sectors the piece that begins at this sector takes, or 0. A
 * size and a name make a piece, with or without the mark: the sectors of
 * one whose writing was cut short are its own until they are erased, and
 * a header its body carries is never looked for. */
static int piece_at(int sector)
{
    const slot_header_t *header = header_at(sector);
    if (header->size < 1 || header->size > MANIFEST_MODULE_MAX_BYTES ||
        sector + sectors_for(header->size) > s_sectors ||
        memchr(header->name, '\0', sizeof header->name) == NULL ||
        !app_manifest_is_valid_name(header->name)) {
        return 0;
    }
    return sectors_for(header->size);
}

/* A whole module begins at this sector: a piece with its mark. */
static bool is_module_at(int sector)
{
    return piece_at(sector) > 0 && header_at(sector)->magic == SLOT_MAGIC;
}

/* Reads what lies in the partition: a piece at every sector that begins
 * one, and after it the sectors it takes. */
static void scan(void)
{
    s_extent_count = 0;
    for (int sector = 0; sector < s_sectors && s_extent_count < EXTENTS_MAX;) {
        int sectors = piece_at(sector);
        if (sectors == 0) {
            sector++;
            continue;
        }
        s_extents[s_extent_count++] = (app_room_extent_t){ sector, sectors };
        sector += sectors;
    }
}

/* The extent of the application's module, or -1. */
static int extent_of(const char *app_name)
{
    for (int i = 0; i < s_extent_count; i++) {
        if (is_module_at(s_extents[i].start) &&
            strcmp(header_at(s_extents[i].start)->name, app_name) == 0) {
            return i;
        }
    }
    return -1;
}

const uint8_t *app_code_find(const char *apps_root, const char *app_name, size_t *size)
{
    char path[APP_STORE_PATH_MAX];
    struct stat module;
    /* An unadopted file is the authoritative version, even if replacing
     * the old cached module failed before its first sector was erased. */
    if (s_partition == NULL || !app_store_is_ready(apps_root, app_name) ||
        snprintf(path, sizeof path, "%s/%s/" APP_FILE_MODULE, apps_root, app_name) >= (int)sizeof path ||
        stat(path, &module) == 0 || errno != ENOENT) {
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
        int sectors = piece_at(sector);
        if (sectors == 0) {
            sector++;
            continue;
        }
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
        int sectors = piece_at(sector);
        if (sectors == 0) {
            sector++;
            continue;
        }
        free_sectors -= sectors;
        sector += sectors;
    }
    return (size_t)free_sectors * SECTOR_BYTES;
}

/* A piece goes, all its sectors erased: what lay in its body must not be
 * read as the mark of another module once the sectors are free - a module
 * can carry bytes that look like one, and the scan looks at every sector.
 * The first sector goes last, so that its header covers the rest until
 * they are erased, whenever the power goes. */
static bool erase_run(int start, int sectors)
{
    return (sectors < 2 ||
            esp_partition_erase_range(s_partition, (size_t)(start + 1) * SECTOR_BYTES,
                                      (size_t)(sectors - 1) * SECTOR_BYTES) == ESP_OK) &&
           esp_partition_erase_range(s_partition, (size_t)start * SECTOR_BYTES, SECTOR_BYTES) ==
               ESP_OK;
}

static bool forget(int extent)
{
    return erase_run(s_extents[extent].start, s_extents[extent].sectors);
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
 * application had there forgotten first - once the file is known to be one
 * that can take its place. False when there is no such run, or the flash
 * refused: the file stays, and is taken next time. */
static bool place(const char *app_name, const char *path)
{
    struct stat found;
    if (stat(path, &found) != 0 || found.st_size < 1 ||
        found.st_size > MANIFEST_MODULE_MAX_BYTES) {
        return false;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    scan();
    int own = extent_of(app_name);
    if (own >= 0) {
        /* A mark that stays would be found before the new one. */
        if (!forget(own)) {
            fclose(file);
            return false;
        }
        scan();
    }
    int sectors = sectors_for((size_t)found.st_size);
    int sector = app_room_first_fit(s_extents, s_extent_count, s_sectors, sectors);
    if (sector < 0) {
        fclose(file);
        return false;
    }
    size_t   start = (size_t)sector * SECTOR_BYTES;
    bool     is_stored = esp_partition_erase_range(s_partition, start,
                                                   (size_t)sectors * SECTOR_BYTES) == ESP_OK;
    slot_header_t header = { .magic = SLOT_MAGIC, .size = (uint32_t)found.st_size };
    strlcpy(header.name, app_name, sizeof header.name);
    memset(header.unused, 0xFF, sizeof header.unused);
    /* All but the mark first: from here the run is one piece, whatever the
     * body brings. */
    bool is_begun = is_stored &&
                    esp_partition_write(s_partition, start + sizeof header.magic,
                                        (const uint8_t *)&header + sizeof header.magic,
                                        sizeof header - sizeof header.magic) == ESP_OK;
    is_stored = is_begun;
    uint32_t size = 0;
    static uint8_t chunk[COPY_CHUNK_BYTES]; /* only the host loop stores */
    size_t   count;
    while (is_stored && (count = fread(chunk, 1, sizeof chunk, file)) > 0) {
        is_stored = size + count <= header.size &&
                    esp_partition_write(s_partition, start + sizeof(slot_header_t) + size, chunk,
                                        count) == ESP_OK;
        size += (uint32_t)count;
    }
    is_stored = is_stored && !ferror(file) && size == header.size;
    fclose(file);
    if (is_stored) {
        is_stored = esp_partition_write(s_partition, start, &header.magic, sizeof header.magic) ==
                    ESP_OK;
    }
    if (is_begun && !is_stored) {
        erase_run(sector, sectors); /* refused, it goes at the next adoption */
    }
    return is_stored;
}

/* Writes a module that lies in the partition back into its folder, as
 * the file it came as: whole or not at all. It is written beside, and
 * takes the module's name only when it is complete, so a loss of power on
 * the way leaves a part a start throws away, never a short module that
 * would take the place of the one in the partition. */
static bool write_back(int extent, const char *path)
{
    const slot_header_t *header = header_at(s_extents[extent].start);
    char part[APP_STORE_PATH_MAX];
    if (snprintf(part, sizeof part, "%s" PART_SUFFIX, path) >= (int)sizeof part) {
        return false;
    }
    FILE *file = fopen(part, "wb");
    if (file == NULL) {
        return false;
    }
    bool is_written = fwrite((const uint8_t *)header + sizeof(slot_header_t), 1, header->size,
                             file) == header->size;
    is_written = fclose(file) == 0 && is_written && rename(part, path) == 0;
    if (!is_written) {
        remove(part);
    }
    return is_written;
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
        if (!is_module_at(s_extents[extent].start) || !app_store_is_ready(apps_root, name)) {
            return;
        }
        struct stat existing;
        if (snprintf(path, sizeof path, "%s/%s/" APP_FILE_MODULE, apps_root, name) >=
                (int)sizeof path ||
            stat(path, &existing) == 0 || errno != ENOENT || !write_back(extent, path)) {
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
            stat(path, &folder) != 0 && errno == ENOENT) {
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

/* A piece whose writing was cut short is nobody's: whatever its body
 * holds goes before anything is placed. */
static void forget_unfinished(void)
{
    scan();
    for (int i = 0; i < s_extent_count; i++) {
        if (!is_module_at(s_extents[i].start)) {
            forget(i);
        }
    }
}

void app_code_adopt(const char *apps_root)
{
    if (s_partition == NULL) {
        return;
    }
    forget_unfinished();
    DIR *directory = opendir(apps_root);
    if (directory == NULL) {
        return;
    }
    bool was_read = false;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (entry == NULL) {
            was_read = errno == 0;
            break;
        }
        char path[APP_STORE_PATH_MAX];
        if (!app_store_is_ready(apps_root, entry->d_name) ||
            snprintf(path, sizeof path, "%s/%s/" APP_FILE_MODULE, apps_root, entry->d_name) >=
                (int)sizeof path) {
            continue;
        }
        char part[APP_STORE_PATH_MAX];
        if (snprintf(part, sizeof part, "%s" PART_SUFFIX, path) < (int)sizeof part) {
            remove(part); /* written back when the power went: the partition has it */
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
    if (closedir(directory) == 0 && was_read) {
        /* Only an available directory can establish that an app was removed. */
        struct stat root;
        if (stat(apps_root, &root) == 0 && S_ISDIR(root.st_mode)) {
            forget_orphans(apps_root);
        }
    }
}
