/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_manifest.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
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
static bool read_string(const cJSON *root, const char *field, char *value, size_t value_size)
{
    value[0] = '\0';
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, field);
    if (item == NULL) {
        return true;
    }
    if (!cJSON_IsString(item) || strlen(item->valuestring) >= value_size) {
        return false;
    }
    strcpy(value, item->valuestring);
    return true;
}

static bool starts_with(const char *text, const char *prefix)
{
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

/* The headers of a poll: an object of strings. A poll without the field
 * has none. */
static bool read_headers(const cJSON *headers, app_poll_t *poll)
{
    poll->header_count = 0;
    if (headers == NULL) {
        return true;
    }
    if (!cJSON_IsObject(headers) || cJSON_GetArraySize(headers) > APP_HEADERS_MAX) {
        return false;
    }
    const cJSON *header;
    cJSON_ArrayForEach(header, headers)
    {
        app_header_t *parsed = &poll->headers[poll->header_count];
        if (!cJSON_IsString(header) || header->string[0] == '\0' ||
            strlen(header->string) > APP_HEADER_NAME_MAX ||
            strlen(header->valuestring) > APP_HEADER_VALUE_MAX) {
            return false;
        }
        strcpy(parsed->name, header->string);
        strcpy(parsed->value, header->valuestring);
        poll->header_count++;
    }
    return true;
}

static bool read_pick(const cJSON *pick, app_poll_t *poll)
{
    poll->pick_count = 0;
    if (pick == NULL) {
        return true;
    }
    if (!cJSON_IsArray(pick) || cJSON_GetArraySize(pick) > JSON_PICK_PATHS_MAX) {
        return false;
    }
    const cJSON *path;
    cJSON_ArrayForEach(path, pick)
    {
        if (!cJSON_IsString(path) || path->valuestring[0] == '\0' ||
            strlen(path->valuestring) > JSON_PICK_PATH_MAX) {
            return false;
        }
        strcpy(poll->pick[poll->pick_count++], path->valuestring);
    }
    return true;
}

static bool read_poll(const cJSON *poll, app_poll_t *parsed)
{
    if (!cJSON_IsObject(poll)) {
        return false;
    }
    if (!read_string(poll, "url", parsed->url, sizeof parsed->url) ||
        !(starts_with(parsed->url, "http://") || starts_with(parsed->url, "https://"))) {
        return false;
    }
    const cJSON *every = cJSON_GetObjectItemCaseSensitive(poll, "every");
    if (!cJSON_IsNumber(every) || every->valuedouble < APP_POLL_EVERY_MIN_S ||
        every->valuedouble > APP_POLL_EVERY_MAX_S) {
        return false;
    }
    parsed->every_s = (uint32_t)every->valuedouble;
    return read_headers(cJSON_GetObjectItemCaseSensitive(poll, "headers"), parsed) &&
           read_pick(cJSON_GetObjectItemCaseSensitive(poll, "pick"), parsed);
}

static const char *const SETTING_TYPES[] = {
    [APP_SETTING_TEXT] = "text",     [APP_SETTING_SECRET] = "secret",
    [APP_SETTING_NUMBER] = "number", [APP_SETTING_CHOICE] = "choice",
    [APP_SETTING_TOGGLE] = "toggle", [APP_SETTING_PLACE] = "place",
};
#define SETTING_TYPE_COUNT ((int)(sizeof SETTING_TYPES / sizeof SETTING_TYPES[0]))

static bool is_text_of(const cJSON *item, size_t min_length, size_t max_length)
{
    return cJSON_IsString(item) && strlen(item->valuestring) >= min_length &&
           strlen(item->valuestring) <= max_length;
}

/* A kv key: what kv_get and kv_set take. */
static bool is_setting_key(const cJSON *key)
{
    if (!is_text_of(key, 1, APP_SETTING_KEY_MAX)) {
        return false;
    }
    for (const char *character = key->valuestring; *character != '\0'; character++) {
        if (*character < '!' || *character > '~') {
            return false;
        }
    }
    return true;
}

/* The type of a setting, or -1 when it is not a sound one. */
static int setting_type(const cJSON *setting)
{
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(setting, "type");
    const cJSON *help = cJSON_GetObjectItemCaseSensitive(setting, "help");
    if (!cJSON_IsObject(setting) || !cJSON_IsString(type) ||
        !is_setting_key(cJSON_GetObjectItemCaseSensitive(setting, "key")) ||
        !is_text_of(cJSON_GetObjectItemCaseSensitive(setting, "title"), 1,
                    APP_SETTING_TITLE_MAX) ||
        (help != NULL && !is_text_of(help, 0, APP_SETTING_HELP_MAX))) {
        return -1;
    }
    for (int known = 0; known < SETTING_TYPE_COUNT; known++) {
        if (strcmp(type->valuestring, SETTING_TYPES[known]) != 0) {
            continue;
        }
        const cJSON *options = cJSON_GetObjectItemCaseSensitive(setting, "options");
        if (known == APP_SETTING_PLACE &&
            (!is_setting_key(cJSON_GetObjectItemCaseSensitive(setting, "latitude")) ||
             !is_setting_key(cJSON_GetObjectItemCaseSensitive(setting, "longitude")))) {
            return -1;
        }
        if (known != APP_SETTING_CHOICE) {
            return options == NULL ? known : -1;
        }
        int count = cJSON_IsArray(options) ? cJSON_GetArraySize(options) : 0;
        if (count < 1 || count > APP_SETTING_OPTIONS_MAX) {
            return -1;
        }
        const cJSON *option;
        cJSON_ArrayForEach(option, options)
        {
            if (!is_text_of(option, 1, APP_SETTING_OPTION_MAX)) {
                return -1;
            }
        }
        return known;
    }
    return -1;
}

static bool fits_type(const cJSON *setting, int type, const char *value);
static bool is_printable(const char *text);

/* What the page shows while nothing is set; the application has to mean
 * the same when it finds nothing in its store. A secret has none, and a
 * place is not a place without its numbers. */
static bool has_sound_default(const cJSON *setting, int type)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(setting, "default");
    if (value == NULL) {
        return true;
    }
    return type != APP_SETTING_SECRET && type != APP_SETTING_PLACE &&
           is_text_of(value, 1, APP_SETTING_VALUE_MAX) && is_printable(value->valuestring) &&
           fits_type(setting, type, value->valuestring);
}

