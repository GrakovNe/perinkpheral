/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The application's strings in the device's language, and text filled from a pattern.
 */
#pragma once

#include "base.h"

/* ---- Localized strings ----
 *
 * Application text is displayed in the language of the device.
 * Translations are provided by the developer in the `strings` folder next
 * to the manifest: one file per language (en.json, ru.json), each an
 * object with the same keys:
 *
 *     { "no_events": "No events", "starts_in": "In {0} min" }
 *
 * tools/perinkpheral generates strings.h: the constants STR_NO_EVENTS,
 * STR_STARTS_IN and the function app_string(id). English is compiled into
 * the module. Every other language is written as a table file.
 * When the application is loaded, Perinkpheral OS copies the table of the device
 * language over the English table. A language without a table is shown
 * in English.
 * The build fails if a language is not listed in sdk/languages.json, if a
 * translation has a missing or an extra key, or if a string contains a
 * character that the glyph packs of the language do not cover.
 *
 * Table format: 16 bits buffer size in bytes, 16 bits string count, one
 * 16-bit offset from the table start per string, all little endian; then the strings,
 * null-terminated. */
APP_EXPORT(app_strings_buffer, uint8_t *app_strings_buffer(void));

/* Replaces "{0}", "{1}" ... in `pattern` with the arguments of those
 * numbers. Arguments can appear in any order.
 * An argument is a text; if `text` is NULL, it is a number.
 * The pattern and each text argument are cut to HOST_TEXT_MAX bytes.
 * The filled result is at most HOST_TEXT_MAX bytes, then cut to capacity,
 * without a terminator, cut between characters. Returns the length.
 * Capacity 0 returns 0; invalid arguments leave the output untouched. */
typedef struct host_text_argument {
    int32_t     number;
    const char *text;
    int32_t     length;
} host_text_argument_t;
#ifdef __wasm__ /* a pointer: four bytes in the module, regardless of the native platform's pointer size */
HOST_LAYOUT(host_text_argument_t, 12);
#endif
#define HOST_TEXT_ARGUMENTS_MAX 4
HOST_IMPORT(text_fill, int32_t text_fill(const char *pattern, int32_t pattern_length,
                                         const host_text_argument_t *arguments, int32_t count,
                                         char *text, int32_t capacity));
