/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Where applications come from. A source is the address of a folder that
 * tools/catalog made: `catalog.json` in it says what is there, and every
 * application lies beside it in a folder of its own. The device knows a
 * few sources, the way a Debian system has its sources.list; it reads
 * their indexes and nothing else tells it what exists. Pure, host-tested. */

#define CATALOG_SOURCES_MAX     4
#define CATALOG_SOURCE_URL_MAX  127
/* All of them as one text, a line each: how they are stored. */
#define CATALOG_SOURCES_TEXT_MAX (CATALOG_SOURCES_MAX * (CATALOG_SOURCE_URL_MAX + 1))
#define CATALOG_URL_MAX         255
#define CATALOG_INDEX_FILE      "catalog.json"
/* No more applications than this on a device: few, and large ones. */
#define CATALOG_INSTALLED_MAX   5
/* What an application is made of at most: the module, the manifest and
 * its data files. */
#define CATALOG_APP_FILES_MAX   8

/* The official applications: always the first source, and not one the
 * user can take away. What a device stores is the sources added to it. */
#define CATALOG_OFFICIAL_SOURCE \
    "https://raw.githubusercontent.com/GrakovNe/perinkpheral-apps/HEAD/catalog"
#define CATALOG_ADDED_SOURCES_MAX (CATALOG_SOURCES_MAX - 1)

/* True for a few lines, each an https address in printable ASCII of at
 * most CATALOG_SOURCE_URL_MAX; an empty text is "no sources". */
bool catalog_sources_are_valid(const char *sources);

/* How many sources the text holds. */
int catalog_source_count(const char *sources);

/* Source number `index`, without a slash at its end; false when there is
 * no such source or it does not fit. */
bool catalog_source_at(const char *sources, int index, char *source, size_t source_size);

/* The address of a file of an application in a source - or of the index,
 * when `app` is NULL. False when it does not fit. */
bool catalog_url(const char *source, const char *app, const char *file, char *url,
                 size_t url_size);

#ifdef __cplusplus
}
#endif
