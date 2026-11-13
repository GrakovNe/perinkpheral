/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Device: what the device is and where to find it - its name, its network
 * and address, the firmware, memory and uptime. A system application: all
 * of it is asked of the host with sys_info().
 */
#include <stdbool.h>

#include "app_text.h"
#include "host_api.h"
#include "strings.h" /* made of the `strings` folder by tools/mkapp */

#define MARGIN            16
#define ROW_HEIGHT        34 /* from one baseline to the next; the first is one row down */
#define VALUE_MAX         47
#define MS_PER_MINUTE     60000
#define MINUTES_PER_HOUR  60

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
    switch (event) {
    case BTN_OK_PRESSED:
        /* Memory and uptime are as of the last draw: OK reads them again. */
        request_redraw(HOST_REDRAW_NORMAL);
        break;
    }
}

void app_on_draw(void)
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
