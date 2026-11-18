/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "wifi_credentials.h"
#include "tinytest.h"

int main(void)
{
    const char ssid[] = "12345678901234567890123456789012";
    const char key[] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    struct {
        unsigned char ssid[32];
        unsigned char password[64];
        unsigned char guard;
    } fields;
    memset(&fields, 0xa5, sizeof fields);
    CHECK(wifi_copy_field(fields.ssid, sizeof fields.ssid, ssid) == 32);
    CHECK(memcmp(fields.ssid, ssid, 32) == 0);
    CHECK(fields.password[0] == 0xa5);
    CHECK(wifi_copy_field(fields.password, sizeof fields.password, key) == 64);
    CHECK(memcmp(fields.password, key, 64) == 0);
    CHECK(fields.guard == 0xa5);

    /* Reusing a driver configuration must not leave an old credential's tail. */
    CHECK(wifi_copy_field(fields.ssid, sizeof fields.ssid, "Desk") == 4);
    CHECK_STR((char *)fields.ssid, "Desk");
    for (size_t index = 4; index < sizeof fields.ssid; index++) {
        CHECK(fields.ssid[index] == 0);
    }
    CHECK(wifi_copy_field(fields.password, sizeof fields.password, "") == 0);
    for (size_t index = 0; index < sizeof fields.password; index++) {
        CHECK(fields.password[index] == 0);
    }
    CHECK(wifi_copy_field(fields.ssid, sizeof fields.ssid, key) == sizeof fields.ssid);
    CHECK(memcmp(fields.ssid, key, sizeof fields.ssid) == 0);
    CHECK(fields.password[0] == 0);
    CHECK(fields.guard == 0xa5);
    REPORT();
}
