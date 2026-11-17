/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "appconf.h"

#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define PROJECT_SLUG "perinkpheral"
#define PROJECT_NAME "Perinkpheral"

const appconf_language_t *appconf_languages(int *count)
{
    static const appconf_language_t LANGUAGES[] = {
        { "en", "English" },   { "ru", "Russian" },    { "uk", "Ukrainian" }, { "be", "Belarusian" },
        { "sr", "Serbian" },   { "bg", "Bulgarian" },  { "kk", "Kazakh" },    { "el", "Greek" },
        { "de", "German" },    { "fr", "French" },     { "es", "Spanish" },   { "it", "Italian" },
        { "pt", "Portuguese" }, { "nl", "Dutch" },     { "pl", "Polish" },    { "cs", "Czech" },
        { "sk", "Slovak" },    { "hu", "Hungarian" },  { "ro", "Romanian" },  { "tr", "Turkish" },
        { "sv", "Swedish" },   { "da", "Danish" },     { "no", "Norwegian" }, { "fi", "Finnish" },
        { "lt", "Lithuanian" }, { "lv", "Latvian" },   { "et", "Estonian" },
    };
    *count = (int)(sizeof LANGUAGES / sizeof LANGUAGES[0]);
    return LANGUAGES;
}

static bool str_fits(const char *s, size_t cap)
{
    return s != NULL && strnlen(s, cap + 1) <= cap;
}

#define ASCII_SPACE          0x20 /* the first printable character */
#define ASCII_LAST_PRINTABLE 0x7e
#define ASCII_DELETE         0x7f
#define WPA_PASSWORD_MIN     8    /* characters: the standard's own least */

static bool has_ctrl(const char *s)
{
    for (const char *p = s; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < ASCII_SPACE || c == ASCII_DELETE) {
            return true;
        }
    }
    return false;
}

static bool is_printable_ascii(const char *s)
{
    for (const char *p = s; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < ASCII_SPACE || c > ASCII_LAST_PRINTABLE) {
            return false;
        }
    }
    return true;
}

static bool host_chars_ok(const char *s)
{
    for (const char *p = s; *p != '\0'; p++) {
        char c = *p;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '-' || c == ':';
        if (!ok) {
            return false;
        }
    }
    return true;
}

static bool tz_ok(const char *s)
{
    if (s[0] == '\0') {
        return false;
    }
    for (const char *p = s; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (c <= ASCII_SPACE || c > ASCII_LAST_PRINTABLE) {
            return false;
        }
    }
    return true;
}

void appconf_device_id(const uint8_t mac[APPCONF_MAC_BYTES], char out[APPCONF_DEVICE_ID_SIZE])
{
    snprintf(out, APPCONF_DEVICE_ID_SIZE, "%02x%02x", mac[APPCONF_MAC_BYTES - 2],
             mac[APPCONF_MAC_BYTES - 1]);
}

void appconf_hostname(const char *device_name, const char device_id[APPCONF_DEVICE_ID_SIZE],
                      char *out, size_t out_sz)
{
    if (out_sz == 0) {
        return;
    }
    size_t o = 0;
    bool pending_dash = false;
    for (const char *p = device_name; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        char keep = 0;
        if (c >= 'A' && c <= 'Z') {
            keep = (char)(c - 'A' + 'a');
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            keep = (char)c;
        }
        if (keep != 0) {
            if (pending_dash && o > 0 && o + 2 <= out_sz) {
                out[o++] = '-';
            }
            if (o + 2 <= out_sz) {
                out[o++] = keep;
            }
            pending_dash = false;
        } else {
            pending_dash = true;
        }
    }
    while (o > 0 && out[o - 1] == '-') {
        o--;
    }
    out[o] = '\0';
    if (o == 0) {
        snprintf(out, out_sz, PROJECT_SLUG "-%s", device_id);
    }
}

void appconf_default_name(const char device_id[APPCONF_DEVICE_ID_SIZE], char *out, size_t out_sz)
{
    char id_upper[APPCONF_DEVICE_ID_SIZE] = { 0 };
    for (int i = 0; i < APPCONF_DEVICE_ID_SIZE - 1; i++) {
        id_upper[i] = (char)toupper((unsigned char)device_id[i]);
    }
    snprintf(out, out_sz, PROJECT_NAME "-%s", id_upper);
}

