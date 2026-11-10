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

/* Everything a user can set. Pure: no ESP-IDF, host-tested. The network
 * fields are here from day one so that NVS keys never have to move. */
typedef struct {
    char device_name[APPCONF_NAME_MAX + 1];
    char wifi_ssid[APPCONF_SSID_MAX + 1];
    char wifi_password[APPCONF_PASS_MAX + 1];
    char ntp_server[APPCONF_HOST_MAX + 1];
    char tz[APPCONF_TZ_MAX + 1]; /* POSIX TZ string, "MSK-3" */

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
