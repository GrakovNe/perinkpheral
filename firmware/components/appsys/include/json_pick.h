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

/* Picks a few values out of a JSON document as it streams by, so that
 * neither the host nor an application ever holds the document: the
 * application names the paths it wants in its manifest -
 *
 *     current.temperature_2m     daily.weather_code[0]     items[2].title
 *
 * - and gets the values as text. A number, true, false and null come as
 * they are written; a string comes without its quotes, escapes resolved,
 * as UTF-8 - what the device has no glyph for is the drawing's to
 * replace, not this one's to lose - and cut, when it is too long,
 * between characters. A path that leads to an object or an
 * array, or nowhere, has no value. Pure, host-tested. */

#define JSON_PICK_PATHS_MAX 16
#define JSON_PICK_PATH_MAX  63
#define JSON_PICK_VALUE_MAX 63 /* a longer value is cut */
#define JSON_PICK_DEPTH_MAX 8  /* nothing is picked from deeper than this */
#define JSON_PICK_KEY_MAX   31 /* a longer key matches nothing */

typedef struct {
    bool is_array;
    int  index;                      /* of the element being read, in an array */
    bool expects_key;                /* in an object: the next string is a key */
    char key[JSON_PICK_KEY_MAX + 1]; /* of the member being read; empty when too long */
} json_pick_level_t;

typedef struct {
    const char *const *paths;
    int                path_count;
    char               values[JSON_PICK_PATHS_MAX][JSON_PICK_VALUE_MAX + 1];
    bool               found[JSON_PICK_PATHS_MAX];

    json_pick_level_t levels[JSON_PICK_DEPTH_MAX];
    int               depth; /* may run past DEPTH_MAX: those levels are only counted */
    enum {
        JSON_PICK_BETWEEN, /* outside any string or literal */
        JSON_PICK_IN_KEY,
        JSON_PICK_IN_STRING,
        JSON_PICK_IN_LITERAL, /* a number, true, false, null */
    } state;
    bool     escaped;        /* the character before was a backslash */
    int      unicode_digits; /* of a \uXXXX still to come */
    uint32_t unicode_value;
    uint32_t high_half;  /* of a character that \u spells in two halves; 0: none waiting */
    int      utf8_left;  /* bytes still to come of a character of several */
    uint32_t utf8_value;
    int      target;         /* which path the value being read is for, or -1 */
    size_t   length;         /* of the key or the value collected so far */
    bool     broken;
} json_pick_t;

/* `paths` must stay where they are while the picker is used; more than
 * JSON_PICK_PATHS_MAX of them are ignored. */
void json_pick_init(json_pick_t *pick, const char *const *paths, int path_count);

/* The next bytes of the document. False once it has turned out not to be
 * JSON; what was found before that stays. */
bool json_pick_feed(json_pick_t *pick, const char *bytes, size_t length);

/* The end of the document: a number at the very end has no other way to
 * know it is complete. */
void json_pick_finish(json_pick_t *pick);

/* The value of path number `index`, or NULL when the document had none. */
const char *json_pick_value(const json_pick_t *pick, int index);

#ifdef __cplusplus
}
#endif
