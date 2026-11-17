/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The device's own id: the last two bytes of its address on the network,
 * as four hex digits and the end of the text. */
#define APPCONF_MAC_BYTES      6
#define APPCONF_DEVICE_ID_SIZE 5
#define APPCONF_NAME_MAX   32
#define APPCONF_SSID_MAX   32
#define APPCONF_PASS_MAX   64
#define APPCONF_HOST_MAX   64
#define APPCONF_TZ_MAX     47
#define APPCONF_CITY_MAX   31 /* a city of Settings' list, by its English name */
#define APPCONF_APP_NAME_MAX 13 /* an application's name: MANIFEST_NAME_MAX */

#define APPCONF_NTP_DEFAULT     "pool.ntp.org"
#define APPCONF_TZ_DEFAULT      "UTC0"

/* The panel's defaults and the range a user may move them in. 60 s lets a
 * clock show minutes; Waveshare's own advice is 180, and every refresh
 * wears the panel, which is rated for a million. */
#define APPCONF_REFRESH_MIN_DATA_S_DEFAULT    60
#define APPCONF_REFRESH_MIN_DATA_S_LEAST      10
#define APPCONF_REFRESH_MIN_DATA_S_MOST       3600
#define APPCONF_REFRESH_MIN_BUTTON_MS_DEFAULT 0
#define APPCONF_REFRESH_MIN_BUTTON_MS_MOST    10000
#define APPCONF_REFRESH_FULL_EVERY_DEFAULT    5
/* 0: every refresh is a full one - the cleanest picture, a blink every
 * time. APPCONF_REFRESH_FULL_NEVER: only when the application on the
 * screen changes - the quietest, and traces pile up. The way e-readers
 * let their owner choose. */
#define APPCONF_REFRESH_FULL_EVERY_LEAST      0
#define APPCONF_REFRESH_FULL_EVERY_MOST       50
#define APPCONF_REFRESH_FULL_NEVER            255
#define APPCONF_REFRESH_KEEPALIVE_H_DEFAULT   24
#define APPCONF_REFRESH_KEEPALIVE_H_LEAST     1
#define APPCONF_REFRESH_KEEPALIVE_H_MOST      168

/* The menu left alone for so many minutes gives the screen to the default
 * application, when there is one. */
#define APPCONF_MENU_IDLE_MIN_DEFAULT 5
#define APPCONF_MENU_IDLE_MIN_LEAST   1
#define APPCONF_MENU_IDLE_MIN_MOST    60

/* Everything a user can set. Pure: no ESP-IDF, host-tested. The network
 * fields are here from day one so that NVS keys never have to move. */
/* The typefaces there are to choose from: gfx_family_t in this order. */
#define APPCONF_FONT_COUNT 2

/* How the device is powered, which is what it does when idle. */
#define APPCONF_POWER_PLUGGED 0 /* the radio always on */
#define APPCONF_POWER_SAVING  1 /* always: light sleep; the radio on for a poll and after a press */
#define APPCONF_POWER_AUTO    2 /* the same whenever no computer is on the USB cable */

#define APPCONF_LANGUAGE_MAX 7

/* The languages the system knows - the same as sdk/languages.json, which a
 * test holds it to. */
typedef struct {
    const char *code;
    const char *name;
} appconf_language_t;
const appconf_language_t *appconf_languages(int *count);

typedef struct {
    char device_name[APPCONF_NAME_MAX + 1];
    char wifi_ssid[APPCONF_SSID_MAX + 1];
    char wifi_password[APPCONF_PASS_MAX + 1];
    char ntp_server[APPCONF_HOST_MAX + 1];
    char tz[APPCONF_TZ_MAX + 1]; /* POSIX TZ string, "MSK-3" */
    /* The city the zone was chosen by, by its English name ("St Petersburg"): several share a
     * zone, and the device and the page show the one chosen. Empty: none, or a zone of its own. */
    char tz_city[APPCONF_CITY_MAX + 1];

    /* How the panel is treated: see refresh_policy.h. */
    uint16_t refresh_min_data_s;    /* least seconds between refreshes nobody asked for */
    uint16_t refresh_min_button_ms; /* least pause before answering a press */
    uint8_t  refresh_full_every;    /* partial refreshes before a full one */
    uint8_t  refresh_keepalive_h;   /* hours without a refresh before one is forced */
    uint8_t  refresh_fast;          /* 1: the shorter full refresh, with less of a blink */
    uint8_t  font;                  /* the typeface: 0 Departure Mono, 1 Jersey */
    uint8_t  dither;                /* 1: applications' greys are dithered; 0: black or white */
    uint8_t  time_auto;             /* 1: the clock is set from the network; 0: by hand */
    uint8_t  power_saving;          /* an APPCONF_POWER_*: plugged in, from its battery, or
                                       from its battery and asleep between polls */

    /* The application the device shows by itself: after a restart, when
     * the menu is left alone, and by BACK in the menu. Empty: none. */
    char    default_app[APPCONF_APP_NAME_MAX + 1];
    uint8_t menu_idle_min;
    uint8_t header_clock; /* 1: the header shows the time where the date stands */
    /* The language applications speak, as its code ("en", "ru"): one of
     * appconf_languages(). */
    char    language[APPCONF_LANGUAGE_MAX + 1];

    char hostname[APPCONF_HOST_MAX + 1]; /* derived, never stored */
} appconf_t;

