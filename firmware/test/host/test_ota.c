/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include <string.h>

#include "ota_health.h"
#include "ota_stream.h"
#include "tinytest.h"

/* ---- the stream, writing into memory instead of flash ---- */

#define IMAGE_BYTES 1000
#define SLOT_BYTES  4096

static uint8_t image[IMAGE_BYTES];

static struct {
    uint8_t written[SLOT_BYTES];
    size_t  length;
    size_t  announced;
    int     begun, finished, aborted;
    bool    refuse_begin, refuse_write, refuse_finish;
} flash;

static bool flash_begin(void *context, size_t image_size)
{
    (void)context;
    flash.begun++;
    flash.announced = image_size;
    return !flash.refuse_begin;
}

static bool flash_write(void *context, const uint8_t *data, size_t length)
{
    (void)context;
    if (flash.refuse_write) {
        return false;
    }
    memcpy(flash.written + flash.length, data, length);
    flash.length += length;
    return true;
}

static bool flash_finish(void *context)
{
    (void)context;
    flash.finished++;
    return !flash.refuse_finish;
}

static void flash_abort(void *context)
{
    (void)context;
    flash.aborted++;
}

static const ota_writer_t WRITER = { flash_begin, flash_write, flash_finish, flash_abort };

/* An image as ESP-IDF lays it out, as far as the stream looks. */
static void make_image(const char *project_name)
{
    for (int i = 0; i < IMAGE_BYTES; i++) {
        image[i] = (uint8_t)(i * 7);
    }
    image[0] = 0xE9;
    const uint8_t description_magic[] = { 0x32, 0x54, 0xCD, 0xAB };
    memcpy(image + 32, description_magic, sizeof description_magic);
    memset(image + 32 + 48, 0, 32);
    strcpy((char *)image + 32 + 48, project_name);
}

static ota_stream_t stream;

static bool start(size_t image_size)
{
    memset(&flash, 0, sizeof flash);
    return ota_stream_start(&stream, &WRITER, NULL, "perinkpheral", image_size, SLOT_BYTES);
}

/* Feeds the whole image in pieces of `piece` bytes. */
static bool feed_all(size_t piece)
{
    for (size_t offset = 0; offset < IMAGE_BYTES; offset += piece) {
        size_t length = IMAGE_BYTES - offset < piece ? IMAGE_BYTES - offset : piece;
        if (!ota_stream_feed(&stream, image + offset, length)) {
            return false;
        }
    }
    return true;
}

static void test_good_image_in_any_pieces(void)
{
    make_image("perinkpheral");
    /* pieces smaller than the head, exactly the head, larger, all at once */
    const size_t pieces[] = { 1, 7, 100, OTA_HEAD_BYTES, OTA_HEAD_BYTES + 1, 512, IMAGE_BYTES };
    for (size_t i = 0; i < sizeof pieces / sizeof pieces[0]; i++) {
        CHECK(start(IMAGE_BYTES));
        CHECK(feed_all(pieces[i]));
        CHECK(ota_stream_end(&stream));
        CHECK(flash.begun == 1 && flash.finished == 1 && flash.aborted == 0);
        CHECK(flash.announced == IMAGE_BYTES && flash.length == IMAGE_BYTES);
        CHECK(memcmp(flash.written, image, IMAGE_BYTES) == 0);
    }
}

static void test_foreign_images_never_reach_the_flash(void)
{
    make_image("efflorescence");
    CHECK(start(IMAGE_BYTES));
    CHECK(!feed_all(64));
    CHECK(flash.begun == 0 && flash.length == 0);
    CHECK_STR(ota_stream_error(&stream), "Invalid image.");

    /* a longer name that only starts like ours */
    make_image("perinkpheral-fork");
    CHECK(start(IMAGE_BYTES));
    CHECK(!feed_all(64));
    CHECK(flash.begun == 0);

    /* not an image at all */
    make_image("perinkpheral");
    image[0] = 'P';
    CHECK(start(IMAGE_BYTES));
    CHECK(!feed_all(IMAGE_BYTES));
    CHECK(flash.begun == 0);

    /* an image without an application description */
    make_image("perinkpheral");
    image[32] = 0;
    CHECK(start(IMAGE_BYTES));
    CHECK(!feed_all(300));
    CHECK(flash.begun == 0);
}

static void test_sizes(void)
{
    make_image("perinkpheral");
    CHECK(!start(OTA_HEAD_BYTES - 1));
    CHECK_STR(ota_stream_error(&stream), "Invalid image.");
    CHECK(!start(SLOT_BYTES + 1));
    CHECK_STR(ota_stream_error(&stream), "Image too large.");
    CHECK(!ota_stream_feed(&stream, image, 10)); /* refused already: nothing is taken */

    /* more than was announced */
    CHECK(start(IMAGE_BYTES - 10));
    CHECK(!feed_all(200));
    CHECK(flash.aborted == 1);

    /* less than was announced */
    CHECK(start(IMAGE_BYTES));
    CHECK(ota_stream_feed(&stream, image, 600));
    CHECK(!ota_stream_end(&stream));
    CHECK(flash.aborted == 1 && flash.finished == 0);
    CHECK_STR(ota_stream_error(&stream), "Upload incomplete.");
}

