/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * About: the mark, the version of the firmware, where the project lives -
 * as a code a phone reads - and whose it is. A system application: it is
 * part of the firmware and written with the same SDK as any other.
 */
#include "app_text.h"
#include "host_api.h"

#define PROJECT_ADDRESS "https://github.com/GrakovNe/perinkpheral"

#define LOGO_SCALE    5
#define LOGO_TOP      24
#define VERSION_TOP   68
#define CODE_TOP      112
#define MODULE_PIXELS 4
/* The small print at the bottom: two lines. */
#define CREDITS_TOP   256
#define CREDITS_STEP  16

static void draw_centered(int32_t top, int32_t font, const char *text)
{
    int32_t length = app_text_length(text);
    gfx_text((gfx_width() - gfx_text_width(font, text, length)) / 2, top, font, text, length);
}

void app_on_button(int32_t event)
{
    if (event == BTN_OK_PRESSED) {
        request_exit();
    }
}

void app_on_draw(void)
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
