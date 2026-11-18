/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "memory.h"
#include "deadlines.h"
#include "host.h"
#include "units.h"
#include "tinytest.h"

static uint32_t free_bytes = 100 * KB;
static size_t block_bytes = 64 * KB;
static int64_t now_us;
static int restarts;
static int sheds;
static int looks_scheduled;
static void (*failed_allocation)(size_t, uint32_t, const char *);

uint32_t esp_get_free_heap_size(void) { return free_bytes; }
size_t heap_caps_get_largest_free_block(uint32_t caps)
{
    (void)caps;
    return block_bytes;
}
int64_t esp_timer_get_time(void) { return now_us; }
int heap_caps_register_failed_alloc_callback(void (*callback)(size_t, uint32_t, const char *))
{
    failed_allocation = callback;
    return 0;
}
bool deadlines_set_in(deadline_kind_t kind, uint32_t owner, int number, int64_t in_ms)
{
    CHECK(kind == DEADLINE_MEMORY_LOOK && owner == 0 && number == 0 && in_ms == 5000);
    looks_scheduled++;
    return true;
}
void host_stop_soon(host_stop_t why)
{
    CHECK(why == HOST_STOP_RESTART);
    restarts++;
}
static void shed(void) { sheds++; }
static void at_second(int64_t seconds) { now_us = seconds * US_PER_SECOND; }

int main(void)
{
    memory_start();
    memory_on_pressure(shed);
    CHECK(failed_allocation != NULL && looks_scheduled == 1);
    /* One transient refusal, then no new request before the ten-minute look. */
    at_second(10);
    free_bytes = 50 * KB;
    CHECK(!memory_has(MEMORY_FOR_A_SESSION));
    at_second(610);
    free_bytes = 100 * KB;
    memory_look();
    CHECK(restarts == 0 && sheds == 0);
    memory_counts_t counts;
    memory_read(&counts);
    CHECK(counts.refusals == 1);

    /* Recovery seen between refusals starts the next timeout anew. */
    at_second(700);
    block_bytes = 16 * KB;
    CHECK(!memory_has(MEMORY_FOR_A_SESSION));
    at_second(1000);
    block_bytes = 17 * KB;
    free_bytes = 60 * KB;
    memory_look();
    at_second(1100);
    block_bytes = 16 * KB;
    CHECK(!memory_has(MEMORY_FOR_A_SESSION));
    at_second(1300);
    memory_look();
    CHECK(restarts == 0);
    at_second(1699);
    memory_look();
    CHECK(restarts == 0);
    at_second(1700);
    memory_look();
    CHECK(restarts == 1); /* Still fragmented for a whole ten minutes. */

    /* Enough contiguous space alone is not recovery when total memory is short. */
    free_bytes = 100 * KB;
    block_bytes = 64 * KB;
    CHECK(memory_has(MEMORY_FOR_A_SESSION));
    at_second(1800);
    free_bytes = 59 * KB;
    CHECK(!memory_has(MEMORY_FOR_A_SESSION));
    at_second(2400);
    memory_look();
    CHECK(restarts == 2);

    /* Critical-memory shedding and restart still work independently. */
    free_bytes = 100 * KB;
    CHECK(memory_has(MEMORY_FOR_A_SESSION));
    memory_look();
    failed_allocation(16, 0, "test");
    memory_look();
    CHECK(sheds == 1 && restarts == 2);
    memory_look();
    free_bytes = 11 * KB;
    memory_look();
    memory_look();
    CHECK(sheds == 2 && restarts == 2);
    memory_look();
    CHECK(restarts == 3);
    memory_read(&counts);
    CHECK(counts.failed_allocations == 1 && counts.sheds == 2 && counts.refusals == 4);
    REPORT();
}
