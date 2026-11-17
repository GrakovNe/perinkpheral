/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The system API: the host's own settings, the installed applications,
 * the device's facts and its irreversible actions.
 */
#pragma once

#include "base.h"

#define HOST_SINCE_sys_setting_get   "0.1.13" /* the host's settings by key */
#define HOST_SINCE_sys_setting_set   "0.1.13"
#define HOST_SINCE_sys_setting_range "0.1.13"

/* ---- System API ----
 *
 * The sys_* calls are used by the host's own applications (Settings,
 * About). Any application can use them if its manifest contains
 * "system": true. The device web page warns the user before installing
 * such an application. A module that imports a sys_* call without this
 * declaration is rejected. */
/* Device information as text. Returns the length; HOST_ERR_NO_SPACE if
 * the buffer is too small; 0 if the value is currently unknown. Off its
 * network the address is the setup access point's, 192.168.4.1. */
#define SYS_INFO_VERSION 0 /* firmware version of the build */
#define SYS_INFO_NAME    1 /* "Perinkpheral-C7B0" */
#define SYS_INFO_NETWORK 2 /* configured Wi-Fi network */
#define SYS_INFO_ADDRESS 3 /* address of the web page: on the network, or on the setup
                              access point */
#define SYS_INFO_WIFI    4 /* one of SYS_WIFI_* */
#define SYS_INFO_SLOT    5 /* running firmware slot: "ota_0" */
#define SYS_INFO_FREE_MEMORY 6 /* free host memory in KB, as digits */
/* 7 was the time zone: sys_setting_get(SYS_KEY_TIME_ZONE) */
#define SYS_INFO_JOIN_CODE   8 /* QR code text that joins a phone to the setup access
                                  point: "WIFI:T:nopass;S:Perinkpheral-C7B0;;" */
#define SYS_INFO_FREE_FLASH  9 /* free flash for applications - their code and files - in
                                  KB, as digits */
#define SYS_INFO_FREE_STORE 10 /* free room of the applications' stores in KB, as digits */
#define SYS_WIFI_ONLINE     "online"
#define SYS_WIFI_CONNECTING "connecting"
#define SYS_WIFI_SETUP      "setup" /* waiting for network settings on the setup access point */
HOST_SYSTEM_IMPORT(sys_info, int32_t sys_info(int32_t what, char *text, int32_t capacity));

/* Draws the wordmark "perINKpheraL" in the square font. (x, y) is the top
 * left corner. `scale`: 1 to SYS_LOGO_SCALE_MAX; scale 5 is 357 px wide.
 * sys_logo() is a drawing call. The width and height calls are valid in
 * any handler. */
#define SYS_LOGO_SCALE_MAX 5
HOST_SYSTEM_IMPORT(sys_logo, int32_t sys_logo(int32_t x, int32_t y, int32_t scale));
HOST_SYSTEM_IMPORT(sys_logo_width, int32_t sys_logo_width(int32_t scale));
HOST_SYSTEM_IMPORT(sys_logo_height, int32_t sys_logo_height(int32_t scale));

/* The host's settings, by key: the same keys as the device page's
 * /api/settings, and the same rules. A value is text: a number in decimal,
 * a flag "0" or "1". Settings on the device and the device's page are two
 * ways of writing one store; the host follows what is stored, and any
 * application with system access may read and write it the same way.
 *
 * sys_setting_get():   writes the value. Returns its length; HOST_ERR_NOT_FOUND
 *                      for a key there is no setting for; HOST_ERR_ARGUMENT for
 *                      a secret, which is never read back (the Wi-Fi password);
 *                      HOST_ERR_NO_SPACE.
 * sys_setting_set():   checks, stores and puts it into effect - at once, but for
 *                      the device's name, the time server and the language
 *                      of the system applications' titles, which are taken at
 *                      start and are in effect after a restart. HOST_ERR_ARGUMENT
 *                      for a value the setting cannot take, or a key that is
 *                      not set this way (the network: SYS_DO_FORGET_NETWORK,
 *                      or the device's page).
 * sys_setting_range(): a number's value and its range. HOST_ERR_ARGUMENT for
 *                      a setting that is text. */
