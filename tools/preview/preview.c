/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Renders the host's screens on the host and writes them as PBM (P4),
 * one file per screen: what the panel would show, before flashing.
 * Built and run by tools/preview/preview.py.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "gfx_icons.h"
#include "kit.h"
#include "list.h"
#include "notice.h"
#include "screens.h"
#include "wm.h"

#define W 400
#define H 300

static uint8_t fb[W / 8 * H];
static gfx_t g;

/* P4 is 1 = black; the framebuffer is 1 = white. */
static int write_pbm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        return 1;
    }
    fprintf(f, "P4\n%d %d\n", W, H);
    for (size_t i = 0; i < sizeof fb; i++) {
        fputc((uint8_t)~fb[i], f);
    }
    fclose(f);
    return 0;
}

/* A small clock, the way an application's manifest would bring it. */
static const uint16_t CLOCK_ICON[SCREENS_ICON_CELLS] = {
    0x0F8, 0x104, 0x222, 0x421, 0x421, 0x439, 0x401, 0x401, 0x202, 0x104, 0x0F8,
};

#define ICONS_IN_A_ROW       4
#define ICON_SHEET_MARGIN    16
#define ICON_SHEET_STEP_X    96
#define ICON_SHEET_STEP_Y    50
#define ICON_SHEET_PAIR_GAP  32

static void row_from_array(void *context, int index, screens_menu_row_t *row)
{
    row->title = ((const char *const *)context)[index];
    row->icon = index == 0 ? CLOCK_ICON : NULL; /* the host's own pages have none */
}

static int save(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.pbm", dir, name);
    return write_pbm(path);
}

/* A cleared screen with the header drawn; `content` is what is left for
 * the view, the way the window manager hands it to a scene. */
