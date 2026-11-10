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

static bool is_valid_name(const char *name)
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

static app_manifest_result_t read_fields(const cJSON *root, app_manifest_t *manifest)
{
    if (!cJSON_IsObject(root)) {
        return APP_MANIFEST_NOT_JSON;
    }
    if (!read_string(root, "name", manifest->name, sizeof manifest->name) ||
        !is_valid_name(manifest->name)) {
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
    }
    return "?";
}
