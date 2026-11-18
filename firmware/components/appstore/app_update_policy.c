/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_update_policy.h"

#include <string.h>

#include "app_manifest.h"
#include "json_tree.h"

static bool stable_version(const char *text, uint32_t *number)
{
    if (text == NULL || !app_version_number(text, number)) {
        return false;
    }
    const char *part = text;
    for (const char *cursor = text;; cursor++) {
        if (*cursor == '.' || *cursor == '\0') {
            if (cursor - part > 1 && *part == '0') {
                return false;
            }
            part = cursor + 1;
        } else if (*cursor < '0' || *cursor > '9') {
            return false;
        }
        if (*cursor == '\0') {
            return true;
        }
    }
}

bool app_update_version_is_newer(const char *installed, const char *candidate)
{
    uint32_t old_version, next_version;
    return stable_version(installed, &old_version) && stable_version(candidate, &next_version) &&
           next_version > old_version;
}

static bool equal_value(const json_t *left, const json_t *right)
{
    if (left == NULL || right == NULL) {
        return left == right;
    }
    if (left->type != right->type) {
        return false;
    }
    if (left->type == JSON_STRING) {
        return strcmp(left->text, right->text) == 0;
    }
    if (left->type == JSON_NUMBER) {
        return left->number == right->number && left->is_whole == right->is_whole;
    }
    if (json_count(left) != json_count(right)) {
        return false;
    }
    const json_t *item;
    int index = 0;
    json_for_each(item, left) {
        const json_t *other = left->type == JSON_OBJECT ? json_get(right, item->name)
                                                       : json_at(right, index++);
        if (!equal_value(item, other)) {
            return false;
        }
    }
    return true;
}

static bool contains_value(const json_t *array, const json_t *value)
{
    const json_t *item;
    json_for_each(item, array) {
        if (equal_value(item, value)) {
            return true;
        }
    }
    return false;
}

static bool subset(const json_t *installed, const json_t *candidate, const char *field)
{
    const json_t *item;
    json_for_each(item, json_get(candidate, field)) {
        if (!contains_value(json_get(installed, field), item)) {
            return false;
        }
    }
    return true;
}

static const json_t *setting(const json_t *root, const char *key)
{
    const json_t *item;
    json_for_each(item, json_get(root, "settings")) {
        const json_t *name = json_get(item, "key");
        if (json_is_string(name) && strcmp(name->text, key) == 0) {
            return item;
        }
    }
    return NULL;
}

static bool same_setting(const json_t *installed, const json_t *candidate, const char *key)
{
    const json_t *old = setting(installed, key);
    const json_t *next = setting(candidate, key);
    return equal_value(json_get(old, "type"), json_get(next, "type")) &&
           equal_value(json_get(old, "default"), json_get(next, "default"));
}

static bool same_address_settings(const json_t *installed, const json_t *candidate,
                                  const char *address)
{
    const char *cursor = address;
    while ((cursor = strstr(cursor, "{{")) != NULL) {
        cursor += 2;
        const char *end = strstr(cursor, "}}");
        if (end == NULL || end - cursor > APP_SETTING_KEY_MAX) {
            return false;
        }
        char key[APP_SETTING_KEY_MAX + 1];
        memcpy(key, cursor, (size_t)(end - cursor));
        key[end - cursor] = '\0';
        if (!same_setting(installed, candidate, key)) {
            return false;
        }
        cursor = end + 2;
    }
    return true;
}

static bool same_destinations(const json_t *installed, const json_t *candidate)
{
    if (!subset(installed, candidate, "hosts")) {
        return false;
    }
    const json_t *companion = json_get(candidate, "companion");
    if (companion != NULL && !equal_value(companion, json_get(installed, "companion"))) {
        return false;
    }
    const json_t *item;
    json_for_each(item, json_get(candidate, "hosts")) {
        if (!same_address_settings(installed, candidate, item->text)) {
            return false;
        }
    }
    json_for_each(item, json_get(candidate, "polls")) {
        const json_t *url = json_get(item, "url");
        bool found = false;
        const json_t *old;
        json_for_each(old, json_get(installed, "polls")) {
            if (equal_value(url, json_get(old, "url"))) {
                found = true;
                break;
            }
        }
        if (!found || !same_address_settings(installed, candidate, url->text)) {
            return false;
        }
    }
    /* URL/link settings are read-only to the application. Changing their
     * types would let updated code overwrite a destination the user chose. */
    json_for_each(item, json_get(installed, "settings")) {
        const json_t *type = json_get(item, "type");
        if (json_is_string(type) &&
            (strcmp(type->text, "url") == 0 || strcmp(type->text, "link") == 0)) {
            const char *key = json_get(item, "key")->text;
            if (!same_setting(installed, candidate, key)) {
                return false;
            }
        }
    }
    return true;
}

bool app_update_policy_allows(const char *installed_json, const char *candidate_json,
                              const char *candidate_version, const char *firmware_version)
{
    app_manifest_t installed, candidate;
    uint32_t firmware;
    if (installed_json == NULL || candidate_json == NULL || candidate_version == NULL ||
        firmware_version == NULL ||
        app_manifest_parse(installed_json, &installed) != APP_MANIFEST_OK ||
        app_manifest_parse(candidate_json, &candidate) != APP_MANIFEST_OK ||
        strcmp(installed.name, candidate.name) != 0 ||
        strcmp(candidate.version, candidate_version) != 0 ||
        !app_update_version_is_newer(installed.version, candidate.version) ||
        !app_version_number(firmware_version, &firmware) || candidate.firmware > firmware ||
        (candidate.system && !installed.system) ||
        (candidate.writes_files && !installed.writes_files)) {
        return false;
    }
    json_t *old = json_parse(installed_json);
    json_t *next = json_parse(candidate_json);
    bool allowed = old != NULL && next != NULL && subset(old, next, "certificates") &&
                   same_destinations(old, next);
    json_free(old);
    json_free(next);
    return allowed;
}
