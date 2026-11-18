// SPDX-License-Identifier: MIT
#include <string.h>
#include "tinytest.h"
#include "recovery_stubs/esp_ota_ops.h"
#include "recovery_stubs/esp_image_format.h"
#include "../../../recovery/main/ota.h"
#include "ota_handoff.h"

enum { IMAGE_BYTES = 1000 };
static uint8_t image[IMAGE_BYTES];
static const esp_partition_t factory = { .subtype = ESP_PARTITION_SUBTYPE_APP_FACTORY, .size = 4096, .address = 0x20000 };
static const esp_partition_t main_slot = { .subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0, .size = 4096, .address = 0xe0000 };
static const esp_partition_t *running = &factory;
static const esp_partition_t metadata = { .size = 8192, .erase_size = 4096 };
static ota_handoff_t handoff;
static bool missing_metadata, refuse_metadata_write;
static char trace[64];
static size_t trace_length;
static bool refuse_factory, refuse_begin, refuse_write, refuse_end, refuse_main;
static bool bad_revision, unreadable, invalid_image;

static void record(char event)
{
    CHECK(trace_length + 1 < sizeof trace);
    trace[trace_length++] = event;
    trace[trace_length] = '\0';
}

const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label)
{
    (void)label;
    if (type == ESP_PARTITION_TYPE_DATA) { return missing_metadata ? NULL : &metadata; }
    return subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ? &main_slot : &factory;
}

const esp_partition_t *esp_ota_get_running_partition(void) { return running; }

esp_err_t esp_partition_read(const esp_partition_t *partition, size_t offset, void *data, size_t length)
{
    if (partition == &metadata) {
        CHECK(offset == OTA_HANDOFF_OFFSET && length == sizeof handoff);
        if (unreadable) { return ESP_FAIL; }
        memcpy(data, &handoff, length);
        return ESP_OK;
    }
    CHECK(partition == &main_slot && offset == 0 && length == OTA_HEAD_BYTES);
    record('R');
    if (unreadable) { return ESP_FAIL; }
    memcpy(data, image, length);
    return ESP_OK;
}

esp_err_t esp_partition_write(const esp_partition_t *partition, size_t offset,
                              const void *data, size_t length)
{
    CHECK(partition == &metadata && offset == OTA_HANDOFF_OFFSET && length == sizeof handoff);
    if (refuse_metadata_write) { return ESP_FAIL; }
    memcpy(&handoff, data, length);
    return ESP_OK;
}

esp_err_t bootloader_common_check_chip_validity(const esp_image_header_t *header, int kind)
{
    (void)kind;
    CHECK(header->chip_id == 13);
    return bad_revision ? ESP_FAIL : ESP_OK;
}

esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition)
{
    record(partition == &factory ? 'F' : 'M');
    return (partition == &factory ? refuse_factory : refuse_main) ? ESP_FAIL : ESP_OK;
}

esp_err_t esp_image_verify(int mode, const esp_partition_pos_t *position, esp_image_metadata_t *metadata)
{
    (void)metadata;
    CHECK(mode == ESP_IMAGE_VERIFY_SILENT);
    CHECK(position->offset == main_slot.address && position->size == main_slot.size);
    record('V');
    return invalid_image ? ESP_FAIL : ESP_OK;
}

esp_err_t esp_ota_begin(const esp_partition_t *partition, size_t size, esp_ota_handle_t *handle)
{
    CHECK(partition == &main_slot && size == IMAGE_BYTES);
    CHECK(trace_length > 0 && trace[trace_length - 1] == 'F');
    record('B');
    *handle = 42;
    return refuse_begin ? ESP_FAIL : ESP_OK;
}

esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *bytes, size_t size)
{
    (void)bytes; (void)size;
    CHECK(handle == 42);
    record('W');
    return refuse_write ? ESP_FAIL : ESP_OK;
}

esp_err_t esp_ota_end(esp_ota_handle_t handle)
{
    CHECK(handle == 42);
    record('E');
    return refuse_end ? ESP_FAIL : ESP_OK;
}

esp_err_t esp_ota_abort(esp_ota_handle_t handle)
{
    CHECK(handle == 42);
    record('A');
    return ESP_OK;
}

static void put_word(size_t offset, uint32_t value)
{
    for (unsigned byte = 0; byte < 4; byte++) { image[offset + byte] = value >> (byte * 8); }
}

static void reset(void)
{
    memset(image, 0, sizeof image);
    image[0] = 0xe9; image[1] = 1; image[12] = 13;
    put_word(28, IMAGE_BYTES - 32);
    put_word(32, 0xabcd5432);
    memcpy(image + 80, "perinkpheral", sizeof "perinkpheral");
    put_word(288, OTA_LAYOUT_MAGIC);
    put_word(292, RECOVERY_LAYOUT);
    trace_length = 0; trace[0] = '\0';
    running = &factory;
    refuse_factory = refuse_begin = refuse_write = refuse_end = refuse_main = false;
    bad_revision = unreadable = invalid_image = false;
    missing_metadata = refuse_metadata_write = false;
    memset(&handoff, 0xff, sizeof handoff);
}

