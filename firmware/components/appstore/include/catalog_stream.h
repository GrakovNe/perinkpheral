/* SPDX-License-Identifier: MIT */
#pragma once

#include "catalog.h"
#include "manifest_limits.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CATALOG_STREAM_MAX_BYTES (512 * 1024)
#define CATALOG_STREAM_DEPTH_MAX 64
#define CATALOG_STREAM_TOKEN_MAX 127

typedef struct {
    char name[MANIFEST_FILE_NAME_MAX + 1];
    uint32_t size;
    uint8_t sha256[32];
} catalog_stream_file_t;

typedef struct {
    bool found;
    bool valid;
    char name[MANIFEST_NAME_MAX + 1];
    char version[MANIFEST_VERSION_MAX + 1];
    char firmware[MANIFEST_VERSION_MAX + 1];
    char author[MANIFEST_AUTHOR_MAX + 1];
    uint32_t abi;
    bool system;
    bool writes_files;
    bool has_trusts;
    size_t file_count;
    catalog_stream_file_t files[CATALOG_APP_FILES_MAX];
} catalog_stream_entry_t;

typedef struct {
    uint8_t kind;
    uint8_t state;
    uint8_t role;
    uint8_t field;
} catalog_stream_level_t;

typedef struct {
    const char *const *wanted;
    size_t wanted_count;
    catalog_stream_entry_t *results;
    catalog_stream_entry_t current;
    catalog_stream_level_t levels[CATALOG_STREAM_DEPTH_MAX];
    size_t depth;
    size_t total_bytes;
    size_t token_length;
    char token[CATALOG_STREAM_TOKEN_MAX + 1];
    uint32_t unicode_value;
    uint32_t high_surrogate;
    uint32_t utf8_value;
    uint32_t utf8_minimum;
    uint32_t entry_fields;
    uint32_t file_fields;
    uint32_t current_matches;
    uint8_t unicode_digits;
    uint8_t utf8_left;
    uint8_t token_kind;
    uint8_t number_state;
    uint8_t literal_at;
    bool token_overflow;
    bool escaped;
    bool broken;
    bool started;
    bool complete;
    bool saw_catalog;
    bool saw_apps;
    bool catalog_version_valid;
} catalog_stream_t;

/* Results are indexed like wanted. Both arrays remain alive until finish.
 * Only use results after finish returns true; a broken/truncated index may
 * already have populated them. No allocations or callbacks are performed. */
bool catalog_stream_init(catalog_stream_t *stream, const char *const *wanted,
                         size_t wanted_count, catalog_stream_entry_t *results);
bool catalog_stream_feed(catalog_stream_t *stream, const char *bytes, size_t length);
bool catalog_stream_finish(catalog_stream_t *stream);

#ifdef __cplusplus
}
#endif
