/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The longest text wifi_qr_text() writes: every character of a 32 byte
 * name escaped, plus the frame around it. */
#define WIFI_QR_TEXT_MAX 96

/* The text that, as a QR code, makes a phone's camera offer to join the
 * open network `ssid`. False if it does not fit into `capacity`. Pure. */
bool wifi_qr_text(const char *ssid, char *text, size_t capacity);

#ifdef __cplusplus
}
#endif
