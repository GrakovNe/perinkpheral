/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_manifest.h"

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
    return read_headers(cJSON_GetObjectItemCaseSensitive(poll, "headers"), parsed);
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

    const cJSON *polls = cJSON_GetObjectItemCaseSensitive(root, "polls");
    manifest->poll_count = 0;
    if (polls == NULL) {
        return APP_MANIFEST_OK;
    }
    if (!cJSON_IsArray(polls) || cJSON_GetArraySize(polls) > APP_POLLS_MAX) {
        return APP_MANIFEST_BAD_POLL;
    }
    const cJSON *poll;
    cJSON_ArrayForEach(poll, polls)
    {
        app_poll_t parsed;
        if (!read_poll(poll, &parsed)) {
            return APP_MANIFEST_BAD_POLL;
        }
        manifest->poll_every_s[manifest->poll_count++] = parsed.every_s;
    }
    return APP_MANIFEST_OK;
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
    case APP_MANIFEST_OTHER_ABI:
        return "\"abi\" is missing or is not the one this host speaks";
    case APP_MANIFEST_BAD_POLL:
        return "\"polls\" has more than 4 entries, or one without an http(s) \"url\" and "
               "an \"every\" of 10 seconds to a day";
    }
    return "?";
}
