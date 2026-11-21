/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>

#include "wifi_plan.h"
#include "wifi_networks.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The radio. It joins the stored network and keeps it, or puts up an open
 * access point for setting the device up; which and when is decided by
 * wifi_plan, this file only carries it out. */

/* The device's own address on its setup access point: where its page is
 * before it has a network. */
#define WIFI_SETUP_ADDRESS "192.168.4.1"

typedef struct {
    const char *ssid;              /* empty: no network is stored */
    const char *password;          /* empty: an open network */
    bool        is_off;            /* Wi-Fi was turned off; it matters with no network stored */
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
    bool         access_point_up; /* the setup access point is up, not only meant to be */
} wifi_status_t;

/* The strings are copied. With Wi-Fi off (WIFI_PHASE_OFF) the driver is
 * not started: no radio, no access point, no name server. The network
 * stack itself is, for the sockets of those who do not ask first. */
void wifi_start(const wifi_config_in_t *config);

void wifi_get_status(wifi_status_t *status);

bool wifi_is_online(void);

/* The signal of the access point the device is on, in dBm, as the device
 * hears it; false when it is on none. */
bool wifi_signal(int *rssi);

/* DEADLINE_WIFI_RETRY has come: time to try the network again. */
void wifi_retry_is_due(void);

/* Turns the radio off to save power, or on again to join the stored
 * network. Off is not taken while the device has no network or its setup
 * access point is up: then it has to be found. From any task. */
void wifi_set_radio(bool on);

typedef enum {
    WIFI_SCAN_OK,
    WIFI_SCAN_BUSY,
    WIFI_SCAN_FAILED,
} wifi_scan_result_t;

/* Scan without disconnecting, returning at most WIFI_SCAN_NETWORKS_MAX distinct
 * networks. Waits up to eight seconds; call from a worker with the radio held on.
 * Connecting, radio-off and concurrent scans return BUSY. */
wifi_scan_result_t wifi_scan_networks(wifi_network_t *records, uint16_t capacity,
                                      uint16_t *count);

#ifdef __cplusplus
}
#endif