/* Last two MAC bytes, lower-case hex: "1a2b". */
void appconf_device_id(const uint8_t mac[APPCONF_MAC_BYTES], char out[APPCONF_DEVICE_ID_SIZE]);

void appconf_default_name(const char device_id[APPCONF_DEVICE_ID_SIZE], char *out, size_t out_sz);

void appconf_defaults(appconf_t *c, const char device_id[APPCONF_DEVICE_ID_SIZE]);

/* The mDNS host from the name: lower case, letters and digits, runs of
 * anything else become one dash. Falls back to "perinkpheral-<id>". */
void appconf_hostname(const char *device_name, const char device_id[APPCONF_DEVICE_ID_SIZE],
                      char *out, size_t out_sz);

void appconf_derive(appconf_t *c, const char device_id[APPCONF_DEVICE_ID_SIZE]);

/* ---- the settings by key ----
 *
 * Every setting has a key, the one name it goes by everywhere: on the
 * device's page, in the system API and in what is stored. This table is
 * what there is to set and how: whoever reads or writes a setting by its
 * key - the page, an application with system access, the store - goes
 * through it, and none of them knows the fields by hand. */
typedef enum {
    APPCONF_TEXT,
    APPCONF_NUMBER,
    APPCONF_FLAG, /* a number that is 0 or 1 */
} appconf_kind_t;

#define APPCONF_SECRET  0x01u /* written, never read back */
#define APPCONF_RESTART 0x02u /* taken at start: a change is in effect after a restart */
#define APPCONF_OWN_WAY 0x04u /* not set by key: the network is changed its own way, asked
                                 about on the device */

typedef struct {
    const char    *key;
    const char    *stored;  /* the key in NVS, as it has been since the first firmware */
    appconf_kind_t kind;
    uint8_t        flags;   /* APPCONF_SECRET, APPCONF_RESTART, APPCONF_OWN_WAY */
    uint16_t       offset;  /* in appconf_t */
    uint16_t       size;    /* a text's room with its end; a number's bytes, 1 or 2 */
    int32_t        least;   /* a number's range */
    int32_t        most;
    int32_t        also;    /* one more value a number may take, or APPCONF_NO_ALSO */
} appconf_field_t;

#define APPCONF_NO_ALSO   (-1)
#define APPCONF_VALUE_MAX (APPCONF_PASS_MAX + 1) /* the longest value as text, with its end */

const appconf_field_t *appconf_fields(int *count);
/* NULL for a key there is no setting for. */
const appconf_field_t *appconf_field(const char *key, size_t length);

/* A number's or a flag's value. */
int32_t appconf_number(const appconf_t *c, const appconf_field_t *field);
/* A text's value. */
const char *appconf_text(const appconf_t *c, const appconf_field_t *field);

/* The value as text: a number in decimal, a flag "0" or "1". */
void appconf_format(const appconf_t *c, const appconf_field_t *field, char *out, size_t size);

/* Puts a value given as text: a number in decimal, in its range; a flag
 * "0" or "1"; a text that fits. False, and the settings as they were, when
 * it is not one. What is checked across fields - a time zone's form, a
 * name's characters - is appconf_validate()'s. */
bool appconf_put(appconf_t *c, const appconf_field_t *field, const char *value);
bool appconf_put_number(appconf_t *c, const appconf_field_t *field, int32_t value);

bool appconf_differs(const appconf_t *one, const appconf_t *other, const appconf_field_t *field);

/* Whether going from `before` to `now` changes a setting that is taken at
 * start. */
bool appconf_needs_restart(const appconf_t *before, const appconf_t *now);

/* Room for what appconf_validate() and settings_change() say is wrong. */
#define APPCONF_PROBLEM_MAX 128

bool appconf_validate(const appconf_t *c, char *err, size_t err_sz);

#ifdef __cplusplus
}
#endif
