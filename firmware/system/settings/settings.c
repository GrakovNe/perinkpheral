/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Settings on the device itself, with four buttons and no keyboard: pages
 * of rows, and under some of them pages of their own. Nothing is typed. A
 * value is one of a few, stepped through with UP and DOWN; a date is its
 * parts, each stepped the same way, OK going to the next and BACK to the
 * one before. What needs text - a network's password, a name - is set from
 * the device's web page.
 *
 * A system application: everything it shows and changes belongs to the
 * host, and goes through the system API.
 */
#include <stdbool.h>

#include "app_text.h"
#include "cities.h"
#include "host_api.h"

/* ---- where the user is ---- */

typedef enum {
    PAGE_ROOT,
    PAGE_GENERAL,
    PAGE_DATE_TIME,
    PAGE_CLOCK,      /* the date and the time, part by part */
    PAGE_ZONE,
    PAGE_APPS,
    PAGE_REMOVE_APP, /* "are you sure" */
    PAGE_DISPLAY,
    PAGE_VALUE,      /* one of a few values of a display setting */
    PAGE_WIFI,
    PAGE_FORGET,     /* "are you sure" */
    PAGE_RESET,      /* "are you sure" */
    PAGE_NOTICE,     /* what came of it: "Restarting." */
} page_t;

/* The pages from the first one down to the one on the screen, each with
 * the row the user was on. */
#define DEPTH_MAX 5
static struct {
    page_t  page;
    int32_t selected;
} path[DEPTH_MAX];
static int32_t depth;

#define HERE (path[depth])

/* ---- what the pages keep between presses ---- */

#define TEXT_MAX 47

static int32_t app_to_remove;
static char    app_to_remove_title[TEXT_MAX + 1];

static const char *notice;
static bool        notice_is_final; /* the device is restarting: nowhere to go back to */

static struct {
    int32_t hour, minute, day, month, year;
} clock;
enum { CLOCK_HOUR, CLOCK_MINUTE, CLOCK_DAY, CLOCK_MONTH, CLOCK_YEAR, CLOCK_PARTS };
#define YEAR_FIRST 2026
#define YEAR_LAST  2099

/* A display setting and the values the buttons step through: any value can
 * be set from the web page, these are the ones worth having here. */
typedef struct {
    const char    *label;
    int32_t        what; /* SYS_NUMBER_* */
    const int32_t *choices;
    int32_t        choice_count;
} display_setting_t;

static const int32_t FULL_EVERY_CHOICES[] = { 0, 1, 2, 3, 5, 10, 20, 50, SYS_REFRESH_FULL_NEVER };
static const int32_t MIN_SECONDS_CHOICES[] = { 10, 20, 30, 60, 120, 180, 300, 600, 900, 1800, 3600 };
static const int32_t FORCE_HOURS_CHOICES[] = { 1, 2, 3, 6, 12, 24, 48, 72, 168 };
#define COUNT_OF(array) ((int32_t)(sizeof(array) / sizeof(array)[0]))

static const display_setting_t DISPLAY_SETTINGS[] = {
    { "Full refresh", SYS_NUMBER_REFRESH_FULL_EVERY, FULL_EVERY_CHOICES,
      COUNT_OF(FULL_EVERY_CHOICES) },
    { "Refresh interval", SYS_NUMBER_REFRESH_MIN_SECONDS, MIN_SECONDS_CHOICES,
      COUNT_OF(MIN_SECONDS_CHOICES) },
    { "Forced refresh", SYS_NUMBER_REFRESH_FORCE_HOURS, FORCE_HOURS_CHOICES,
      COUNT_OF(FORCE_HOURS_CHOICES) },
};

static const display_setting_t *edited; /* on PAGE_VALUE */
static int32_t                  edited_choice;

/* A page of settings is redrawn at every press, and a full refresh in the
 * middle of stepping through a list is a second of a black screen: none
 * of the host's making while this application is on the screen. */
#define REDRAW HOST_REDRAW_NO_BLINK

/* ---- small things ---- */

static bool is_same(const char *text, const char *other)
{
    int32_t at = 0;
    while (text[at] != '\0' && text[at] == other[at]) {
        at++;
    }
    return text[at] == other[at];
}

static void copy_text(char *to, int32_t to_size, const char *from)
{
    app_text_t text;
    app_text_start(&text, to, to_size);
    app_text_add(&text, from);
}

