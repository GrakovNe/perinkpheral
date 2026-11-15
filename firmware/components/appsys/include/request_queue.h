/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_manifest.h"
#include "host_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The requests applications make themselves (http_request), waiting for
 * the network, which takes one at a time. Each application has
 * HOST_HTTP_PENDING_MAX waiting at most; a request is a block of its own
 * size, and all of them together no more than REQUEST_QUEUE_BYTES_MAX - a
 * request is a URL and a few headers, a hundred bytes or two, and a body
 * is rare. The applications take turns: the next is the oldest of one
 * that was not served last. Pure, host-tested; whoever shares it between
 * tasks locks around it. */

#define REQUEST_QUEUE_APPS      8 /* applications that may have requests waiting */
#define REQUEST_QUEUE_SLOTS     (REQUEST_QUEUE_APPS * HOST_HTTP_PENDING_MAX)
#define REQUEST_QUEUE_BYTES_MAX (16 * 1024)
#define REQUEST_QUEUE_NO_APP    0 /* no application has this id */

typedef struct {
    int      app;    /* the application's id */
    int      number; /* what the answer is delivered under: the application's tag */
    uint32_t order;  /* the oldest goes first */
    int64_t  queued_ms;
    int      method;
    int      header_count;
    int      body_length;
    size_t   bytes;  /* of the whole block */
    /* The URL, terminated; each header's name and value, terminated; the body. */
    char text[];
} queued_request_t;

typedef struct {
    queued_request_t *slots[REQUEST_QUEUE_SLOTS];
    size_t            bytes;
    uint32_t          next_order;
    int               last_served; /* the application served last */
} request_queue_t;

void request_queue_init(request_queue_t *queue);

typedef enum {
    REQUEST_QUEUE_ADDED,
    REQUEST_QUEUE_FULL,       /* the application has as many waiting as it may, or no room is left */
    REQUEST_QUEUE_NO_MEMORY,
} request_queue_result_t;

request_queue_result_t request_queue_add(request_queue_t *queue, int app, int number, int method,
                                         const char *url, const app_header_t *headers,
                                         int header_count, const uint8_t *body, int body_length,
                                         int64_t now_ms);

/* The slot of the request to make next; -1 when none waits. */
int request_queue_next(const request_queue_t *queue);

/* The request in a slot, NULL for an empty one. */
const queued_request_t *request_queue_at(const request_queue_t *queue, int slot);

/* Takes the request out: made, answered or given up. `served` notes its
 * application as the one served last. */
void request_queue_drop(request_queue_t *queue, int slot, bool served);

/* A request that has waited longer than `wait_ms`, oldest first; -1 when
 * none has, and then *recheck_in_ms is when the first will have (or -1
 * when nothing waits). */
int request_queue_overdue(const request_queue_t *queue, int64_t now_ms, int64_t wait_ms,
                          int64_t *recheck_in_ms);

bool request_queue_is_empty(const request_queue_t *queue);

/* The requests of applications that are not among `apps` go. */
void request_queue_keep_apps(request_queue_t *queue, const int *apps, int count);

/* What a request carries, unpacked from its block: the URL, then a header
 * at a time. */
const char *request_queue_url(const queued_request_t *request);
const char *request_queue_after(const char *text); /* the next terminated text */

#ifdef __cplusplus
}
#endif
