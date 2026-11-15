/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "request_queue.h"

#include <stdlib.h>
#include <string.h>

void request_queue_init(request_queue_t *queue)
{
    memset(queue, 0, sizeof *queue);
    queue->last_served = REQUEST_QUEUE_NO_APP;
}

request_queue_result_t request_queue_add(request_queue_t *queue, int app, int number, int method,
                                         const char *url, const app_header_t *headers,
                                         int header_count, const uint8_t *body, int body_length,
                                         int64_t now_ms)
{
    size_t bytes = sizeof(queued_request_t) + strlen(url) + 1 + (size_t)body_length;
    for (int i = 0; i < header_count; i++) {
        bytes += strlen(headers[i].name) + 1 + strlen(headers[i].value) + 1;
    }
    int own_waiting = 0, free_slot = -1;
    for (int i = 0; i < REQUEST_QUEUE_SLOTS; i++) {
        if (queue->slots[i] == NULL) {
            free_slot = i;
        } else if (queue->slots[i]->app == app) {
            own_waiting++;
        }
    }
    if (free_slot < 0 || own_waiting >= HOST_HTTP_PENDING_MAX ||
        queue->bytes + bytes > REQUEST_QUEUE_BYTES_MAX) {
        return REQUEST_QUEUE_FULL;
    }
    queued_request_t *request = malloc(bytes);
    if (request == NULL) {
        return REQUEST_QUEUE_NO_MEMORY;
    }
    request->app = app;
    request->number = number;
    request->order = queue->next_order++;
    request->queued_ms = now_ms;
    request->method = method;
    request->header_count = header_count;
    request->body_length = body_length;
    request->bytes = bytes;
    char *at = request->text;
    at = stpcpy(at, url) + 1;
    for (int i = 0; i < header_count; i++) {
        at = stpcpy(at, headers[i].name) + 1;
        at = stpcpy(at, headers[i].value) + 1;
    }
    if (body_length > 0) {
        memcpy(at, body, (size_t)body_length);
    }
    queue->slots[free_slot] = request;
    queue->bytes += bytes;
    return REQUEST_QUEUE_ADDED;
}

int request_queue_next(const request_queue_t *queue)
{
    int oldest = -1, oldest_of_another = -1;
    for (int i = 0; i < REQUEST_QUEUE_SLOTS; i++) {
        const queued_request_t *request = queue->slots[i];
        if (request == NULL) {
            continue;
        }
        if (oldest < 0 || request->order < queue->slots[oldest]->order) {
            oldest = i;
        }
        if (request->app != queue->last_served &&
            (oldest_of_another < 0 || request->order < queue->slots[oldest_of_another]->order)) {
            oldest_of_another = i;
        }
    }
    return oldest_of_another >= 0 ? oldest_of_another : oldest;
}

const queued_request_t *request_queue_at(const request_queue_t *queue, int slot)
{
    return slot >= 0 && slot < REQUEST_QUEUE_SLOTS ? queue->slots[slot] : NULL;
}

void request_queue_drop(request_queue_t *queue, int slot, bool served)
{
    queued_request_t *request = queue->slots[slot];
    if (request == NULL) {
        return;
    }
    if (served) {
        queue->last_served = request->app;
    }
    queue->bytes -= request->bytes;
    free(request);
    queue->slots[slot] = NULL;
}

int request_queue_overdue(const request_queue_t *queue, int64_t now_ms, int64_t wait_ms,
                          int64_t *recheck_in_ms)
{
    int     overdue = -1;
    int64_t soonest = -1;
    for (int i = 0; i < REQUEST_QUEUE_SLOTS; i++) {
        const queued_request_t *request = queue->slots[i];
        if (request == NULL) {
            continue;
        }
        int64_t waited_ms = now_ms - request->queued_ms;
        if (waited_ms > wait_ms) {
            if (overdue < 0 || request->order < queue->slots[overdue]->order) {
                overdue = i;
            }
        } else if (soonest < 0 || wait_ms - waited_ms + 1 < soonest) {
            soonest = wait_ms - waited_ms + 1;
        }
    }
    *recheck_in_ms = overdue >= 0 ? -1 : soonest;
    return overdue;
}

bool request_queue_is_empty(const request_queue_t *queue)
{
    for (int i = 0; i < REQUEST_QUEUE_SLOTS; i++) {
        if (queue->slots[i] != NULL) {
            return false;
        }
    }
    return true;
}

void request_queue_keep_apps(request_queue_t *queue, const int *apps, int count)
{
    for (int i = 0; i < REQUEST_QUEUE_SLOTS; i++) {
        if (queue->slots[i] == NULL) {
            continue;
        }
        bool is_kept = false;
        for (int kept = 0; kept < count && !is_kept; kept++) {
            is_kept = queue->slots[i]->app == apps[kept];
        }
        if (!is_kept) {
            request_queue_drop(queue, i, false);
        }
    }
}

const char *request_queue_url(const queued_request_t *request)
{
    return request->text;
}

const char *request_queue_after(const char *text)
{
    return text + strlen(text) + 1;
}
