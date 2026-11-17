/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * Time and text: the clock, the local time, timers, text_fill and the log.
 */
#include "app_internal.h"

/* text_fill(): "{0}" and "{1}" of a pattern replaced by its arguments. */
HOST_FUNCTION(text_fill)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, pattern_offset);
    m3ApiGetArg(int32_t, pattern_length);
    m3ApiGetArg(uint32_t, arguments_offset);
    m3ApiGetArg(int32_t, count);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, capacity);

    char pattern[HOST_TEXT_MAX + 1];
    char filled[HOST_TEXT_MAX + 1];
    /* An argument as the application's memory has it: three words of 32
     * bits. A place in the pattern is three characters: "{0}". */
    enum { ARGUMENT_WORDS = 3, ARGUMENT_BYTES = ARGUMENT_WORDS * 4, PLACE_LENGTH = 3 };
    if (count < 0 || count > HOST_TEXT_ARGUMENTS_MAX || capacity < 0 ||
        !app_text_copy(runtime, _mem, pattern_offset, pattern_length, pattern)) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    const uint8_t *arguments =
        count > 0 ? app_memory_at(runtime, _mem, arguments_offset, count * ARGUMENT_BYTES) : NULL;
    uint8_t *text = app_memory_to_fill(runtime, _mem, text_offset, capacity);
    if ((count > 0 && arguments == NULL) || text == NULL) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    size_t used = 0;
    for (const char *at = pattern; *at != '\0' && used < HOST_TEXT_MAX;) {
        int number = at[0] == '{' && at[1] >= '0' && at[1] <= '9' && at[2] == '}' ? at[1] - '0' : -1;
        if (number < 0 || number >= count) {
            filled[used++] = *at++;
            continue;
        }
        at += PLACE_LENGTH;
        uint32_t words[ARGUMENT_WORDS];
        memcpy(words, arguments + number * ARGUMENT_BYTES, sizeof words);
        char piece[HOST_TEXT_MAX + 1];
        if (words[1] == 0) {
            snprintf(piece, sizeof piece, "%d", (int)(int32_t)words[0]);
        } else if (!app_text_copy(runtime, _mem, words[1], (int32_t)words[2], piece)) {
            m3ApiReturn(HOST_ERR_ARGUMENT);
        }
        size_t length = strlen(piece);
        if (length > HOST_TEXT_MAX - used) {
            length = gfx_text_cut(piece, HOST_TEXT_MAX - used);
        }
        memcpy(filled + used, piece, length);
        used += length;
    }
    /* Full to the brim: the last character may be only half there - its first byte and
     * fewer of the others than it says it has. Only such a one goes. */
    if (used == HOST_TEXT_MAX) {
        size_t start = used;
        while (start > 0 && gfx_is_utf8_tail(filled[start - 1])) {
            start--;
        }
        unsigned char first = start > 0 ? (unsigned char)filled[start - 1] : 0;
        size_t whole = first >= 0xF0 ? 4 : first >= 0xE0 ? 3 : first >= 0xC0 ? 2 : 1;
        if (start > 0 && first >= 0xC0 && used - (start - 1) < whole) {
            used = start - 1;
        }
    }
    filled[used] = '\0';
    size_t length = gfx_text_cut(filled, (size_t)capacity);
    memcpy(text, filled, length);
    m3ApiReturn((int32_t)length);
}

HOST_FUNCTION(timer_set)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, id);
    m3ApiGetArg(int32_t, milliseconds);
    if (id < 0 || id >= HOST_TIMER_COUNT || milliseconds < 0) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    APP_OF(runtime)->services->timer_set(id, milliseconds);
    m3ApiReturn(HOST_OK);
}

HOST_FUNCTION(time_now)
{
    m3ApiReturnType(int64_t);
    m3ApiReturn(APP_OF(runtime)->services->unix_time());
}

HOST_FUNCTION(time_zone_offset)
{
    m3ApiReturnType(int32_t);
    m3ApiReturn(APP_OF(runtime)->services->time_zone_offset());
}