static void test_edges(void)
{
    /* an image that is nothing but its head */
    make_image("perinkpheral");
    memset(&flash, 0, sizeof flash);
    CHECK(ota_stream_start(&stream, &WRITER, NULL, "perinkpheral", OTA_HEAD_BYTES, SLOT_BYTES));
    CHECK(ota_stream_feed(&stream, image, OTA_HEAD_BYTES));
    CHECK(ota_stream_end(&stream));
    CHECK(flash.length == OTA_HEAD_BYTES && flash.finished == 1);

    /* a piece of nothing changes nothing */
    CHECK(start(IMAGE_BYTES));
    CHECK(ota_stream_feed(&stream, image, 0));
    CHECK(feed_all(250));
    CHECK(ota_stream_feed(&stream, image, 0));
    CHECK(ota_stream_end(&stream) && flash.length == IMAGE_BYTES);

    /* a project name too long to compare is refused, not cut to a prefix
     * that another project's image could match */
    memset(&flash, 0, sizeof flash);
    CHECK(ota_stream_start(&stream, &WRITER, NULL, "a-project-name-of-31-characters",
                           IMAGE_BYTES, SLOT_BYTES));
    CHECK(!ota_stream_start(&stream, &WRITER, NULL, "a-project-name-of-32-characters!",
                            IMAGE_BYTES, SLOT_BYTES));
    CHECK(!ota_stream_feed(&stream, image, 10));
}

static void test_writer_failures(void)
{
    make_image("perinkpheral");

    CHECK(start(IMAGE_BYTES));
    flash.refuse_begin = true;
    CHECK(!feed_all(512));
    CHECK(flash.aborted == 0); /* it never began: nothing to let go of */

    CHECK(start(IMAGE_BYTES));
    flash.refuse_write = true;
    CHECK(!feed_all(512));
    CHECK(flash.aborted == 1);
    CHECK(!ota_stream_end(&stream));
    CHECK(flash.aborted == 1 && flash.finished == 0); /* and only once */

    CHECK(start(IMAGE_BYTES));
    flash.refuse_finish = true;
    CHECK(feed_all(512));
    CHECK(!ota_stream_end(&stream));
    CHECK(flash.aborted == 0);

    /* the sender went away */
    CHECK(start(IMAGE_BYTES));
    CHECK(ota_stream_feed(&stream, image, 500));
    ota_stream_abort(&stream);
    CHECK(flash.aborted == 1);
    CHECK(!ota_stream_feed(&stream, image + 500, 500));
}

/* ---- the probation of a fresh image ---- */

static void test_health(void)
{
    ota_health_input_t input = { .network_stored = true, .web_server_up = true };

    /* a network is stored: only being on it counts */
    input.uptime_s = 5;
    CHECK(ota_health(&input) == OTA_HEALTH_WAIT);
    input.access_point_up = true; /* the rescue access point is not a pass */
    input.uptime_s = 100;
    CHECK(ota_health(&input) == OTA_HEALTH_WAIT);
    input.online = true;
    CHECK(ota_health(&input) == OTA_HEALTH_CONFIRM);

    /* online but nothing serves the next update */
    input.web_server_up = false;
    CHECK(ota_health(&input) == OTA_HEALTH_WAIT);
    input.uptime_s = OTA_HEALTH_DEADLINE_S;
    CHECK(ota_health(&input) == OTA_HEALTH_ROLL_BACK);

    /* no network stored: the setup access point, once it has settled */
    ota_health_input_t setup = { .access_point_up = true, .web_server_up = true };
    setup.uptime_s = OTA_HEALTH_SETUP_SETTLE_S - 1;
    CHECK(ota_health(&setup) == OTA_HEALTH_WAIT);
    setup.uptime_s = OTA_HEALTH_SETUP_SETTLE_S;
    CHECK(ota_health(&setup) == OTA_HEALTH_CONFIRM);
    setup.access_point_up = false;
    setup.uptime_s = OTA_HEALTH_DEADLINE_S - 1;
    CHECK(ota_health(&setup) == OTA_HEALTH_WAIT);
    setup.uptime_s = OTA_HEALTH_DEADLINE_S;
    CHECK(ota_health(&setup) == OTA_HEALTH_ROLL_BACK);
}

int main(void)
{
    test_good_image_in_any_pieces();
    test_foreign_images_never_reach_the_flash();
    test_sizes();
    test_edges();
    test_writer_failures();
    test_health();
    REPORT();
}
