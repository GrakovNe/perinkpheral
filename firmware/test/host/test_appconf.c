/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "appconf.h"

#include <string.h>

#include "tinytest.h"

static const char ID[] = "1a2b";

static void test_device_id(void)
{
    const uint8_t mac1[6] = { 0x7c, 0x4f, 0xad, 0xbb, 0x1a, 0x2b };
    char id[5];
    appconf_device_id(mac1, id);
    CHECK_STR(id, "1a2b");

    const uint8_t mac2[6] = { 0, 0, 0, 0, 0xff, 0x0a };
    appconf_device_id(mac2, id);
    CHECK_STR(id, "ff0a");
}

static void test_hostname(void)
{
    char h[APPCONF_HOST_MAX + 1];

    appconf_hostname("Perinkpheral-1A2B", ID, h, sizeof h);
    CHECK_STR(h, "perinkpheral-1a2b");

    appconf_hostname("  --foo--bar--  ", ID, h, sizeof h);
    CHECK_STR(h, "foo-bar");

    appconf_hostname("-a-", ID, h, sizeof h);
    CHECK_STR(h, "a");

    appconf_hostname("a_b!", ID, h, sizeof h);
    CHECK_STR(h, "a-b");

    appconf_hostname("\xC3\xA9\xC3\xA0", ID, h, sizeof h);
    CHECK_STR(h, "perinkpheral-1a2b");

    appconf_hostname("", ID, h, sizeof h);
    CHECK_STR(h, "perinkpheral-1a2b");

    char long_name[APPCONF_NAME_MAX + 1];
    memset(long_name, 'a', sizeof long_name - 1);
    long_name[sizeof long_name - 1] = '\0';
    appconf_hostname(long_name, ID, h, sizeof h);
    CHECK(strlen(h) == APPCONF_NAME_MAX);
}

static void test_defaults(void)
{
    appconf_t c;
    appconf_defaults(&c, ID);

    CHECK_STR(c.device_name, "Perinkpheral-1A2B");
    CHECK_STR(c.hostname, "perinkpheral-1a2b");
    CHECK_STR(c.ntp_server, "pool.ntp.org");
    CHECK_STR(c.tz, "UTC0");
    CHECK(c.wifi_ssid[0] == '\0');
    CHECK(c.wifi_password[0] == '\0');

    char err[128];
    CHECK(appconf_validate(&c, err, sizeof err));
}

static void test_derive(void)
{
    appconf_t c;
    appconf_defaults(&c, ID);
    strcpy(c.device_name, "Desk Screen!!");
    appconf_derive(&c, ID);
    CHECK_STR(c.hostname, "desk-screen");
}

static void test_validate_name(void)
{
    appconf_t c;
    char err[128];

    appconf_defaults(&c, ID);
    c.device_name[0] = '\0';
    CHECK(!appconf_validate(&c, err, sizeof err));

    appconf_defaults(&c, ID);
    memset(c.device_name, 'x', APPCONF_NAME_MAX + 1);
    CHECK(!appconf_validate(&c, err, sizeof err));

    appconf_defaults(&c, ID);
    memset(c.device_name, 'x', APPCONF_NAME_MAX);
    CHECK(appconf_validate(&c, err, sizeof err));

    appconf_defaults(&c, ID);
    c.device_name[3] = '\t';
    CHECK(!appconf_validate(&c, err, sizeof err));
}

static void test_validate_wifi(void)
{
    appconf_t c;
    char err[128];

    appconf_defaults(&c, ID);
    strcpy(c.wifi_ssid, "CafeOpen");
    CHECK(appconf_validate(&c, err, sizeof err));

    appconf_defaults(&c, ID);
    strcpy(c.wifi_ssid, "Home");
    strcpy(c.wifi_password, "1234567");
    CHECK(!appconf_validate(&c, err, sizeof err));
    strcpy(c.wifi_password, "12345678");
    CHECK(appconf_validate(&c, err, sizeof err));

    appconf_defaults(&c, ID);
    strcpy(c.wifi_password, "12345678");
    CHECK(!appconf_validate(&c, err, sizeof err));

    appconf_defaults(&c, ID);
    strcpy(c.wifi_ssid, "Home");
    strcpy(c.wifi_password, "p\xC3\xA4ssword123");
    CHECK(!appconf_validate(&c, err, sizeof err));
}

static void test_validate_ntp_tz(void)
{
    appconf_t c;
    char err[128];

    appconf_defaults(&c, ID);
    c.ntp_server[0] = '\0';
    CHECK(!appconf_validate(&c, err, sizeof err));

    appconf_defaults(&c, ID);
    strcpy(c.ntp_server, "bad server");
    CHECK(!appconf_validate(&c, err, sizeof err));

    appconf_defaults(&c, ID);
    strcpy(c.tz, "MSK-3");
    CHECK(appconf_validate(&c, err, sizeof err));
    strcpy(c.tz, "CET-1CEST,M3.5.0,M10.5.0/3");
    CHECK(appconf_validate(&c, err, sizeof err));
    strcpy(c.tz, "with space");
    CHECK(!appconf_validate(&c, err, sizeof err));
    c.tz[0] = '\0';
    CHECK(!appconf_validate(&c, err, sizeof err));
}

static void test_refresh_policy_settings(void)
{
    appconf_t c;
    char err[128];
    appconf_defaults(&c, ID);
    CHECK(c.refresh_min_data_s == 60 && c.refresh_min_button_ms == 0);
    CHECK(c.refresh_full_every == 5 && c.refresh_keepalive_h == 24);
    CHECK(appconf_validate(&c, err, sizeof err));

    /* each number has a range, and its ends are inside it */
    c.refresh_min_data_s = 9;
    CHECK(!appconf_validate(&c, err, sizeof err));
    c.refresh_min_data_s = 10;
    CHECK(appconf_validate(&c, err, sizeof err));
    c.refresh_min_data_s = 3601;
    CHECK(!appconf_validate(&c, err, sizeof err));
    c.refresh_min_data_s = 3600;

    c.refresh_min_button_ms = 10001;
    CHECK(!appconf_validate(&c, err, sizeof err));
    c.refresh_min_button_ms = 10000;
    CHECK(appconf_validate(&c, err, sizeof err));

    c.refresh_full_every = 254;
    CHECK(!appconf_validate(&c, err, sizeof err));
    c.refresh_full_every = 51;
    CHECK(!appconf_validate(&c, err, sizeof err));
    c.refresh_full_every = 0; /* always full */
    CHECK(appconf_validate(&c, err, sizeof err));
    c.refresh_full_every = APPCONF_REFRESH_FULL_NEVER;
    CHECK(appconf_validate(&c, err, sizeof err));
    c.dither = 2;
    CHECK(!appconf_validate(&c, err, sizeof err));
    c.dither = 0;
    CHECK(appconf_validate(&c, err, sizeof err));
    c.refresh_full_every = 50;

    c.refresh_keepalive_h = 0;
    CHECK(!appconf_validate(&c, err, sizeof err));
    c.refresh_keepalive_h = 169;
    CHECK(!appconf_validate(&c, err, sizeof err));
    c.refresh_keepalive_h = 168;
    CHECK(appconf_validate(&c, err, sizeof err));
}

int main(void)
{
    test_refresh_policy_settings();
    test_device_id();
    test_hostname();
    test_defaults();
    test_derive();
    test_validate_name();
    test_validate_wifi();
    test_validate_ntp_tz();
    REPORT();
}
