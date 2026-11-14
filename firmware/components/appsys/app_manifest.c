/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_manifest.h"

_Static_assert(MANIFEST_PICK_PATHS_MAX == JSON_PICK_PATHS_MAX &&
                   MANIFEST_PICK_PATH_MAX == JSON_PICK_PATH_MAX,
               "a manifest names as many paths, as long, as the picker takes");

#include <stdlib.h>
#include <string.h>

#include "json_tree.h"
#include "host_api.h"

static bool is_name_character(char character)
{
    return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
           character == '-' || character == '_';
}

bool app_manifest_is_valid_name(const char *name)
{
    if (name[0] == '\0') {
        return false;
    }
    for (const char *character = name; *character != '\0'; character++) {
        if (!is_name_character(*character)) {
            return false;
        }
    }
    return true;
}

/* False when the field is there but is not a string that fits. A missing
 * field leaves `value` empty. */
static bool read_string(const json_t *root, const char *field, char *value, size_t value_size)
{
    value[0] = '\0';
    const json_t *item = json_get(root, field);
    if (item == NULL) {
        return true;
    }
    if (!json_is_string(item) || strlen(item->text) >= value_size) {
        return false;
    }
    strcpy(value, item->text);
    return true;
}

static bool starts_with(const char *text, const char *prefix)
{
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

/* The headers of a poll: an object of strings. A poll without the field
 * has none. */
static bool read_headers(const json_t *headers, app_poll_t *poll)
{
    poll->header_count = 0;
    if (headers == NULL) {
        return true;
    }
    if (!json_is_object(headers) || json_count(headers) > APP_HEADERS_MAX) {
        return false;
    }
    const json_t *header;
    json_for_each(header, headers)
    {
        app_header_t *parsed = &poll->headers[poll->header_count];
        if (!json_is_string(header) || header->name[0] == '\0' ||
            strlen(header->name) > APP_HEADER_NAME_MAX ||
            strlen(header->text) > APP_HEADER_VALUE_MAX) {
            return false;
        }
        strcpy(parsed->name, header->name);
        strcpy(parsed->value, header->text);
        poll->header_count++;
    }
    return true;
}

static bool read_pick(const json_t *pick, app_poll_t *poll)
{
    poll->pick_count = 0;
    if (pick == NULL) {
        return true;
    }
    if (!json_is_array(pick) || json_count(pick) > JSON_PICK_PATHS_MAX) {
        return false;
    }
    const json_t *path;
    json_for_each(path, pick)
    {
        if (!json_is_string(path) || path->text[0] == '\0' ||
            strlen(path->text) > JSON_PICK_PATH_MAX) {
            return false;
        }
        strcpy(poll->pick[poll->pick_count++], path->text);
    }
    return true;
}

static int address_setting(const json_t *settings, const char *key, size_t key_length);

/* The page that sets the application up from a browser: somewhere a
 * browser will take a secret from, so https and nothing else. */
static bool is_companion_url(const json_t *companion)
{
    static const char SCHEME[] = "https://";
    if (!json_is_string(companion)) {
        return false;
    }
    const char *url = companion->text;
    size_t      length = strlen(url);
    if (length <= sizeof SCHEME - 1 || length > APP_URL_MAX || !starts_with(url, SCHEME)) {
        return false;
    }
    for (const char *at = url; *at != '\0'; at++) {
        if (*at <= ' ' || *at > '~' || *at == '"' || *at == '<' || *at == '>') {
            return false;
        }
    }
    return true;
}

uint32_t app_manifest_interval_seconds(const char *text)
{
    uint32_t number = 0;
    int      digits = 0;
    while (*text >= '0' && *text <= '9' && digits < 6) {
        number = number * 10 + (uint32_t)(*text++ - '0');
        digits++;
    }
    if (digits == 0) {
        return 0;
    }
    while (*text == ' ') {
        text++;
    }
    uint32_t unit = *text == '\0' || *text == 's' ? 1
                    : *text == 'm'               ? 60
                    : *text == 'h'               ? 3600
                    : *text == 'd'               ? 86400
                                                 : 0;
    /* More than a day is not a pace anybody is given, and would not fit. */
    return unit == 0 || number > APP_POLL_EVERY_MAX_S / unit ? 0 : number * unit;
}

/* "{{key}}" and nothing else before `rest`: the key, when it is a setting
 * of the "url" type - the server the user named - or of the "link" type,
 * which is the whole address and has nothing after it. */
static bool starts_with_server_setting(const json_t *settings, const char *text, const char **rest)
{
    if (!starts_with(text, "{{")) {
        return false;
    }
    const char *key = text + 2;
    const char *closing = strstr(key, "}}");
    int type = closing != NULL ? address_setting(settings, key, (size_t)(closing - key)) : -1;
    if (type < 0 || (type == APP_SETTING_LINK && closing[2] != '\0')) {
        return false;
    }
    *rest = closing + 2;
    return true;
}

/* A poll goes to a server the manifest names, or to the one the user
 * named in a setting made for it. */
static bool read_poll(const json_t *poll, const json_t *settings, app_poll_t *parsed)
{
    if (!json_is_object(poll)) {
        return false;
    }
    const char *after_server;
    if (!read_string(poll, "url", parsed->url, sizeof parsed->url) ||
        !(starts_with(parsed->url, "http://") || starts_with(parsed->url, "https://") ||
          starts_with_server_setting(settings, parsed->url, &after_server))) {
        return false;
    }
    const json_t *every = json_get(poll, "every");
    parsed->every_key[0] = '\0';
    if (json_is_string(every)) {
        /* "{{key}}": the pace is the user's choice in that setting, and
         * its default - or a quarter of an hour - until one is made. */
        const char  *text = every->text;
        size_t       length = strlen(text);
        const json_t *setting;
        bool         is_found = false;
        if (length < 5 || length - 4 > APP_SETTING_KEY_MAX || !starts_with(text, "{{") ||
            strcmp(text + length - 2, "}}") != 0) {
            return false;
        }
        memcpy(parsed->every_key, text + 2, length - 4);
        parsed->every_key[length - 4] = '\0';
        parsed->every_s = APP_POLL_EVERY_UNSET_S;
        json_for_each(setting, settings)
        {
            const json_t *key = json_get(setting, "key");
            if (!json_is_string(key) || strcmp(key->text, parsed->every_key) != 0) {
                continue;
            }
            is_found = true;
            const json_t *preset = json_get(setting, "default");
            uint32_t     pace = json_is_string(preset) ? app_manifest_interval_seconds(preset->text)
                                                       : 0;
            if (pace >= APP_POLL_EVERY_MIN_S && pace <= APP_POLL_EVERY_MAX_S) {
                parsed->every_s = pace;
            }
        }
        if (!is_found) {
            return false;
        }
    } else if (!json_is_number(every) || every->number < APP_POLL_EVERY_MIN_S ||
               every->number > APP_POLL_EVERY_MAX_S) {
        return false;
    } else {
        parsed->every_s = (uint32_t)every->number;
    }
    return read_headers(json_get(poll, "headers"), parsed) &&
           read_pick(json_get(poll, "pick"), parsed);
}

static const char *const SETTING_TYPES[] = {
    [APP_SETTING_TEXT] = "text",     [APP_SETTING_SECRET] = "secret",
    [APP_SETTING_NUMBER] = "number", [APP_SETTING_CHOICE] = "choice",
    [APP_SETTING_TOGGLE] = "toggle", [APP_SETTING_PLACE] = "place",
    [APP_SETTING_URL] = "url", [APP_SETTING_LINK] = "link",
};
#define SETTING_TYPE_COUNT ((int)(sizeof SETTING_TYPES / sizeof SETTING_TYPES[0]))

static bool is_text_of(const json_t *item, size_t min_length, size_t max_length)
{
    return json_is_string(item) && strlen(item->text) >= min_length &&
           strlen(item->text) <= max_length;
}

/* A text a person reads: so many characters at most, and no more bytes
 * than letters of two bytes would take. */
static bool is_words_of(const json_t *item, size_t min_characters, size_t max_characters)
{
    if (!json_is_string(item)) {
        return false;
    }
    size_t characters = 0;
    for (const char *at = item->text; *at != '\0'; at++) {
        characters += ((unsigned char)*at & 0xC0) != 0x80;
    }
    return characters >= min_characters && characters <= max_characters &&
           strlen(item->text) <= max_characters * MANIFEST_TEXT_BYTES_PER_CHARACTER;
}

/* A kv key: what kv_get and kv_set take. */
static bool is_setting_key(const json_t *key)
{
    if (!is_text_of(key, 1, APP_SETTING_KEY_MAX)) {
        return false;
    }
    for (const char *character = key->text; *character != '\0'; character++) {
        if (*character < '!' || *character > '~') {
            return false;
        }
    }
    return true;
}

/* The type of a setting, or -1 when it is not a sound one. */
static int setting_type(const json_t *setting)
{
    const json_t *type = json_get(setting, "type");
    const json_t *help = json_get(setting, "help");
    if (!json_is_object(setting) || !json_is_string(type) ||
        !is_setting_key(json_get(setting, "key")) ||
        !is_words_of(json_get(setting, "title"), 1,
                     APP_SETTING_TITLE_MAX) ||
        (help != NULL && !is_words_of(help, 0, APP_SETTING_HELP_MAX))) {
        return -1;
    }
    for (int known = 0; known < SETTING_TYPE_COUNT; known++) {
        if (strcmp(type->text, SETTING_TYPES[known]) != 0) {
            continue;
        }
        const json_t *options = json_get(setting, "options");
        if (known == APP_SETTING_PLACE &&
            (!is_setting_key(json_get(setting, "latitude")) ||
             !is_setting_key(json_get(setting, "longitude")))) {
            return -1;
        }
        if (known != APP_SETTING_CHOICE) {
            return options == NULL ? known : -1;
        }
        int count = json_is_array(options) ? json_count(options) : 0;
        if (count < 1 || count > APP_SETTING_OPTIONS_MAX) {
            return -1;
        }
        const json_t *option;
        json_for_each(option, options)
        {
            if (!is_words_of(option, 1, APP_SETTING_OPTION_MAX)) {
                return -1;
            }
        }
        return known;
    }
    return -1;
}

static bool fits_type(const json_t *setting, int type, const char *value);
static bool is_server_value(const char *value);
static bool is_link_value(const char *value);

/* The type of the setting with this key when it is an address of the
 * user's - a server ("url") or a whole link - and -1 otherwise. */
static int address_setting(const json_t *settings, const char *key, size_t key_length)
{
    const json_t *setting;
    json_for_each(setting, settings)
    {
        int type = setting_type(setting);
        if (type != APP_SETTING_URL && type != APP_SETTING_LINK) {
            continue;
        }
        const char *its_key = json_get(setting, "key")->text;
        if (strlen(its_key) == key_length && memcmp(its_key, key, key_length) == 0) {
            return type;
        }
    }
    return -1;
}
static bool is_printable(const char *text);

/* What the page shows while nothing is set; the application has to mean
 * the same when it finds nothing in its store. A secret has none, and a
 * place is not a place without its numbers. */
static bool has_sound_default(const json_t *setting, int type)
{
    const json_t *value = json_get(setting, "default");
    if (value == NULL) {
        return true;
    }
    return type != APP_SETTING_SECRET && type != APP_SETTING_PLACE &&
           is_text_of(value, 1, APP_SETTING_VALUE_MAX) && is_printable(value->text) &&
           fits_type(setting, type, value->text);
}

/* Whether the settings have a number under the key that `named` holds. */
static bool has_number_setting(const json_t *settings, const json_t *named)
{
    const json_t *setting;
    json_for_each(setting, settings)
    {
        if (setting_type(setting) == APP_SETTING_NUMBER &&
            strcmp(json_get(setting, "key")->text,
                   named->text) == 0) {
            return true;
        }
    }
    return false;
}

/* A place fills two number settings: they have to be there, and be two. */
static bool is_sound_place(const json_t *settings, const json_t *place)
{
    const json_t *latitude = json_get(place, "latitude");
    const json_t *longitude = json_get(place, "longitude");
    return strcmp(latitude->text, longitude->text) != 0 &&
           has_number_setting(settings, latitude) && has_number_setting(settings, longitude);
}

static bool are_sound_settings(const json_t *settings)
{
    if (settings == NULL) {
        return true;
    }
    if (!json_is_array(settings) || json_count(settings) > APP_SETTINGS_MAX) {
        return false;
    }
    const json_t *setting;
    json_for_each(setting, settings)
    {
        int type = setting_type(setting);
        if (type < 0 || (type == APP_SETTING_PLACE && !is_sound_place(settings, setting)) ||
            !has_sound_default(setting, type)) {
            return false;
        }
        /* Two settings under one key would be one value with two forms. */
        const char *key = json_get(setting, "key")->text;
        for (const json_t *earlier = settings->child; earlier != setting; earlier = earlier->next) {
            if (strcmp(json_get(earlier, "key")->text, key) == 0) {
                return false;
            }
        }
    }
    return true;
}

static bool is_number_text(const char *text)
{
    const char *at = text + (*text == '-' ? 1 : 0);
    int digits = 0, points = 0;
    for (; *at != '\0'; at++) {
        if (*at == '.') {
            points++;
        } else if (*at >= '0' && *at <= '9') {
            digits++;
        } else {
            return false;
        }
    }
    return digits > 0 && points <= 1;
}

static bool fits_type(const json_t *setting, int type, const char *value)
{
    /* Empty is "not set", whatever the type. */
    if (value[0] == '\0') {
        return true;
    }
    switch (type) {
    case APP_SETTING_NUMBER:
        return is_number_text(value);
    case APP_SETTING_TOGGLE:
        return strcmp(value, "0") == 0 || strcmp(value, "1") == 0;
    case APP_SETTING_URL:
        return is_server_value(value);
    case APP_SETTING_LINK:
        return is_link_value(value);
    case APP_SETTING_CHOICE: {
        const json_t *option;
        json_for_each(option, json_get(setting, "options"))
        {
            if (strcmp(option->text, value) == 0) {
                return true;
            }
        }
        return false;
    }
    default:
        return true;
    }
}

/* A value ends up in a URL or in a header of a request: a line break in
 * it would be a header of the user's own making. Version 1 is ASCII. */
static bool is_printable(const char *text)
{
    for (const char *character = text; *character != '\0'; character++) {
        if (*character < ' ' || *character > '~') {
            return false;
        }
    }
    return true;
}

bool app_manifest_accepts_setting(const char *json, const char *key, const char *value,
                                  bool *is_secret)
{
    json_t *root = json_parse(json);
    bool   accepted = false;
    const json_t *setting;
    json_for_each(setting, json_get(root, "settings"))
    {
        int type = setting_type(setting);
        if (type < 0 ||
            strcmp(json_get(setting, "key")->text, key) != 0) {
            continue;
        }
        accepted = strlen(value) <= APP_SETTING_VALUE_MAX && is_printable(value) &&
                   fits_type(setting, type, value);
        if (is_secret != NULL) {
            *is_secret = type == APP_SETTING_SECRET;
        }
        break;
    }
    json_free(root);
    return accepted;
}

bool app_manifest_is_server_setting(const char *json, const char *key)
{
    json_t *root = json_parse(json);
    bool   is_server = false;
    const json_t *setting;
    json_for_each(setting, json_get(root, "settings"))
    {
        const json_t *named = json_get(setting, "key");
        if (json_is_string(named) && strcmp(named->text, key) == 0) {
            is_server = setting_type(setting) == APP_SETTING_URL ||
                        setting_type(setting) == APP_SETTING_LINK;
            break;
        }
    }
    json_free(root);
    return is_server;
}

static bool are_sound_hosts(const json_t *hosts, const json_t *settings)
{
    if (hosts == NULL) {
        return true;
    }
    if (!json_is_array(hosts) || json_count(hosts) > APP_HOSTS_MAX) {
        return false;
    }
    const json_t *host;
    json_for_each(host, hosts)
    {
        if (!is_text_of(host, 1, APP_HOST_MAX)) {
            return false;
        }
        /* "{{server}}": wherever the user said the server is. */
        const char *rest;
        if (starts_with(host->text, "{{") &&
            !(starts_with_server_setting(settings, host->text, &rest) && rest[0] == '\0')) {
            return false;
        }
    }
    return true;
}

static char lower(char character)
{
    return character >= 'A' && character <= 'Z' ? (char)(character + ('a' - 'A')) : character;
}

/* A host as a URL or a manifest names it: the name, and the port when one
 * is written. */
typedef struct {
    const char *name;
    size_t      name_length;
    int         port; /* NO_PORT: none written */
} host_t;

#define NO_PORT    0
#define HTTP_PORT  80
#define HTTPS_PORT 443

/* "name" or "name:port", `length` characters of it. */
static host_t host_of(const char *text, size_t length)
{
    host_t      host = { text, length, NO_PORT };
    const char *colon = memchr(text, ':', length);
    if (colon != NULL) {
        host.name_length = (size_t)(colon - text);
        for (const char *digit = colon + 1; digit < text + length; digit++) {
            host.port = host.port * 10 + (*digit - '0');
        }
    }
    return host;
}

/* The host of an http or https URL, its port the scheme's own when the
 * URL names none; false when the URL has no host to be sure of. */
static bool host_in_url(const char *url, host_t *host)
{
    bool        is_https = starts_with(url, "https://");
    const char *text = is_https                      ? url + sizeof "https://" - 1
                       : starts_with(url, "http://") ? url + sizeof "http://" - 1
                                                     : NULL;
    if (text == NULL) {
        return false;
    }
    /* "user@host" would make the part before the @ look like the host. */
    size_t length = strcspn(text, "/?#");
    if (length == 0 || memchr(text, '@', length) != NULL) {
        return false;
    }
    *host = host_of(text, length);
    if (host->port == NO_PORT) {
        host->port = is_https ? HTTPS_PORT : HTTP_PORT;
    }
    return host->name_length > 0;
}

/* Host names are the same in any case. */
static bool is_same_name(const host_t *host, const host_t *other)
{
    if (host->name_length != other->name_length) {
        return false;
    }
    for (size_t i = 0; i < host->name_length; i++) {
        if (lower(host->name[i]) != lower(other->name[i])) {
            return false;
        }
    }
    return true;
}

static bool is_scheme_port(int port)
{
    return port == HTTP_PORT || port == HTTPS_PORT;
}

bool app_manifest_is_same_host(const char *url, const char *other_url)
{
    host_t host, other;
    if (!host_in_url(url, &host) || !host_in_url(other_url, &other)) {
        return false;
    }
    /* http://name and https://name are one host: a server that sends its
     * visitors to the safe door is not sending them elsewhere. */
    return is_same_name(&host, &other) &&
           (host.port == other.port || (is_scheme_port(host.port) && is_scheme_port(other.port)));
}

/* A server and nothing more: "https://ha.example.org",
 * "http://192.168.1.10:32400". No path, no query, nobody's name and
 * password in front of the host. */
static bool is_server_value(const char *value)
{
    host_t host;
    return host_in_url(value, &host) && strpbrk(strstr(value, "://") + 3, "/?#") == NULL;
}

/* A whole address: http or https, a host to be sure of, and nothing in it
 * that is not a printable character - no space, no line break. */
static bool is_link_value(const char *value)
{
    host_t host;
    if (!host_in_url(value, &host)) {
        return false;
    }
    for (const char *at = value; *at != '\0'; at++) {
        if (*at <= ' ' || *at > '~') {
            return false;
        }
    }
    return true;
}

bool app_manifest_allows_url(const char *json, const char *url,
                             app_manifest_value_fn value_of, void *context)
{
    host_t host;
    if (!host_in_url(url, &host)) {
        return false;
    }
    json_t *root = json_parse(json);
    bool   allowed = false;
    const json_t *listed;
    json_for_each(listed, json_get(root, "hosts"))
    {
        if (!json_is_string(listed)) {
            continue;
        }
        host_t named;
        char   server[APP_SETTING_VALUE_MAX + 1];
        if (starts_with(listed->text, "{{")) {
            /* Wherever the user said the server is: the host of that setting,
             * at the port it has there. */
            char   key[APP_SETTING_KEY_MAX + 1];
            size_t key_length = strlen(listed->text) - 4; /* without the braces */
            if (value_of == NULL || key_length > APP_SETTING_KEY_MAX) {
                continue;
            }
            memcpy(key, listed->text + 2, key_length);
            key[key_length] = '\0';
            if (value_of(context, key, server, sizeof server) && host_in_url(server, &named) &&
                is_same_name(&named, &host) && named.port == host.port) {
                allowed = true;
                break;
            }
            continue;
        }
        /* A name listed without a port is the name at its scheme's own. */
        named = host_of(listed->text, strlen(listed->text));
        bool is_same_port = named.port == NO_PORT ? is_scheme_port(host.port)
                                                  : named.port == host.port;
        if (is_same_name(&named, &host) && is_same_port) {
            allowed = true;
            break;
        }
    }
    json_free(root);
    return allowed;
}

/* The kinds of news an application may ask for, by the names a manifest
 * has for them. */
static const struct {
    const char *name;
    int         intent;
} INTENTS[] = {
    { "time_changed", HOST_INTENT_TIME_CHANGED },
    { "network_up", HOST_INTENT_NETWORK_UP },
    { "started", HOST_INTENT_STARTED },
};

/* A kind the host does not know is a mistake, not something to pass over:
 * the application would wait for news that never comes. */
static bool read_intents(const json_t *intents, uint32_t *asked)
{
    *asked = 0;
    if (intents == NULL) {
        return true;
    }
    if (!json_is_array(intents)) {
        return false;
    }
    const json_t *intent;
    json_for_each(intent, intents)
    {
        bool is_known = false;
        for (size_t i = 0; json_is_string(intent) && i < sizeof INTENTS / sizeof INTENTS[0]; i++) {
            if (strcmp(intent->text, INTENTS[i].name) == 0) {
                *asked |= 1u << INTENTS[i].intent;
                is_known = true;
            }
        }
        if (!is_known) {
            return false;
        }
    }
    return true;
}

static bool read_icon(const json_t *icon, app_manifest_t *manifest)
{
    manifest->has_icon = false;
    if (icon == NULL) {
        return true;
    }
    if (!json_is_array(icon) || json_count(icon) != APP_ICON_CELLS) {
        return false;
    }
    int          row = 0;
    const json_t *line;
    json_for_each(line, icon)
    {
        if (!is_text_of(line, APP_ICON_CELLS, APP_ICON_CELLS)) {
            return false;
        }
        uint16_t cells = 0;
        for (int column = 0; column < APP_ICON_CELLS; column++) {
            char cell = line->text[column];
            if (cell != '#' && cell != '.') {
                return false;
            }
            cells = (uint16_t)(cells << 1 | (cell == '#'));
        }
        manifest->icon[row++] = cells;
    }
    manifest->has_icon = true;
    return true;
}

static app_manifest_result_t read_fields(const json_t *root, app_manifest_t *manifest)
{
    if (!json_is_object(root)) {
        return APP_MANIFEST_NOT_JSON;
    }
    if (!read_string(root, "name", manifest->name, sizeof manifest->name) ||
        !app_manifest_is_valid_name(manifest->name)) {
        return APP_MANIFEST_BAD_NAME;
    }
    if (!is_words_of(json_get(root, "title"), 0, APP_TITLE_MAX) ||
        !read_string(root, "title", manifest->title, sizeof manifest->title) ||
        manifest->title[0] == '\0') {
        return APP_MANIFEST_BAD_TITLE;
    }
    if (!read_string(root, "version", manifest->version, sizeof manifest->version)) {
        return APP_MANIFEST_BAD_FIELD;
    }

    const json_t *companion = json_get(root, "companion");
    if (companion != NULL && !is_companion_url(companion)) {
        return APP_MANIFEST_BAD_FIELD;
    }

    const json_t *fullscreen = json_get(root, "fullscreen");
    if (fullscreen != NULL && !json_is_bool(fullscreen)) {
        return APP_MANIFEST_BAD_FIELD;
    }
    manifest->fullscreen = json_is_true(fullscreen);
    const json_t *system = json_get(root, "system");
    if (system != NULL && !json_is_bool(system)) {
        return APP_MANIFEST_BAD_FIELD;
    }
    manifest->system = json_is_true(system);

    const json_t *abi = json_get(root, "abi");
    if (!json_is_number(abi) || !abi->is_whole || abi->number != HOST_API_VERSION) {
        return APP_MANIFEST_OTHER_ABI;
    }

    if (!read_icon(json_get(root, "icon"), manifest)) {
        return APP_MANIFEST_BAD_FIELD;
    }
    if (!are_sound_hosts(json_get(root, "hosts"),
                         json_get(root, "settings"))) {
        return APP_MANIFEST_BAD_HOSTS;
    }
    if (!are_sound_settings(json_get(root, "settings"))) {
        return APP_MANIFEST_BAD_SETTING;
    }
    if (!read_intents(json_get(root, "intents"), &manifest->intents)) {
        return APP_MANIFEST_BAD_INTENT;
    }

    const json_t *polls = json_get(root, "polls");
    manifest->poll_count = 0;
    if (polls == NULL) {
        return APP_MANIFEST_OK;
    }
    if (!json_is_array(polls) || json_count(polls) > APP_POLLS_MAX) {
        return APP_MANIFEST_BAD_POLL;
    }
    const json_t *poll;
    /* A poll in full is a few kilobytes: not for the stack of whoever reads
     * a manifest. */
    app_poll_t *parsed = malloc(sizeof *parsed);
    if (parsed == NULL) {
        return APP_MANIFEST_BAD_POLL;
    }
    app_manifest_result_t result = APP_MANIFEST_OK;
    json_for_each(poll, polls)
    {
        if (!read_poll(poll, json_get(root, "settings"), parsed)) {
            result = APP_MANIFEST_BAD_POLL;
            break;
        }
        strcpy(manifest->poll_every_key[manifest->poll_count], parsed->every_key);
        manifest->poll_every_s[manifest->poll_count++] = parsed->every_s;
    }
    free(parsed);
    return result;
}

/* A zero written as an escape ends a C string in the middle, and the
 * parser gives no length to notice it by: "good\u0000x" would be read
 * as "good". No manifest needs one, so the text itself is refused. */
#define ESCAPED_ZERO "\\u0000"

app_manifest_result_t app_manifest_parse(const char *json, app_manifest_t *manifest)
{
    if (strstr(json, ESCAPED_ZERO) != NULL) {
        return APP_MANIFEST_BAD_FIELD;
    }
    json_t *root = json_parse(json);
    app_manifest_result_t result = read_fields(root, manifest);
    json_free(root);
    return result;
}

app_manifest_result_t app_manifest_parse_poll(const char *json, int poll, app_poll_t *parsed)
{
    json_t *root = json_parse(json);
    const json_t *polls = json_get(root, "polls");
    /* NULL for a number that is not there, and for no array at all. */
    const json_t *wanted = poll >= 0 ? json_at(polls, poll) : NULL;
    app_manifest_result_t result =
        wanted != NULL &&
                read_poll(wanted, json_get(root, "settings"), parsed)
            ? APP_MANIFEST_OK
            : APP_MANIFEST_BAD_POLL;
    json_free(root);
    return result;
}

const char *app_manifest_result_text(app_manifest_result_t result)
{
    switch (result) {
    case APP_MANIFEST_OK:
        return "ok";
    case APP_MANIFEST_NOT_JSON:
        return "not a JSON object";
    case APP_MANIFEST_BAD_NAME:
        return "\"name\" is missing or is not 1-13 of a-z, 0-9, - and _";
    case APP_MANIFEST_BAD_TITLE:
        return "\"title\" is missing, empty or longer than 23";
    case APP_MANIFEST_BAD_FIELD:
        return "a field has the wrong type or is too long";
    case APP_MANIFEST_BAD_HOSTS:
        return "\"hosts\" must be a list of at most 4 host names";
    case APP_MANIFEST_BAD_SETTING:
        return "\"settings\" must be a list of at most 8, each with a key, a type "
               "(text, secret, number, choice, toggle, place, url, link) and a title; a place names two "
               "number settings as its \"latitude\" and \"longitude\"";
    case APP_MANIFEST_BAD_INTENT:
        return "\"intents\" must be a list of time_changed, network_up, started";
    case APP_MANIFEST_OTHER_ABI:
        return "\"abi\" is missing or is not the one this host speaks";
    case APP_MANIFEST_BAD_POLL:
        return "\"polls\" has more than 4 entries, or one without an http(s) \"url\" and "
               "an \"every\" of 10 seconds to a day";
    }
    return "?";
}

int app_manifest_setting_type(const char *json, const char *key)
{
    json_t *root = json_parse(json);
    int    found = -1;
    const json_t *setting;
    json_for_each(setting, json_get(root, "settings"))
    {
        int type = setting_type(setting);
        if (type >= 0 &&
            strcmp(json_get(setting, "key")->text, key) == 0) {
            found = type;
            break;
        }
    }
    json_free(root);
    return found;
}

int app_manifest_secret_keys(const char *json, char keys[][APP_SETTING_KEY_MAX + 1], int capacity)
{
    json_t *root = json_parse(json);
    int    count = 0;
    const json_t *setting;
    json_for_each(setting, json_get(root, "settings"))
    {
        if (setting_type(setting) == APP_SETTING_SECRET && count < capacity) {
            strcpy(keys[count++], json_get(setting, "key")->text);
        }
    }
    json_free(root);
    return count;
}
