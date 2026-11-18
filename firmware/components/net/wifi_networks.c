/* SPDX-License-Identifier: MIT */
#include "wifi_networks.h"

#include <stddef.h>
#include <string.h>

static bool is_text_ssid(const char ssid[WIFI_SSID_MAX + 1])
{
    const char *end = memchr(ssid, '\0', WIFI_SSID_MAX + 1);
    if (end == NULL || end == ssid) {
        return false;
    }
    const unsigned char *text = (const unsigned char *)ssid;
    size_t length = (size_t)(end - ssid);
    for (size_t at = 0; at < length;) {
        uint32_t point = text[at++];
        if (point < 0x80) {
            continue;
        }
        int remaining;
        uint32_t minimum;
        if (point >= 0xc2 && point <= 0xdf) {
            remaining = 1;
            minimum = 0x80;
            point &= 0x1f;
        } else if (point >= 0xe0 && point <= 0xef) {
            remaining = 2;
            minimum = 0x800;
            point &= 0x0f;
        } else if (point >= 0xf0 && point <= 0xf4) {
            remaining = 3;
            minimum = 0x10000;
            point &= 0x07;
        } else {
            return false;
        }
        if (at + (size_t)remaining > length) {
            return false;
        }
        while (remaining-- > 0) {
            unsigned char next = text[at++];
            if ((next & 0xc0) != 0x80) {
                return false;
            }
            point = (point << 6) | (next & 0x3f);
        }
        if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff)) {
            return false;
        }
    }
    return true;
}

void wifi_networks_add(wifi_network_t *records, uint16_t capacity, uint16_t *count,
                       const wifi_network_t *candidate)
{
    if (records == NULL || count == NULL || candidate == NULL || capacity == 0 ||
        *count > capacity || !is_text_ssid(candidate->ssid)) {
        return;
    }
    for (uint16_t index = 0; index < *count; index++) {
        if (strcmp(records[index].ssid, candidate->ssid) == 0) {
            if (records[index].rssi >= candidate->rssi) {
                return;
            }
            memmove(records + index, records + index + 1,
                    (size_t)(*count - index - 1) * sizeof *records);
            --*count;
            break;
        }
    }
    uint16_t position = 0;
    while (position < *count && records[position].rssi >= candidate->rssi) {
        position++;
    }
    if (position >= capacity) {
        return;
    }
    if (*count < capacity) {
        ++*count;
    }
    memmove(records + position + 1, records + position,
            (size_t)(*count - position - 1) * sizeof *records);
    records[position] = *candidate;
}
