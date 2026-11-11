/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The name server of the setup access point: whatever name a phone asks
 * for, the answer is the device. That is what makes the phone decide it
 * is behind a sign-in page and open the device's page by itself. */

/* A question and one address record fit into this; a longer question is
 * not answered. */
#define CAPTIVE_DNS_PACKET_MAX 512

/* Writes the answer to `query` into `answer` and returns its length, or 0
 * when the packet is not a question worth answering. A question for an
 * IPv4 address gets `address`; any other question gets "no such record".
 * Pure. */
size_t captive_dns_answer(const uint8_t *query, size_t query_length, const uint8_t address[4],
                          uint8_t *answer, size_t capacity);

/* Starts the task that answers on port 53 while captive_dns_enable(true). */
void captive_dns_start(const uint8_t address[4]);
void captive_dns_enable(bool enabled);

#ifdef __cplusplus
}
#endif