static void start_screen(const screens_header_t *header, gfx_t *content)
{
    gfx_t header_surface;
    gfx_clear(&g, false);
    wm_split(&g, &header_surface, content);
    screens_header(&header_surface, header);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: preview <out-dir> [jersey]\n");
        return 2;
    }
    const char *dir = argv[1];
    gfx_init(&g, fb, W, H);
    /* preview <out-dir> jersey: everything in the other family. */
    if (argc > 2 && strcmp(argv[2], "jersey") == 0) {
        gfx_font_use(GFX_FAMILY_JERSEY);
    }

    const screens_header_t online = { true, NULL, "Wed 11 Nov" };
    /* As the device shows it before there is a network: no Wi-Fi, no date. */
    const screens_header_t offline = { false, NULL, NULL };
    gfx_t content;

    const char *titles[] = { "Clock", "Hello", "Device", "About" };
    start_screen(&online, &content);
    screens_menu(&content, row_from_array, titles, 4, 0);
    if (save(dir, "menu")) {
        return 1;
    }
    start_screen(&offline, &content);
    screens_menu(&content, row_from_array, titles, 4, 1);
    if (save(dir, "menu-offline")) {
        return 1;
    }

    /* A list longer than the screen: at its start, in the middle, at its end. */
    const char *many[] = { "Weather", "Clock", "Timer", "Hello", "Bad Apple",
                           "Settings", "Device", "About", "Calendar", "Plex" };
    const int selections[] = { 0, 5, 9 };
    const char *const names[] = { "menu-scroll-start", "menu-scroll-middle", "menu-scroll-end" };
    for (int shot = 0; shot < 3; shot++) {
        start_screen(&online, &content);
        screens_menu(&content, row_from_array, many, 10, selections[shot]);
        if (save(dir, names[shot])) {
            return 1;
        }
    }

    /* The host's notices take the whole screen. */
    gfx_init(&g, fb, W, H);
    const notice_t asking = { .first = "Update", .second = "firmware?", .icon = GFX_ICON_UPDATE,
        .ok = "Update", .back = "Cancel" };
    memset(fb, 0xFF, sizeof fb);
    notice_draw(&g, &asking);
    if (save(dir, "notice-question")) {
        return 1;
    }
    const notice_t joining = { .first = "Join", .second = "GrakovNe Home?", .icon = GFX_ICON_SIGNAL,
        .ok = "Join", .back = "Cancel" };
    memset(fb, 0xFF, sizeof fb);
    notice_draw(&g, &joining);
    if (save(dir, "notice-join")) {
        return 1;
    }
    const notice_t failed = { .first = "Storage", .second = "error", .icon = GFX_ICON_WARNING,
        .ok = "Format", .back = "Skip" };
    memset(fb, 0xFF, sizeof fb);
    notice_draw(&g, &failed);
    if (save(dir, "notice-error")) {
        return 1;
    }
    const notice_t crashed = { .first = "Weather", .second = "crashed",
                               .detail = "out of bounds memory access", .icon = GFX_ICON_WARNING,
                               .back = "Close" };
    memset(fb, 0xFF, sizeof fb);
    notice_draw(&g, &crashed);
    if (save(dir, "notice-crash")) {
        return 1;
    }
    const notice_t working = { .first = "Updating", .second = "in progress", .icon = NOTICE_NO_ICON,
        .is_working = true };
    memset(fb, 0xFF, sizeof fb);
    notice_draw(&g, &working);
    if (save(dir, "notice-progress")) {
        return 1;
    }

    /* ---- the kit: what applications are put together from ---- */

    /* The primitives on one page: a title, text, a caption, icons in their
     * three sizes, a button, a bar, the row of buttons. */
    start_screen(&online, &content);
    {
        int top = kit_title(&content, "Title");
        kit_text(&content, KIT_MARGIN, top, KIT_TEXT, "Text, as it is read", 0);
        kit_text(&content, KIT_MARGIN, top + 30, KIT_CAPTION, "CAPTION, BESIDE SOMETHING LARGER", 0);
        int row_top = top + 54;
        kit_icon(&content, KIT_MARGIN, row_top + 22, GFX_ICON_BELL, KIT_ICON_SMALL);
        kit_icon(&content, KIT_MARGIN + 36, row_top, GFX_ICON_BELL, KIT_ICON_MEDIUM);
        kit_text(&content, 120, row_top - 12, KIT_VALUE, "21", 0);
        kit_button(&content, 262, row_top + 36, "UP", "More", false);
        kit_bar(&content, KIT_MARGIN, row_top + 58, content.w - 2 * KIT_MARGIN, 37, 100);
        kit_actions(&content, "Start", "Close");
    }
    if (save(dir, "kit-primitives")) {
        return 1;
    }

    /* A value chosen from a row of them. */
    const kit_picker_t pickers[] = {
        { "10 min", "15 min", "30 min", "Save", "Cancel" },
        { NULL, "Off", "On", "Save", "Cancel" },
        { "Saint Petersburg", "Stockholm", NULL, "Save", "Cancel" },
    };
    const char *const picker_names[] = { "kit-picker", "kit-picker-first", "kit-picker-last" };
    for (int shot = 0; shot < 3; shot++) {
        start_screen(&online, &content);
        kit_picker(&content, &pickers[shot]);
        if (save(dir, picker_names[shot])) {
            return 1;
        }
    }

    /* A time and a date, a time alone, a date alone. */
    const kit_date_t dates[] = {
        { 2026, 11, 12, 21, 5, true, true, KIT_DATE_HOUR, "Next", "Cancel" },
        { 2026, 11, 12, 21, 5, true, true, KIT_DATE_MONTH, "Next", "Back" },
        { 2026, 11, 12, 7, 30, true, false, KIT_DATE_MINUTE, "Save", "Back" },
        { 2026, 11, 12, 0, 0, false, true, KIT_DATE_YEAR, "Save", "Back" },
    };
    const char *const date_names[] = { "kit-date-hour", "kit-date-month", "kit-time", "kit-date" };
    for (int shot = 0; shot < 4; shot++) {
        start_screen(&online, &content);
        kit_date(&content, &dates[shot]);
        if (save(dir, date_names[shot])) {
            return 1;
        }
    }

    /* The list in its three uses: rows with values, one of several chosen,
     * and facts nobody chooses from. */
    {
        const char *const labels[] = { "Date and time", "Time zone", "Power saving" };
        const char *const values[] = { "Automatic", "Saint Petersburg", "Off" };
        start_screen(&online, &content);
        for (int i = 0; i < 3; i++) {
            list_row_t row = { labels[i], values[i], LIST_NO_ICON, NULL };
            list_draw_row(&content, 3, 1, i, &row, false);
        }
        if (save(dir, "kit-list")) {
            return 1;
        }
        const char *const options[] = { "Celsius", "Fahrenheit", "Kelvin" };
        start_screen(&online, &content);
        for (int i = 0; i < 3; i++) {
            list_row_t row = { options[i], NULL, i == 0 ? GFX_ICON_CHECK : LIST_NO_ICON, NULL };
            list_draw_row(&content, 3, 1, i, &row, false);
        }
        if (save(dir, "kit-options")) {
            return 1;
        }
        const char *const facts[] = { "Network", "Address", "Firmware", "Uptime" };
        const char *const told[] = { "GrakovNe Home", "192.168.1.70", "0.1.0", "3 h 12 min" };
        start_screen(&online, &content);
        for (int i = 0; i < 4; i++) {
            list_row_t row = { facts[i], told[i], LIST_NO_ICON, NULL };
            list_draw_row(&content, 4, -1, i, &row, false);
        }
        if (save(dir, "kit-details")) {
            return 1;
        }
    }

    /* What an application says when it has nothing to show, and a question
     * of its own: the host's notice under the header. */
    const notice_t not_set_up = { .first = "Not set up", .detail = "Open the page",
                                  .icon = GFX_ICON_GEAR };
    start_screen(&online, &content);
    notice_draw(&content, &not_set_up);
    if (save(dir, "kit-message")) {
        return 1;
    }
    const notice_t no_network = { .first = "No network", .icon = GFX_ICON_NO_SIGNAL,
                                  .ok = "Retry" };
    start_screen(&offline, &content);
    notice_draw(&content, &no_network);
    if (save(dir, "kit-message-retry")) {
        return 1;
    }

    /* The host's icons: each as it stands beside text, and twice as large. */
    start_screen(&online, &content);
    for (int icon = 0; icon < GFX_ICON_COUNT; icon++) {
        int column = icon % ICONS_IN_A_ROW, row = icon / ICONS_IN_A_ROW;
        int left = ICON_SHEET_MARGIN + column * ICON_SHEET_STEP_X;
        int top = ICON_SHEET_MARGIN + row * ICON_SHEET_STEP_Y;
        gfx_icon(&content, (gfx_icon_id_t)icon, left, top + GFX_ICON_CELLS, 2, true);
        gfx_icon(&content, (gfx_icon_id_t)icon, left + ICON_SHEET_PAIR_GAP, top, 4, true);
    }
    if (save(dir, "icons")) {
        return 1;
    }

    return 0;
}
