/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "json_pick.h"
#include "manifest_limits.h"

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
#define APP_NAME_MAX    MANIFEST_NAME_MAX
#define APP_TITLE_MAX   MANIFEST_TITLE_MAX
#define APP_VERSION_MAX MANIFEST_VERSION_MAX

/* What the host fetches for the application and how often. A poll is
 * known to the application by its number in the manifest. */
#define APP_POLLS_MAX            MANIFEST_POLLS_MAX
#define APP_POLL_EVERY_MIN_S     MANIFEST_POLL_EVERY_MIN_S
#define APP_POLL_EVERY_MAX_S     MANIFEST_POLL_EVERY_MAX_S
#define APP_URL_MAX              MANIFEST_URL_MAX
#define APP_HEADERS_MAX          MANIFEST_HEADERS_MAX
#define APP_HEADER_NAME_MAX      MANIFEST_HEADER_NAME_MAX
#define APP_HEADER_VALUE_MAX     MANIFEST_HEADER_VALUE_MAX

/* An application may bring an icon, drawn like the host's own: 11 rows of
 * 11 characters in the manifest, '#' for ink and '.' for paper.
 *
 *     "icon": [ "...........", "....###....", ... ]
 *
 * The device draws it beside the title in its list, a cell two pixels;
 * the web page draws it larger. */
#define APP_ICON_CELLS MANIFEST_ICON_CELLS

typedef struct {
    char name[APP_NAME_MAX + 1];       /* a-z, 0-9, '-' and '_' */
    char title[APP_TITLE_MAX + 1];     /* what the menu shows */
    char version[APP_VERSION_MAX + 1]; /* empty when the manifest has none */
    bool fullscreen;                   /* absent means false: under the header */
    bool system;                       /* asks for the system API; absent means false */
    /* The kinds of news it asked for under "intents": bit HOST_INTENT_*
     * of each. */
    uint32_t intents;
    /* Not from the manifest: what the host calls this application - a
     * number made of its code and its manifest, never 0. Whoever lists the
     * installed applications fills it in. */
    uint32_t id;
    int      poll_count;
    uint32_t poll_every_s[APP_POLLS_MAX];
    /* The icon, when the manifest has one: a row a word, the leftmost cell
     * the highest of its APP_ICON_CELLS bits, a set bit ink. */
    bool     has_icon;
    uint16_t icon[APP_ICON_CELLS];
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
    /* The paths to pick out of a JSON answer (see json_pick.h). None: the
     * application takes the answer as it comes, in pieces. */
    char         pick[JSON_PICK_PATHS_MAX][JSON_PICK_PATH_MAX + 1];
    int          pick_count;
} app_poll_t;

typedef enum {
    APP_MANIFEST_OK,
    APP_MANIFEST_NOT_JSON,
    APP_MANIFEST_BAD_NAME,  /* missing, too long, or characters outside the set */
    APP_MANIFEST_BAD_TITLE, /* missing, empty or too long */
    APP_MANIFEST_BAD_FIELD, /* a known field of the wrong type or too long */
    APP_MANIFEST_OTHER_ABI, /* missing, or not the ABI this host speaks */
    APP_MANIFEST_BAD_HOSTS,   /* "hosts" is not a list of a few host names */
    APP_MANIFEST_BAD_SETTING, /* "settings" is not a list of sound settings */
    APP_MANIFEST_BAD_INTENT,  /* "intents" is not a list of kinds the host knows */
    APP_MANIFEST_BAD_POLL,  /* too many polls, or one without a sound url and every */
} app_manifest_result_t;

app_manifest_result_t app_manifest_parse(const char *json, app_manifest_t *manifest);

/* The poll numbered `poll` of a manifest that app_manifest_parse()
 * accepted. It is read on demand: a poll in full is too large to keep for
 * every installed application. */
app_manifest_result_t app_manifest_parse_poll(const char *json, int poll, app_poll_t *parsed);

/* For the log and for the tool that checks a manifest before it ships. */
const char *app_manifest_result_text(app_manifest_result_t result);

