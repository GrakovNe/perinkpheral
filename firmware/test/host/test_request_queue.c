/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "request_queue.h"

#include <string.h>

#include "tinytest.h"

#define A 11
#define B 22

static request_queue_t queue;

/* A queue anew: what a test before left in it given back first. */
static void fresh(void)
{
    request_queue_keep_apps(&queue, NULL, 0);
    request_queue_init(&queue);
}

static bool add(int app, int number, int64_t now_ms)
{
    return request_queue_add(&queue, app, number, HOST_HTTP_GET, "https://example.org/x", NULL, 0,
                             NULL, 0, now_ms) == REQUEST_QUEUE_ADDED;
}

/* Takes the next one out as made, and says whose and which it was. */
static int take(int *app)
{
    int slot = request_queue_next(&queue);
    const queued_request_t *request = request_queue_at(&queue, slot);
    if (request == NULL) {
        return -1;
    }
    int number = request->number;
    *app = request->app;
    request_queue_drop(&queue, slot, true);
    return number;
}

static void test_turns(void)
{
    fresh();
    CHECK(request_queue_is_empty(&queue) && request_queue_next(&queue) == -1);
    CHECK(add(A, 1, 0) && add(A, 2, 0) && add(A, 3, 0) && add(B, 1, 0));
    int app;
    /* the oldest first, and then the other application gets its turn */
    CHECK(take(&app) == 1 && app == A);
    CHECK(take(&app) == 1 && app == B);
    CHECK(take(&app) == 2 && app == A);
    /* only A is left: A again */
    CHECK(take(&app) == 3 && app == A);
    CHECK(take(&app) == -1 && request_queue_is_empty(&queue));
    CHECK(queue.bytes == 0);
}

static void test_limits(void)
{
    fresh();
    for (int i = 0; i < HOST_HTTP_PENDING_MAX; i++) {
        CHECK(add(A, i, 0));
    }
    /* one application's share is full; another still has room */
    CHECK(!add(A, 99, 0));
    CHECK(add(B, 1, 0));

    /* the bytes of all of them together */
    fresh();
    static uint8_t body[HOST_HTTP_BODY_MAX];
    int added = 0;
    for (int app = 1; app <= REQUEST_QUEUE_APPS; app++) {
        for (int i = 0; i < HOST_HTTP_PENDING_MAX; i++) {
            added += request_queue_add(&queue, app, i, HOST_HTTP_POST, "https://e.org", NULL, 0, body,
                                       (int)sizeof body, 0) == REQUEST_QUEUE_ADDED;
        }
    }
    CHECK(added > 0 && added < REQUEST_QUEUE_SLOTS);
    CHECK(queue.bytes <= REQUEST_QUEUE_BYTES_MAX);
    request_queue_keep_apps(&queue, NULL, 0);
    CHECK(request_queue_is_empty(&queue) && queue.bytes == 0);
}

static void test_what_it_carries(void)
{
    fresh();
    app_header_t headers[2] = { { "Accept", "application/json" }, { "X-Token", "abc" } };
    static const uint8_t BODY[] = { 'h', 'i', 0, '!' };
    CHECK(request_queue_add(&queue, A, 7, HOST_HTTP_POST, "https://example.org/api", headers, 2, BODY,
                            (int)sizeof BODY, 5) == REQUEST_QUEUE_ADDED);
    const queued_request_t *request = request_queue_at(&queue, request_queue_next(&queue));
    CHECK(request->app == A && request->number == 7 && request->method == HOST_HTTP_POST &&
          request->queued_ms == 5 && request->header_count == 2 && request->body_length == 4);
    const char *at = request_queue_url(request);
    CHECK(strcmp(at, "https://example.org/api") == 0);
    at = request_queue_after(at);
    CHECK(strcmp(at, "Accept") == 0);
    at = request_queue_after(at);
    CHECK(strcmp(at, "application/json") == 0);
    at = request_queue_after(request_queue_after(at));
    CHECK(strcmp(at, "abc") == 0);
    at = request_queue_after(at);
    CHECK(memcmp(at, BODY, sizeof BODY) == 0);
    request_queue_drop(&queue, request_queue_next(&queue), false);
    CHECK(queue.last_served == REQUEST_QUEUE_NO_APP); /* given up is not served */
}

static void test_overdue_and_gone(void)
{
    fresh();
    int64_t recheck;
    CHECK(request_queue_overdue(&queue, 0, 100, &recheck) == -1 && recheck == -1);
    CHECK(add(A, 1, 0) && add(B, 2, 50));
    /* nothing waited long enough: looked at again when the first has */
    CHECK(request_queue_overdue(&queue, 60, 100, &recheck) == -1 && recheck == 41);
    /* the first is overdue, and the oldest of them is said first */
    int slot = request_queue_overdue(&queue, 200, 100, &recheck);
    CHECK(request_queue_at(&queue, slot)->number == 1);
    request_queue_drop(&queue, slot, false);
    /* an application that is gone takes its requests with it */
    int kept[] = { A };
    request_queue_keep_apps(&queue, kept, 1);
    CHECK(request_queue_is_empty(&queue));
}

int main(void)
{
    test_turns();
    test_limits();
    test_what_it_carries();
    test_overdue_and_gone();
    REPORT();
}
