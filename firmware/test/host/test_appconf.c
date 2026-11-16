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
    char id[APPCONF_DEVICE_ID_SIZE];
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

static void test_default_app(void)
{
    appconf_t c;
    char err[128];
    appconf_defaults(&c, ID);
    CHECK(c.default_app[0] == '\0' && c.menu_idle_min == 5);
    CHECK(appconf_validate(&c, err, sizeof err));

    strcpy(c.default_app, "clock");
    CHECK(appconf_validate(&c, err, sizeof err));
    strcpy(c.default_app, "sys.settings"); /* a dot is in no installed name */
    CHECK(!appconf_validate(&c, err, sizeof err));
    strcpy(c.default_app, "Clock");
    CHECK(!appconf_validate(&c, err, sizeof err));
    c.default_app[0] = '\0';

    c.menu_idle_min = 0;
    CHECK(!appconf_validate(&c, err, sizeof err));
    c.menu_idle_min = 61;
    CHECK(!appconf_validate(&c, err, sizeof err));
    c.menu_idle_min = 60;
    CHECK(appconf_validate(&c, err, sizeof err));
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

/* The settings by key: every field reachable, read and written as text by
 * the same rules the page and the system API use. */
static void test_fields(void)
{
    appconf_t c;
    appconf_defaults(&c, "1a2b");
    int count;
    const appconf_field_t *fields = appconf_fields(&count);
    CHECK(count > 0);
    for (int i = 0; i < count; i++) {
        /* Found by its own key, stored under a key NVS can hold. */
        CHECK(appconf_field(fields[i].key, strlen(fields[i].key)) == &fields[i]);
        CHECK(strlen(fields[i].stored) <= 15);
        CHECK(fields[i].offset + fields[i].size <= sizeof(appconf_t));
        /* What is read back as text is taken again as it was. */
        char text[APPCONF_VALUE_MAX];
        appconf_format(&c, &fields[i], text, sizeof text);
        appconf_t again = c;
        CHECK(appconf_put(&again, &fields[i], text));
        CHECK(!appconf_differs(&c, &again, &fields[i]));
        for (int j = i + 1; j < count; j++) {
            CHECK(strcmp(fields[i].key, fields[j].key) != 0);
            CHECK(strcmp(fields[i].stored, fields[j].stored) != 0);
        }
    }
    CHECK(appconf_field("nope", 4) == NULL);
    CHECK(appconf_field("font", 3) == NULL);

    /* A number in its range, its one extra value, and nothing else. */
    const appconf_field_t *full = appconf_field("refresh_full_every", 18);
    CHECK(appconf_put(&c, full, "50"));
    CHECK(c.refresh_full_every == 50);
    CHECK(appconf_put(&c, full, "255"));
    CHECK(c.refresh_full_every == APPCONF_REFRESH_FULL_NEVER);
    CHECK(!appconf_put(&c, full, "51"));
    CHECK(!appconf_put(&c, full, "-1"));
    CHECK(!appconf_put(&c, full, " 5"));
    CHECK(!appconf_put(&c, full, "5x"));
    CHECK(!appconf_put(&c, full, ""));
    CHECK(c.refresh_full_every == APPCONF_REFRESH_FULL_NEVER);

    /* Two bytes wide. */
    const appconf_field_t *data = appconf_field("refresh_min_data_s", 18);
    CHECK(appconf_put(&c, data, "3600"));
    CHECK(c.refresh_min_data_s == 3600);
    CHECK(appconf_number(&c, data) == 3600);
    CHECK(!appconf_put(&c, data, "70000"));

    /* A flag is 0 or 1. */
    const appconf_field_t *dither = appconf_field("dither", 6);
    CHECK(dither->kind == APPCONF_FLAG);
    CHECK(appconf_put(&c, dither, "0"));
    CHECK(c.dither == 0);
    CHECK(!appconf_put(&c, dither, "2"));

    /* A text that fits, and one that does not. */
    const appconf_field_t *zone = appconf_field("tz", 2);
    CHECK(appconf_put(&c, zone, "MSK-3"));
    CHECK_STR(c.tz, "MSK-3");
    char long_text[APPCONF_TZ_MAX + 2];
    memset(long_text, 'A', sizeof long_text - 1);
    long_text[sizeof long_text - 1] = '\0';
    CHECK(!appconf_put(&c, zone, long_text));
    CHECK_STR(c.tz, "MSK-3");

    /* The network is set its own way, and its password never read back. */
    const appconf_field_t *password = appconf_field("wifi_password", 13);
    CHECK((password->flags & APPCONF_SECRET) != 0);
    CHECK((password->flags & APPCONF_OWN_WAY) != 0);
    CHECK((appconf_field("wifi_ssid", 9)->flags & APPCONF_OWN_WAY) != 0);

    /* What is taken at start asks for a restart; what takes effect at once
     * does not. */
    appconf_t before = c, now = c;
    CHECK(!appconf_needs_restart(&before, &now));
    CHECK(appconf_put(&now, zone, "UTC0"));
    CHECK(appconf_put(&now, dither, "1"));
    CHECK(!appconf_needs_restart(&before, &now));
    CHECK(appconf_put(&now, appconf_field("device_name", 11), "Desk"));
    CHECK(appconf_needs_restart(&before, &now));
}

int main(void)
{
    test_fields();
    test_refresh_policy_settings();
    test_default_app();
    test_device_id();
    test_hostname();
    test_defaults();
    test_derive();
    test_validate_name();
    test_validate_wifi();
    test_validate_ntp_tz();
    REPORT();
}