void appconf_defaults(appconf_t *c, const char device_id[APPCONF_DEVICE_ID_SIZE])
{
    memset(c, 0, sizeof *c);
    appconf_default_name(device_id, c->device_name, sizeof c->device_name);
    snprintf(c->ntp_server, sizeof c->ntp_server, "%s", APPCONF_NTP_DEFAULT);
    snprintf(c->tz, sizeof c->tz, "%s", APPCONF_TZ_DEFAULT);
    c->refresh_min_data_s = APPCONF_REFRESH_MIN_DATA_S_DEFAULT;
    c->refresh_min_button_ms = APPCONF_REFRESH_MIN_BUTTON_MS_DEFAULT;
    c->refresh_full_every = APPCONF_REFRESH_FULL_EVERY_DEFAULT;
    c->dither = 1;
    c->time_auto = 1;
    c->refresh_keepalive_h = APPCONF_REFRESH_KEEPALIVE_H_DEFAULT;
    c->menu_idle_min = APPCONF_MENU_IDLE_MIN_DEFAULT;
    strcpy(c->language, "en");
    appconf_derive(c, device_id);
}

void appconf_derive(appconf_t *c, const char device_id[APPCONF_DEVICE_ID_SIZE])
{
    appconf_hostname(c->device_name, device_id, c->hostname, sizeof c->hostname);
}

/* The characters an application's name is made of. */
static bool is_app_name_or_empty(const char *name)
{
    for (; *name != '\0'; name++) {
        bool is_name_char = (*name >= 'a' && *name <= 'z') || (*name >= '0' && *name <= '9') ||
                            *name == '-' || *name == '_';
        if (!is_name_char) {
            return false;
        }
    }
    return true;
}

bool appconf_validate(const appconf_t *c, char *err, size_t err_sz)
{
#define BAD(...)                                \
    do {                                        \
        snprintf(err, err_sz, __VA_ARGS__);     \
        return false;                           \
    } while (0)

    if (!str_fits(c->device_name, APPCONF_NAME_MAX)) {
        BAD("Device name is too long (max %d).", APPCONF_NAME_MAX);
    }
    if (c->device_name[0] == '\0') {
        BAD("Device name is empty.");
    }
    if (has_ctrl(c->device_name)) {
        BAD("Device name has control characters.");
    }

    if (!str_fits(c->wifi_ssid, APPCONF_SSID_MAX)) {
        BAD("Wi-Fi SSID is too long (max %d).", APPCONF_SSID_MAX);
    }
    if (has_ctrl(c->wifi_ssid)) {
        BAD("Wi-Fi SSID has control characters.");
    }
    if (!str_fits(c->wifi_password, APPCONF_PASS_MAX)) {
        BAD("Wi-Fi password is too long (max %d).", APPCONF_PASS_MAX);
    }
    if (!is_printable_ascii(c->wifi_password)) {
        BAD("Wi-Fi password must be printable ASCII.");
    }
    if (c->wifi_ssid[0] == '\0' && c->wifi_password[0] != '\0') {
        BAD("Wi-Fi password without an SSID.");
    }
    size_t pass_len = strlen(c->wifi_password);
    if (c->wifi_ssid[0] != '\0' && pass_len > 0 && pass_len < WPA_PASSWORD_MIN) {
        BAD("Wi-Fi password must be at least 8 characters.");
    }

    if (!str_fits(c->ntp_server, APPCONF_HOST_MAX)) {
        BAD("NTP server is too long (max %d).", APPCONF_HOST_MAX);
    }
    if (c->ntp_server[0] == '\0') {
        BAD("NTP server is empty.");
    }
    if (!host_chars_ok(c->ntp_server)) {
        BAD("NTP server has invalid characters.");
    }

    if (!str_fits(c->tz, APPCONF_TZ_MAX) || !tz_ok(c->tz)) {
        BAD("Time zone must be a POSIX TZ string (max %d).", APPCONF_TZ_MAX);
    }
    if (!str_fits(c->tz_city, APPCONF_CITY_MAX) || !is_printable_ascii(c->tz_city)) {
        BAD("Invalid city.");
    }

    if (c->refresh_min_data_s < APPCONF_REFRESH_MIN_DATA_S_LEAST ||
        c->refresh_min_data_s > APPCONF_REFRESH_MIN_DATA_S_MOST) {
        BAD("Update interval must be %d-%d s.", APPCONF_REFRESH_MIN_DATA_S_LEAST,
            APPCONF_REFRESH_MIN_DATA_S_MOST);
    }
    if (c->refresh_min_button_ms > APPCONF_REFRESH_MIN_BUTTON_MS_MOST) {
        BAD("Button delay must be at most %d ms.",
            APPCONF_REFRESH_MIN_BUTTON_MS_MOST);
    }
    if (c->refresh_full_every > APPCONF_REFRESH_FULL_EVERY_MOST &&
        c->refresh_full_every != APPCONF_REFRESH_FULL_NEVER) {
        BAD("Full refresh must be %d-%d, or %d for never.",
            APPCONF_REFRESH_FULL_EVERY_LEAST, APPCONF_REFRESH_FULL_EVERY_MOST,
            APPCONF_REFRESH_FULL_NEVER);
    }
    if (c->refresh_fast > 1) {
        BAD("Fast refresh must be 0 or 1.");
    }
    if (c->header_clock > 1) {
        BAD("Clock in header must be 0 or 1.");
    }
    {
        int  count;
        bool is_known = false;
        const appconf_language_t *languages = appconf_languages(&count);
        for (int i = 0; i < count && !is_known; i++) {
            is_known = strcmp(languages[i].code, c->language) == 0;
        }
        if (!is_known) {
            BAD("Unknown language.");
        }
    }
    if (c->font >= APPCONF_FONT_COUNT) {
        BAD("Unknown font.");
    }
    if (c->dither > 1) {
        BAD("Dithering must be 0 or 1.");
    }
    if (c->time_auto > 1) {
        BAD("Automatic time must be 0 or 1.");
    }
    if (c->power_saving > APPCONF_POWER_AUTO) {
        BAD("Power saving must be 0, 1 or 2.");
    }
    if (!str_fits(c->default_app, APPCONF_APP_NAME_MAX) || !is_app_name_or_empty(c->default_app)) {
        BAD("Invalid default app.");
    }
    if (c->menu_idle_min < APPCONF_MENU_IDLE_MIN_LEAST ||
        c->menu_idle_min > APPCONF_MENU_IDLE_MIN_MOST) {
        BAD("Idle timeout must be %d-%d min.", APPCONF_MENU_IDLE_MIN_LEAST,
            APPCONF_MENU_IDLE_MIN_MOST);
    }
    if (c->refresh_keepalive_h < APPCONF_REFRESH_KEEPALIVE_H_LEAST ||
        c->refresh_keepalive_h > APPCONF_REFRESH_KEEPALIVE_H_MOST) {
        BAD("Keep-alive must be %d-%d h.", APPCONF_REFRESH_KEEPALIVE_H_LEAST,
            APPCONF_REFRESH_KEEPALIVE_H_MOST);
    }

    return true;
#undef BAD
}

