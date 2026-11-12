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

/* The host's heap, guarded. An application cannot take it - it has its
 * own area, of one size - so what can run it short is the host's own
 * work for somebody: a TLS session, a page of JSON, a firmware image on
 * its way in. Each of those asks here first and is refused when there is
 * no room, which the asker says in its own words ("Not enough memory.",
 * HOST_FAILED_NO_MEMORY).
 *
 * And when the heap is short all the same, the host does not wait to
 * fall over: it lets go of what it can - the page's connections - and,
 * if that does not help, restarts in an orderly way. A device on a shelf
 * has to come back by itself. */

typedef enum {
    MEMORY_FOR_A_SESSION, /* a request over the network, TLS included */
    MEMORY_FOR_A_PAGE,    /* an answer of the web server's put together in memory */
    MEMORY_FOR_AN_UPDATE, /* a firmware image taken in */
    MEMORY_NEEDS,
} memory_need_t;

/* Once, at start. */
void memory_start(void);

/* Whether there is room for it now. A refusal is counted. From any task. */
bool memory_has(memory_need_t need);

/* `shed` is called, in the host loop, when the heap is critically short:
 * whoever can give memory back does. A few of them. */
void memory_on_pressure(void (*shed)(void));

/* DEADLINE_MEMORY_LOOK has come. In the host loop. */
void memory_look(void);

typedef struct {
    uint32_t refusals;           /* requests turned away by memory_has() */
    uint32_t failed_allocations; /* allocations that found no room */
    uint32_t sheds;              /* times the host let go of what it could */
} memory_counts_t;

void memory_read(memory_counts_t *counts);

#ifdef __cplusplus
}
#endif
