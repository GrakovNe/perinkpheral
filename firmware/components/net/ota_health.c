/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "ota_health.h"

ota_health_t ota_health(const ota_health_input_t *input)
{
    bool reachable = input->network_stored
                         ? input->online
                         : input->access_point_up &&
                               input->uptime_s >= OTA_HEALTH_SETUP_SETTLE_S;
    if (input->web_server_up && reachable) {
        return OTA_HEALTH_CONFIRM;
    }
    return input->uptime_s >= OTA_HEALTH_DEADLINE_S ? OTA_HEALTH_ROLL_BACK : OTA_HEALTH_WAIT;
}