/* ---- the settings by key ---- */

#define TEXT(name, store, field, flags)                                                   \
    { name, store, APPCONF_TEXT, flags, offsetof(appconf_t, field),                       \
      sizeof(((appconf_t *)0)->field), 0, 0, APPCONF_NO_ALSO }
#define NUMBER(name, store, field, least, most, also)                                     \
    { name, store, APPCONF_NUMBER, 0, offsetof(appconf_t, field),                         \
      sizeof(((appconf_t *)0)->field), least, most, also }
#define FLAG(name, store, field)                                                          \
    { name, store, APPCONF_FLAG, 0, offsetof(appconf_t, field),                           \
      sizeof(((appconf_t *)0)->field), 0, 1, APPCONF_NO_ALSO }

static const appconf_field_t FIELDS[] = {
    TEXT("device_name", "name", device_name, APPCONF_RESTART),
    TEXT("wifi_ssid", "wssid", wifi_ssid, APPCONF_RESTART | APPCONF_OWN_WAY),
    TEXT("wifi_password", "wpass", wifi_password, APPCONF_RESTART | APPCONF_OWN_WAY | APPCONF_SECRET),
    TEXT("ntp_server", "ntp", ntp_server, APPCONF_RESTART),
    TEXT("tz", "tz", tz, 0),
    TEXT("tz_city", "tz_city", tz_city, 0),
    NUMBER("refresh_min_data_s", "rf_data", refresh_min_data_s, APPCONF_REFRESH_MIN_DATA_S_LEAST,
           APPCONF_REFRESH_MIN_DATA_S_MOST, APPCONF_NO_ALSO),
    NUMBER("refresh_min_button_ms", "rf_button", refresh_min_button_ms, 0,
           APPCONF_REFRESH_MIN_BUTTON_MS_MOST, APPCONF_NO_ALSO),
    NUMBER("refresh_full_every", "rf_full", refresh_full_every, APPCONF_REFRESH_FULL_EVERY_LEAST,
           APPCONF_REFRESH_FULL_EVERY_MOST, APPCONF_REFRESH_FULL_NEVER),
    NUMBER("refresh_keepalive_h", "rf_keep", refresh_keepalive_h, APPCONF_REFRESH_KEEPALIVE_H_LEAST,
           APPCONF_REFRESH_KEEPALIVE_H_MOST, APPCONF_NO_ALSO),
    FLAG("refresh_fast", "refresh_fast", refresh_fast),
    FLAG("dither", "dither", dither),
    NUMBER("font", "font", font, 0, APPCONF_FONT_COUNT - 1, APPCONF_NO_ALSO),
    FLAG("time_auto", "time_auto", time_auto),
    NUMBER("power_saving", "power_saving", power_saving, APPCONF_POWER_PLUGGED, APPCONF_POWER_AUTO,
           APPCONF_NO_ALSO),
    TEXT("default_app", "default_app", default_app, 0),
    NUMBER("menu_idle_min", "menu_idle", menu_idle_min, APPCONF_MENU_IDLE_MIN_LEAST,
           APPCONF_MENU_IDLE_MIN_MOST, APPCONF_NO_ALSO),
    FLAG("header_clock", "header_clock", header_clock),
    /* The system applications take their titles at start. */
    TEXT("language", "language", language, APPCONF_RESTART),
};

