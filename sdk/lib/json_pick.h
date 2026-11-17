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
 *
 * A list path has one "[*]" in place of an index: every element of that
 * array, in order.
 *
 *     chart.result[0].indicators.quote[0].close[*]     items[*].title
 *
 * The elements are kept in a buffer the caller gives with
 * json_pick_lists(), each as its text and a terminator plus one byte;
 * elements that do not fit are dropped. An element without the rest of the
 * path ("items[*].title" in an item that has no title) is skipped, so two
 * lists of one array have the same order but may differ in length. A
 * value goes to the first path that names it.
 * No dependencies; covered by host tests. */

#define JSON_PICK_PATHS_MAX 16
#define JSON_PICK_PATH_MAX  63
#define JSON_PICK_VALUE_MAX 63 /* longer values are cut */
#define JSON_PICK_DEPTH_MAX 8  /* deeper levels are not extracted */
#define JSON_PICK_KEY_MAX   31 /* longer keys never match */
#define JSON_PICK_ANY       "[*]" /* in a path: every element of the array */

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

    /* List paths: their elements, one after another, each the path's index
     * in a byte and the text with its terminator. */
    char    *list;
    size_t   list_size;
    size_t   list_used;
    bool     list_is_cut; /* an element did not fit and was dropped */
    uint16_t counts[JSON_PICK_PATHS_MAX];
    /* Where the last element asked for was found: the next one is read on
     * from there. */
    int      read_path;
    int      read_element;
    size_t   read_at;
} json_pick_t;

/* Initializes the extractor. `paths` must remain valid while it is used.
 * Paths beyond JSON_PICK_PATHS_MAX are ignored. */
void json_pick_init(json_pick_t *pick, const char *const *paths, int path_count);

/* Feeds the next bytes of the document. Returns false once the document
 * is found to be invalid JSON; values extracted before that are kept. */
bool json_pick_feed(json_pick_t *pick, const char *bytes, size_t length);

/* Signals the end of the document. Required to complete a number at the
 * very end of the input. Returns whether the document ended whole: every
 * object and array closed, no string left open, nothing invalid on the way -
 * {"price":123 is a value found in a document that is not one. */
bool json_pick_finish(json_pick_t *pick);

/* Returns the value of path number `index`, or NULL if the document had
 * none. NULL for a list path: see json_pick_element(). */
const char *json_pick_value(const json_pick_t *pick, int index);

/* Gives the buffer for the elements of list paths; before the first
 * json_pick_feed(). Without one, list paths have no elements. */
void json_pick_lists(json_pick_t *pick, char *buffer, size_t size);

/* Whether path number `index` has "[*]". */
bool json_pick_is_list(const json_pick_t *pick, int index);

/* The number of values of path number `index`: the elements kept of a list
 * path; 1 or 0 for any other path, as it has a value or not. */
int json_pick_count(const json_pick_t *pick, int index);

/* Value number `element` of path number `index`, or NULL: an element of a
 * list path, or the value of any other path as element 0. Reading the
 * elements in order is fast; any other order is a search from the start. */
const char *json_pick_element(json_pick_t *pick, int index, int element);

/* Whether an element of a list path was dropped for want of room. */
bool json_pick_is_cut(const json_pick_t *pick);

#ifdef __cplusplus
}
#endif
