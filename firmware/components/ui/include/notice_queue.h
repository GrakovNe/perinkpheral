/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What applications asked to be told to the user, waiting their turn: one
 * is on the screen, the rest after it, oldest first; a restart forgets
 * them. Which scene shows one is the host's - the queue says when. Pure,
 * host-tested. */

#define NOTICE_QUEUE_MAX        4
#define NOTICE_QUEUE_LINE_BYTES 64 /* first and second, with the terminator: host_notice_t's */
#define NOTICE_QUEUE_DETAIL_BYTES 96

typedef struct {
    char first[NOTICE_QUEUE_LINE_BYTES];
    char second[NOTICE_QUEUE_LINE_BYTES];
    char detail[NOTICE_QUEUE_DETAIL_BYTES];
    int  icon;
} notice_told_t;

typedef struct {
    notice_told_t waiting[NOTICE_QUEUE_MAX]; /* the first is the one shown, when one is */
    int           count;
    bool          is_shown;
    bool          is_seen; /* dismissed: it leaves once the handler has returned */
} notice_queue_t;

void notice_queue_init(notice_queue_t *queue);

/* Joins the queue; a text too long is cut between characters, NULL is
 * empty. False when the queue is full. */
bool notice_queue_add(notice_queue_t *queue, const char *first, const char *second,
                      const char *detail, int icon);

/* The user dismissed the one shown; from inside its scene's handler. */
void notice_queue_seen(notice_queue_t *queue);

/* After the handler: true when the one shown was dismissed and has left
 * the queue - its scene is to be closed. */
bool notice_queue_take_dismissed(notice_queue_t *queue);

/* The one shown is off the screen by another way (the long press that
 * empties the stack): seen all the same, and out of the queue. */
void notice_queue_gone(notice_queue_t *queue);

/* The next to show, now shown; NULL when one is shown already or none waits. */
const notice_told_t *notice_queue_next(notice_queue_t *queue);

bool notice_queue_is_shown(const notice_queue_t *queue);

#ifdef __cplusplus
}
#endif
