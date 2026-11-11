/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>

#include "wifi_plan.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The radio. It joins the stored network and keeps it, or puts up an open
 * access point for setting the device up; which and when is decided by
 * wifi_plan, this file only carries it out. */

#define WIFI_SSID_MAX 32

/* The device's own address on its setup access point: where its page is
 * before it has a network. */
#define WIFI_SETUP_ADDRESS "192.168.4.1"

typedef struct {
    const char *ssid;              /* empty: no network is stored */
    const char *password;          /* empty: an open network */
    const char *hostname;
    const char *access_point_name; /* what the setup access point is called */
    /* Called from the Wi-Fi task whenever the phase changes; it must not
     * block. May be NULL. */
    void (*on_change)(void);
} wifi_config_in_t;

typedef struct {
    wifi_phase_t phase;
    char         ssid[WIFI_SSID_MAX + 1];              /* the stored network */
    char         access_point_name[WIFI_SSID_MAX + 1];
    char         address[16];                          /* empty unless online */
    bool         is_radio_off;   /* turned off to save power; the phase is as it was left */
    bool         was_reached;    /* the last attempt to join the network succeeded */
} wifi_status_t;

/* The strings are copied. */
void wifi_start(const wifi_config_in_t *config);

void wifi_get_status(wifi_status_t *status);

bool wifi_is_online(void);

/* Turns the radio off to save power, or on again to join the stored
 * network. Off is not taken while the device has no network or its setup
 * access point is up: then it has to be found. From any task. */
void wifi_set_radio(bool on);

#ifdef __cplusplus
}
#endif
