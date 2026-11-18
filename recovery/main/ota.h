// SPDX-License-Identifier: MIT
#pragma once
#include "control.h"
#include "ota_stream.h"

/* Header checks, including this chip's revision, before any erase or consent. */
const char *recovery_ota_check_header(const uint8_t header[OTA_HEAD_BYTES]);
/* Starting only validates size; the first feed after confirmation begins writing. */
bool recovery_ota_start(ota_stream_t *stream, size_t image_size);
/* Explicit physical retry of the installed image; never automatic after rollback. */
bool recovery_ota_retry(void);
