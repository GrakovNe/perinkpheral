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
 * Tier          Task                 Priority  Stack, bytes
 * ------------  -------------------  --------  ------------
 * network       Wi-Fi (ESP-IDF)      23        not ours to move
 *               esp_timer (ESP-IDF)  22
 *               events (ESP-IDF)     20
 *               TCP/IP (ESP-IDF)     18
 * input         buttons              10        3072
 * screen        host loop             6        12288 (the main task: sdkconfig)
 * servers       web server            5        6144
 *               wifi plan             5        4096
 * background    poller                4        8192  the TLS handshake runs on it
 *               ota probation         4        3072
 *               captive DNS           4        3072
 * the rest      development console   2        3072
 *               mDNS (sdkconfig)      1
 *
 * A stack is taken from the heap for as long as its task lives: these are
 * 30 KB of it, always.
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

#define STACK_BUTTONS       3072
#define STACK_WEB_SERVER    6144
#define STACK_WEB_WAITS     6144 /* a handler of the web server's, while it waits: its stack */
#define STACK_WIFI_PLAN     4096
#define STACK_POLLER        8192
#define STACK_OTA_PROBATION 3072
#define STACK_CAPTIVE_DNS   3072
#define STACK_DEV_CONSOLE   3072

_Static_assert(PRIORITY_NETWORK_LEAST > PRIORITY_INPUT && PRIORITY_INPUT > PRIORITY_HOST_LOOP &&
                   PRIORITY_HOST_LOOP > PRIORITY_SERVER && PRIORITY_SERVER > PRIORITY_BACKGROUND &&
                   PRIORITY_BACKGROUND > PRIORITY_DEV_CONSOLE,
               "the tiers keep their order");
