/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "appconf.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define PROJECT_SLUG "perinkpheral"
#define PROJECT_NAME "Perinkpheral"

static bool str_fits(const char *s, size_t cap)
{
    return s != NULL && strnlen(s, cap + 1) <= cap;
}

static bool has_ctrl(const char *s)
{
    for (const char *p = s; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c == 0x7f) {
            return true;
        }
    }
    return false;
}

static bool is_printable_ascii(const char *s)
{
    for (const char *p = s; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c > 0x7e) {
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
        if (c <= 0x20 || c > 0x7e) {
            return false;
        }
    }
    return true;
}

void appconf_device_id(const uint8_t mac[6], char out[5])
{
    snprintf(out, 5, "%02x%02x", mac[4], mac[5]);
}

void appconf_hostname(const char *device_name, const char device_id[5],
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

void appconf_default_name(const char device_id[5], char *out, size_t out_sz)
{
    char id_upper[5] = { 0 };
    for (int i = 0; i < 4; i++) {
        id_upper[i] = (char)toupper((unsigned char)device_id[i]);
    }
    snprintf(out, out_sz, PROJECT_NAME "-%s", id_upper);
}

void appconf_defaults(appconf_t *c, const char device_id[5])
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
    appconf_derive(c, device_id);
}

void appconf_derive(appconf_t *c, const char device_id[5])
{
    appconf_hostname(c->device_name, device_id, c->hostname, sizeof c->hostname);
}

/* The characters an application's name is made of. */
static bool is_app_name_or_empty(const char *name)
{
    for (; *name != '\0'; name++) {
        bool is_of_a_name = (*name >= 'a' && *name <= 'z') || (*name >= '0' && *name <= '9') ||
                            *name == '-' || *name == '_';
        if (!is_of_a_name) {
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
    if (c->wifi_ssid[0] != '\0' && pass_len > 0 && pass_len < 8) {
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
