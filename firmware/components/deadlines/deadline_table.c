/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "deadline_table.h"

#include <stddef.h>

void deadline_table_init(deadline_table_t *table)
{
    for (int i = 0; i < DEADLINES_MAX; i++) {
        table->entries[i].is_set = false;
    }
    table->next_order = 0;
}

static bool is_same(deadline_id_t id, deadline_id_t other)
{
    return id.kind == other.kind && id.owner == other.owner && id.number == other.number;
}

static deadline_t *find(deadline_table_t *table, deadline_id_t id)
{
    for (int i = 0; i < DEADLINES_MAX; i++) {
        if (table->entries[i].is_set && is_same(table->entries[i].id, id)) {
            return &table->entries[i];
        }
    }
    return NULL;
}

bool deadline_table_set(deadline_table_t *table, deadline_id_t id, int64_t at_ms,
                        bool moves_with_clock)
{
    deadline_t *entry = find(table, id);
    for (int i = 0; entry == NULL && i < DEADLINES_MAX; i++) {
        if (!table->entries[i].is_set) {
            entry = &table->entries[i];
        }
    }
    if (entry == NULL) {
        return false;
    }
    entry->is_set = true;
    entry->moves_with_clock = moves_with_clock;
    entry->id = id;
    entry->at_ms = at_ms;
    entry->order = table->next_order++;
    return true;
}

void deadline_table_cancel(deadline_table_t *table, deadline_id_t id)
{
    deadline_t *entry = find(table, id);
    if (entry != NULL) {
        entry->is_set = false;
    }
}

void deadline_table_keep_only(deadline_table_t *table, int kind, const uint32_t *owners,
                              int owner_count)
{
    for (int i = 0; i < DEADLINES_MAX; i++) {
        deadline_t *entry = &table->entries[i];
        if (!entry->is_set || entry->id.kind != kind) {
            continue;
        }
        bool is_kept = false;
        for (int owner = 0; owner < owner_count && !is_kept; owner++) {
            is_kept = owners[owner] == entry->id.owner;
        }
        entry->is_set = is_kept;
    }
}

/* The one that comes first: the earliest, and of two for one moment the
 * one set first. */
static const deadline_t *first(const deadline_table_t *table)
{
    const deadline_t *earliest = NULL;
    for (int i = 0; i < DEADLINES_MAX; i++) {
        const deadline_t *entry = &table->entries[i];
        if (!entry->is_set) {
            continue;
        }
        if (earliest == NULL || entry->at_ms < earliest->at_ms ||
            (entry->at_ms == earliest->at_ms && entry->order < earliest->order)) {
            earliest = entry;
        }
    }
    return earliest;
}

bool deadline_table_take_due(deadline_table_t *table, int64_t now_ms, deadline_id_t *id)
{
    const deadline_t *earliest = first(table);
    if (earliest == NULL || earliest->at_ms > now_ms) {
        return false;
    }
    *id = earliest->id;
    table->entries[earliest - table->entries].is_set = false;
    return true;
}

int64_t deadline_table_nearest(const deadline_table_t *table)
{
    const deadline_t *earliest = first(table);
    return earliest != NULL ? earliest->at_ms : DEADLINE_NONE;
}

int64_t deadline_table_when(const deadline_table_t *table, deadline_id_t id)
{
    const deadline_t *entry = find((deadline_table_t *)table, id);
    return entry != NULL ? entry->at_ms : DEADLINE_NONE;
}

void deadline_table_clock_moved(deadline_table_t *table, int64_t delta_ms)
{
    for (int i = 0; i < DEADLINES_MAX; i++) {
        deadline_t *entry = &table->entries[i];
        if (entry->is_set && entry->moves_with_clock) {
            entry->at_ms += delta_ms;
        }
    }
}
