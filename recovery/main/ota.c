// SPDX-License-Identifier: MIT
#include "ota.h"
#include "esp_ota_ops.h"
#include "bootloader_common.h"
#include "esp_image_format.h"
#include <stddef.h>
#include <string.h>

_Static_assert(offsetof(esp_image_header_t, chip_id) == 12, "image chip field moved");
_Static_assert(sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) == 32,
               "image description moved");

const char *recovery_ota_check_header(const uint8_t header[OTA_HEAD_BYTES])
{
    const char *error = ota_image_check_header(header, "perinkpheral", RECOVERY_LAYOUT);
    if (error != NULL) { return error; }
    esp_image_header_t image_header;
    memcpy(&image_header, header, sizeof image_header);
    return bootloader_common_check_chip_validity(&image_header, ESP_IMAGE_APPLICATION) == ESP_OK
               ? NULL : "Image is incompatible with this chip revision.";
}


static esp_ota_handle_t handle;
static bool writing;
static const esp_partition_t *main_partition(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
}

static bool begin(void *context, size_t size)
{
    (void)context;
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *main = main_partition();
    /* Both journal copies must be gone BEFORE any byte of main is erased. */
    if (!running || running->subtype != ESP_PARTITION_SUBTYPE_APP_FACTORY || !main ||
        esp_ota_set_boot_partition(running) != ESP_OK) {
        return false;
    }
    writing = esp_ota_begin(main, size, &handle) == ESP_OK;
    return writing;
}

static bool write_piece(void *context, const uint8_t *bytes, size_t size)
{
    (void)context;
    return esp_ota_write(handle, bytes, size) == ESP_OK;
}

static bool finish(void *context)
{
    (void)context;
    esp_err_t result = esp_ota_end(handle);
    writing = false;
    return result == ESP_OK && esp_ota_set_boot_partition(main_partition()) == ESP_OK;
}

static void abort_write(void *context)
{
    (void)context;
    if (writing) {
        esp_ota_abort(handle);
        writing = false;
    }
}

bool recovery_ota_start(ota_stream_t *stream, size_t image_size)
{
    static const ota_writer_t writer = { begin, write_piece, finish, abort_write };
    const esp_partition_t *main = main_partition();
    return !writing && main && ota_stream_start(stream, &writer, NULL, "perinkpheral", RECOVERY_LAYOUT,
                                    image_size, main->size);
}

bool recovery_ota_retry(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *main = main_partition();
    if (writing || !running || running->subtype != ESP_PARTITION_SUBTYPE_APP_FACTORY || !main) {
        return false;
    }
    uint8_t header[OTA_HEAD_BYTES];
    if (esp_partition_read(main, 0, header, sizeof header) != ESP_OK ||
        recovery_ota_check_header(header) != NULL) {
        return false;
    }
    esp_partition_pos_t position = { .offset = main->address, .size = main->size };
    esp_image_metadata_t metadata;
    if (esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &position, &metadata) != ESP_OK) {
        return false;
    }
    /* A complete valid image earns probation again, without retaining the old
     * journal entry which would otherwise select this same single OTA slot. */
    return esp_ota_set_boot_partition(running) == ESP_OK &&
           esp_ota_set_boot_partition(main) == ESP_OK;
}
