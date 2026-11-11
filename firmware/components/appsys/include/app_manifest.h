/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* manifest.json of an application, as far as the host reads it today:
 *
 *   { "name": "plex", "title": "Now Playing", "version": "0.1.0",
 *     "abi": 1, "fullscreen": false,
 *     "polls": [
 *       { "url": "http://192.168.1.10:32400/status/sessions",
 *         "headers": { "X-Plex-Token": "{{token}}" }, "every": 15 } ] }
 *
 * Fields the host does not know yet are skipped, so a manifest written
 * for a later host still loads. Pure, host-tested. */

/* The name is the application's folder, its key in the catalog and, after
 * a prefix that keeps applications apart from the host, the NVS namespace
 * of its store - which is what limits its length. */
#define APP_NAME_MAX    13
#define APP_TITLE_MAX   23
#define APP_VERSION_MAX 15

/* What the host fetches for the application and how often. A poll is
 * known to the application by its number in the manifest. */
#define APP_POLLS_MAX            4
#define APP_POLL_EVERY_MIN_S     10
#define APP_POLL_EVERY_MAX_S     (24 * 60 * 60)
#define APP_URL_MAX              255
#define APP_HEADERS_MAX          4
#define APP_HEADER_NAME_MAX      31
#define APP_HEADER_VALUE_MAX     127

typedef struct {
    char name[APP_NAME_MAX + 1];       /* a-z, 0-9, '-' and '_' */
    char title[APP_TITLE_MAX + 1];     /* what the menu shows */
    char version[APP_VERSION_MAX + 1]; /* empty when the manifest has none */
    bool fullscreen;                   /* absent means false: under the header */
    int      poll_count;
    uint32_t poll_every_s[APP_POLLS_MAX];
} app_manifest_t;

typedef struct {
    char name[APP_HEADER_NAME_MAX + 1];
    char value[APP_HEADER_VALUE_MAX + 1];
} app_header_t;

/* One poll in full. The URL and the header values are as written, with
 * {{key}} in them still to be replaced (see text_template.h). */
typedef struct {
    char         url[APP_URL_MAX + 1];
    app_header_t headers[APP_HEADERS_MAX];
    int          header_count;
    uint32_t     every_s;
} app_poll_t;

typedef enum {
    APP_MANIFEST_OK,
    APP_MANIFEST_NOT_JSON,
    APP_MANIFEST_BAD_NAME,  /* missing, too long, or characters outside the set */
    APP_MANIFEST_BAD_TITLE, /* missing, empty or too long */
    APP_MANIFEST_BAD_FIELD, /* a known field of the wrong type or too long */
    APP_MANIFEST_OTHER_ABI, /* missing, or not the ABI this host speaks */
    APP_MANIFEST_BAD_POLL,  /* too many polls, or one without a sound url and every */
} app_manifest_result_t;

app_manifest_result_t app_manifest_parse(const char *json, app_manifest_t *manifest);

/* The poll numbered `poll` of a manifest that app_manifest_parse()
 * accepted. It is read on demand: a poll in full is too large to keep for
 * every installed application. */
app_manifest_result_t app_manifest_parse_poll(const char *json, int poll, app_poll_t *parsed);

/* For the log and for the tool that checks a manifest before it ships. */
const char *app_manifest_result_text(app_manifest_result_t result);

/* 1-13 of a-z, 0-9, - and _: what an application may be called. */
bool app_manifest_is_valid_name(const char *name);

#ifdef __cplusplus
}
#endif
