/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "ota_stream.h"

#include <string.h>

/* The layout of an ESP-IDF application image; ota.c asserts these against
 * the SDK's own structs. */
#define IMAGE_MAGIC_BYTE         0xE9
#define DESCRIPTION_OFFSET       (24 + 8) /* after the image and segment headers */
#define DESCRIPTION_MAGIC_WORD   0xABCD5432u
#define PROJECT_NAME_OFFSET      (DESCRIPTION_OFFSET + 48)
#define PROJECT_NAME_FIELD_BYTES 32
#define LAYOUT_OFFSET            (DESCRIPTION_OFFSET + 256) /* right after the description */

static bool fail(ota_stream_t *stream, const char *error)
{
    if (stream->error == NULL) {
        stream->error = error;
        if (stream->writer_begun) {
            stream->writer->abort(stream->writer_context);
        }
    }
    return false;
}

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static bool head_is_ours(const ota_stream_t *stream)
{
    const uint8_t *name = stream->head + PROJECT_NAME_OFFSET;
    size_t our_length = strlen(stream->project_name);
    return stream->head[0] == IMAGE_MAGIC_BYTE &&
           read_u32_le(stream->head + DESCRIPTION_OFFSET) == DESCRIPTION_MAGIC_WORD &&
           our_length < PROJECT_NAME_FIELD_BYTES &&
           memcmp(name, stream->project_name, our_length) == 0 && name[our_length] == '\0';
}

/* Whether the image was built for the partition table this device has. */
static bool is_header_compatible(const ota_stream_t *stream)
{
    const uint8_t *layout = stream->head + LAYOUT_OFFSET;
    return read_u32_le(layout) == OTA_LAYOUT_MAGIC &&
           read_u32_le(layout + sizeof(uint32_t)) == stream->partition_table;
}

bool ota_stream_start(ota_stream_t *stream, const ota_writer_t *writer, void *writer_context,
                      const char *project_name, uint32_t partition_table, size_t image_size,
                      size_t slot_size)
{
    memset(stream, 0, sizeof *stream);
    stream->writer = writer;
    stream->writer_context = writer_context;
    stream->image_size = image_size;
    if (strlen(project_name) > OTA_PROJECT_NAME_MAX) {
        /* Cut short it would match images of another name. */
        return fail(stream, "Internal error.");
    }
    strcpy(stream->project_name, project_name);
    stream->partition_table = partition_table;
    if (image_size < OTA_HEAD_BYTES) {
        return fail(stream, "Invalid image.");
    }
    if (image_size > slot_size) {
        return fail(stream, "Image too large.");
    }
    return true;
}

static bool pass_on(ota_stream_t *stream, const uint8_t *data, size_t length)
{
    if (length > 0 && !stream->writer->write(stream->writer_context, data, length)) {
        return fail(stream, "Write failed.");
    }
    return true;
}

bool ota_stream_feed(ota_stream_t *stream, const uint8_t *data, size_t length)
{
    if (stream->error != NULL) {
        return false;
    }
    if (length > stream->image_size - stream->received) {
        return fail(stream, "Invalid upload.");
    }
    size_t already = stream->received;
    stream->received += length;

    if (already < OTA_HEAD_BYTES) {
        size_t head_bytes = OTA_HEAD_BYTES - already;
        if (head_bytes > length) {
            head_bytes = length;
        }
        memcpy(stream->head + already, data, head_bytes);
        data += head_bytes;
        length -= head_bytes;
        if (already + head_bytes < OTA_HEAD_BYTES) {
            return true; /* still not enough to judge */
        }
        if (!head_is_ours(stream)) {
            return fail(stream, "Invalid image.");
        }
        if (!is_header_compatible(stream)) {
            return fail(stream, "Image is for another partition table. Flash over USB.");
        }
        if (!stream->writer->begin(stream->writer_context, stream->image_size)) {
            return fail(stream, "Write failed.");
        }
        stream->writer_begun = true;
        if (!pass_on(stream, stream->head, OTA_HEAD_BYTES)) {
            return false;
        }
    }
    return pass_on(stream, data, length);
}

bool ota_stream_end(ota_stream_t *stream)
{
    if (stream->error != NULL) {
        return false;
    }
    if (stream->received != stream->image_size) {
        return fail(stream, "Upload incomplete.");
    }
    if (!stream->writer->finish(stream->writer_context)) {
        /* A writer that failed to finish has already let go of the slot. */
        stream->writer_begun = false;
        return fail(stream, "Image verification failed.");
    }
    return true;
}

void ota_stream_abort(ota_stream_t *stream)
{
    fail(stream, "Upload interrupted.");
}

const char *ota_stream_error(const ota_stream_t *stream)
{
    return stream->error != NULL ? stream->error : "";
}
