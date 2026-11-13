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

/* Streaming JSON value extractor. Extracts a fixed set of values from a
 * JSON document without holding the document in memory.
 *
 * The host uses it for polls with "pick" in the manifest. An application
 * uses it for a streamed answer: feed every piece from app_on_data(), then
 * read the values after the last piece.
 *
 * Path syntax:
 *
 *     current.temperature_2m     daily.weather_code[0]     items[2].title
 *
 * Values are returned as text. Numbers, true, false and null are returned
 * as written. Strings are returned without quotes, with escapes resolved,
 * as UTF-8, cut between characters if too long. A path that points to an
 * object, to an array or to nothing has no value.
 * No dependencies; covered by host tests. */

#define JSON_PICK_PATHS_MAX 16
#define JSON_PICK_PATH_MAX  63
#define JSON_PICK_VALUE_MAX 63 /* longer values are cut */
#define JSON_PICK_DEPTH_MAX 8  /* deeper levels are not extracted */
#define JSON_PICK_KEY_MAX   31 /* longer keys never match */

typedef struct {
    bool is_array;
    int  index;                      /* array: index of the current element */
    bool expects_key;                /* object: the next string is a key */
    char key[JSON_PICK_KEY_MAX + 1]; /* key of the current member; empty if too long */
} json_pick_level_t;

typedef struct {
    const char *const *paths;
    int                path_count;
    char               values[JSON_PICK_PATHS_MAX][JSON_PICK_VALUE_MAX + 1];
    bool               found[JSON_PICK_PATHS_MAX];

    json_pick_level_t levels[JSON_PICK_DEPTH_MAX];
    int               depth; /* can exceed DEPTH_MAX: deeper levels are counted only */
    enum {
        JSON_PICK_BETWEEN, /* outside strings and literals */
        JSON_PICK_IN_KEY,
        JSON_PICK_IN_STRING,
        JSON_PICK_IN_LITERAL, /* number, true, false, null */
    } state;
    bool     escaped;        /* the previous character was a backslash */
    int      unicode_digits; /* remaining digits of a \uXXXX escape */
    uint32_t unicode_value;
    uint32_t high_half;  /* pending high surrogate of a \u pair; 0: none */
    int      utf8_left;  /* remaining bytes of a multi-byte character */
    uint32_t utf8_value;
    int      target;         /* path index of the value being read, or -1 */
    size_t   length;         /* length of the key or value collected so far */
    bool     broken;
} json_pick_t;

/* Initializes the extractor. `paths` must remain valid while it is used.
 * Paths beyond JSON_PICK_PATHS_MAX are ignored. */
void json_pick_init(json_pick_t *pick, const char *const *paths, int path_count);

/* Feeds the next bytes of the document. Returns false once the document
 * is found to be invalid JSON; values extracted before that are kept. */
bool json_pick_feed(json_pick_t *pick, const char *bytes, size_t length);

/* Signals the end of the document. Required to complete a number at the
 * very end of the input. */
void json_pick_finish(json_pick_t *pick);

/* Returns the value of path number `index`, or NULL if the document had
 * none. */
const char *json_pick_value(const json_pick_t *pick, int index);

#ifdef __cplusplus
}
#endif
