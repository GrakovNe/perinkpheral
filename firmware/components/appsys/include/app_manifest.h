/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* manifest.json of an application, as far as the host reads it today:
 *
 *   { "name": "hello", "title": "Hello", "version": "0.1.0",
 *     "abi": 1, "fullscreen": false }
 *
 * Fields the host does not know yet are skipped, so a manifest written
 * for a later host still loads. Pure, host-tested. */

/* The name is the application's folder, its key in the catalog and the
 * namespace of its settings, which is what limits its length. */
#define APP_NAME_MAX    15
#define APP_TITLE_MAX   23
#define APP_VERSION_MAX 15

typedef struct {
    char name[APP_NAME_MAX + 1];       /* a-z, 0-9, '-' and '_' */
    char title[APP_TITLE_MAX + 1];     /* what the menu shows */
    char version[APP_VERSION_MAX + 1]; /* empty when the manifest has none */
    bool fullscreen;                   /* absent means false: under the header */
} app_manifest_t;

typedef enum {
    APP_MANIFEST_OK,
    APP_MANIFEST_NOT_JSON,
    APP_MANIFEST_BAD_NAME,  /* missing, too long, or characters outside the set */
    APP_MANIFEST_BAD_TITLE, /* missing, empty or too long */
    APP_MANIFEST_BAD_FIELD, /* a known field of the wrong type or too long */
    APP_MANIFEST_OTHER_ABI, /* missing, or not the ABI this host speaks */
} app_manifest_result_t;

app_manifest_result_t app_manifest_parse(const char *json, app_manifest_t *manifest);

/* For the log and for the tool that checks a manifest before it ships. */
const char *app_manifest_result_text(app_manifest_result_t result);

#ifdef __cplusplus
}
#endif