/* Whether the settings have a number under the key that `named` holds. */
static bool has_number_setting(const cJSON *settings, const cJSON *named)
{
    const cJSON *setting;
    cJSON_ArrayForEach(setting, settings)
    {
        if (setting_type(setting) == APP_SETTING_NUMBER &&
            strcmp(cJSON_GetObjectItemCaseSensitive(setting, "key")->valuestring,
                   named->valuestring) == 0) {
            return true;
        }
    }
    return false;
}

/* A place fills two number settings: they have to be there, and be two. */
static bool is_sound_place(const cJSON *settings, const cJSON *place)
{
    const cJSON *latitude = cJSON_GetObjectItemCaseSensitive(place, "latitude");
    const cJSON *longitude = cJSON_GetObjectItemCaseSensitive(place, "longitude");
    return strcmp(latitude->valuestring, longitude->valuestring) != 0 &&
           has_number_setting(settings, latitude) && has_number_setting(settings, longitude);
}

static bool are_sound_settings(const cJSON *settings)
{
    if (settings == NULL) {
        return true;
    }
    if (!cJSON_IsArray(settings) || cJSON_GetArraySize(settings) > APP_SETTINGS_MAX) {
        return false;
    }
    const cJSON *setting;
    cJSON_ArrayForEach(setting, settings)
    {
        int type = setting_type(setting);
        if (type < 0 || (type == APP_SETTING_PLACE && !is_sound_place(settings, setting)) ||
            !has_sound_default(setting, type)) {
            return false;
        }
        /* Two settings under one key would be one value with two forms. */
        const char *key = cJSON_GetObjectItemCaseSensitive(setting, "key")->valuestring;
        for (const cJSON *earlier = settings->child; earlier != setting; earlier = earlier->next) {
            if (strcmp(cJSON_GetObjectItemCaseSensitive(earlier, "key")->valuestring, key) == 0) {
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

static bool fits_type(const cJSON *setting, int type, const char *value)
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
    case APP_SETTING_CHOICE: {
        const cJSON *option;
        cJSON_ArrayForEach(option, cJSON_GetObjectItemCaseSensitive(setting, "options"))
        {
            if (strcmp(option->valuestring, value) == 0) {
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
    cJSON *root = cJSON_Parse(json);
    bool   accepted = false;
    const cJSON *setting;
    cJSON_ArrayForEach(setting, cJSON_GetObjectItemCaseSensitive(root, "settings"))
    {
        int type = setting_type(setting);
        if (type < 0 ||
            strcmp(cJSON_GetObjectItemCaseSensitive(setting, "key")->valuestring, key) != 0) {
            continue;
        }
        accepted = strlen(value) <= APP_SETTING_VALUE_MAX && is_printable(value) &&
                   fits_type(setting, type, value);
        if (is_secret != NULL) {
            *is_secret = type == APP_SETTING_SECRET;
        }
        break;
    }
    cJSON_Delete(root);
    return accepted;
}

static bool are_sound_hosts(const cJSON *hosts)
{
    if (hosts == NULL) {
        return true;
    }
    if (!cJSON_IsArray(hosts) || cJSON_GetArraySize(hosts) > APP_HOSTS_MAX) {
        return false;
    }
    const cJSON *host;
    cJSON_ArrayForEach(host, hosts)
    {
        if (!is_text_of(host, 1, APP_HOST_MAX)) {
            return false;
        }
    }
    return true;
}

static char lower(char character)
{
    return character >= 'A' && character <= 'Z' ? (char)(character + ('a' - 'A')) : character;
}

/* Host names are the same in any case. */
static bool is_same_host(const char *host, const char *other, size_t other_length)
{
    if (strlen(host) != other_length) {
        return false;
    }
    for (size_t i = 0; i < other_length; i++) {
        if (lower(host[i]) != lower(other[i])) {
            return false;
        }
    }
    return true;
}

bool app_manifest_allows_url(const char *json, const char *url)
{
    const char *host = starts_with(url, "http://")    ? url + sizeof "http://" - 1
                       : starts_with(url, "https://") ? url + sizeof "https://" - 1
                                                      : NULL;
    if (host == NULL) {
        return false;
    }
    /* "user@host" would make the part before the @ look like the host. */
    size_t host_length = strcspn(host, "/?#");
    if (host_length == 0 || memchr(host, '@', host_length) != NULL) {
        return false;
    }
    cJSON *root = cJSON_Parse(json);
    bool   allowed = false;
    const cJSON *listed;
    cJSON_ArrayForEach(listed, cJSON_GetObjectItemCaseSensitive(root, "hosts"))
    {
        if (cJSON_IsString(listed) && is_same_host(listed->valuestring, host, host_length)) {
            allowed = true;
            break;
        }
    }
    cJSON_Delete(root);
    return allowed;
}

static bool read_icon(const cJSON *icon, app_manifest_t *manifest)
{
    manifest->has_icon = false;
    if (icon == NULL) {
        return true;
    }
    if (!cJSON_IsArray(icon) || cJSON_GetArraySize(icon) != APP_ICON_CELLS) {
        return false;
    }
    int          row = 0;
    const cJSON *line;
    cJSON_ArrayForEach(line, icon)
    {
        if (!is_text_of(line, APP_ICON_CELLS, APP_ICON_CELLS)) {
            return false;
        }
        uint16_t cells = 0;
        for (int column = 0; column < APP_ICON_CELLS; column++) {
            char cell = line->valuestring[column];
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

static app_manifest_result_t read_fields(const cJSON *root, app_manifest_t *manifest)
{
    if (!cJSON_IsObject(root)) {
        return APP_MANIFEST_NOT_JSON;
    }
    if (!read_string(root, "name", manifest->name, sizeof manifest->name) ||
        !app_manifest_is_valid_name(manifest->name)) {
        return APP_MANIFEST_BAD_NAME;
    }
    if (!read_string(root, "title", manifest->title, sizeof manifest->title) ||
        manifest->title[0] == '\0') {
        return APP_MANIFEST_BAD_TITLE;
    }
    if (!read_string(root, "version", manifest->version, sizeof manifest->version)) {
        return APP_MANIFEST_BAD_FIELD;
    }

    const cJSON *fullscreen = cJSON_GetObjectItemCaseSensitive(root, "fullscreen");
    if (fullscreen != NULL && !cJSON_IsBool(fullscreen)) {
        return APP_MANIFEST_BAD_FIELD;
    }
    manifest->fullscreen = cJSON_IsTrue(fullscreen);

    const cJSON *abi = cJSON_GetObjectItemCaseSensitive(root, "abi");
    if (!cJSON_IsNumber(abi) || abi->valuedouble != HOST_API_VERSION) {
        return APP_MANIFEST_OTHER_ABI;
    }

    if (!read_icon(cJSON_GetObjectItemCaseSensitive(root, "icon"), manifest)) {
        return APP_MANIFEST_BAD_FIELD;
    }
    if (!are_sound_hosts(cJSON_GetObjectItemCaseSensitive(root, "hosts"))) {
        return APP_MANIFEST_BAD_HOSTS;
    }
    if (!are_sound_settings(cJSON_GetObjectItemCaseSensitive(root, "settings"))) {
        return APP_MANIFEST_BAD_SETTING;
    }

    const cJSON *polls = cJSON_GetObjectItemCaseSensitive(root, "polls");
    manifest->poll_count = 0;
    if (polls == NULL) {
        return APP_MANIFEST_OK;
    }
    if (!cJSON_IsArray(polls) || cJSON_GetArraySize(polls) > APP_POLLS_MAX) {
        return APP_MANIFEST_BAD_POLL;
    }
    const cJSON *poll;
    /* A poll in full is a few kilobytes: not for the stack of whoever reads
     * a manifest. */
    app_poll_t *parsed = malloc(sizeof *parsed);
    if (parsed == NULL) {
        return APP_MANIFEST_BAD_POLL;
    }
    app_manifest_result_t result = APP_MANIFEST_OK;
    cJSON_ArrayForEach(poll, polls)
    {
        if (!read_poll(poll, parsed)) {
            result = APP_MANIFEST_BAD_POLL;
            break;
        }
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
    cJSON *root = cJSON_Parse(json);
    app_manifest_result_t result = read_fields(root, manifest);
    cJSON_Delete(root);
    return result;
}

app_manifest_result_t app_manifest_parse_poll(const char *json, int poll, app_poll_t *parsed)
{
    cJSON *root = cJSON_Parse(json);
    const cJSON *polls = cJSON_GetObjectItemCaseSensitive(root, "polls");
    /* NULL for a number that is not there, and for no array at all. */
    const cJSON *wanted = poll >= 0 ? cJSON_GetArrayItem(polls, poll) : NULL;
    app_manifest_result_t result =
        wanted != NULL && read_poll(wanted, parsed) ? APP_MANIFEST_OK : APP_MANIFEST_BAD_POLL;
    cJSON_Delete(root);
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
               "(text, secret, number, choice, toggle, place) and a title; a place names two "
               "number settings as its \"latitude\" and \"longitude\"";
    case APP_MANIFEST_OTHER_ABI:
        return "\"abi\" is missing or is not the one this host speaks";
    case APP_MANIFEST_BAD_POLL:
        return "\"polls\" has more than 4 entries, or one without an http(s) \"url\" and "
               "an \"every\" of 10 seconds to a day";
    }
    return "?";
}
