/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "notice_queue.h"

#include <stddef.h>
#include <string.h>

#include "gfx.h"

void notice_queue_init(notice_queue_t *queue)
{
    memset(queue, 0, sizeof *queue);
}

bool notice_queue_add(notice_queue_t *queue, const char *first, const char *second,
                      const char *detail, int icon)
{
    if (queue->count >= NOTICE_QUEUE_MAX) {
        return false;
    }
    notice_told_t *told = &queue->waiting[queue->count++];
    gfx_text_copy(told->first, sizeof told->first, first);
    gfx_text_copy(told->second, sizeof told->second, second);
    gfx_text_copy(told->detail, sizeof told->detail, detail);
    told->icon = icon;
    return true;
}

static void drop_first(notice_queue_t *queue)
{
    memmove(&queue->waiting[0], &queue->waiting[1],
            (size_t)(NOTICE_QUEUE_MAX - 1) * sizeof queue->waiting[0]);
    queue->count--;
    queue->is_shown = false;
}

void notice_queue_seen(notice_queue_t *queue)
{
    queue->is_seen = true;
}

bool notice_queue_take_dismissed(notice_queue_t *queue)
{
    bool was_seen = queue->is_seen;
    queue->is_seen = false;
    if (!was_seen || !queue->is_shown) {
        return false;
    }
    drop_first(queue);
    return true;
}

void notice_queue_gone(notice_queue_t *queue)
{
    if (queue->is_shown) {
        drop_first(queue);
    }
}

const notice_told_t *notice_queue_next(notice_queue_t *queue)
{
    if (queue->is_shown || queue->count == 0) {
        return NULL;
    }
    queue->is_shown = true;
    return &queue->waiting[0];
}

bool notice_queue_is_shown(const notice_queue_t *queue)
{
    return queue->is_shown;
}