/* Days from 1970-01-01 to a date of the calendar; any year, any month
 * number - the thirteenth month is next January. */
static int64_t days_from_civil(int64_t year, int64_t month, int64_t day)
{
    year += (month - 1) / 12;
    month = (month - 1) % 12 + 1;
    if (month < 1) {
        month += 12;
        year -= 1;
    }
    year -= month <= 2;
    int64_t era = (year >= 0 ? year : year - 399) / 400;
    int64_t year_of_era = year - era * 400;
    int64_t day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    int64_t day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + day_of_era - 719468;
}

/* The moment as the user's clock and calendar show it. */
static void take_apart(const app_t *app, int64_t utc_seconds, host_local_time_t *parts_out)
{
    /* Local time is UTC moved by the offset, so the calendar of UTC reads it. */
    time_t local_seconds = (time_t)(utc_seconds + app->services->time_zone_offset_at(utc_seconds));
    struct tm parts;
    gmtime_r(&local_seconds, &parts);
    *parts_out = (host_local_time_t){
        .year = parts.tm_year + 1900,
        .month = parts.tm_mon + 1,
        .day = parts.tm_mday,
        .weekday = parts.tm_wday,
        .hour = parts.tm_hour,
        .minute = parts.tm_min,
        .second = parts.tm_sec,
    };
}

static int32_t local_time_at(IM3Runtime runtime, void *memory, uint32_t parts_offset,
                             int64_t utc_seconds)
{
    host_local_time_t *parts = (host_local_time_t *)app_memory_to_fill(
        runtime, memory, parts_offset, (int32_t)sizeof(host_local_time_t));
    if (parts == NULL) {
        return HOST_ERR_ARGUMENT;
    }
    /* The application's memory is not aligned for anybody: copy, do not assign. */
    host_local_time_t taken_apart;
    take_apart(APP_OF(runtime), utc_seconds, &taken_apart);
    memcpy(parts, &taken_apart, sizeof taken_apart);
    return HOST_OK;
}

HOST_FUNCTION(time_local)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, now_offset);
    int64_t utc_seconds = APP_OF(runtime)->services->unix_time();
    if (utc_seconds == 0) {
        m3ApiReturn(app_memory_to_fill(runtime, _mem, now_offset,
                                          (int32_t)sizeof(host_local_time_t)) != NULL
                        ? HOST_ERR_NOT_FOUND
                        : HOST_ERR_ARGUMENT);
    }
    m3ApiReturn(local_time_at(runtime, _mem, now_offset, utc_seconds));
}

HOST_FUNCTION(time_local_at)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int64_t, unix_time);
    m3ApiGetArg(uint32_t, parts_offset);
    m3ApiReturn(local_time_at(runtime, _mem, parts_offset, unix_time));
}

/* A moment in words. Whole days are counted on the user's calendar. */
/* The longest moment in words, in any language: two bytes a letter. */
#define TIME_TEXT_BYTES 64

