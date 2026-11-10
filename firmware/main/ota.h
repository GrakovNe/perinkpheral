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

/* Firmware updates over the network: an image is uploaded into the spare
 * slot, the device restarts into it, and the new image is kept only if it
 * comes back reachable (see ota_health); otherwise the bootloader returns
 * to the old one. */

/* Starts the probation of a freshly installed image, if this is one.
 * network_stored: there are Wi-Fi settings to connect with. */
void ota_start(bool network_stored);

/* True while this image is still on probation. */
bool ota_is_on_probation(void);

/* An upload, one at a time. Each returns NULL or what went wrong; after
 * an error the upload is over. */
const char *ota_upload_begin(size_t image_size);
const char *ota_upload_data(const uint8_t *data, size_t length);
/* The image is in place: restart to run it. */
const char *ota_upload_end(void);
void ota_upload_abort(void);

#ifdef __cplusplus
}
#endif