/* What the user sets for an application, declared in its manifest:
 *
 *     "settings": [
 *       { "key": "token", "type": "secret", "title": "Plex token" },
 *       { "key": "units", "type": "choice", "title": "Units",
 *         "options": ["celsius", "fahrenheit"], "help": "..." }
 *     ]
 *
 * The device's web page draws a form from it without running the
 * application. A value is text in the application's own store under
 * `key`, so the code reads it with kv_get() by the same name and the host
 * puts it into URLs and headers as {{key}}.
 *
 * A setting may have a "default": what the page shows while nothing is
 * set. Nothing is stored for it - the application has to mean the same
 * when kv_get() finds nothing.
 *
 * A place is asked for by its name, and a request needs where it is:
 *
 *       { "key": "city", "type": "place", "title": "City",
 *         "latitude": "lat", "longitude": "lon" },
 *       { "key": "lat", "type": "number", "title": "Latitude" },
 *       { "key": "lon", "type": "number", "title": "Longitude" }
 *
 * The page offers places as the name is typed and, with the one chosen,
 * fills the two number settings the place names; it does not show them. */
#define APP_SETTINGS_MAX        MANIFEST_SETTINGS_MAX
#define APP_SETTING_KEY_MAX     MANIFEST_SETTING_KEY_MAX
#define APP_SETTING_TITLE_MAX   MANIFEST_SETTING_TITLE_MAX
#define APP_SETTING_HELP_MAX    MANIFEST_SETTING_HELP_MAX
#define APP_SETTING_VALUE_MAX   MANIFEST_SETTING_VALUE_MAX
#define APP_SETTING_OPTIONS_MAX MANIFEST_SETTING_OPTIONS_MAX
#define APP_SETTING_OPTION_MAX  MANIFEST_SETTING_OPTION_MAX

typedef enum {
    APP_SETTING_TEXT,
    APP_SETTING_SECRET, /* the page can write it and cannot read it back */
    APP_SETTING_NUMBER,
    APP_SETTING_CHOICE, /* one of "options" */
    APP_SETTING_TOGGLE, /* "1" or "0" */
    APP_SETTING_PLACE,  /* the name of a place; see below */
    APP_SETTING_URL,    /* a server of the user's own: "https://ha.example.org", no path */
    APP_SETTING_LINK,   /* a whole address the user was given: a calendar's "https://host/path?key" */
} app_setting_type_t;

/* Whether `value` may be stored under `key`: the manifest has such a
 * setting, and the value fits its type. `is_secret` may be NULL. The
 * manifest is one that app_manifest_parse() accepted. */
/* Whether `key` is a setting of the "url" type: where the user said the
 * server is. The application's requests go there, so it is the user's to
 * write and never the application's. */
bool app_manifest_is_server_setting(const char *json, const char *key);

bool app_manifest_accepts_setting(const char *json, const char *key, const char *value,
                                  bool *is_secret);

/* Where an application may send requests of its own (http_request): the
 * manifest's "hosts", up to APP_HOSTS_MAX names, each as it stands in a
 * URL - "api.example.com", "192.168.1.10:32400" - or "{{key}}" for the
 * server the user named in the setting of the "url" type under that key,
 * whose value `value_of` gives. True when the URL is http or https and its
 * host is one of them. The polls of the manifest are the author's own and
 * are not held to the list.
 *
 * A server of the user's own is how an application reaches what lives in
 * somebody's home: a poll or a request may begin with "{{key}}" of such a
 * setting ("{{server}}/api/states"), and only begin with it. */
#define APP_HOSTS_MAX MANIFEST_HOSTS_MAX
#define APP_HOST_MAX  MANIFEST_HOST_MAX
typedef bool (*app_manifest_value_fn)(void *context, const char *key, char *value,
                                      size_t value_size);
bool app_manifest_allows_url(const char *json, const char *url,
                             app_manifest_value_fn value_of, void *context);

/* The type of the setting under `key`, an app_setting_type_t; -1 when the
 * manifest has none. */
int app_manifest_setting_type(const char *json, const char *key);

/* The keys of the settings of the "secret" type; returns how many. */
int app_manifest_secret_keys(const char *json, char keys[][16], int capacity);

/* Whether two http or https URLs name the same host and port - a port
 * that is not written being the scheme's own, and http and https of one
 * name being one host: what a redirect is measured against. */
bool app_manifest_is_same_host(const char *url, const char *other_url);

/* 1-13 of a-z, 0-9, - and _: what an application may be called. */
bool app_manifest_is_valid_name(const char *name);

#ifdef __cplusplus
}
#endif