/* What the host knows under `what`; empty when it is not known. */
static void read_info(int32_t what, char value[TEXT_MAX + 1])
{
    int32_t length = sys_info(what, value, TEXT_MAX);
    value[length > 0 ? length : 0] = '\0';
}

static int32_t read_number(int32_t what)
{
    sys_number_t number = { 0 };
    sys_number_get(what, &number);
    return number.value;
}

static int32_t wrapped(int32_t value, int32_t first, int32_t last)
{
    return value > last ? first : value < first ? last : value;
}

/* ---- the time zone ---- */

static const char CITY_KEY[] = "city";

/* The city the zone was chosen by; a zone several cities share is shown by
 * the one the user picked, and one set elsewhere by the first that has it.
 * -1 when no city here has the zone. */
static int32_t current_city(void)
{
    char zone[TEXT_MAX + 1];
    char picked[TEXT_MAX + 1];
    read_info(SYS_INFO_TIME_ZONE, zone);
    int32_t picked_length = kv_get(CITY_KEY, sizeof CITY_KEY - 1, picked, TEXT_MAX);
    picked[picked_length > 0 ? picked_length : 0] = '\0';
    int32_t first_with_zone = -1;
    for (int32_t city = 0; city < CITY_COUNT; city++) {
        if (!is_same(CITIES[city].zone, zone)) {
            continue;
        }
        if (is_same(CITIES[city].name, picked)) {
            return city;
        }
        if (first_with_zone < 0) {
            first_with_zone = city;
        }
    }
    return first_with_zone;
}

/* ---- the clock ---- */

#define SECONDS_PER_MINUTE 60
#define SECONDS_PER_HOUR   3600
#define SECONDS_PER_DAY    86400

