/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "poll_round.h"
#include "tinytest.h"

static void test_all_items(int polls, int items, bool one_app)
{
    poll_schedule_t schedule;
    poll_schedule_init(&schedule);
    poll_rounds_t rounds = { 0 };
    int seen[POLL_SCHEDULE_MAX][16] = { 0 };
    int deadlines[POLL_SCHEDULE_MAX] = { 0 };
    for (int index = 0; index < polls; index++) {
        uint32_t first_ms;
        CHECK(poll_schedule_add(&schedule, one_app ? 1 : index + 1,
                                one_app ? index : 0, 300, &first_ms));
    }
    poll_schedule_want_all(&schedule);
    int calls = 0;
    poll_turn_t turn;
    while (poll_rounds_next(&rounds, poll_schedule_next_wanted(&schedule), &turn)) {
        if (++calls > polls * items) {
            CHECK(false);
            break;
        }
        int index = one_app ? turn.poll : turn.app - 1;
        CHECK(turn.item >= 0 && turn.item < items);
        CHECK(seen[index][turn.item]++ == 0);
        if (!turn.is_continuation) {
            poll_schedule_answered(&schedule, turn.app, turn.poll);
        }
        /* The second poll gets a turn before the first list finishes. */
        if (calls == 2 && polls > 1 && items > 1) {
            CHECK(index == 1 && turn.item == 0);
        }
        if (poll_rounds_answered(&rounds, &turn, items)) {
            CHECK(turn.item == items - 1);
            deadlines[index]++;
        }
    }
    CHECK(calls == polls * items);
    CHECK(!poll_rounds_pending(&rounds));
    CHECK(poll_schedule_next_wanted(&schedule) == NULL);
    for (int index = 0; index < polls; index++) {
        CHECK(deadlines[index] == 1);
        for (int item = 0; item < items; item++) {
            CHECK(seen[index][item] == 1);
        }
    }
}

static void test_waiting_and_removed(void)
{
    poll_rounds_t rounds = { 0 };
    poll_entry_t first = { .app = 1, .poll = 0 };
    poll_entry_t second = { .app = 2, .poll = 1 };
    poll_entry_t third = { .app = 3, .poll = 2 };
    poll_turn_t turn, repeated;
    CHECK(poll_rounds_next(&rounds, &first, &turn));
    CHECK(!poll_rounds_answered(&rounds, &turn, 5));
    CHECK(poll_rounds_next(&rounds, &second, &turn));
    CHECK(!poll_rounds_answered(&rounds, &turn, 5));
    CHECK(poll_rounds_next(&rounds, &third, &turn));
    CHECK(turn.app == 1 && turn.item == 1 && turn.is_continuation);
    /* Waiting for memory does not consume another item. */
    CHECK(poll_rounds_next(&rounds, &third, &repeated));
    CHECK(repeated.app == turn.app && repeated.item == turn.item);
    /* Removal cancels just that round; the waiting one survives. */
    CHECK(poll_rounds_answered(&rounds, &turn, 0));
    CHECK(poll_rounds_next(&rounds, &third, &turn));
    CHECK(turn.app == 2 && turn.item == 1);
    CHECK(poll_rounds_answered(&rounds, &turn, 0));
    CHECK(poll_rounds_next(&rounds, &third, &turn));
    CHECK(turn.app == 3 && turn.item == 0 && !turn.is_continuation);
    CHECK(poll_rounds_answered(&rounds, &turn, 1));
    CHECK(!poll_rounds_pending(&rounds));
}

static void test_single_between_items(void)
{
    poll_rounds_t rounds = { 0 };
    poll_entry_t first = { .app = 1 };
    poll_entry_t second = { .app = 2 };
    poll_turn_t turn;
    CHECK(poll_rounds_next(&rounds, &first, &turn));
    CHECK(!poll_rounds_answered(&rounds, &turn, 2));
    CHECK(poll_rounds_next(&rounds, &second, &turn));
    CHECK(poll_rounds_answered(&rounds, &turn, 0)); /* Failed single poll. */
    CHECK(poll_rounds_next(&rounds, NULL, &turn));
    CHECK(turn.app == 1 && turn.item == 1 && turn.is_continuation);
    CHECK(poll_rounds_answered(&rounds, &turn, 2));
    CHECK(!poll_rounds_next(&rounds, NULL, &turn));
}

static void test_requested_again_during_answer(void)
{
    poll_schedule_t schedule;
    poll_schedule_init(&schedule);
    uint32_t first_ms;
    CHECK(poll_schedule_add(&schedule, 1, 0, 300, &first_ms));
    CHECK(poll_schedule_want(&schedule, 1, 0));
    poll_rounds_t rounds = { 0 };
    poll_turn_t turn;
    int calls = 0, finished = 0;
    int seen[3] = { 0 };
    while (poll_rounds_next(&rounds, poll_schedule_next_wanted(&schedule), &turn)) {
        if (++calls > 6) {
            CHECK(false);
            break;
        }
        if (!turn.is_continuation) {
            poll_schedule_answered(&schedule, turn.app, turn.poll);
        }
        if (calls == 1) {
            CHECK(poll_schedule_want(&schedule, 1, 0)); /* app_on_data calls poll_run(). */
        }
        seen[turn.item]++;
        finished += poll_rounds_answered(&rounds, &turn, 3);
    }
    CHECK(calls == 6 && finished == 2);
    CHECK(seen[0] == 2 && seen[1] == 2 && seen[2] == 2);
}

int main(void)
{
    test_all_items(3, 5, true); /* Stocks: Yahoo, MOEX quotes and MOEX candles. */
    test_all_items(3, 3, false);
    test_all_items(POLL_SCHEDULE_MAX, 16, false);
    test_all_items(POLL_SCHEDULE_MAX, 1, false);
    test_waiting_and_removed();
    test_single_between_items();
    test_requested_again_during_answer();
    REPORT();
}
