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

/* The deadlines somebody set, and nothing else. A deadline is a moment of
 * the clock, in milliseconds since 1970, whatever it was set as: "in so
 * long" is turned into a moment when it is set. What it remembers of how
 * it was set is only what happens when the clock is put to another time:
 * a deadline "in so long" moves with it, one "at such a time" stays.
 * Pure, host-tested. */

#define DEADLINES_MAX 80
#define DEADLINE_NONE INT64_MAX

/* Whose deadline: its kind, the owner among those of the kind (an
 * application's id; 0 where there is one owner) and the owner's own
 * number for it. Setting the same three again sets the deadline anew. */
typedef struct {
    int      kind;
    uint32_t owner;
    int      number;
} deadline_id_t;

typedef struct {
    bool          is_set;
    bool          moves_with_clock; /* set "in so long" */
    deadline_id_t id;
    int64_t       at_ms;
    uint32_t      order; /* of setting: of two that have come the older goes first */
} deadline_t;

typedef struct {
    deadline_t entries[DEADLINES_MAX];
    uint32_t   next_order;
} deadline_table_t;

void deadline_table_init(deadline_table_t *table);

/* False when the table is full. */
bool deadline_table_set(deadline_table_t *table, deadline_id_t id, int64_t at_ms,
                        bool moves_with_clock);

/* One that was not set is gone already. */
void deadline_table_cancel(deadline_table_t *table, deadline_id_t id);

/* Takes back every deadline of the kind whose owner is not among
 * `owners`: those owners are no more. */
void deadline_table_keep_only(deadline_table_t *table, int kind, const uint32_t *owners,
                              int owner_count);

/* A deadline that has come, taken out of the table; false when none has.
 * The earliest first. */
bool deadline_table_take_due(deadline_table_t *table, int64_t now_ms, deadline_id_t *id);

/* The moment of the nearest, or DEADLINE_NONE. */
int64_t deadline_table_nearest(const deadline_table_t *table);

/* The same among the kinds of `kinds` only, a bit for each kind. */
int64_t deadline_table_nearest_of(const deadline_table_t *table, uint32_t kinds);

/* Takes back every deadline whose kind is not among `kinds`. */
void deadline_table_keep_kinds(deadline_table_t *table, uint32_t kinds);

/* The moment a deadline is set for, or DEADLINE_NONE when it is not. */
int64_t deadline_table_when(const deadline_table_t *table, deadline_id_t id);

/* The clock was put `delta_ms` ahead (back, when negative). */
void deadline_table_clock_moved(deadline_table_t *table, int64_t delta_ms);

#ifdef __cplusplus
}
#endif
