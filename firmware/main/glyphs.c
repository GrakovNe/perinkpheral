/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "glyphs.h"

#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"

static const char *TAG = "glyphs";

#define PARTITION_LABEL   "glyphs"
#define PARTITION_SUBTYPE 0x41
#define SECTOR_BYTES      4096
#define MAGIC_BYTES       4 /* written last: a pack cut short by a loss of power is no pack */

static const esp_partition_t *s_partition;
static const uint8_t         *s_mapped;
static int                    s_sectors;

static int sectors_of(size_t bytes)
{
    return (int)((bytes + SECTOR_BYTES - 1) / SECTOR_BYTES);
}

/* The size of the pack that begins at the sector, or 0. */
static size_t pack_at(int sector)
{
    return gfx_pack_bytes(s_mapped + (size_t)sector * SECTOR_BYTES,
                          (size_t)(s_sectors - sector) * SECTOR_BYTES);
}

/* Every pack in the partition, to the fonts anew. */
static void give_to_the_fonts(void)
{
    gfx_packs_clear();
    for (int sector = 0; sector < s_sectors;) {
        size_t bytes = pack_at(sector);
        if (bytes == 0) {
            sector++;
            continue;
        }
        const uint8_t *pack = s_mapped + (size_t)sector * SECTOR_BYTES;
        if (!gfx_pack_add(pack, bytes)) {
            ESP_LOGW(TAG, "\"%s\" is not taken: too many", gfx_pack_name(pack));
        }
        sector += sectors_of(bytes);
    }
}

void glyphs_start(void)
{
    s_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, PARTITION_SUBTYPE,
                                           PARTITION_LABEL);
    if (s_partition == NULL) {
        ESP_LOGW(TAG, "no partition for glyph packs: letters beyond ASCII are spelled");
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
    give_to_the_fonts();
    glyphs_pack_t packs[GLYPHS_PACKS_MAX];
    ESP_LOGI(TAG, "%d packs, %u bytes free", glyphs_list(packs), (unsigned)glyphs_free_bytes());
}

int glyphs_list(glyphs_pack_t packs[GLYPHS_PACKS_MAX])
{
    int count = 0;
    for (int sector = 0; s_partition != NULL && sector < s_sectors && count < GLYPHS_PACKS_MAX;) {
        size_t bytes = pack_at(sector);
        if (bytes == 0) {
            sector++;
            continue;
        }
        strlcpy(packs[count].name, gfx_pack_name(s_mapped + (size_t)sector * SECTOR_BYTES),
                sizeof packs[count].name);
        packs[count].bytes = bytes;
        count++;
        sector += sectors_of(bytes);
    }
    return count;
}

size_t glyphs_free_bytes(void)
{
    int free_sectors = 0;
    for (int sector = 0; s_partition != NULL && sector < s_sectors;) {
        size_t bytes = pack_at(sector);
        if (bytes == 0) {
            free_sectors++;
            sector++;
        } else {
            sector += sectors_of(bytes);
        }
    }
    return (size_t)free_sectors * SECTOR_BYTES;
}

/* The sector the pack of that name begins at, or -1. */
static int sector_of(const char *name)
{
    for (int sector = 0; s_partition != NULL && sector < s_sectors;) {
        size_t bytes = pack_at(sector);
        if (bytes == 0) {
            sector++;
        } else if (strcmp(gfx_pack_name(s_mapped + (size_t)sector * SECTOR_BYTES), name) == 0) {
            return sector;
        } else {
            sector += sectors_of(bytes);
        }
    }
    return -1;
}

/* The first run of so many sectors with no pack in it, or -1. */
static int free_run(int wanted)
{
    int run = 0;
    for (int sector = 0; sector < s_sectors;) {
        size_t bytes = pack_at(sector);
        if (bytes != 0) {
            run = 0;
            sector += sectors_of(bytes);
            continue;
        }
        if (++run == wanted) {
            return sector - wanted + 1;
        }
        sector++;
    }
    return -1;
}

bool glyphs_remove(const char *name)
{
    int sector = sector_of(name);
    if (sector < 0) {
        return false;
    }
    /* The fonts let go of it before it is gone. */
    gfx_packs_clear();
    size_t bytes = pack_at(sector);
    bool   is_gone = esp_partition_erase_range(s_partition, (size_t)sector * SECTOR_BYTES,
                                               (size_t)sectors_of(bytes) * SECTOR_BYTES) == ESP_OK;
    give_to_the_fonts();
    ESP_LOGI(TAG, "\"%s\" %s", name, is_gone ? "removed" : "could not be removed");
    return is_gone;
}

bool glyphs_install(const uint8_t *pack, size_t size)
{
    if (s_partition == NULL || size > GLYPHS_PACK_MAX_BYTES || gfx_pack_bytes(pack, size) != size) {
        return false;
    }
    char name[GFX_PACK_NAME_MAX + 1];
    strlcpy(name, gfx_pack_name(pack), sizeof name);
    if (name[0] == '\0') {
        return false;
    }
    /* A newer one takes the place of one of the same name - after it is
     * written, when there is room for both; before, when there is not. */
    int old = sector_of(name);
    int sector = free_run(sectors_of(size));
    if (sector < 0 && old >= 0) {
        glyphs_remove(name);
        old = -1;
        sector = free_run(sectors_of(size));
    }
    if (sector < 0) {
        ESP_LOGW(TAG, "no room for \"%s\"", name);
        return false;
    }
    size_t old_bytes = old >= 0 ? pack_at(old) : 0;
    gfx_packs_clear();
    size_t start = (size_t)sector * SECTOR_BYTES;
    bool   is_stored =
        esp_partition_erase_range(s_partition, start, (size_t)sectors_of(size) * SECTOR_BYTES) ==
            ESP_OK &&
        esp_partition_write(s_partition, start + MAGIC_BYTES, pack + MAGIC_BYTES,
                            size - MAGIC_BYTES) == ESP_OK &&
        esp_partition_write(s_partition, start, pack, MAGIC_BYTES) == ESP_OK;
    if (is_stored && old >= 0) {
        esp_partition_erase_range(s_partition, (size_t)old * SECTOR_BYTES,
                                  (size_t)sectors_of(old_bytes) * SECTOR_BYTES);
    }
    give_to_the_fonts();
    ESP_LOGI(TAG, "\"%s\", %u bytes: %s", name, (unsigned)size,
             is_stored ? "installed" : "could not be written");
    return is_stored && sector_of(name) >= 0;
}
