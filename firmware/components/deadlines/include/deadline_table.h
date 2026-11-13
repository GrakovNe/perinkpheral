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

/* Table of deadlines. A deadline is stored as an absolute time,
 * milliseconds since 1970; a relative deadline is converted when it is
 * set. `moves_with_clock` records the difference: on a clock change a
 * relative deadline is shifted, an absolute one is not.
 * Pure, host-tested. */

#define DEADLINES_MAX 80
#define DEADLINE_NONE INT64_MAX

/* Deadline identity: kind, owner within the kind (application id; 0 for
 * kinds with one owner), number within the owner. Setting an existing
 * identity replaces the deadline. */
typedef struct {
    int      kind;
    uint32_t owner;
    int      number;
} deadline_id_t;

typedef struct {
    bool          is_set;
    bool          moves_with_clock; /* relative deadline */
    deadline_id_t id;
    int64_t       at_ms;
    uint32_t      order; /* sequence of setting; among due deadlines the lower is taken first */
} deadline_t;

typedef struct {
    deadline_t entries[DEADLINES_MAX];
    uint32_t   next_order;
} deadline_table_t;

void deadline_table_init(deadline_table_t *table);

/* False when the table is full. */
bool deadline_table_set(deadline_table_t *table, deadline_id_t id, int64_t at_ms,
                        bool moves_with_clock);

/* No effect when the deadline is not set. */
void deadline_table_cancel(deadline_table_t *table, deadline_id_t id);

/* Cancels the deadlines of `kind` whose owner is not in `owners`. */
void deadline_table_keep_only(deadline_table_t *table, int kind, const uint32_t *owners,
                              int owner_count);

/* Removes the earliest due deadline and returns it in `id`. False: none
 * is due. */
bool deadline_table_take_due(deadline_table_t *table, int64_t now_ms, deadline_id_t *id);

/* Time of the nearest deadline, or DEADLINE_NONE. */
int64_t deadline_table_nearest(const deadline_table_t *table);

/* The same for the kinds in the bit mask `kinds` (bit n: kind n). */
int64_t deadline_table_nearest_of(const deadline_table_t *table, uint32_t kinds);

/* Cancels the deadlines whose kind is not in the bit mask `kinds`. */
void deadline_table_keep_kinds(deadline_table_t *table, uint32_t kinds);

/* Time of the deadline, or DEADLINE_NONE when it is not set. */
int64_t deadline_table_when(const deadline_table_t *table, deadline_id_t id);

/* Applies a clock change of `delta_ms` (negative: backwards). */
void deadline_table_clock_moved(deadline_table_t *table, int64_t delta_ms);

#ifdef __cplusplus
}
#endif