HOST_FUNCTION(time_text)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int64_t, unix_time);
    m3ApiGetArg(int32_t, style);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, capacity);
    app_t *app = APP_OF(runtime);
    char  *text = (char *)app_memory_to_fill(runtime, _mem, text_offset, capacity);
    if (text == NULL || capacity < 1 || style < HOST_TIME_CLOCK || style > HOST_TIME_WEEKDAY_SHORT) {
        m3ApiReturn(HOST_ERR_ARGUMENT);
    }
    host_local_time_t at, today;
    take_apart(app, unix_time, &at);
    int64_t now = app->services->unix_time();
    take_apart(app, now, &today);
    int64_t days = days_from_civil(at.year, at.month, at.day) -
                   days_from_civil(today.year, today.month, today.day);
    /* The names are the host's words, in the device's language. */
    const char *weekday = word((word_t)(WORD_SUNDAY + at.weekday));
    const char *weekday_short = word((word_t)(WORD_SUN + at.weekday));
    const char *month = word((word_t)(WORD_JANUARY + at.month - 1));
    const char *month_short = word((word_t)(WORD_JAN + at.month - 1));
    char line[TIME_TEXT_BYTES];
    char clock[sizeof "00:00" + 2]; /* and room the compiler asks for: an hour is an int */
    snprintf(clock, sizeof clock, "%02d:%02d", (int)at.hour, (int)at.minute);
    switch (style) {
    case HOST_TIME_CLOCK:
        snprintf(line, sizeof line, "%s", clock);
        break;
    case HOST_TIME_DATE:
        snprintf(line, sizeof line, "%s %d %s", weekday_short, (int)at.day, month_short);
        break;
    case HOST_TIME_WEEKDAY:
        snprintf(line, sizeof line, "%s", weekday);
        break;
    case HOST_TIME_LONG_DATE:
        snprintf(line, sizeof line, "%d %s", (int)at.day, month);
        break;
    case HOST_TIME_WEEKDAY_SHORT:
        snprintf(line, sizeof line, "%s", weekday_short);
        break;
    default: {
        /* How near: today a time, this week a day, beyond it a date. */
        bool with_clock = style == HOST_TIME_NEAR;
        if (now == 0 || days < 0 || days >= 7) {
            snprintf(line, sizeof line, "%d %s", (int)at.day, month_short);
        } else if (days == 0) {
            snprintf(line, sizeof line, "%s", with_clock ? clock : word(WORD_TODAY));
        } else {
            const char *day = days == 1 ? word(WORD_TOMORROW) : weekday_short;
            if (with_clock) {
                snprintf(line, sizeof line, "%s %s", day, clock);
            } else {
                snprintf(line, sizeof line, "%s", day);
            }
        }
        break;
    }
    }
    size_t length = gfx_text_cut(line, (size_t)capacity);
    memcpy(text, line, length);
    m3ApiReturn((int32_t)length);
}

HOST_FUNCTION(time_from_local)
{
    m3ApiReturnType(int64_t);
    m3ApiGetArg(uint32_t, parts_offset);
    const uint8_t *given = app_memory_at(runtime, _mem, parts_offset,
                                         (int32_t)sizeof(host_local_time_t));
    if (given == NULL) {
        m3ApiReturn(0);
    }
    host_local_time_t parts;
    memcpy(&parts, given, sizeof parts);
    app_t *app = APP_OF(runtime);
    /* The date and time as if they were UTC; the moment is that less the
     * zone's offset - the offset at the moment itself, which a second look
     * finds when the clocks changed in between. */
    int64_t as_utc = days_from_civil(parts.year, parts.month, parts.day) * SECONDS_PER_DAY +
                     (int64_t)parts.hour * SECONDS_PER_HOUR + (int64_t)parts.minute * SECONDS_PER_MINUTE +
                     parts.second;
    int64_t moment = as_utc - app->services->time_zone_offset_at(as_utc);
    moment = as_utc - app->services->time_zone_offset_at(moment);
    m3ApiReturn(moment);
}

HOST_FUNCTION(time_uptime_ms)
{
    m3ApiReturnType(int64_t);
    m3ApiReturn(APP_OF(runtime)->services->uptime_ms());
}

HOST_FUNCTION(time_boot_id)
{
    m3ApiReturnType(uint32_t);
    const app_services_t *services = APP_OF(runtime)->services;
    m3ApiReturn(services->boot_id != NULL ? services->boot_id() : 0);
}

HOST_FUNCTION(log)
{
    m3ApiGetArg(int32_t, level);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(int32_t, length);
    const uint8_t *text = app_memory_at(runtime, _mem, text_offset, length);
    if (text != NULL) {
        /* One line of the log, and nothing that could pass for another:
         * what is not a printable character is shown as one that is. */
        char line[HOST_LOG_TEXT_MAX];
        if (length > HOST_LOG_TEXT_MAX) {
            length = HOST_LOG_TEXT_MAX;
        }
        for (int32_t i = 0; i < length; i++) {
            line[i] = text[i] >= ' ' && text[i] <= '~' ? (char)text[i] : '?';
        }
        APP_OF(runtime)->services->log(level, line, length);
    }
    m3ApiSuccess();
}
