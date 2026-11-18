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

/* Updates are written by the separate recovery firmware. Failed probation
 * returns to recovery, which remains selected until another upload or physical retry. */

/* Starts the probation of a freshly installed image, if this is one.
 * network_stored: there are Wi-Fi settings to connect with. */
void ota_start(bool network_stored);

/* True while this image is still on probation. */
bool ota_is_on_probation(void);

/* Select recovery durably; the caller then performs an orderly restart.
 * An update keeps its preparation screen until the upload arrives. */
const char *ota_enter_recovery(bool update);

#ifdef __cplusplus
}
#endif
