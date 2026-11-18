/* SPDX-License-Identifier: MIT */
#include "captive_portal.h"

#include <string.h>
#include <strings.h>

static bool host_is(const char *host, size_t length, const char *name)
{
    return name != NULL && name[0] && strlen(name) == length &&
           strncasecmp(host, name, length) == 0;
}

bool captive_portal_redirect(bool access_point, const char *host, const char *path,
                             const char *station_address, const char *hostname)
{
    if (!access_point || host == NULL || !host[0] || path == NULL ||
        strncmp(path, "/api/", 5) == 0) {
        return false;
    }
    size_t length = strcspn(host, ":");
    if (host_is(host, length, CAPTIVE_PORTAL_ADDRESS) ||
        host_is(host, length, station_address) || host_is(host, length, hostname)) {
        return false;
    }
    if (hostname != NULL) {
        size_t name_length = strlen(hostname);
        if (name_length && length == name_length + sizeof ".local" - 1 &&
            strncasecmp(host, hostname, name_length) == 0 &&
            strncasecmp(host + name_length, ".local", sizeof ".local" - 1) == 0) {
            return false;
        }
    }
    return true;
}
