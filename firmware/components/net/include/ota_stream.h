/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A firmware image arriving in pieces. The start is held back until it
 * can be told whose image it is: an image of another project for the same
 * chip would boot, never confirm itself and, if it does not crash, never
 * roll back. Only then do the bytes go on to whatever writes them. No
 * flash in here: pure, host-tested. */

/* Where the image goes. Each returns false on failure; after that only
 * abort() is called. */
typedef struct {
    bool (*begin)(void *context, size_t image_size);
    bool (*write)(void *context, const uint8_t *data, size_t length);
    /* Everything arrived: check it and make it the image to boot. */
    bool (*finish)(void *context);
    void (*abort)(void *context);
} ota_writer_t;

/* The image header, the first segment's header, the application
 * description and, after it, what this project says of its image
 * (ota_layout_t): all that is needed to recognize an image. */
#define OTA_HEAD_BYTES (24 + 8 + 256 + 16)

/* What an image of this project carries right after its description: the
 * partition table it was built for. An image for another table would
 * look for its applications where there are none, and the table itself
 * cannot be changed over the air. Sixteen bytes, as they lie in the
 * image. */
#define OTA_LAYOUT_MAGIC 0x54524150u /* "PART" */

typedef struct {
    uint32_t magic;
    uint32_t partition_table; /* a number that is another for another table */
    uint32_t reserved[2];
} ota_layout_t;

#define OTA_PROJECT_NAME_MAX 31

typedef struct {
    const ota_writer_t *writer;
    void               *writer_context;
    char                project_name[OTA_PROJECT_NAME_MAX + 1];
    uint32_t            partition_table;
    size_t              image_size;
    size_t              received;
    uint8_t             head[OTA_HEAD_BYTES];
    bool                writer_begun;
    const char         *error; /* NULL while all is well */
} ota_stream_t;

/* False, with the reason in ota_stream_error(), when the size alone rules
 * the image out. slot_size is the room there is for it. */
bool ota_stream_start(ota_stream_t *stream, const ota_writer_t *writer, void *writer_context,
                      const char *project_name, uint32_t partition_table, size_t image_size,
                      size_t slot_size);

/* False when the image is refused or cannot be written; nothing more is
 * accepted then. */
bool ota_stream_feed(ota_stream_t *stream, const uint8_t *data, size_t length);

/* Call when the sender is done. True: the image is in place. */
bool ota_stream_end(ota_stream_t *stream);

/* The sender went away in the middle. */
void ota_stream_abort(ota_stream_t *stream);

const char *ota_stream_error(const ota_stream_t *stream);

#ifdef __cplusplus
}
#endif
