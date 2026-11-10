/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "wifi_plan.h"

#include "tinytest.h"

static wifi_plan_t plan;

static wifi_plan_actions_t step(wifi_plan_event_t event, int64_t now_ms)
{
    return wifi_plan_step(&plan, event, now_ms);
}

static void test_no_network_is_setup(void)
{
    wifi_plan_init(&plan, false);
    wifi_plan_actions_t actions = step(WIFI_EVENT_STARTED, 0);
    CHECK(actions.start_access_point && !actions.connect && actions.retry_in_ms == 0);
    CHECK(plan.phase == WIFI_PHASE_SETUP);
    CHECK(wifi_phase_has_access_point(plan.phase));

    /* stray events change nothing and start nothing a second time */
    actions = step(WIFI_EVENT_DISCONNECTED, 1000);
    CHECK(!actions.start_access_point && !actions.connect && actions.retry_in_ms == 0);
    actions = step(WIFI_EVENT_RETRY_DUE, 2000);
    CHECK(!actions.connect);
}

static void test_connects_at_once(void)
{
    wifi_plan_init(&plan, true);
    wifi_plan_actions_t actions = step(WIFI_EVENT_STARTED, 0);
    CHECK(actions.connect && !actions.start_access_point);
    CHECK(plan.phase == WIFI_PHASE_CONNECTING);

    actions = step(WIFI_EVENT_CONNECTED, 1500);
    CHECK(plan.phase == WIFI_PHASE_ONLINE);
    CHECK(!actions.stop_access_point); /* there was none to stop */
}

static void test_retries_back_off(void)
{
    wifi_plan_init(&plan, true);
    step(WIFI_EVENT_STARTED, 0);

    const uint32_t expected_ms[] = { 2000, 4000, 8000, 16000, 30000, 30000 };
    int64_t now = 0;
    for (int attempt = 0; attempt < 6; attempt++) {
        wifi_plan_actions_t failed = step(WIFI_EVENT_DISCONNECTED, now);
        CHECK(!failed.connect && failed.retry_in_ms == expected_ms[attempt]);
        now += failed.retry_in_ms;
        if (now >= 120 * 1000) {
            break;
        }
        CHECK(step(WIFI_EVENT_RETRY_DUE, now).connect);
    }
}

static void test_rescue_after_two_minutes(void)
{
    wifi_plan_init(&plan, true);
    step(WIFI_EVENT_STARTED, 0);

    wifi_plan_actions_t actions = step(WIFI_EVENT_DISCONNECTED, 119 * 1000);
    CHECK(plan.phase == WIFI_PHASE_CONNECTING && !actions.start_access_point);

    actions = step(WIFI_EVENT_DISCONNECTED, 120 * 1000);
    CHECK(plan.phase == WIFI_PHASE_RESCUE);
    CHECK(actions.start_access_point && actions.retry_in_ms == 60 * 1000);

    /* it keeps trying, without starting the access point again */
    CHECK(step(WIFI_EVENT_RETRY_DUE, 180 * 1000).connect);
    actions = step(WIFI_EVENT_DISCONNECTED, 185 * 1000);
    CHECK(!actions.start_access_point && actions.retry_in_ms == 60 * 1000);

    /* the network came back: the access point goes */
    actions = step(WIFI_EVENT_CONNECTED, 250 * 1000);
    CHECK(plan.phase == WIFI_PHASE_ONLINE && actions.stop_access_point);
}

static void test_lost_network_starts_a_fresh_run(void)
{
    wifi_plan_init(&plan, true);
    step(WIFI_EVENT_STARTED, 0);
    step(WIFI_EVENT_CONNECTED, 1000);

    /* after a week online a drop is tried again at once... */
    int64_t a_week_ms = 7LL * 24 * 3600 * 1000;
    wifi_plan_actions_t actions = step(WIFI_EVENT_DISCONNECTED, a_week_ms);
    CHECK(actions.connect && plan.phase == WIFI_PHASE_CONNECTING);

    /* ...and the two minutes to rescue count from the drop, not from boot */
    actions = step(WIFI_EVENT_DISCONNECTED, a_week_ms + 1000);
    CHECK(plan.phase == WIFI_PHASE_CONNECTING && actions.retry_in_ms == 2000);

    /* a pause from before the reconnect does not start a second attempt */
    step(WIFI_EVENT_CONNECTED, a_week_ms + 2000);
    CHECK(!step(WIFI_EVENT_RETRY_DUE, a_week_ms + 3000).connect);
}

int main(void)
{
    test_no_network_is_setup();
    test_connects_at_once();
    test_retries_back_off();
    test_rescue_after_two_minutes();
    test_lost_network_starts_a_fresh_run();
    REPORT();
}
