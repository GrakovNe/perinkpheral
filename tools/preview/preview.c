/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Renders the host's screens on the host and writes them as PBM (P4),
 * one file per screen: what the panel would show, before flashing.
 * Built by tools/preview/Makefile and run by `tools/perinkpheral preview`.
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
#include "screen.h"
#include "wm.h"
#include "words.h"

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

#define ICONS_IN_A_ROW       5
#define ICON_SHEET_MARGIN    16
#define ICON_SHEET_STEP_X    76
#define ICON_SHEET_STEP_Y    50
#define ICON_SHEET_PAIR_GAP  26
#define ICONS_OF_THE_HOST    GFX_ICON_SUN /* the host's own come first; the weather and the rest after */

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

    const screens_header_t online = { GFX_ICON_SIGNAL, NULL, "Wed 11 Nov", SCREENS_NO_ICON };
    /* As the device shows it before there is a network: no Wi-Fi, no date. */
    const screens_header_t offline = { GFX_ICON_NO_SIGNAL, NULL, NULL, SCREENS_NO_ICON };
    gfx_t content;

    const char *titles[] = { "Clock", "Hello", "Settings", "About" };
    start_screen(&online, &content);
    screens_menu(&content, row_from_array, titles, 4, 0, false, NULL);
    if (save(dir, "menu")) {
        return 1;
    }
    start_screen(&offline, &content);
    screens_menu(&content, row_from_array, titles, 4, 1, false, NULL);
    if (save(dir, "menu-offline")) {
        return 1;
    }
    /* A row being moved: in a frame, what OK and BACK do along the bottom. */
    start_screen(&online, &content);
    screens_menu(&content, row_from_array, titles, 4, 1, true, NULL);
    if (save(dir, "menu-moving")) {
        return 1;
    }

    /* A list longer than the screen: at its start, in the middle, at its end. */
    const char *many[] = { "Weather", "Clock", "Timer", "Stocks", "donut.c",
                           "Settings", "About", "Calendar", "Air quality", "Stopwatch" };
    const int selections[] = { 0, 5, 9 };
    const char *const names[] = { "menu-scroll-start", "menu-scroll-middle", "menu-scroll-end" };
    for (int shot = 0; shot < 3; shot++) {
        start_screen(&online, &content);
        screens_menu(&content, row_from_array, many, 10, selections[shot], false, NULL);
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
    const notice_t joining = { .first = "Join", .second = "Home Wi-Fi?", .icon = GFX_ICON_SIGNAL,
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

    /* What an application keeps in the header while it is away: its icon
     * in place of the host's, its line beside it. */
    const screens_header_t counting = { GFX_ICON_SIGNAL, NULL, "12 min", GFX_ICON_HOURGLASS };
    start_screen(&counting, &content);
    screens_menu(&content, row_from_array, titles, 4, 0, false, NULL);
    if (save(dir, "header-status")) {
        return 1;
    }
    /* Both sides taken, by two applications. */
    const screens_header_t both = { GFX_ICON_PLAY, "Blue in Green", "12 min", GFX_ICON_HOURGLASS };
    start_screen(&both, &content);
    screens_menu(&content, row_from_array, titles, 4, 0, false, NULL);
    if (save(dir, "header-status-both")) {
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
        const list_t three = { .count = 3, .selected = 1 };
        const list_t four_facts = { .count = 4, .selected = LIST_NONE_SELECTED };
        const char *const labels[] = { "Date and time", "Time zone", "Power saving" };
        const char *const values[] = { "Automatic", "Saint Petersburg", "Off" };
        start_screen(&online, &content);
        for (int i = 0; i < 3; i++) {
            list_row_t row = { labels[i], values[i], LIST_NO_ICON, NULL };
            list_draw_row(&content, &three, i, &row, false);
        }
        if (save(dir, "kit-list")) {
            return 1;
        }
        const char *const options[] = { "Celsius", "Fahrenheit", "Kelvin" };
        start_screen(&online, &content);
        for (int i = 0; i < 3; i++) {
            list_row_t row = { options[i], NULL, i == 0 ? GFX_ICON_CHECK : LIST_NO_ICON, NULL };
            list_draw_row(&content, &three, i, &row, false);
        }
        if (save(dir, "kit-options")) {
            return 1;
        }
        const char *const facts[] = { "Network", "Address", "Firmware", "Uptime" };
        const char *const told[] = { "Home Wi-Fi", "192.168.1.70", "0.1.0", "3 h 12 min" };
        start_screen(&online, &content);
        for (int i = 0; i < 4; i++) {
            list_row_t row = { facts[i], told[i], LIST_NO_ICON, NULL };
            list_draw_row(&content, &four_facts, i, &row, false);
        }
        if (save(dir, "kit-details")) {
            return 1;
        }
    }

    /* The chart in its four kinds, as the kit application shows them. */
    start_screen(&online, &content);
    {
        static const int32_t SERIES[] = { 712, 715, 715, 721, 730, 728, 735, 741, 752, 760,
                                          758, 771, 780, 779, 790, 802, 815, 811, 824, 840 };
        static const int32_t DAILY[] = { 3, 0, 6, 9, 2, 7, 6, 11, 8, 4, 13, 9, 1, 11 };
        static const char *const NAMES[] = { "LINE", "AREA", "BARS", "STEPS" };
        int top = kit_title(&content, "Charts");
        /* The chart under its caption, whatever the typeface's height, as
         * the kit application lays it out. */
        int caption = gfx_font_height(kit_font(KIT_CAPTION)) + 6;
        int column = (content.w - 3 * KIT_MARGIN) / 2;
        int row = (content.h - top - 2 * KIT_MARGIN) / 2;
        for (int i = 0; i < 4; i++) {
            int left = KIT_MARGIN + i % 2 * (column + KIT_MARGIN);
            int at = top + i / 2 * (row + KIT_MARGIN);
            kit_text(&content, left, at, KIT_CAPTION, NAMES[i], 0);
            const kit_chart_t chart = {
                .left = left, .top = at + caption, .width = column, .height = row - caption,
                .values = i == 2 ? DAILY : SERIES,
                .count = i == 2 ? (int)(sizeof DAILY / sizeof DAILY[0])
                                : (int)(sizeof SERIES / sizeof SERIES[0]),
                .kind = (kit_chart_kind_t)i,
                .flags = KIT_CHART_AXIS | (i == 0 ? KIT_CHART_RANGE | KIT_CHART_MARK_LAST : 0) |
                         (i == 2 ? KIT_CHART_FROM_ZERO : 0),
                .fill_shade = 4,
            };
            kit_chart(&content, &chart);
        }
    }
    if (save(dir, "kit-charts")) {
        return 1;
    }

    /* One chart as the picture of a screen: the value, and how it came to be. */
    start_screen(&online, &content);
    {
        static const int32_t SERIES[] = { 712, 715, 715, 721, 730, 728, 735, 741, 752, 760,
                                          758, 771, 780, 779, 790, 802, 815, 811, 824, 840 };
        int top = kit_title(&content, "Stars");
        kit_text(&content, KIT_MARGIN, top - 6, KIT_VALUE, "840", 0);
        const kit_chart_t chart = {
            .left = KIT_MARGIN, .top = top + 96, .width = content.w - 2 * KIT_MARGIN,
            .height = content.h - top - 96 - KIT_MARGIN,
            .values = SERIES, .count = (int)(sizeof SERIES / sizeof SERIES[0]),
            .kind = KIT_CHART_AREA, .flags = KIT_CHART_AXIS | KIT_CHART_RANGE | KIT_CHART_MARK_LAST,
            .fill_shade = 4,
        };
        kit_chart(&content, &chart);
    }
    if (save(dir, "kit-chart-screen")) {
        return 1;
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

    /* The host's icons: each as it stands beside text, and twice as large - the
     * host's own on one sheet, the weather, a home's things and the sky on another. */
    static const struct {
        const char *name;
        int         first, end;
    } SHEETS[] = { { "icons", 0, ICONS_OF_THE_HOST },
                   { "icons-more", ICONS_OF_THE_HOST, GFX_ICON_PARTICLES },
                   { "icons-air", GFX_ICON_PARTICLES, GFX_ICON_COUNT } };
    for (size_t sheet = 0; sheet < sizeof SHEETS / sizeof SHEETS[0]; sheet++) {
        start_screen(&online, &content);
        for (int icon = SHEETS[sheet].first; icon < SHEETS[sheet].end; icon++) {
            int at = icon - SHEETS[sheet].first;
            int left = ICON_SHEET_MARGIN + at % ICONS_IN_A_ROW * ICON_SHEET_STEP_X;
            int top = ICON_SHEET_MARGIN + at / ICONS_IN_A_ROW * ICON_SHEET_STEP_Y;
            gfx_draw_icon(&content, (gfx_icon_id_t)icon, left, top + GFX_ICON_CELLS, 2, true);
            gfx_draw_icon(&content, (gfx_icon_id_t)icon, left + ICON_SHEET_PAIR_GAP, top, 4, true);
        }
        if (save(dir, SHEETS[sheet].name)) {
            return 1;
        }
    }

    const struct { const char *name; recovery_state_t state; const char *address; recovery_wait_t waiting; } recovery[] = {
        { "recovery-preparing", WAITING, "", RECOVERY_WAIT_PREPARING },
        { "recovery-preparing-online", WAITING, "192.168.1.123", RECOVERY_WAIT_PREPARING },
        { "recovery-connecting", WAITING, "", RECOVERY_WAIT_CONNECTING },
        { "recovery-waiting", WAITING, "", RECOVERY_WAIT_NETWORK },
        { "recovery-online", WAITING, "192.168.1.123", RECOVERY_WAIT_NETWORK },
        { "recovery-confirm", CONFIRM, "192.168.1.123", RECOVERY_WAIT_PREPARING },
        { "recovery-writing", WRITING, "192.168.1.123", RECOVERY_WAIT_PREPARING },
        { "recovery-failed", FAILED, "", RECOVERY_WAIT_NETWORK },
        { "recovery-checking", RECEIVING, "192.168.1.123", RECOVERY_WAIT_PREPARING },
        { "recovery-starting", RESTART, "192.168.1.123", RECOVERY_WAIT_PREPARING },
    };
    for (size_t index = 0; index < sizeof recovery / sizeof recovery[0]; index++) {
        gfx_init(&g, fb, W, H);
        recovery_screen_draw(&g, recovery[index].state, "0.1.35-1234567", recovery[index].address,
                             "perink-recovery", recovery[index].waiting);
        if (save(dir, recovery[index].name)) { return 1; }
    }

    if (argc > 3) {
        static uint8_t glyph_pack[64 * 1024];
        FILE *file = fopen(argv[3], "rb");
        if (file == NULL) { return 1; }
        size_t length = fread(glyph_pack, 1, sizeof glyph_pack, file);
        bool whole = !ferror(file) && fgetc(file) == EOF;
        fclose(file);
        if (!whole || !gfx_pack_add(glyph_pack, length)) { return 1; }
        words_use("ru");
        for (size_t index = 0; index < sizeof recovery / sizeof recovery[0]; index++) {
            gfx_init(&g, fb, W, H);
            recovery_screen_draw(&g, recovery[index].state, "0.1.35-1234567", recovery[index].address,
                                 "perink-recovery", recovery[index].waiting);
            char name[64];
            snprintf(name, sizeof name, "%s-ru", recovery[index].name);
            if (save(dir, name)) { return 1; }
        }
        words_use("en");
    }

    gfx_init(&g, fb, W, H);
    recovery_screen_draw(&g, CONFIRM, "1234567890123456789012345678901", "192.168.1.123",
                         "perink-recovery", RECOVERY_WAIT_PREPARING);
    if (save(dir, "recovery-long-version")) { return 1; }

    return 0;
}
