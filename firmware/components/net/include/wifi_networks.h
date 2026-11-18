/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define WIFI_SSID_MAX 32
#define WIFI_SCAN_NETWORKS_MAX 16

typedef struct {
    char ssid[WIFI_SSID_MAX + 1];
    int8_t rssi;
    bool secured;
} wifi_network_t;

/* Keep the strongest record for each visible UTF-8 SSID, strongest first.
 * Hidden networks can still be entered manually. */
void wifi_networks_add(wifi_network_t *records, uint16_t capacity, uint16_t *count,
                       const wifi_network_t *candidate);