static void test_clear_journal_before_erasing(void)
{
    reset();
    ota_stream_t stream;
    CHECK(recovery_ota_start(&stream, IMAGE_BYTES));
    CHECK_STR(trace, "");
    CHECK(recovery_ota_check_header(image) == NULL);
    CHECK_STR(trace, "");
    CHECK(ota_stream_feed(&stream, image, OTA_HEAD_BYTES));
    CHECK_STR(trace, "FBW");
    CHECK(!recovery_ota_retry());
    CHECK_STR(trace, "FBW");
    CHECK(ota_stream_feed(&stream, image + OTA_HEAD_BYTES, IMAGE_BYTES - OTA_HEAD_BYTES));
    CHECK(ota_stream_end(&stream));
    CHECK_STR(trace, "FBWWEM");
    ota_stream_abort(&stream);
    CHECK_STR(trace, "FBWWEM");
}

static void test_failures_keep_recovery_selected(void)
{
    ota_stream_t stream;
    reset();
    refuse_factory = true;
    CHECK(recovery_ota_start(&stream, IMAGE_BYTES));
    CHECK(!ota_stream_feed(&stream, image, IMAGE_BYTES));
    CHECK_STR(trace, "F");

    reset();
    refuse_begin = true;
    CHECK(recovery_ota_start(&stream, IMAGE_BYTES));
    CHECK(!ota_stream_feed(&stream, image, IMAGE_BYTES));
    CHECK_STR(trace, "FB");

    reset();
    refuse_write = true;
    CHECK(recovery_ota_start(&stream, IMAGE_BYTES));
    CHECK(!ota_stream_feed(&stream, image, IMAGE_BYTES));
    CHECK_STR(trace, "FBWA");

    reset();
    CHECK(recovery_ota_start(&stream, IMAGE_BYTES));
    CHECK(ota_stream_feed(&stream, image, OTA_HEAD_BYTES));
    ota_stream_abort(&stream);
    CHECK_STR(trace, "FBWA");

    reset();
    refuse_end = true;
    CHECK(recovery_ota_start(&stream, IMAGE_BYTES));
    CHECK(ota_stream_feed(&stream, image, IMAGE_BYTES));
    CHECK(!ota_stream_end(&stream));
    CHECK_STR(trace, "FBWWE");

    reset();
    refuse_main = true;
    CHECK(recovery_ota_start(&stream, IMAGE_BYTES));
    CHECK(ota_stream_feed(&stream, image, IMAGE_BYTES));
    CHECK(!ota_stream_end(&stream));
    CHECK_STR(trace, "FBWWEM");
}

static void test_retry_validates_before_journal_changes(void)
{
    reset();
    CHECK(recovery_ota_retry());
    CHECK_STR(trace, "RVFM");
    const size_t corrupt[] = { 0, 12, 80, 288, 292 };
    for (size_t index = 0; index < sizeof corrupt / sizeof corrupt[0]; index++) {
        reset(); image[corrupt[index]] ^= 1;
        CHECK(!recovery_ota_retry());
        CHECK_STR(trace, "R");
    }
    reset(); bad_revision = true;
    CHECK(!recovery_ota_retry());
    CHECK_STR(trace, "R");
    reset(); unreadable = true;
    CHECK(!recovery_ota_retry());
    CHECK_STR(trace, "R");
    reset(); refuse_factory = true;
    CHECK(!recovery_ota_retry());
    CHECK_STR(trace, "RVF");
    reset(); invalid_image = true;
    CHECK(!recovery_ota_retry());
    CHECK_STR(trace, "RV");
    reset(); running = &main_slot;
    CHECK(!recovery_ota_retry());
    CHECK_STR(trace, "");
}

static void test_update_intent_is_consumed_once(void)
{
    reset();
    CHECK(!recovery_ota_take_handoff(true));
    handoff = (ota_handoff_t){ OTA_HANDOFF_MAGIC, ~OTA_HANDOFF_MAGIC };
    CHECK(recovery_ota_take_handoff(true));
    CHECK(handoff.magic == 0 && handoff.check == 0);
    CHECK(!recovery_ota_take_handoff(true));

    /* A power loss or crash between requesting recovery and its boot is not
     * an ongoing update, and must not leave a hint for the next restart. */
    handoff = (ota_handoff_t){ OTA_HANDOFF_MAGIC, ~OTA_HANDOFF_MAGIC };
    CHECK(!recovery_ota_take_handoff(false));
    CHECK(!recovery_ota_take_handoff(true));

    handoff = (ota_handoff_t){ OTA_HANDOFF_MAGIC, 0xffffffff };
    CHECK(!recovery_ota_take_handoff(true));
    handoff = (ota_handoff_t){ OTA_HANDOFF_MAGIC, ~OTA_HANDOFF_MAGIC };
    unreadable = true;
    CHECK(!recovery_ota_take_handoff(true));
    unreadable = false;
    refuse_metadata_write = true;
    CHECK(!recovery_ota_take_handoff(true));
    refuse_metadata_write = false;
    missing_metadata = true;
    CHECK(!recovery_ota_take_handoff(true));
    CHECK_STR(trace, "");
}

int main(void)
{
    test_update_intent_is_consumed_once();
    test_clear_journal_before_erasing();
    test_failures_keep_recovery_selected();
    test_retry_validates_before_journal_changes();
    REPORT();
}