static const char *const MONTHS[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

static int32_t days_in_month(int32_t month, int32_t year)
{
    static const int32_t DAYS[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    bool is_leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return month == 2 && is_leap ? 29 : DAYS[month - 1];
}

/* Days from 1970-01-01 to the date, by the civil calendar. */
static int64_t days_since_epoch(int32_t year, int32_t month, int32_t day)
{
    /* The year is counted from March, so that the leap day is its last. */
    int32_t march_year = month <= 2 ? year - 1 : year;
    int32_t era = march_year / 400;
    int32_t year_of_era = march_year - era * 400;
    int32_t day_of_year = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    int32_t day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return (int64_t)era * 146097 + day_of_era - 719468;
}

static void clock_from_now(void)
{
    host_local_time_t now;
    if (time_local(&now) == HOST_OK && now.year >= YEAR_FIRST && now.year <= YEAR_LAST) {
        clock.hour = now.hour;
        clock.minute = now.minute;
        clock.day = now.day;
        clock.month = now.month;
        clock.year = now.year;
    } else {
        clock.hour = 12;
        clock.minute = 0;
        clock.day = 1;
        clock.month = 1;
        clock.year = YEAR_FIRST;
    }
}

static void clock_step(int32_t part, int32_t by)
{
    switch (part) {
    case CLOCK_HOUR:
        clock.hour = wrapped(clock.hour + by, 0, 23);
        break;
    case CLOCK_MINUTE:
        clock.minute = wrapped(clock.minute + by, 0, 59);
        break;
    case CLOCK_DAY:
        clock.day = wrapped(clock.day + by, 1, days_in_month(clock.month, clock.year));
        break;
    case CLOCK_MONTH:
        clock.month = wrapped(clock.month + by, 1, 12);
        break;
    case CLOCK_YEAR:
        clock.year = wrapped(clock.year + by, YEAR_FIRST, YEAR_LAST);
        break;
    }
    /* 31 March, and the month goes to April. */
    int32_t last_day = days_in_month(clock.month, clock.year);
    if (clock.day > last_day) {
        clock.day = last_day;
    }
}

static bool clock_save(void)
{
    int64_t local_seconds = days_since_epoch(clock.year, clock.month, clock.day) * SECONDS_PER_DAY +
                            clock.hour * SECONDS_PER_HOUR + clock.minute * SECONDS_PER_MINUTE;
    /* The host keeps UTC, and says how far the zone is from it only for a
     * time it knows: set once to find that out, and once more to be right. */
    if (sys_time_set(local_seconds) != HOST_OK) {
        return false;
    }
    int32_t offset = time_zone_offset();
    if (sys_time_set(local_seconds - offset) != HOST_OK) {
        return false;
    }
    /* Near a change of the clocks the first guess is an hour off: the
     * offset at the time that was meant says so. */
    int32_t offset_then = time_zone_offset();
    return offset_then == offset || sys_time_set(local_seconds - offset_then) == HOST_OK;
}

/* ---- the rows of a page ---- */

static int32_t app_count(void)
{
    char title[TEXT_MAX + 1];
    int32_t count = 0;
    while (sys_app_title(count, title, TEXT_MAX) >= 0) {
        count++;
    }
    return count;
}

static void format_choice(const display_setting_t *setting, int32_t value, char text[TEXT_MAX + 1])
{
    app_text_t out;
    app_text_start(&out, text, TEXT_MAX + 1);
    switch (setting->what) {
    case SYS_NUMBER_REFRESH_FULL_EVERY:
        if (value == 0) {
            app_text_add(&out, "Always");
        } else if (value == SYS_REFRESH_FULL_NEVER) {
            app_text_add(&out, "Never");
        } else {
            app_text_add(&out, "Every ");
            app_text_add_number(&out, value);
        }
        break;
    case SYS_NUMBER_REFRESH_MIN_SECONDS:
        if (value >= SECONDS_PER_MINUTE && value % SECONDS_PER_MINUTE == 0) {
            app_text_add_number(&out, value / SECONDS_PER_MINUTE);
            app_text_add(&out, " min");
        } else {
            app_text_add_number(&out, value);
            app_text_add(&out, " s");
        }
        break;
    default:
        app_text_add_number(&out, value);
        app_text_add(&out, " h");
        break;
    }
}

static bool is_automatic(void)
{
    return read_number(SYS_NUMBER_TIME_AUTOMATIC) != 0;
}

static bool has_network(void)
{
    char network[TEXT_MAX + 1];
    read_info(SYS_INFO_NETWORK, network);
    return network[0] != '\0';
}

static int32_t row_count(page_t page)
{
    switch (page) {
    case PAGE_ROOT:
        return 5;
    case PAGE_GENERAL:
        return 3;
    case PAGE_DATE_TIME:
        return is_automatic() ? 1 : 2; /* by hand only when not from the network */
    case PAGE_ZONE:
        return CITY_COUNT;
    case PAGE_APPS: {
        int32_t apps = app_count();
        return apps > 0 ? apps : 1; /* or a row that says there are none */
    }
    case PAGE_DISPLAY:
        return COUNT_OF(DISPLAY_SETTINGS);
    case PAGE_WIFI:
        return has_network() ? 4 : 3; /* nothing to forget without a network */
    default:
        return 0;
    }
}

/* The label of a row and the value shown against the right edge; either
 * may be empty. */
static void describe_row(page_t page, int32_t row, char label[TEXT_MAX + 1],
                         char value[TEXT_MAX + 1])
{
    static const char *const ROOT[] = { "General", "Apps", "Display", "Wi-Fi", "Reset" };
    static const char *const GENERAL[] = { "Date & time", "Time zone", "Power saving" };
    static const char *const WIFI[] = { "Network", "Address", "Status", "Forget network" };
    label[0] = '\0';
    value[0] = '\0';
    switch (page) {
    case PAGE_ROOT:
        copy_text(label, TEXT_MAX + 1, ROOT[row]);
        break;
    case PAGE_GENERAL:
        copy_text(label, TEXT_MAX + 1, GENERAL[row]);
        if (row == 1) {
            int32_t city = current_city();
            copy_text(value, TEXT_MAX + 1, city >= 0 ? CITIES[city].name : "Custom");
        } else if (row == 2) {
            copy_text(value, TEXT_MAX + 1, "Off"); /* there is no other yet */
        }
        break;
    case PAGE_DATE_TIME:
        if (row == 0) {
            copy_text(label, TEXT_MAX + 1, "Automatic");
            copy_text(value, TEXT_MAX + 1, is_automatic() ? "On" : "Off");
        } else {
            copy_text(label, TEXT_MAX + 1, "Set date and time");
        }
        break;
    case PAGE_ZONE:
        copy_text(label, TEXT_MAX + 1, CITIES[row].name);
        break;
    case PAGE_APPS: {
        int32_t length = sys_app_title(row, label, TEXT_MAX);
        if (length < 0) {
            copy_text(label, TEXT_MAX + 1, "No apps");
        } else {
            label[length] = '\0';
        }
        break;
    }
    case PAGE_DISPLAY:
        copy_text(label, TEXT_MAX + 1, DISPLAY_SETTINGS[row].label);
        format_choice(&DISPLAY_SETTINGS[row], read_number(DISPLAY_SETTINGS[row].what), value);
        break;
    case PAGE_WIFI:
        copy_text(label, TEXT_MAX + 1, WIFI[row]);
        if (row == 0) {
            read_info(SYS_INFO_NETWORK, value);
            if (value[0] == '\0') {
                copy_text(value, TEXT_MAX + 1, "Not set");
            }
        } else if (row == 1) {
            read_info(SYS_INFO_ADDRESS, value);
        } else if (row == 2) {
            read_info(SYS_INFO_WIFI, value);
            copy_text(value, TEXT_MAX + 1,
                      is_same(value, SYS_WIFI_ONLINE)       ? "Online"
                      : is_same(value, SYS_WIFI_CONNECTING) ? "Connecting"
                                                            : "Setup mode");
        }
        break;
    default:
        break;
    }
}

static const char *page_title(page_t page)
{
    switch (page) {
    case PAGE_GENERAL:
        return "General";
    case PAGE_DATE_TIME:
    case PAGE_CLOCK:
        return "Date & time";
    case PAGE_ZONE:
        return "Time zone";
    case PAGE_APPS:
    case PAGE_REMOVE_APP:
        return "Apps";
    case PAGE_DISPLAY:
        return "Display";
    case PAGE_VALUE:
        return edited->label;
    case PAGE_WIFI:
    case PAGE_FORGET:
        return "Wi-Fi";
    case PAGE_RESET:
        return "Reset";
    default:
        return "Settings";
    }
}

/* ---- moving between pages ---- */

/* The page on the screen changed, or what it shows did. */
static void show(void)
{
    const char *title = page_title(HERE.page);
    header_text(HOST_HEADER_LEFT, title, app_text_length(title));
    request_redraw(REDRAW);
}

static void open(page_t page, int32_t selected)
{
    if (depth + 1 < DEPTH_MAX) {
        depth++;
    }
    HERE.page = page;
    HERE.selected = selected;
    show();
}

static void go_back(void)
{
    if (depth == 0) {
        request_exit();
        return;
    }
    depth--;
    /* A page may have fewer rows than when it was left. */
    int32_t rows = row_count(HERE.page);
    if (rows > 0 && HERE.selected >= rows) {
        HERE.selected = rows - 1;
    }
    show();
}

/* What an action came to. A final notice stays until the device restarts. */
static void tell(const char *text, bool is_final)
{
    notice = text;
    notice_is_final = is_final;
    depth = 0;
    HERE.page = PAGE_ROOT;
    HERE.selected = 0;
    open(PAGE_NOTICE, 0);
}

static void tell_result(int32_t result)
{
    if (result == HOST_OK) {
        tell("Restarting.", true);
    } else {
        tell(result == HOST_ERR_FAILED ? "Storage error." : "Failed.", false);
    }
}

/* ---- OK on a row ---- */

static void activate(page_t page, int32_t row)
{
    switch (page) {
    case PAGE_ROOT: {
        static const page_t SECTIONS[] = { PAGE_GENERAL, PAGE_APPS, PAGE_DISPLAY, PAGE_WIFI,
                                           PAGE_RESET };
        open(SECTIONS[row], 0);
        break;
    }
    case PAGE_GENERAL:
        if (row == 0) {
            open(PAGE_DATE_TIME, 0);
        } else if (row == 1) {
            int32_t city = current_city();
            open(PAGE_ZONE, city >= 0 ? city : 0);
        }
        break;
    case PAGE_DATE_TIME:
        if (row == 0) {
            sys_number_set(SYS_NUMBER_TIME_AUTOMATIC, is_automatic() ? 0 : 1);
            show();
        } else if (is_automatic()) {
            /* Turned on from the web page while this row was on the screen. */
            HERE.selected = 0;
            show();
        } else {
            clock_from_now();
            open(PAGE_CLOCK, CLOCK_HOUR);
        }
        break;
    case PAGE_ZONE:
        if (sys_text_set(SYS_TEXT_TIME_ZONE, CITIES[row].zone,
                         app_text_length(CITIES[row].zone)) == HOST_OK) {
            kv_set(CITY_KEY, sizeof CITY_KEY - 1, CITIES[row].name,
                   app_text_length(CITIES[row].name));
            go_back();
        } else {
            tell("Failed.", false);
        }
        break;
    case PAGE_APPS: {
        int32_t length = sys_app_title(row, app_to_remove_title, TEXT_MAX);
        if (length >= 0) {
            app_to_remove_title[length] = '\0';
            app_to_remove = row;
            open(PAGE_REMOVE_APP, 0);
        }
        break;
    }
    case PAGE_DISPLAY: {
        edited = &DISPLAY_SETTINGS[row];
        /* The value may be one the web page set: start from the nearest
         * choice that is not below it. */
        int32_t value = read_number(edited->what);
        edited_choice = edited->choice_count - 1;
        for (int32_t choice = 0; choice < edited->choice_count; choice++) {
            if (edited->choices[choice] >= value) {
                edited_choice = choice;
                break;
            }
        }
        open(PAGE_VALUE, 0);
        break;
    }
    case PAGE_WIFI:
        if (row == 3) {
            open(PAGE_FORGET, 0);
        }
        break;
    default:
        break;
    }
}

/* ---- the buttons ---- */

static bool is_list(page_t page)
{
    return row_count(page) > 0;
}

static bool is_question(page_t page)
{
    return page == PAGE_REMOVE_APP || page == PAGE_FORGET || page == PAGE_RESET;
}

/* The second answer of a question is the one that does it. */
#define ANSWER_CANCEL 0
#define ANSWER_DO     1

static void answer_question(page_t page)
{
    if (HERE.selected != ANSWER_DO) {
        go_back();
        return;
    }
    switch (page) {
    case PAGE_REMOVE_APP:
        tell_result(sys_app_remove(app_to_remove));
        break;
    case PAGE_FORGET:
        tell_result(sys_do(SYS_DO_FORGET_NETWORK));
        break;
    default:
        tell_result(sys_do(SYS_DO_FACTORY_RESET));
        break;
    }
}

static void press_up_or_down(int32_t by)
{
    page_t page = HERE.page;
    if (page == PAGE_CLOCK) {
        /* UP is later, as on any clock that is set with two buttons. */
        clock_step(HERE.selected, -by);
    } else if (page == PAGE_VALUE) {
        int32_t choice = edited_choice + by;
        if (choice < 0 || choice >= edited->choice_count) {
            return; /* the ends are ends: no wrapping from "Never" to "Always" */
        }
        edited_choice = choice;
    } else if (page == PAGE_ZONE) {
        HERE.selected = wrapped(HERE.selected + by, 0, CITY_COUNT - 1);
    } else if (is_question(page)) {
        HERE.selected = HERE.selected == ANSWER_CANCEL ? ANSWER_DO : ANSWER_CANCEL;
    } else if (is_list(page)) {
        HERE.selected = wrapped(HERE.selected + by, 0, row_count(page) - 1);
    } else {
        return;
    }
    request_redraw(REDRAW);
}

static void press_ok(void)
{
    page_t page = HERE.page;
    if (page == PAGE_CLOCK) {
        if (HERE.selected + 1 < CLOCK_PARTS) {
            HERE.selected++;
            request_redraw(REDRAW);
        } else if (clock_save()) {
            go_back();
        } else {
            tell("Failed.", false);
        }
    } else if (page == PAGE_VALUE) {
        if (sys_number_set(edited->what, edited->choices[edited_choice]) == HOST_OK) {
            go_back();
        } else {
            tell("Failed.", false);
        }
    } else if (is_question(page)) {
        answer_question(page);
    } else if (page == PAGE_NOTICE) {
        if (!notice_is_final) {
            go_back();
        }
    } else {
        activate(page, HERE.selected);
    }
}

static void press_back(void)
{
    if (HERE.page == PAGE_CLOCK && HERE.selected > 0) {
        HERE.selected--;
        request_redraw(REDRAW);
    } else if (HERE.page == PAGE_NOTICE && notice_is_final) {
        return;
    } else {
        go_back();
    }
}

void app_init(void)
{
    depth = 0;
    HERE.page = PAGE_ROOT;
    HERE.selected = 0;
    show();
}

void app_on_button(int32_t event)
{
    switch (event) {
    case BTN_UP_PRESSED:
        press_up_or_down(-1);
        break;
    case BTN_DOWN_PRESSED:
        press_up_or_down(1);
        break;
    case BTN_OK_PRESSED:
        press_ok();
        break;
    case BTN_BACK_PRESSED:
        press_back();
        break;
    }
}

/* ---- the picture ---- */

#define MARGIN       16
#define LIST_TOP_GAP 8
#define ROW_HEIGHT   42
#define ROW_BASELINE 30 /* under the row's top */
/* The bar behind the selected row, as in the host's menu. */
#define BAR_INSET    8
#define BAR_HEIGHT   36
#define BAR_RISE     26 /* its top above the row's baseline */
#define VALUE_GAP    14 /* the least between a label and its value */
#define LINE_HEIGHT  30

static int32_t text_width(int32_t font, const char *text)
{
    return gfx_text_width(font, text, app_text_length(text));
}

static void draw_text(int32_t left, int32_t baseline, int32_t font, const char *text)
{
    gfx_text(left, baseline - gfx_font_ascent(font), font, text, app_text_length(text));
}

static void draw_centered(int32_t baseline, int32_t font, const char *text)
{
    draw_text((gfx_width() - text_width(font, text)) / 2, baseline, font, text);
}

/* ---- small pictures, in cells of two pixels like the host's icons ---- */

#define CELL 2

static void draw_cells(const char *const *rows, int32_t rows_count, int32_t left, int32_t top)
{
    for (int32_t row = 0; row < rows_count; row++) {
        /* A run of ink in a row is one rectangle. */
        for (int32_t column = 0; rows[row][column] != '\0';) {
            if (rows[row][column] != '#') {
                column++;
                continue;
            }
            int32_t run_start = column;
            while (rows[row][column] == '#') {
                column++;
            }
            gfx_rect(left + run_start * CELL, top + row * CELL, (column - run_start) * CELL, CELL,
                     HOST_RECT_FILLED);
        }
    }
}

#define CHEVRON_COLUMNS 9
#define CHEVRON_ROWS    5
#define CHEVRON_WIDTH   (CHEVRON_COLUMNS * CELL)
#define CHEVRON_HEIGHT  (CHEVRON_ROWS * CELL)
static const char *const CHEVRON_UP[CHEVRON_ROWS] = {
    "....#....", "...###...", "..##.##..", ".##...##.", "##.....##",
};
static const char *const CHEVRON_DOWN[CHEVRON_ROWS] = {
    "##.....##", ".##...##.", "..##.##..", "...###...", "....#....",
};

#define TRASH_CELLS 11
static const char *const TRASH[TRASH_CELLS] = {
    "....###....", ".#########.", "...........", "..#######..", "..#.#.#.#..", "..#.#.#.#..",
    "..#.#.#.#..", "..#.#.#.#..", "..#.#.#.#..", "..#######..", "...........",
};

/* Cuts a value that would run into its label, with "..." for what is gone. */
static void fit(char text[TEXT_MAX + 1], int32_t max_width)
{
    int32_t length = app_text_length(text);
    while (length > 0 && text_width(HOST_FONT_TEXT, text) > max_width) {
        length--;
        app_text_t out;
        app_text_start(&out, text + length, TEXT_MAX + 1 - length);
        app_text_add(&out, "...");
    }
}

/* One row of a list at the place number `slot` on the screen. */
static void draw_row(int32_t slot, const char *label, char value[TEXT_MAX + 1], bool is_selected)
{
    int32_t baseline = LIST_TOP_GAP + slot * ROW_HEIGHT + ROW_BASELINE;
    if (is_selected) {
        gfx_rect(BAR_INSET, baseline - BAR_RISE, gfx_width() - 2 * BAR_INSET, BAR_HEIGHT,
                 HOST_RECT_FILLED);
        gfx_color(HOST_PAPER);
    }
    draw_text(MARGIN, baseline, HOST_FONT_TEXT, label);
    fit(value, gfx_width() - 2 * MARGIN - text_width(HOST_FONT_TEXT, label) - VALUE_GAP);
    draw_text(gfx_width() - MARGIN - text_width(HOST_FONT_TEXT, value), baseline, HOST_FONT_TEXT,
              value);
    gfx_color(HOST_INK);
}

static void draw_list(page_t page, int32_t selected)
{
    int32_t rows = row_count(page);
    int32_t visible = (gfx_height() - LIST_TOP_GAP) / ROW_HEIGHT;
    /* The selected row stays in the middle while there is more both ways. */
    int32_t first = selected - visible / 2;
    if (first > rows - visible) {
        first = rows - visible;
    }
    if (first < 0) {
        first = 0;
    }
    for (int32_t slot = 0; slot < visible && first + slot < rows; slot++) {
        char label[TEXT_MAX + 1];
        char value[TEXT_MAX + 1];
        describe_row(page, first + slot, label, value);
        draw_row(slot, label, value, first + slot == selected);
        if (page == PAGE_APPS && sys_app_title(first + slot, value, TEXT_MAX) >= 0) {
            /* What OK does to an application here, said by a picture. */
            int32_t baseline = LIST_TOP_GAP + slot * ROW_HEIGHT + ROW_BASELINE;
            gfx_color(first + slot == selected ? HOST_PAPER : HOST_INK);
            draw_cells(TRASH, TRASH_CELLS, gfx_width() - MARGIN - TRASH_CELLS * CELL,
                       baseline - (TRASH_CELLS - 1) * CELL);
            gfx_color(HOST_INK);
        }
    }
}

/* A question: what will happen, in up to three lines, and the two answers
 * as the last rows of the screen. */
static void draw_question(const char *first, const char *second, const char *third,
                          const char *action, int32_t selected)
{
    const char *const lines[] = { first, second, third };
    for (int32_t line = 0; line < 3; line++) {
        draw_text(MARGIN, (line + 1) * LINE_HEIGHT, HOST_FONT_TEXT, lines[line]);
    }
    int32_t visible = (gfx_height() - LIST_TOP_GAP) / ROW_HEIGHT;
    char no_value[TEXT_MAX + 1] = "";
    draw_row(visible - 2, "Cancel", no_value, selected == ANSWER_CANCEL);
    draw_row(visible - 1, action, no_value, selected == ANSWER_DO);
}

/* ---- setting a value: what UP and DOWN lead to is on the screen ---- */

#define BOX_STROKE 2

/* A frame two pixels thick. */
static void draw_box(int32_t left, int32_t top, int32_t width, int32_t height)
{
    for (int32_t inset = 0; inset < BOX_STROKE; inset++) {
        gfx_rect(left + inset, top + inset, width - 2 * inset, height - 2 * inset,
                 HOST_RECT_OUTLINE);
    }
}

#define PART_PAD       6 /* from the text of a part to its frame */
#define PART_ARROW_GAP 6 /* from the frame to the arrows over and under it */

/* A part of the date or the time. The one being set is in a frame, with
 * an arrow over it and one under it: UP and DOWN change this one. */
static int32_t draw_part(int32_t left, int32_t baseline, int32_t font, const char *text,
                         bool is_selected)
{
    int32_t width = text_width(font, text);
    draw_text(left, baseline, font, text);
    if (is_selected) {
        int32_t top = baseline - gfx_font_cap_height(font) - PART_PAD;
        int32_t height = gfx_font_cap_height(font) + 2 * PART_PAD;
        draw_box(left - PART_PAD, top, width + 2 * PART_PAD, height);
        int32_t arrow_left = left + (width - CHEVRON_WIDTH) / 2;
        draw_cells(CHEVRON_UP, CHEVRON_ROWS, arrow_left, top - PART_ARROW_GAP - CHEVRON_HEIGHT);
        draw_cells(CHEVRON_DOWN, CHEVRON_ROWS, arrow_left, top + height + PART_ARROW_GAP);
    }
    return width;
}

#define CLOCK_TIME_BASELINE 92
#define CLOCK_DATE_BASELINE 186
#define CLOCK_HINT_BASELINE 250
#define CLOCK_PART_GAP      10 /* between the parts of a line, for their frames */

static void draw_clock(int32_t part)
{
    char hour[4], minute[4], day[4], year[8];
    app_text_t text;
    app_text_start(&text, hour, sizeof hour);
    app_text_add_padded(&text, clock.hour, 2);
    app_text_start(&text, minute, sizeof minute);
    app_text_add_padded(&text, clock.minute, 2);
    app_text_start(&text, day, sizeof day);
    app_text_add_padded(&text, clock.day, 2);
    app_text_start(&text, year, sizeof year);
    app_text_add_number(&text, clock.year);
    const char *month = MONTHS[clock.month - 1];

    int32_t font = HOST_FONT_LARGE;
    int32_t left = (gfx_width() - text_width(font, hour) - text_width(font, ":") -
                    text_width(font, minute) - 2 * CLOCK_PART_GAP) / 2;
    left += draw_part(left, CLOCK_TIME_BASELINE, font, hour, part == CLOCK_HOUR) + CLOCK_PART_GAP;
    left += draw_part(left, CLOCK_TIME_BASELINE, font, ":", false) + CLOCK_PART_GAP;
    draw_part(left, CLOCK_TIME_BASELINE, font, minute, part == CLOCK_MINUTE);

    font = HOST_FONT_MEDIUM;
    int32_t gap = text_width(font, " ");
    left = (gfx_width() - text_width(font, day) - text_width(font, month) -
            text_width(font, year) - 2 * gap) / 2;
    left += draw_part(left, CLOCK_DATE_BASELINE, font, day, part == CLOCK_DAY) + gap;
    left += draw_part(left, CLOCK_DATE_BASELINE, font, month, part == CLOCK_MONTH) + gap;
    draw_part(left, CLOCK_DATE_BASELINE, font, year, part == CLOCK_YEAR);

    draw_centered(CLOCK_HINT_BASELINE, HOST_FONT_SMALL,
                  part + 1 < CLOCK_PARTS ? "OK: next   BACK: previous" : "OK: save   BACK: previous");
}

/* One of a row of values: the chosen one in a frame in the middle, the one
 * UP leads to over it and the one DOWN leads to under it. Empty text is
 * the end of the row: no arrow that way. */
#define WHEEL_BOX_LEFT       40
#define WHEEL_BOX_TOP        96
#define WHEEL_BOX_HEIGHT     56
#define WHEEL_BASELINE       136
#define WHEEL_ABOVE_BASELINE 76
#define WHEEL_BELOW_BASELINE 196
#define WHEEL_UP_TOP         26
#define WHEEL_DOWN_TOP       212
#define WHEEL_HINT_BASELINE  250

static void draw_wheel(const char *above, const char *chosen, const char *below)
{
    int32_t arrow_left = (gfx_width() - CHEVRON_WIDTH) / 2;
    if (above[0] != '\0') {
        draw_cells(CHEVRON_UP, CHEVRON_ROWS, arrow_left, WHEEL_UP_TOP);
        draw_centered(WHEEL_ABOVE_BASELINE, HOST_FONT_TEXT, above);
    }
    draw_box(WHEEL_BOX_LEFT, WHEEL_BOX_TOP, gfx_width() - 2 * WHEEL_BOX_LEFT, WHEEL_BOX_HEIGHT);
    draw_centered(WHEEL_BASELINE, HOST_FONT_MEDIUM, chosen);
    if (below[0] != '\0') {
        draw_centered(WHEEL_BELOW_BASELINE, HOST_FONT_TEXT, below);
        draw_cells(CHEVRON_DOWN, CHEVRON_ROWS, arrow_left, WHEEL_DOWN_TOP);
    }
    draw_centered(WHEEL_HINT_BASELINE, HOST_FONT_SMALL, "OK: save   BACK: cancel");
}

static void draw_value(void)
{
    char above[TEXT_MAX + 1] = "";
    char chosen[TEXT_MAX + 1];
    char below[TEXT_MAX + 1] = "";
    if (edited_choice > 0) {
        format_choice(edited, edited->choices[edited_choice - 1], above);
    }
    format_choice(edited, edited->choices[edited_choice], chosen);
    if (edited_choice + 1 < edited->choice_count) {
        format_choice(edited, edited->choices[edited_choice + 1], below);
    }
    draw_wheel(above, chosen, below);
}

static void draw_zone(int32_t city)
{
    draw_wheel(CITIES[wrapped(city - 1, 0, CITY_COUNT - 1)].name, CITIES[city].name,
               CITIES[wrapped(city + 1, 0, CITY_COUNT - 1)].name);
}

void app_on_draw(void)
{
    page_t page = HERE.page;
    char line[TEXT_MAX + 1];
    app_text_t text;
    app_text_start(&text, line, sizeof line);
    switch (page) {
    case PAGE_CLOCK:
        draw_clock(HERE.selected);
        break;
    case PAGE_VALUE:
        draw_value();
        break;
    case PAGE_ZONE:
        draw_zone(HERE.selected);
        break;
    case PAGE_REMOVE_APP:
        /* A title is a line of its own: the longest fills it. */
        app_text_add(&text, app_to_remove_title);
        draw_question("Remove app?", line, "Settings are kept.", "Remove", HERE.selected);
        break;
    case PAGE_FORGET:
        draw_question("Forget network?", "The device restarts", "in setup mode.", "Forget",
                      HERE.selected);
        break;
    case PAGE_RESET:
        draw_question("Factory reset?", "All settings and app", "data will be erased.", "Reset",
                      HERE.selected);
        break;
    case PAGE_NOTICE:
        draw_text(MARGIN, LINE_HEIGHT, HOST_FONT_TEXT, notice);
        break;
    default:
        draw_list(page, HERE.selected);
        break;
    }
}
