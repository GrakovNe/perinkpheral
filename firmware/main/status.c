/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "status.h"

void status_read(status_t *status)
{
    status->wifi_connected = false;
    status->date_known = false;
    status->date[0] = '\0';
}
