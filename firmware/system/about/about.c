/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * About: what the device is, on two pages that OK turns between. The first
 * is the mark, the version of the firmware, where the project lives - as a
 * code a phone reads - and whose it is; it is the splash at power-up too.
 * The second is the device's state: its name, its network and address, the
 * firmware and its slot, memory and uptime, all asked of the host with
 * sys_info(). A system application: part of the firmware, written with the
 * same SDK as any other.
 */
#include <stdbool.h>

#include "app_text.h"
#include "host_api.h"
#include "strings.h" /* made of the `strings` folder by tools/mkapp */

#define PROJECT_ADDRESS "https://github.com/GrakovNe/perinkpheral"

#define LOGO_SCALE    5
#define LOGO_TOP      24
#define VERSION_TOP   68
#define CODE_TOP      112
#define MODULE_PIXELS 4
/* The small print at the bottom: two lines. */
#define CREDITS_TOP   256
#define CREDITS_STEP  16

#define MARGIN            16
#define ROW_HEIGHT        34 /* from one baseline to the next; the first is one row down */
#define VALUE_MAX         47
#define MS_PER_MINUTE     60000
#define MINUTES_PER_HOUR  60

/* Every start is at the mark: the splash is this page. */
static bool shows_the_state;

static void draw_centered(int32_t top, int32_t font, const char *text)
{
    int32_t length = app_text_length(text);
    gfx_text((gfx_width() - gfx_text_width(font, text, length)) / 2, top, font, text, length);
}

static void draw_row(int32_t row, const char *label, const char *value)
{
    int32_t top = (row + 1) * ROW_HEIGHT - gfx_font_ascent(HOST_FONT_TEXT);
    int32_t value_length = app_text_length(value);
    gfx_text(MARGIN, top, HOST_FONT_TEXT, label, app_text_length(label));
    gfx_text(gfx_width() - MARGIN - gfx_text_width(HOST_FONT_TEXT, value, value_length), top,
             HOST_FONT_TEXT, value, value_length);
}

/* What the host knows under `what`, as a string; empty when it is not
 * known or does not fit. */
static void read_info(int32_t what, char value[VALUE_MAX + 1])
{
    int32_t length = sys_info(what, value, VALUE_MAX);
    value[length > 0 ? length : 0] = '\0';
}

static bool is_text(const char *text, const char *expected)
{
    int32_t length = app_text_length(expected);
    if (app_text_length(text) != length) {
        return false;
    }
    for (int32_t at = 0; at < length; at++) {
        if (text[at] != expected[at]) {
            return false;
        }
    }
    return true;
}

void app_on_button(int32_t event)
{
    if (event == BTN_OK_PRESSED) {
        /* Round and round; memory and uptime are read anew each time. */
        shows_the_state = !shows_the_state;
        request_redraw(HOST_REDRAW_NORMAL);
    }
}

static void draw_the_mark(void)
{
    sys_logo((gfx_width() - sys_logo_width(LOGO_SCALE)) / 2, LOGO_TOP, LOGO_SCALE);

    char version[48];
    app_text_t text;
    app_text_start(&text, version, sizeof version);
    app_text_add(&text, "v");
    int32_t prefix = app_text_length(version);
    int32_t length = sys_info(SYS_INFO_VERSION, version + prefix, sizeof version - prefix - 1);
    version[prefix + (length > 0 ? length : 0)] = '\0';
    draw_centered(VERSION_TOP, HOST_FONT_TEXT, version);

    int32_t modules = gfx_qr(0, 0, 0, PROJECT_ADDRESS, sizeof PROJECT_ADDRESS - 1);
    gfx_qr((gfx_width() - modules * MODULE_PIXELS) / 2, CODE_TOP, MODULE_PIXELS, PROJECT_ADDRESS,
           sizeof PROJECT_ADDRESS - 1);

    draw_centered(CREDITS_TOP, HOST_FONT_SMALL, "(c) 2026 Max Grakov");
    draw_centered(CREDITS_TOP + CREDITS_STEP, HOST_FONT_SMALL, "MIT License");
}

static void draw_the_state(void)
{
    char value[VALUE_MAX + 1];
    int32_t row = 0;

    read_info(SYS_INFO_NAME, value);
    draw_row(row++, app_string(STR_NAME), value);

    /* On its network, or still looking for it, or waiting to be told one:
     * then it is found on its own access point, at the address below. */
    read_info(SYS_INFO_WIFI, value);
    if (is_text(value, SYS_WIFI_ONLINE)) {
        read_info(SYS_INFO_NETWORK, value);
        draw_row(row++, app_string(STR_WIFI), value);
    } else {
        draw_row(row++, app_string(STR_WIFI),
                 app_string(is_text(value, SYS_WIFI_CONNECTING) ? STR_CONNECTING : STR_SETUP_MODE));
    }
    read_info(SYS_INFO_ADDRESS, value);
    draw_row(row++, app_string(STR_IP_ADDRESS), value);

    read_info(SYS_INFO_VERSION, value);
    draw_row(row++, app_string(STR_FIRMWARE), value);
    read_info(SYS_INFO_SLOT, value);
    draw_row(row++, app_string(STR_SLOT), value);

    app_text_t text;
    read_info(SYS_INFO_FREE_MEMORY, value);
    app_text_start(&text, value + app_text_length(value), sizeof value - app_text_length(value));
    app_text_add(&text, app_string(STR_UNIT_KB));
    draw_row(row++, app_string(STR_FREE_MEMORY), value);

    int32_t minutes = (int32_t)(time_uptime_ms() / MS_PER_MINUTE);
    app_text_start(&text, value, sizeof value);
    if (minutes >= MINUTES_PER_HOUR) {
        app_text_add_number(&text, minutes / MINUTES_PER_HOUR);
        app_text_add(&text, app_string(STR_UNIT_H));
    }
    app_text_add_number(&text, minutes % MINUTES_PER_HOUR);
    app_text_add(&text, app_string(STR_UNIT_MIN));
    draw_row(row++, app_string(STR_UPTIME), value);
}

void app_on_draw(void)
{
    if (shows_the_state) {
        draw_the_state();
    } else {
        draw_the_mark();
    }
}