#define SYS_SETTING_MAX 64 /* the longest value, in bytes: a time server's name */
#define SYS_KEY_TIME_AUTOMATIC      "time_auto"           /* 1: time from the network */
#define SYS_KEY_TIME_ZONE           "tz"                  /* POSIX TZ string: "MSK-3" */
#define SYS_KEY_TIME_ZONE_CITY      "tz_city"             /* the city it was chosen by: "St Petersburg" */
#define SYS_KEY_LANGUAGE            "language"            /* "en", "ru" */
#define SYS_KEY_FONT                "font"                /* 0 Departure Mono, 1 Jersey */
#define SYS_KEY_REFRESH_FULL_EVERY  "refresh_full_every"  /* partial refreshes before a full one;
                                                             0: always full; SYS_REFRESH_FULL_NEVER */
#define SYS_KEY_REFRESH_MIN_SECONDS "refresh_min_data_s"  /* least seconds between refreshes
                                                             without a press */
#define SYS_KEY_REFRESH_FORCE_HOURS "refresh_keepalive_h" /* most hours without a refresh */
#define SYS_KEY_REFRESH_FAST        "refresh_fast"        /* 1: the fast full refresh */
#define SYS_KEY_POWER_SAVING        "power_saving"        /* 0 off; 1 on: the device sleeps when
                                                             idle, the radio is on for polls and
                                                             for a while after a press; 2 on
                                                             unless a computer is on the USB port */
#define SYS_KEY_MENU_IDLE_MINUTES   "menu_idle_min"       /* idle minutes in the menu before the
                                                             default application opens */
#define SYS_KEY_HEADER_CLOCK        "header_clock"        /* 1: the time instead of the date */
#define SYS_KEY_DEFAULT_APP         "default_app"         /* an application's name; "": none */
#define SYS_KEY_DEVICE_NAME         "device_name"         /* in effect after a restart */
#define SYS_REFRESH_FULL_NEVER      255
typedef struct {
    int32_t value;
    int32_t least;
    int32_t most;
} sys_number_t;
HOST_LAYOUT(sys_number_t, 12);
HOST_SYSTEM_IMPORT(sys_setting_get, int32_t sys_setting_get(const char *key, int32_t key_length,
                                                            char *value, int32_t capacity));
HOST_SYSTEM_IMPORT(sys_setting_set, int32_t sys_setting_set(const char *key, int32_t key_length,
                                                            const char *value, int32_t length));
HOST_SYSTEM_IMPORT(sys_setting_range, int32_t sys_setting_range(const char *key, int32_t key_length,
                                                                sys_number_t *number));

/* Sets the clock, in Unix seconds. Returns HOST_ERR_FAILED while the time
 * source is the network. */
HOST_SYSTEM_IMPORT(sys_time_set, int32_t sys_time_set(int64_t unix_seconds));

/* Installed applications.
 * An application id is derived from its code and its manifest; another
 * version has another id.
 * sys_app_list():   writes up to `capacity` ids, in the order of their
 *                   titles. Returns
 *                   the number of installed applications.
 * sys_app_title():  writes the title. Returns its length.
 * sys_app_remove(): removes the files of the application and keeps its
 *                   store - once the handler that asks has returned, so
 *                   sys_app_list() lists it until then.
 * All return HOST_ERR_NOT_FOUND for an id that is not installed;
 * sys_app_remove() returns HOST_ERR_ARGUMENT for the application that calls
 * it - it is removed from another, or from the device's page. */
#define SYS_APP_LIST_MAX 32 /* maximum `capacity`: more than a device holds */
HOST_SYSTEM_IMPORT(sys_app_list, int32_t sys_app_list(uint32_t *ids, int32_t capacity));
HOST_SYSTEM_IMPORT(sys_app_title, int32_t sys_app_title(uint32_t id, char *text, int32_t capacity));
HOST_SYSTEM_IMPORT(sys_app_remove, int32_t sys_app_remove(uint32_t id));

/* Default application: opened after a restart, after the menu idle time,
 * and by BACK in the menu.
 * sys_app_default() returns its id, or 0 if none is set.
 * sys_app_set_default(0) clears the setting. The setting is stored by
 * application name and survives an update of the application. */
HOST_SYSTEM_IMPORT(sys_app_default, uint32_t sys_app_default(void));
HOST_SYSTEM_IMPORT(sys_app_set_default, int32_t sys_app_set_default(uint32_t id));

/* Irreversible actions. The device restarts after each. */
#define SYS_DO_RESTART        0
#define SYS_DO_FORGET_NETWORK 1 /* the device starts with its setup access point */
#define SYS_DO_FACTORY_RESET  2 /* erases the settings and all application stores */
HOST_SYSTEM_IMPORT(sys_do, int32_t sys_do(int32_t action));
