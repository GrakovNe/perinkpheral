/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

/* The priorities of every task of the firmware, in one place. A number is
 * a FreeRTOS priority: the larger runs first.
 *
 * The rule: what the user sees and touches comes before what is computed
 * for them, and what keeps the radio alive comes before both. A task that
 * only computes goes under the host loop. A new task is given its tier
 * here before it is written.
 *
 * Tier          Task                 Priority
 * ------------  -------------------  --------
 * network       Wi-Fi (ESP-IDF)      23         not ours to move
 *               esp_timer (ESP-IDF)  22
 *               events (ESP-IDF)     20
 *               TCP/IP (ESP-IDF)     18
 * input         buttons              10
 * screen        host loop             6
 * servers       web server            5
 *               wifi plan             5
 * background    poller                4
 *               ota probation         4
 *               captive DNS           4
 * the rest      development console   2
 *               mDNS (sdkconfig)      1
 *
 * Interrupts are all of one level; a handler of ours hands on and does
 * nothing else. Events have two queues and the host loop takes input
 * first (main/events.c). */

#define PRIORITY_NETWORK_LEAST 18 /* ESP-IDF's TCP/IP task: nothing of ours reaches it */

#define PRIORITY_INPUT       10
#define PRIORITY_HOST_LOOP   6
#define PRIORITY_SERVER      5
#define PRIORITY_BACKGROUND  4
#define PRIORITY_DEV_CONSOLE 2

_Static_assert(PRIORITY_NETWORK_LEAST > PRIORITY_INPUT && PRIORITY_INPUT > PRIORITY_HOST_LOOP &&
                   PRIORITY_HOST_LOOP > PRIORITY_SERVER && PRIORITY_SERVER > PRIORITY_BACKGROUND &&
                   PRIORITY_BACKGROUND > PRIORITY_DEV_CONSOLE,
               "the tiers keep their order");
