/* SPDX-License-Identifier: MIT */
#include "wifi_networks.h"
#include "tinytest.h"

static void add(wifi_network_t *records, uint16_t capacity, uint16_t *count,
                const char *ssid, int8_t rssi, bool secured)
{
    wifi_network_t network = { .rssi = rssi, .secured = secured };
    size_t length = strlen(ssid);
    CHECK(length <= WIFI_SSID_MAX);
    memcpy(network.ssid, ssid, length);
    wifi_networks_add(records, capacity, count, &network);
}

int main(void)
{
    struct {
        wifi_network_t networks[3];
        unsigned char guard;
    } output = { .guard = 0xa5 };
    uint16_t count = 0;
    add(output.networks, 3, &count, "Desk", -70, true);
    add(output.networks, 3, &count, "Guest", -40, false);
    add(output.networks, 3, &count, "Other", -80, true);
    CHECK(count == 3);
    CHECK_STR(output.networks[0].ssid, "Guest");
    CHECK_STR(output.networks[2].ssid, "Other");
    /* A mesh's repeated BSSIDs use one slot, retaining its strongest record. */
    add(output.networks, 3, &count, "Desk", -30, true);
    add(output.networks, 3, &count, "Desk", -60, false);
    CHECK(count == 3);
    CHECK_STR(output.networks[0].ssid, "Desk");
    CHECK(output.networks[0].rssi == -30 && output.networks[0].secured);
    add(output.networks, 3, &count, "Weak", -90, false);
    CHECK_STR(output.networks[2].ssid, "Other");
    add(output.networks, 3, &count, "Strong", -20, false);
    CHECK(count == 3 && output.guard == 0xa5);
    CHECK_STR(output.networks[0].ssid, "Strong");
    CHECK_STR(output.networks[1].ssid, "Desk");
    CHECK_STR(output.networks[2].ssid, "Guest");

    count = 0;
    add(output.networks, 3, &count, "12345678901234567890123456789012", -45, true);
    add(output.networks, 3, &count, "Дом", -35, true);
    add(output.networks, 3, &count, "Cafe\xf0\x9f\x8c\x9f", -55, false);
    CHECK(count == 3 && output.guard == 0xa5);
    CHECK(strlen(output.networks[1].ssid) == WIFI_SSID_MAX);
    CHECK_STR(output.networks[0].ssid, "Дом");

    const char *invalid[] = { "", "\xff", "\xc0\xaf", "\xed\xa0\x80",
                              "\xf4\x90\x80\x80", "\xe2\x82", ("\xe2" "ab") };
    for (size_t index = 0; index < sizeof invalid / sizeof invalid[0]; index++) {
        uint16_t empty = 0;
        add(output.networks, 3, &empty, invalid[index], -1, false);
        CHECK(empty == 0 && output.guard == 0xa5);
    }
    wifi_network_t unterminated;
    memset(&unterminated, 'x', sizeof unterminated);
    count = 0;
    wifi_networks_add(output.networks, 3, &count, &unterminated);
    CHECK(count == 0);
    add(output.networks, 0, &count, "No capacity", -10, false);
    CHECK(count == 0 && output.guard == 0xa5);

    wifi_network_t many[WIFI_SCAN_NETWORKS_MAX];
    count = 0;
    for (int index = 0; index < 40; index++) {
        char ssid[16];
        snprintf(ssid, sizeof ssid, "Network-%02d", index);
        add(many, WIFI_SCAN_NETWORKS_MAX, &count, ssid, (int8_t)(-80 + index), false);
        add(many, WIFI_SCAN_NETWORKS_MAX, &count, ssid, -100, true);
    }
    CHECK(count == WIFI_SCAN_NETWORKS_MAX);
    CHECK_STR(many[0].ssid, "Network-39");
    CHECK_STR(many[WIFI_SCAN_NETWORKS_MAX - 1].ssid, "Network-24");
    for (uint16_t index = 0; index < count; index++) {
        CHECK(!many[index].secured);
        if (index > 0) CHECK(many[index - 1].rssi >= many[index].rssi);
    }
    REPORT();
}
