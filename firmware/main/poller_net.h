/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_delivery.h"
#include "app_manifest.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The poller's requests, as HTTP: made one at a time on the task that
 * calls, each to its end. An answer is handed over while the call runs,
 * in the form the application gets it. */

typedef struct {
    /* An answer, or the news that there is none, for the application: the
     * data is only there until this returns. */
    void (*deliver)(const char *app_name, const app_delivery_t *delivery);
    /* Whether the network, asked just now, was there. */
    void (*network_worked)(bool worked);
} poller_net_host_t;

void poller_net_start(const poller_net_host_t *host);

/* The value of a setting of the application named by `context`: what
 * {{key}} stands for in its addresses and headers. */
bool poller_net_setting_of(void *context, const char *key, char *value, size_t value_size);

/* Poll number `poll_number` of the application's manifest. */
void poller_net_poll(const char *app_name, int poll_number);

/* A poll that cannot be made now - `why` is a HOST_FAILED_*: no network,
 * no memory. The application on the screen is told; one that is not is
 * left asleep. */
void poller_net_poll_not_made(const char *app_name, int poll_number, int why);

/* A request of the application's own, answered under `number`. */
typedef struct {
    int                 number;
    int                 method; /* a HOST_HTTP_* */
    const char         *url;
    const app_header_t *headers;
    int                 header_count;
    const uint8_t      *body;
    int                 body_length;
} poller_ask_t;

void poller_net_request(const char *app_name, const poller_ask_t *ask);

/* The request under `number` was not made: `why` is a HOST_FAILED_*. */
void poller_net_tell_failure(const char *app_name, int number, int why);

/* A file for the host itself: `take` gets it in pieces. True when it came
 * whole; `http_status` is 0 when the server was not reached. */
typedef bool (*poller_net_take_fn)(void *context, const uint8_t *piece, size_t length);
bool poller_net_fetch(const char *url, size_t max_bytes, poller_net_take_fn take, void *context,
                      int *http_status);

#ifdef __cplusplus
}
#endif
