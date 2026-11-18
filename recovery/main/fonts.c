// SPDX-License-Identifier: MIT
#include "fonts.h"

#include "esp_partition.h"
#include "gfx.h"

enum { GLYPHS_SUBTYPE = 0x41, SECTOR_BYTES = 4096 };

void recovery_fonts_start(void)
{
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, GLYPHS_SUBTYPE, "glyphs");
    if (partition == NULL) { return; }
    const void *mapped;
    esp_partition_mmap_handle_t handle;
    if (esp_partition_mmap(partition, 0, partition->size, ESP_PARTITION_MMAP_DATA,
                           &mapped, &handle) != ESP_OK) { return; }
    /* The mapping lives until reboot; recovery never writes this partition. */
    const uint8_t *bytes = mapped;
    for (size_t offset = 0; offset < partition->size;) {
        size_t length = gfx_pack_bytes(bytes + offset, partition->size - offset);
        if (length != 0) {
            gfx_pack_add(bytes + offset, length);
            offset += ((length + SECTOR_BYTES - 1) / SECTOR_BYTES) * SECTOR_BYTES;
        } else {
            offset += SECTOR_BYTES;
        }
    }
}
