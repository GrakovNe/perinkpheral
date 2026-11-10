/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "wifi_plan.h"

#define FIRST_RETRY_MS     2000
#define LONGEST_RETRY_MS   (30 * 1000)
#define RESCUE_AFTER_MS    (120 * 1000)
#define RESCUE_RETRY_MS    (60 * 1000)

void wifi_plan_init(wifi_plan_t *plan, bool has_network)
{
    plan->phase = WIFI_PHASE_SETUP;
    plan->has_network = has_network;
    plan->trying_since_ms = 0;
    plan->failed_attempts = 0;
}

bool wifi_phase_has_access_point(wifi_phase_t phase)
{
    return phase == WIFI_PHASE_SETUP || phase == WIFI_PHASE_RESCUE;
}

/* 2 s, 4 s, 8 s ... and no longer than half a minute. */
static uint32_t pause_before_attempt(int failed_attempts)
{
    uint32_t pause_ms = FIRST_RETRY_MS;
    for (int attempt = 1; attempt < failed_attempts && pause_ms < LONGEST_RETRY_MS; attempt++) {
        pause_ms *= 2;
    }
    return pause_ms < LONGEST_RETRY_MS ? pause_ms : LONGEST_RETRY_MS;
}

static void begin_trying(wifi_plan_t *plan, int64_t now_ms)
{
    plan->phase = WIFI_PHASE_CONNECTING;
    plan->trying_since_ms = now_ms;
    plan->failed_attempts = 0;
}

static wifi_plan_actions_t on_disconnected(wifi_plan_t *plan, int64_t now_ms)
{
    wifi_plan_actions_t actions = { 0 };
    if (plan->phase == WIFI_PHASE_ONLINE) {
        /* It worked a moment ago: try again at once. */
        begin_trying(plan, now_ms);
        actions.connect = true;
        return actions;
    }
    plan->failed_attempts++;
    if (plan->phase == WIFI_PHASE_CONNECTING &&
        now_ms - plan->trying_since_ms >= RESCUE_AFTER_MS) {
        plan->phase = WIFI_PHASE_RESCUE;
        actions.start_access_point = true;
    }
    actions.retry_in_ms = plan->phase == WIFI_PHASE_RESCUE
                              ? RESCUE_RETRY_MS
                              : pause_before_attempt(plan->failed_attempts);
    return actions;
}

wifi_plan_actions_t wifi_plan_step(wifi_plan_t *plan, wifi_plan_event_t event, int64_t now_ms)
{
    wifi_plan_actions_t actions = { 0 };
    if (!plan->has_network) {
        /* Nothing to connect to: the access point from the start, and
         * nothing else ever. */
        actions.start_access_point = event == WIFI_EVENT_STARTED;
        return actions;
    }
    switch (event) {
    case WIFI_EVENT_STARTED:
        begin_trying(plan, now_ms);
        actions.connect = true;
        break;
    case WIFI_EVENT_CONNECTED:
        actions.stop_access_point = wifi_phase_has_access_point(plan->phase);
        plan->phase = WIFI_PHASE_ONLINE;
        plan->failed_attempts = 0;
        break;
    case WIFI_EVENT_DISCONNECTED:
        actions = on_disconnected(plan, now_ms);
        break;
    case WIFI_EVENT_RETRY_DUE:
        /* A pause that ran out after the network came back means nothing. */
        actions.connect = plan->phase != WIFI_PHASE_ONLINE;
        break;
    }
    return actions;
}
