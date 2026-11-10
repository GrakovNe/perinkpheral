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

bool ota_stream_start(ota_stream_t *stream, const ota_writer_t *writer, void *writer_context,
                      const char *project_name, size_t image_size, size_t slot_size)
{
    memset(stream, 0, sizeof *stream);
    stream->writer = writer;
    stream->writer_context = writer_context;
    stream->image_size = image_size;
    if (strlen(project_name) > OTA_PROJECT_NAME_MAX) {
        /* Cut short it would match images of another name. */
        return fail(stream, "the project name is too long to check images against");
    }
    strcpy(stream->project_name, project_name);
    if (image_size < OTA_HEAD_BYTES) {
        return fail(stream, "too small to be a firmware image");
    }
    if (image_size > slot_size) {
        return fail(stream, "the image does not fit the firmware slot");
    }
    return true;
}

static bool pass_on(ota_stream_t *stream, const uint8_t *data, size_t length)
{
    if (length > 0 && !stream->writer->write(stream->writer_context, data, length)) {
        return fail(stream, "writing the image failed");
    }
    return true;
}

bool ota_stream_feed(ota_stream_t *stream, const uint8_t *data, size_t length)
{
    if (stream->error != NULL) {
        return false;
    }
    if (length > stream->image_size - stream->received) {
        return fail(stream, "more data than the size announced");
    }
    size_t already = stream->received;
    stream->received += length;

    if (already < OTA_HEAD_BYTES) {
        size_t into_head = OTA_HEAD_BYTES - already;
        if (into_head > length) {
            into_head = length;
        }
        memcpy(stream->head + already, data, into_head);
        data += into_head;
        length -= into_head;
        if (already + into_head < OTA_HEAD_BYTES) {
            return true; /* still not enough to judge */
        }
        if (!head_is_ours(stream)) {
            return fail(stream, "not a firmware image of this device");
        }
        if (!stream->writer->begin(stream->writer_context, stream->image_size)) {
            return fail(stream, "cannot start writing the image");
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
        return fail(stream, "the image was cut short");
    }
    if (!stream->writer->finish(stream->writer_context)) {
        /* A writer that failed to finish has already let go of the slot. */
        stream->writer_begun = false;
        return fail(stream, "the image did not pass the check");
    }
    return true;
}

void ota_stream_abort(ota_stream_t *stream)
{
    fail(stream, "the upload was interrupted");
}

const char *ota_stream_error(const ota_stream_t *stream)
{
    return stream->error != NULL ? stream->error : "";
}
