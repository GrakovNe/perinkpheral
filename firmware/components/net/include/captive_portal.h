/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>

#define CAPTIVE_PORTAL_ADDRESS "192.168.4.1"
#define CAPTIVE_PORTAL_URL "http://" CAPTIVE_PORTAL_ADDRESS "/"

/* HTTP probes to foreign hosts need the local address before page API calls work. */
bool captive_portal_redirect(bool access_point, const char *host, const char *path,
                             const char *station_address, const char *hostname);
