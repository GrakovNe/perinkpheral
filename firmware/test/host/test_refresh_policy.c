/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "refresh_policy.h"

#include "tinytest.h"

static const refresh_policy_cfg_t CFG = {
    .min_data_ms = 180 * 1000,
    .min_button_ms = 2000,
    .full_every = 5,
    .keepalive_ms = 24u * 3600u * 1000u,
};

static refresh_kind_t go(refresh_policy_t *p, refresh_reason_t why, int64_t now)
{
    refresh_kind_t k = refresh_policy_decide(p, &CFG, why, now);
    refresh_policy_note(p, k, why, now);
    return k;
}

static void test_first_is_full(void)
{
    refresh_policy_t p;
    refresh_policy_init(&p);
    CHECK(refresh_policy_decide(&p, &CFG, REFRESH_FOR_DATA, 0) == REFRESH_FULL);
    CHECK(refresh_policy_decide(&p, &CFG, REFRESH_FOR_BUTTON, 0) == REFRESH_FULL);
    CHECK(refresh_policy_decide(&p, &CFG, REFRESH_FOR_KEEPALIVE, 0) == REFRESH_FULL);
    CHECK(go(&p, REFRESH_FOR_DATA, 1000) == REFRESH_FULL);
    CHECK(p.shown && p.partials == 0 && p.last_ms == 1000);
}

static void test_data_interval(void)
{
    refresh_policy_t p;
    refresh_policy_init(&p);
    go(&p, REFRESH_FOR_SWITCH, 0);
    /* kept apart from one another, not from what the user did: a clock
     * opened at twenty past shows the next minute as it comes */
    CHECK(go(&p, REFRESH_FOR_DATA, 40 * 1000) == REFRESH_PARTIAL);
    CHECK(go(&p, REFRESH_FOR_DATA, 218 * 1000) == REFRESH_NONE);
    CHECK(p.last_ms == 40 * 1000);             /* a refusal changes nothing */
    /* a second early is in time */
    CHECK(go(&p, REFRESH_FOR_DATA, 219 * 1000) == REFRESH_PARTIAL);
    /* a press in between does not put the next one off */
    go(&p, REFRESH_FOR_BUTTON, 390 * 1000);
    CHECK(go(&p, REFRESH_FOR_DATA, 398 * 1000) == REFRESH_PARTIAL);
    CHECK(go(&p, REFRESH_FOR_DATA, 399 * 1000) == REFRESH_NONE);
}

static void test_button_interval(void)
{
    refresh_policy_t p;
    refresh_policy_init(&p);
    go(&p, REFRESH_FOR_SWITCH, 0);
    CHECK(go(&p, REFRESH_FOR_BUTTON, 1999) == REFRESH_NONE);
    CHECK(go(&p, REFRESH_FOR_BUTTON, 2000) == REFRESH_PARTIAL);
    CHECK(go(&p, REFRESH_FOR_BUTTON, 3000) == REFRESH_NONE);
    CHECK(go(&p, REFRESH_FOR_BUTTON, 4000) == REFRESH_PARTIAL);
}

static void test_full_after_n_partials(void)
{
    refresh_policy_t p;
    refresh_policy_init(&p);
    go(&p, REFRESH_FOR_SWITCH, 0);
    int64_t t = 0;
    for (int i = 0; i < 5; i++) {
        t += 10 * 1000;
        CHECK(go(&p, REFRESH_FOR_BUTTON, t) == REFRESH_PARTIAL);
    }
    CHECK(p.partials == 5);
    t += 10 * 1000;
    CHECK(go(&p, REFRESH_FOR_BUTTON, t) == REFRESH_FULL);
    CHECK(p.partials == 0);
}

static void test_switch_always_full(void)
{
    refresh_policy_t p;
    refresh_policy_init(&p);
    go(&p, REFRESH_FOR_SWITCH, 0);
    go(&p, REFRESH_FOR_BUTTON, 5000);
    CHECK(p.partials == 1);
    CHECK(go(&p, REFRESH_FOR_SWITCH, 5001) == REFRESH_FULL); /* no interval */
    CHECK(p.partials == 0);
}

static void test_keepalive(void)
{
    refresh_policy_t p;
    refresh_policy_init(&p);
    go(&p, REFRESH_FOR_SWITCH, 0);
    int64_t day = 24LL * 3600 * 1000;
    CHECK(go(&p, REFRESH_FOR_KEEPALIVE, day - 1) == REFRESH_NONE);
    CHECK(go(&p, REFRESH_FOR_KEEPALIVE, day) == REFRESH_FULL);
    CHECK(go(&p, REFRESH_FOR_KEEPALIVE, day + 1) == REFRESH_NONE);
    /* any refresh in between postpones it */
    go(&p, REFRESH_FOR_DATA, day + 1000 * 1000);
    CHECK(go(&p, REFRESH_FOR_KEEPALIVE, 2 * day) == REFRESH_NONE);
}

