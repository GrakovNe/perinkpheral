/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Whether a freshly installed firmware has earned its place. The device
 * is updated over the network, so an image counts as good only once the
 * way the next update would arrive is open again: the web server is up
 * and the device can be reached. One that does not get there in time is
 * rolled back. Pure, host-tested. */

/* A fresh image has this long to prove itself. */
#define OTA_HEALTH_DEADLINE_S 180
/* With no network stored there is nothing to connect to: the setup access
 * point and the web server having stayed up this long is the proof. */
#define OTA_HEALTH_SETUP_SETTLE_S 20

typedef enum {
    OTA_HEALTH_WAIT,
    OTA_HEALTH_CONFIRM,
    OTA_HEALTH_ROLL_BACK,
} ota_health_t;

typedef struct {
    bool     network_stored;
    bool     online;
    bool     access_point_up;
    bool     web_server_up;
    uint32_t uptime_s;
} ota_health_input_t;

ota_health_t ota_health(const ota_health_input_t *input);

#ifdef __cplusplus
}
#endif
