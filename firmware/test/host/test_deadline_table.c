/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "deadline_table.h"

#include "tinytest.h"

enum { TIMER, ALARM };

static const deadline_id_t TIMER_0 = { TIMER, 0, 0 };
static const deadline_id_t ALARM_A2 = { ALARM, 0x1111, 2 };
static const deadline_id_t ALARM_A3 = { ALARM, 0x1111, 3 };
static const deadline_id_t ALARM_B0 = { ALARM, 0x7a7a, 0 };

static bool takes(deadline_table_t *table, int64_t now_ms, deadline_id_t wanted)
{
    deadline_id_t id;
    return deadline_table_take_due(table, now_ms, &id) && id.kind == wanted.kind &&
           id.owner == wanted.owner && id.number == wanted.number;
}

static bool takes_none(deadline_table_t *table, int64_t now_ms)
{
    deadline_id_t id;
    return !deadline_table_take_due(table, now_ms, &id);
}

static void test_empty(void)
{
    deadline_table_t table;
    deadline_table_init(&table);
    CHECK(takes_none(&table, 1000));
    CHECK(deadline_table_nearest(&table) == DEADLINE_NONE);
    CHECK(deadline_table_when(&table, TIMER_0) == DEADLINE_NONE);
}

static void test_the_earliest_first_and_once(void)
{
    deadline_table_t table;
    deadline_table_init(&table);
    CHECK(deadline_table_set(&table, ALARM_B0, 31000, true));
    CHECK(deadline_table_set(&table, ALARM_A2, 11000, true));
    CHECK(deadline_table_set(&table, ALARM_A3, 21000, false));
    CHECK(deadline_table_nearest(&table) == 11000);
    CHECK(takes_none(&table, 10999));
    CHECK(takes(&table, 11000, ALARM_A2));
    CHECK(takes_none(&table, 11000));
    CHECK(deadline_table_nearest(&table) == 21000);
    /* all that have come, in the order they came */
    CHECK(takes(&table, 99999, ALARM_A3));
    CHECK(takes(&table, 99999, ALARM_B0));
    CHECK(takes_none(&table, 99999));
}

static void test_two_for_one_moment_in_the_order_set(void)
{
    deadline_table_t table;
    deadline_table_init(&table);
    deadline_table_set(&table, ALARM_B0, 5000, true);
    deadline_table_set(&table, TIMER_0, 5000, true);
    CHECK(takes(&table, 5000, ALARM_B0));
    CHECK(takes(&table, 5000, TIMER_0));
}

static void test_set_anew_and_cancel(void)
{
    deadline_table_t table;
    deadline_table_init(&table);
    deadline_table_set(&table, TIMER_0, 5000, true);
    deadline_table_set(&table, TIMER_0, 9000, true);
    CHECK(deadline_table_when(&table, TIMER_0) == 9000);
    CHECK(takes_none(&table, 5000));
    deadline_table_cancel(&table, TIMER_0);
    CHECK(takes_none(&table, 9000));
    deadline_table_cancel(&table, TIMER_0); /* gone already */
}

static void test_the_clock_is_set(void)
{
    deadline_table_t table;
    deadline_table_init(&table);
    /* "in ten seconds", set at 1000, and "at 21000" */
    deadline_table_set(&table, ALARM_A2, 11000, true);
    deadline_table_set(&table, ALARM_A3, 21000, false);
    /* an hour ahead: ten seconds are still ten seconds, a time is a time */
    deadline_table_clock_moved(&table, 3600000);
    CHECK(deadline_table_when(&table, ALARM_A2) == 3611000);
    CHECK(deadline_table_when(&table, ALARM_A3) == 21000);
    /* the time has passed with the clock: it has come */
    CHECK(takes(&table, 3601000, ALARM_A3));
    CHECK(takes_none(&table, 3610999));
    CHECK(takes(&table, 3611000, ALARM_A2));
    /* and back */
    deadline_table_set(&table, TIMER_0, 3620000, true);
    deadline_table_clock_moved(&table, -3600000);
    CHECK(deadline_table_when(&table, TIMER_0) == 20000);
}

static void test_owners_that_are_no_more(void)
{
    deadline_table_t table;
    deadline_table_init(&table);
    deadline_table_set(&table, ALARM_A2, 1000, true);
    deadline_table_set(&table, ALARM_B0, 2000, true);
    deadline_table_set(&table, TIMER_0, 3000, true);
    const uint32_t left[] = { 0x7a7a };
    deadline_table_keep_only(&table, ALARM, left, 1);
    CHECK(takes(&table, 99999, ALARM_B0));
    CHECK(takes(&table, 99999, TIMER_0)); /* another kind is not theirs to lose */
    CHECK(takes_none(&table, 99999));
}

static void test_what_outlives_a_sleep(void)
{
    deadline_table_t table;
    deadline_table_init(&table);
    deadline_table_set(&table, TIMER_0, 1000, true);
    deadline_table_set(&table, ALARM_A2, 5000, true);
    deadline_table_set(&table, ALARM_B0, 3000, false);
    /* the timer is nearer, and is not what the chip wakes for */
    CHECK(deadline_table_nearest_of(&table, 1u << ALARM) == 3000);
    CHECK(deadline_table_nearest_of(&table, 1u << TIMER) == 1000);
    CHECK(deadline_table_nearest_of(&table, 0) == DEADLINE_NONE);
    deadline_table_keep_kinds(&table, 1u << ALARM);
    CHECK(deadline_table_when(&table, TIMER_0) == DEADLINE_NONE);
    CHECK(deadline_table_when(&table, ALARM_A2) == 5000);
    CHECK(deadline_table_nearest(&table) == 3000);
}

static void test_the_table_is_of_a_size(void)
{
    deadline_table_t table;
    deadline_table_init(&table);
    for (int i = 0; i < DEADLINES_MAX; i++) {
        deadline_id_t id = { ALARM, 1, i };
        CHECK(deadline_table_set(&table, id, 1, true));
    }
    CHECK(!deadline_table_set(&table, TIMER_0, 1, true));
    deadline_id_t there = { ALARM, 1, 0 };
    CHECK(deadline_table_set(&table, there, 2, true)); /* one that is there can be set anew */
}

int main(void)
{
    test_empty();
    test_the_earliest_first_and_once();
    test_two_for_one_moment_in_the_order_set();
    test_set_anew_and_cancel();
    test_the_clock_is_set();
    test_owners_that_are_no_more();
    test_what_outlives_a_sleep();
    test_the_table_is_of_a_size();
    REPORT();
}
