/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "catalog.h"

#include <stdio.h>
#include <string.h>

/* HTTPS and nothing else: it is what vouches for an application, and the
 * checksums of an index that came in the clear vouch for nothing. */
static bool is_web_address(const char *line, size_t length)
{
    static const char HTTPS[] = "https://";
    bool is_https = length > sizeof HTTPS - 1 && strncmp(line, HTTPS, sizeof HTTPS - 1) == 0;
    if (!is_https || length > CATALOG_SOURCE_URL_MAX) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        if (line[i] <= ' ' || line[i] > '~') {
            return false;
        }
    }
    return true;
}

/* Calls `visit` for every line that is not empty; stops when it says so. */
typedef bool (*line_fn)(const char *line, size_t length, void *context);

static void for_each_line(const char *text, line_fn visit, void *context)
{
    const char *line = text;
    while (*line != '\0') {
        size_t length = strcspn(line, "\n");
        if (length > 0 && !visit(line, length, context)) {
            return;
        }
        line += length;
        if (*line == '\n') {
            line++;
        }
    }
}

typedef struct {
    int  count;
    bool all_valid;
} check_t;

static bool check_line(const char *line, size_t length, void *context)
{
    check_t *check = context;
    check->count++;
    check->all_valid = check->all_valid && is_web_address(line, length);
    return true;
}

bool catalog_sources_are_valid(const char *sources)
{
    check_t check = { 0, true };
    for_each_line(sources, check_line, &check);
    return check.all_valid && check.count <= CATALOG_SOURCES_MAX;
}

int catalog_source_count(const char *sources)
{
    check_t check = { 0, true };
    for_each_line(sources, check_line, &check);
    return check.count;
}

typedef struct {
    int    wanted;
    char  *source;
    size_t source_size;
    bool   found;
} pick_t;

static bool pick_line(const char *line, size_t length, void *context)
{
    pick_t *pick = context;
    if (pick->wanted-- > 0) {
        return true;
    }
    /* "…/catalog/" and "…/catalog" are one folder. */
    while (length > 0 && line[length - 1] == '/') {
        length--;
    }
    if (length > 0 && length < pick->source_size) {
        memcpy(pick->source, line, length);
        pick->source[length] = '\0';
        pick->found = true;
    }
    return false;
}

bool catalog_source_at(const char *sources, int index, char *source, size_t source_size)
{
    pick_t pick = { index, source, source_size, false };
    if (index >= 0) {
        for_each_line(sources, pick_line, &pick);
    }
    return pick.found;
}

bool catalog_url(const char *source, const char *app, const char *file, char *url,
                 size_t url_size)
{
    int written = app != NULL ? snprintf(url, url_size, "%s/%s/%s", source, app, file)
                              : snprintf(url, url_size, "%s/%s", source, file);
    return written > 0 && (size_t)written < url_size;
}