#undef TEXT
#undef NUMBER
#undef FLAG

#define FIELD_COUNT (sizeof FIELDS / sizeof FIELDS[0])

const appconf_field_t *appconf_fields(int *count)
{
    *count = (int)FIELD_COUNT;
    return FIELDS;
}

const appconf_field_t *appconf_field(const char *key, size_t length)
{
    for (size_t i = 0; i < FIELD_COUNT; i++) {
        if (strlen(FIELDS[i].key) == length && memcmp(FIELDS[i].key, key, length) == 0) {
            return &FIELDS[i];
        }
    }
    return NULL;
}

int32_t appconf_number(const appconf_t *c, const appconf_field_t *field)
{
    const uint8_t *at = (const uint8_t *)c + field->offset;
    if (field->size == sizeof(uint16_t)) {
        uint16_t value;
        memcpy(&value, at, sizeof value);
        return value;
    }
    return *at;
}

const char *appconf_text(const appconf_t *c, const appconf_field_t *field)
{
    return (const char *)c + field->offset;
}

void appconf_format(const appconf_t *c, const appconf_field_t *field, char *out, size_t size)
{
    if (field->kind == APPCONF_TEXT) {
        snprintf(out, size, "%s", appconf_text(c, field));
    } else {
        snprintf(out, size, "%ld", (long)appconf_number(c, field));
    }
}

bool appconf_put_number(appconf_t *c, const appconf_field_t *field, int32_t value)
{
    if (field->kind == APPCONF_TEXT) {
        return false;
    }
    bool is_in_range = (value >= field->least && value <= field->most) ||
                       (field->also != APPCONF_NO_ALSO && value == field->also);
    if (!is_in_range) {
        return false;
    }
    uint8_t *at = (uint8_t *)c + field->offset;
    if (field->size == sizeof(uint16_t)) {
        uint16_t narrow = (uint16_t)value;
        memcpy(at, &narrow, sizeof narrow);
    } else {
        *at = (uint8_t)value;
    }
    return true;
}

/* A whole number in decimal, nothing before or after it. */
static bool parse_number(const char *text, int32_t *value)
{
    if (text[0] == '\0' || isspace((unsigned char)text[0])) {
        return false;
    }
    char *end = NULL;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed < INT32_MIN || parsed > INT32_MAX) {
        return false;
    }
    *value = (int32_t)parsed;
    return true;
}

bool appconf_put(appconf_t *c, const appconf_field_t *field, const char *value)
{
    if (field->kind == APPCONF_TEXT) {
        size_t length = strlen(value);
        if (length >= field->size) {
            return false;
        }
        memcpy((char *)c + field->offset, value, length + 1);
        return true;
    }
    int32_t number;
    return parse_number(value, &number) && appconf_put_number(c, field, number);
}

bool appconf_differs(const appconf_t *one, const appconf_t *other, const appconf_field_t *field)
{
    if (field->kind == APPCONF_TEXT) {
        return strcmp(appconf_text(one, field), appconf_text(other, field)) != 0;
    }
    return appconf_number(one, field) != appconf_number(other, field);
}

bool appconf_needs_restart(const appconf_t *before, const appconf_t *now)
{
    for (size_t i = 0; i < FIELD_COUNT; i++) {
        if ((FIELDS[i].flags & APPCONF_RESTART) != 0 && appconf_differs(before, now, &FIELDS[i])) {
            return true;
        }
    }
    return false;
}
