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

#define APPCONF_NAME_MAX   32
#define APPCONF_SSID_MAX   32
#define APPCONF_PASS_MAX   64
#define APPCONF_HOST_MAX   64
#define APPCONF_TZ_MAX     47

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

/* Everything a user can set. Pure: no ESP-IDF, host-tested. The network
 * fields are here from day one so that NVS keys never have to move. */
/* The typefaces there are to choose from: gfx_family_t in this order. */
#define APPCONF_FONT_COUNT 2

/* How the device is powered, which is what it does when idle. */
#define APPCONF_POWER_PLUGGED 0 /* the radio always on */
#define APPCONF_POWER_SAVING  1 /* always: light sleep; the radio on for a poll and after a press */
#define APPCONF_POWER_AUTO    2 /* the same whenever no computer is on the USB cable */

typedef struct {
    char device_name[APPCONF_NAME_MAX + 1];
    char wifi_ssid[APPCONF_SSID_MAX + 1];
    char wifi_password[APPCONF_PASS_MAX + 1];
    char ntp_server[APPCONF_HOST_MAX + 1];
    char tz[APPCONF_TZ_MAX + 1]; /* POSIX TZ string, "MSK-3" */

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

    char hostname[APPCONF_HOST_MAX + 1]; /* derived, never stored */
} appconf_t;

/* Last two MAC bytes, lower-case hex: "1a2b". */
void appconf_device_id(const uint8_t mac[6], char out[5]);

void appconf_default_name(const char device_id[5], char *out, size_t out_sz);

void appconf_defaults(appconf_t *c, const char device_id[5]);

/* The mDNS host from the name: lower case, letters and digits, runs of
 * anything else become one dash. Falls back to "perinkpheral-<id>". */
void appconf_hostname(const char *device_name, const char device_id[5],
                      char *out, size_t out_sz);

void appconf_derive(appconf_t *c, const char device_id[5]);

bool appconf_validate(const appconf_t *c, char *err, size_t err_sz);

#ifdef __cplusplus
}
#endif
