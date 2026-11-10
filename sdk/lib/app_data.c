/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The buffer the host fills with a piece of a response before it calls
 * app_on_data(). One for every application, whether it has polls or not:
 * a kilobyte of the sixty-four.
 */
#include "host_api.h"

uint8_t *app_data_buffer(void)
{
    static uint8_t buffer[HOST_DATA_CHUNK_MAX];
    return buffer;
}