static void test_wait(void)
{
    refresh_policy_t p;
    refresh_policy_init(&p);
    /* nothing shown yet: any reason goes out at once */
    CHECK(refresh_policy_wait_ms(&p, &CFG, REFRESH_FOR_DATA, 0) == 0);
    go(&p, REFRESH_FOR_DATA, 10000);

    CHECK(refresh_policy_wait_ms(&p, &CFG, REFRESH_FOR_DATA, 10000) == 179 * 1000);
    CHECK(refresh_policy_wait_ms(&p, &CFG, REFRESH_FOR_DATA, 70000) == 119 * 1000);
    CHECK(refresh_policy_wait_ms(&p, &CFG, REFRESH_FOR_BUTTON, 11000) == 1000);
    CHECK(refresh_policy_wait_ms(&p, &CFG, REFRESH_FOR_SWITCH, 10001) == 0);

    /* it agrees with the decision on both sides of the moment */
    CHECK(refresh_policy_wait_ms(&p, &CFG, REFRESH_FOR_DATA, 188999) == 1);
    CHECK(refresh_policy_decide(&p, &CFG, REFRESH_FOR_DATA, 188999) == REFRESH_NONE);
    CHECK(refresh_policy_wait_ms(&p, &CFG, REFRESH_FOR_DATA, 189000) == 0);
    CHECK(refresh_policy_decide(&p, &CFG, REFRESH_FOR_DATA, 189000) != REFRESH_NONE);
}

static void test_on_request(void)
{
    refresh_policy_t p;
    refresh_policy_init(&p);
    go(&p, REFRESH_FOR_SWITCH, 0);
    /* asked to be shown soon: no waiting for the data interval, only a second */
    CHECK(go(&p, REFRESH_FOR_REQUEST, 500) == REFRESH_NONE);
    CHECK(refresh_policy_wait_ms(&p, &CFG, REFRESH_FOR_REQUEST, 500) == 500);
    CHECK(go(&p, REFRESH_FOR_REQUEST, 1000) == REFRESH_PARTIAL);
    CHECK(go(&p, REFRESH_FOR_REQUEST, 1999) == REFRESH_NONE);
    CHECK(go(&p, REFRESH_FOR_REQUEST, 2000) == REFRESH_PARTIAL);

    /* a clean picture is a full refresh whatever the count, a second apart too */
    CHECK(p.partials == 2);
    CHECK(go(&p, REFRESH_FOR_CLEAN, 2500) == REFRESH_NONE);
    CHECK(refresh_policy_wait_ms(&p, &CFG, REFRESH_FOR_CLEAN, 2500) == 500);
    CHECK(go(&p, REFRESH_FOR_CLEAN, 3000) == REFRESH_FULL);
    CHECK(p.partials == 0);

    /* requests count towards the full refresh like any other */
    for (int i = 1; i <= 5; i++) {
        CHECK(go(&p, REFRESH_FOR_REQUEST, 3000 + i * 1000) == REFRESH_PARTIAL);
    }
    CHECK(go(&p, REFRESH_FOR_REQUEST, 9000) == REFRESH_FULL);
}

static void test_a_row_of_pictures(void)
{
    refresh_policy_t p;
    refresh_policy_init(&p);
    /* even a row starts with a full refresh: the panel has nothing to compare with */
    CHECK(go(&p, REFRESH_FOR_ROW, 0) == REFRESH_FULL);
    for (int i = 1; i <= 20; i++) {
        CHECK(go(&p, REFRESH_FOR_ROW, i * 1000) == REFRESH_PARTIAL);
    }
    CHECK(p.partials == 20);
    /* the next of the row as soon as the panel can take it */
    CHECK(refresh_policy_wait_ms(&p, &CFG, REFRESH_FOR_ROW, 20001) == 0);
    CHECK(go(&p, REFRESH_FOR_ROW, 20001) == REFRESH_PARTIAL);
    CHECK(p.partials == 21);
    /* the row is over: whatever comes next cleans up after it */
    CHECK(go(&p, REFRESH_FOR_BUTTON, 30000) == REFRESH_FULL);
    CHECK(p.partials == 0);
}

int main(void)
{
    test_wait();
    test_first_is_full();
    test_data_interval();
    test_button_interval();
    test_full_after_n_partials();
    test_switch_always_full();
    test_keepalive();
    test_on_request();
    test_a_row_of_pictures();
    REPORT();
}
