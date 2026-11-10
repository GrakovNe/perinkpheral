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

/* What the device does about Wi-Fi and when, with no radio in it: given
 * what just happened it says what to do next. Pure, host-tested. */

typedef enum {
    WIFI_PHASE_SETUP,      /* no network is stored: only the setup access point */
    WIFI_PHASE_CONNECTING, /* trying the stored network */
    WIFI_PHASE_ONLINE,
    /* The stored network has not answered for too long: the setup access
     * point is up as well, so that the user can correct it, and the device
     * keeps trying. */
    WIFI_PHASE_RESCUE,
} wifi_phase_t;

typedef enum {
    WIFI_EVENT_STARTED,
    WIFI_EVENT_CONNECTED,    /* associated and has an address */
    WIFI_EVENT_DISCONNECTED, /* lost the network, or an attempt failed */
    WIFI_EVENT_RETRY_DUE,    /* the pause asked for in the last answer is over */
} wifi_plan_event_t;

typedef struct {
    bool     start_access_point;
    bool     stop_access_point;
    bool     connect;       /* start an attempt now */
    uint32_t retry_in_ms;   /* then say WIFI_EVENT_RETRY_DUE after this long; 0: no */
} wifi_plan_actions_t;

typedef struct {
    wifi_phase_t phase;
    bool         has_network;      /* a network is stored */
    int64_t      trying_since_ms;  /* when the current run of failures began */
    int          failed_attempts;  /* in that run */
} wifi_plan_t;

void wifi_plan_init(wifi_plan_t *plan, bool has_network);

wifi_plan_actions_t wifi_plan_step(wifi_plan_t *plan, wifi_plan_event_t event, int64_t now_ms);

/* The setup access point is up in this phase. */
bool wifi_phase_has_access_point(wifi_phase_t phase);

#ifdef __cplusplus
}
#endif
