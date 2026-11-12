/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "memory.h"

#include <stddef.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"

#include "deadlines.h"
#include "host.h"

static const char *TAG = "memory";

#define KB 1024

/* What each kind of work needs to begin: so much free in all, and so much
 * of it in one piece. Measured on the device (2026-11-12): a TLS session
 * takes about 50 KB at its peak, and its largest single block is the
 * 16 KB record it reads into. */
static const struct {
    size_t free_bytes;
    size_t one_block_bytes;
} NEEDS[MEMORY_NEEDS] = {
    [MEMORY_FOR_A_SESSION] = { 60 * KB, 17 * KB },
    [MEMORY_FOR_A_PAGE] = { 24 * KB, 8 * KB },
    [MEMORY_FOR_AN_UPDATE] = { 32 * KB, 8 * KB },
};

/* Below this the host is one allocation from falling over. */
#define CRITICAL_FREE_BYTES  (12 * KB)
#define CRITICAL_BLOCK_BYTES (4 * KB)
#define LOOK_EVERY_MS        5000
/* Critical at so many looks in a row: the first lets go of what can be
 * let go of, the last restarts. */
#define LOOKS_BEFORE_RESTART 3

#define SHEDDERS_MAX 4

static volatile uint32_t s_refusals;
static volatile uint32_t s_failed_allocations;
static uint32_t          s_failures_seen; /* by the last look */
static uint32_t          s_sheds;
static int               s_critical_looks;
static void (*s_shedders[SHEDDERS_MAX])(void);
static int               s_shedder_count;

/* In whichever task found no room, perhaps with its locks held: counted
 * and nothing else. */
static void on_failed_allocation(size_t size, uint32_t caps, const char *function)
{
    (void)size;
    (void)caps;
    (void)function;
    s_failed_allocations++;
}

void memory_start(void)
{
    heap_caps_register_failed_alloc_callback(on_failed_allocation);
    deadlines_set_in(DEADLINE_MEMORY_LOOK, 0, 0, LOOK_EVERY_MS);
}

bool memory_has(memory_need_t need)
{
    bool has = esp_get_free_heap_size() >= NEEDS[need].free_bytes &&
               heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT) >= NEEDS[need].one_block_bytes;
    if (!has) {
        s_refusals++;
    }
    return has;
}

void memory_on_pressure(void (*shed)(void))
{
    if (s_shedder_count < SHEDDERS_MAX) {
        s_shedders[s_shedder_count++] = shed;
    }
}

void memory_look(void)
{
    size_t free_bytes = esp_get_free_heap_size();
    size_t block_bytes = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    uint32_t failures = s_failed_allocations;
    bool is_critical = free_bytes < CRITICAL_FREE_BYTES || block_bytes < CRITICAL_BLOCK_BYTES ||
                       failures != s_failures_seen;
    s_failures_seen = failures;
    if (!is_critical) {
        s_critical_looks = 0;
    } else {
        s_critical_looks++;
        ESP_LOGW(TAG, "short of memory: %u free, %u in one block, %u allocations failed",
                 (unsigned)free_bytes, (unsigned)block_bytes, (unsigned)failures);
        if (s_critical_looks == 1) {
            s_sheds++;
            for (int i = 0; i < s_shedder_count; i++) {
                s_shedders[i]();
            }
        } else if (s_critical_looks >= LOOKS_BEFORE_RESTART) {
            ESP_LOGE(TAG, "still short of memory: restarting");
            host_stop_soon(HOST_STOP_RESTART);
            return; /* nothing more to look at */
        }
    }
    deadlines_set_in(DEADLINE_MEMORY_LOOK, 0, 0, LOOK_EVERY_MS);
}

void memory_read(memory_counts_t *counts)
{
    counts->refusals = s_refusals;
    counts->failed_allocations = s_failed_allocations;
    counts->sheds = s_sheds;
}
