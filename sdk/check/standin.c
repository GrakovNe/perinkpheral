/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The stand-in host: see standin.h. The calls are in the contract's order
 * of subjects; each refuses what the contract says the device refuses.
 */
#include "standin.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "json_pick.h"

standin_handlers_t standin_handlers;

static standin_t *world;

/* memcpy(), of nothing when the length is 0: an application may pass NULL with it, which the
 * C library's memcpy() does not take (glibc declares both pointers never null). */
static void *copy(void *to, const void *from, size_t length)
{
    return length > 0 ? memcpy(to, from, length) : to;
}

/* A handler's time on the device (CLAUDE.md, "What a handler may count on"). */
#define HANDLER_SECONDS 3

#define SURFACE_WIDTH       400
#define SURFACE_HEIGHT      300
#define HEADER_HEIGHT       40
#define COORDINATE_LIMIT    4096 /* host/gfx.h: drawing coordinates are bounded */
#define STORAGE_ENTRY_BYTES 32   /* host/store.h: 64 bytes a value and its length rounded up */
#define FILE_BLOCK_BYTES    4096
#define MS_PER_SECOND       1000
/* The clock of a subject that names no time: 2026-11-21 12:00 UTC. A device knows the time. */
#define DEFAULT_TIME        1795262400LL

standin_t *standin(void)
{
    if (world == NULL) {
        /* Shared, so that a forked run writes where its parent reads. */
        world = mmap(NULL, sizeof *world, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
        if (world == MAP_FAILED) {
            perror("standin: no memory for the host");
            exit(2);
        }
        standin_reset();
    }
    return world;
}

static uint32_t next_boot_id(uint32_t previous)
{
    return previous + 0x9E3779B1u != 0 ? previous + 0x9E3779B1u : 1;
}

void standin_reset(void)
{
    standin_t *host = standin();
    uint32_t boot_id = next_boot_id(host->boot_id);
    memset(host, 0, sizeof *host);
    host->boot_id = boot_id;
    host->now = standin_subject.at != 0 ? standin_subject.at : DEFAULT_TIME;
    host->zone_offset = standin_subject.zone_offset;
    host->answer_item = -1;
    host->is_new = true;
    host->status_icon[0] = host->status_icon[1] = HOST_ICON_NONE;
    for (int32_t index = 0; index < standin_subject.file_count; index++) {
        FILE *file = fopen(standin_subject.files[index].file, "rb");
        static uint8_t bytes[STANDIN_FILE_BYTES];
        if (file == NULL) {
            fprintf(stderr, "standin: cannot read %s\n", standin_subject.files[index].file);
            exit(2);
        }
        size_t size = fread(bytes, 1, sizeof bytes, file);
        fclose(file);
        standin_file_put(standin_subject.files[index].name, bytes, (int32_t)size);
    }
    for (int32_t index = 0; index < standin_subject.store_count; index++) {
        standin_set(standin_subject.store[index].key, standin_subject.store[index].value);
    }
}

/* ---- What a run leaves for who ran it ---- */

void standin_note(const char *format, ...)
{
    standin_t *host = standin();
    if (host->note_count >= STANDIN_NOTES) {
        return;
    }
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(host->notes[host->note_count++], STANDIN_NOTE_BYTES, format, arguments);
    va_end(arguments);
}

static void say(char *into, const char *format, ...)
{
    if (into[0] != '\0') {
        return; /* the first is the one worth reading */
    }
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(into, STANDIN_NOTE_BYTES, format, arguments);
    va_end(arguments);
}

static const char *running(void)
{
    return world->handler != NULL ? world->handler : "no handler";
}

/* ---- The clock ---- */

void standin_clock(int64_t unix_time)
{
    standin()->now = unix_time;
    world->clock_ms_part = 0;
}

void standin_pass(int64_t milliseconds)
{
    standin_t *host = standin();
    host->uptime_ms += milliseconds;
    if (host->now != 0) {
        host->clock_ms_part += milliseconds;
        host->now += host->clock_ms_part / MS_PER_SECOND;
        host->clock_ms_part %= MS_PER_SECOND;
    }
}

void standin_forget_session(void)
{
    standin()->session_length = 0;
    world->session_inverted = false;
}

void standin_restart(void)
{
    standin_t *host = standin();
    host->uptime_ms = 0;
    host->boot_id = next_boot_id(host->boot_id);
    host->session_length = 0;
    host->session_inverted = false;
    host->request_count = 0;
    memset(host->alarms, 0, sizeof host->alarms);
    memset(host->timer_due_ms, 0, sizeof host->timer_due_ms);
    /* A status in the header is the running firmware's: a restart shows none. */
    memset(host->status_text, 0, sizeof host->status_text);
    host->status_icon[0] = host->status_icon[1] = HOST_ICON_NONE;
    host->is_loaded = host->is_on_screen = false;
}

/* ---- Log ---- */

void log_write(int32_t level, const char *text, int32_t length)
{
    standin_t *host = standin();
    if (length < 0 || text == NULL) {
        return;
    }
    if (length > HOST_LOG_TEXT_MAX) {
        length = HOST_LOG_TEXT_MAX;
    }
    copy(host->last_log, text, (size_t)length);
    host->last_log[length] = '\0';
    host->logs++;
    if (level == HOST_LOG_ERROR) {
        host->error_logs++;
    }
}

/* ---- Session ---- */

int32_t session_set(const void *data, int32_t length)
{
    standin_t *host = standin();
    /* Attached after app_on_load() returns, and an instance off the screen only reads it. */
    if (data == NULL || length < 0 || length > HOST_SESSION_MAX || host->is_loading || !host->is_session_writable) {
        return HOST_ERR_ARGUMENT;
    }
    copy(host->session, data, (size_t)length);
    host->session_length = length;
    return HOST_OK;
}

int32_t session_get(void *buffer, int32_t capacity)
{
    standin_t *host = standin();
    if (buffer == NULL || capacity < 0) {
        return HOST_ERR_ARGUMENT;
    }
    int32_t length = host->is_loading ? 0 : host->session_length;
    if (length > capacity) {
        return HOST_ERR_NO_SPACE;
    }
    copy(buffer, host->session, (size_t)length);
    return length;
}

/* ---- Text ---- */

static bool is_utf8_tail(char byte)
{
    return ((uint8_t)byte & 0xC0) == 0x80;
}

/* gfx_text_cut(): at most `room` bytes of `length`, never inside a character. */
static int32_t text_cut(const char *text, int32_t length, int32_t room)
{
    if (length <= room) {
        return length;
    }
    int32_t cut = room > 0 ? room : 0;
    while (cut > 0 && is_utf8_tail(text[cut])) {
        cut--;
    }
    return cut;
}

int32_t text_fill(const char *pattern, int32_t pattern_length, const host_text_argument_t *arguments,
                  int32_t count, char *text, int32_t capacity)
{
    if (pattern == NULL || pattern_length < 0 || count < 0 || count > HOST_TEXT_ARGUMENTS_MAX ||
        (count > 0 && arguments == NULL) || text == NULL || capacity < 0) {
        return HOST_ERR_ARGUMENT;
    }
    if (pattern_length > HOST_TEXT_MAX) {
        pattern_length = HOST_TEXT_MAX;
    }
    /* As app_api_time.c fills it: a piece cut between characters where the text is full, a
     * character only half there at HOST_TEXT_MAX taken out, then cut to the capacity. */
    char filled[HOST_TEXT_MAX + 1];
    int32_t used = 0;
    for (int32_t at = 0; at < pattern_length && used < HOST_TEXT_MAX;) {
        bool is_slot = pattern[at] == '{' && at + 2 < pattern_length && pattern[at + 2] == '}' &&
                       pattern[at + 1] >= '0' && pattern[at + 1] < '0' + count;
        if (!is_slot) {
            filled[used++] = pattern[at++];
            continue;
        }
        const host_text_argument_t *argument = &arguments[pattern[at + 1] - '0'];
        char number[16];
        const char *piece = argument->text;
        int32_t piece_length = argument->length;
        if (piece == NULL) {
            piece_length = snprintf(number, sizeof number, "%d", argument->number);
            piece = number;
        }
        if (piece_length < 0) {
            return HOST_ERR_ARGUMENT;
        }
        if (piece_length > HOST_TEXT_MAX) {
            piece_length = HOST_TEXT_MAX;
        }
        piece_length = text_cut(piece, piece_length, HOST_TEXT_MAX - used);
        copy(filled + used, piece, (size_t)piece_length);
        used += piece_length;
        at += 3;
    }
    if (used == HOST_TEXT_MAX) {
        int32_t start = used;
        while (start > 0 && is_utf8_tail(filled[start - 1])) {
            start--;
        }
        uint8_t first = start > 0 ? (uint8_t)filled[start - 1] : 0;
        int32_t whole = first >= 0xF0 ? 4 : first >= 0xE0 ? 3 : first >= 0xC0 ? 2 : 1;
        if (start > 0 && first >= 0xC0 && used - (start - 1) < whole) {
            used = start - 1;
        }
    }
    used = text_cut(filled, used, capacity);
    copy(text, filled, (size_t)used);
    return used;
}

/* ---- Store ---- */

static bool is_key(const char *key, int32_t length)
{
    if (key == NULL || length < 1 || length > HOST_KV_KEY_MAX) {
        return false;
    }
    for (int32_t at = 0; at < length; at++) {
        if (key[at] < '!' || key[at] > '~') {
            return false;
        }
    }
    return true;
}

static standin_value_t *value_of(const char *key, int32_t length)
{
    for (int index = 0; index < STANDIN_STORE_VALUES; index++) {
        standin_value_t *value = &world->values[index];
        if (value->is_stored && (int32_t)strlen(value->key) == length && memcmp(value->key, key, (size_t)length) == 0) {
            return value;
        }
    }
    return NULL;
}

static const standin_setting_t *setting_of(const char *key, int32_t length)
{
    for (int32_t index = 0; index < standin_subject.setting_count; index++) {
        const standin_setting_t *setting = &standin_subject.settings[index];
        if ((int32_t)strlen(setting->key) == length && memcmp(setting->key, key, (size_t)length) == 0) {
            return setting;
        }
    }
    return NULL;
}

static int32_t taken_by(int32_t value_length)
{
    return (2 + (value_length + STORAGE_ENTRY_BYTES - 1) / STORAGE_ENTRY_BYTES) * STORAGE_ENTRY_BYTES;
}

int32_t standin_store_bytes(void)
{
    int32_t total = 0;
    for (int index = 0; index < STANDIN_STORE_VALUES; index++) {
        if (standin()->values[index].is_stored) {
            total += taken_by(world->values[index].length);
        }
    }
    return total;
}

/* The device's write: HOST_OK, or HOST_ERR_NO_SPACE over what an application may keep. */
static int32_t store(const char *key, int32_t key_length, const void *bytes, int32_t length, bool keep_empty)
{
    standin_value_t *value = value_of(key, key_length);
    if (length == 0 && !keep_empty) {
        if (value != NULL) {
            value->is_stored = false;
        }
        return HOST_OK;
    }
    int32_t others = standin_store_bytes() - (value != NULL ? taken_by(value->length) : 0);
    if (others + taken_by(length) > HOST_KV_TOTAL_MAX) {
        return HOST_ERR_NO_SPACE;
    }
    for (int index = 0; value == NULL && index < STANDIN_STORE_VALUES; index++) {
        if (!world->values[index].is_stored) {
            value = &world->values[index];
        }
    }
    if (value == NULL) {
        return HOST_ERR_NO_SPACE;
    }
    copy(value->key, key, (size_t)key_length);
    value->key[key_length] = '\0';
    copy(value->value, bytes, (size_t)length);
    value->length = length;
    value->is_stored = true;
    return HOST_OK;
}

void standin_set(const char *key, const char *text)
{
    standin();
    /* A setting saved empty is kept empty, over its default; NULL forgets it. */
    int32_t result = store(key, (int32_t)strlen(key), text != NULL ? text : "",
                           text != NULL ? (int32_t)strlen(text) : 0, text != NULL);
    if (result != HOST_OK) {
        fprintf(stderr, "standin: \"%s\" does not fit in the store\n", key);
        exit(2);
    }
}

const uint8_t *standin_value(const char *key, int32_t *length)
{
    standin();
    standin_value_t *value = value_of(key, (int32_t)strlen(key));
    if (value == NULL) {
        return NULL;
    }
    if (length != NULL) {
        *length = value->length;
    }
    return value->value;
}

int32_t kv_get(const char *key, int32_t key_length, void *bytes, int32_t capacity)
{
    standin();
    if (!is_key(key, key_length) || bytes == NULL || capacity < 0) {
        return HOST_ERR_ARGUMENT;
    }
    const uint8_t *found = NULL;
    int32_t length = 0;
    standin_value_t *value = value_of(key, key_length);
    const standin_setting_t *setting = setting_of(key, key_length);
    if (value != NULL) {
        found = value->value;
        length = value->length;
    } else if (setting != NULL && setting->otherwise != NULL) {
        found = (const uint8_t *)setting->otherwise;
        length = (int32_t)strlen(setting->otherwise);
    } else {
        return HOST_ERR_NOT_FOUND;
    }
    if (length > capacity) {
        return HOST_ERR_NO_SPACE;
    }
    copy(bytes, found, (size_t)length);
    return length;
}

static int32_t refused_write(int32_t result, const char *what, const char *name, int32_t name_length, int32_t length)
{
    world->refused_writes++;
    /* Only what the limits refuse is the application's doing: a test's refusal is the test's. */
    if (world->refuse_writes == HOST_OK) {
        say(world->refused_what, "%s(\"%.*s\", %d bytes) in %s answered %d", what, name_length, name, length,
            running(), result);
    }
    return result;
}

int32_t kv_set(const char *key, int32_t key_length, const void *bytes, int32_t length)
{
    standin_t *host = standin();
    const standin_setting_t *setting = is_key(key, key_length) ? setting_of(key, key_length) : NULL;
    if (!is_key(key, key_length) || length < 0 || length > HOST_KV_VALUE_MAX || (bytes == NULL && length > 0) ||
        (setting != NULL && setting->kind == STANDIN_SETTING_USERS)) {
        return refused_write(HOST_ERR_ARGUMENT, "kv_set", key != NULL ? key : "", key != NULL ? key_length : 0, length);
    }
    host->handler_writes++;
    if (host->refuse_writes != HOST_OK) {
        return refused_write(host->refuse_writes, "kv_set", key, key_length, length);
    }
    /* The device's store (NVS) compares before it writes: the value it already holds, or the
     * removal of one it does not, leaves the flash as it was. */
    const standin_value_t *held = value_of(key, key_length);
    bool is_change = length == 0 ? held != NULL
                                 : held == NULL || held->length != length || memcmp(held->value, bytes, (size_t)length) != 0;
    int32_t result = store(key, key_length, bytes, length, false);
    if (result != HOST_OK) {
        return refused_write(result, "kv_set", key, key_length, length);
    }
    host->store_writes++;
    host->store_changes += is_change;
    return HOST_OK;
}

/* ---- Files ---- */

static bool is_file_name(const char *name, int32_t length)
{
    if (name == NULL || length < 1 || length > HOST_FILE_NAME_MAX || name[0] == '.') {
        return false;
    }
    for (int32_t at = 0; at < length; at++) {
        char letter = name[at];
        if (!((letter >= 'a' && letter <= 'z') || (letter >= '0' && letter <= '9') || letter == '.' ||
              letter == '-' || letter == '_')) {
            return false;
        }
    }
    return true;
}

static standin_stored_file_t *file_of(const char *name, int32_t length)
{
    for (int index = 0; index < STANDIN_FILES; index++) {
        standin_stored_file_t *file = &world->files[index];
        if (file->is_stored && (int32_t)strlen(file->name) == length && memcmp(file->name, name, (size_t)length) == 0) {
            return file;
        }
    }
    return NULL;
}

static int32_t blocks_of(int32_t size)
{
    return (size + FILE_BLOCK_BYTES - 1) / FILE_BLOCK_BYTES * FILE_BLOCK_BYTES;
}

void standin_file_put(const char *name, const void *bytes, int32_t size)
{
    standin();
    int32_t length = (int32_t)strlen(name);
    standin_stored_file_t *file = file_of(name, length);
    for (int index = 0; file == NULL && index < STANDIN_FILES; index++) {
        if (!world->files[index].is_stored) {
            file = &world->files[index];
        }
    }
    if (file == NULL || size < 0 || size > STANDIN_FILE_BYTES || length > HOST_FILE_NAME_MAX) {
        fprintf(stderr, "standin: no room for the file %s\n", name);
        exit(2);
    }
    strcpy(file->name, name);
    copy(file->bytes, bytes, (size_t)size);
    file->size = size;
    file->is_stored = true;
}

const uint8_t *standin_file(const char *name, int32_t *size)
{
    standin();
    standin_stored_file_t *file = file_of(name, (int32_t)strlen(name));
    if (file == NULL) {
        return NULL;
    }
    if (size != NULL) {
        *size = file->size;
    }
    return file->bytes;
}

int32_t file_size(const char *name, int32_t name_length)
{
    standin();
    if (!is_file_name(name, name_length)) {
        return HOST_ERR_ARGUMENT;
    }
    standin_stored_file_t *file = file_of(name, name_length);
    return file != NULL ? file->size : HOST_ERR_NOT_FOUND;
}

int32_t file_read(const char *name, int32_t name_length, int32_t offset, void *bytes, int32_t capacity)
{
    standin();
    if (!is_file_name(name, name_length) || offset < 0 || bytes == NULL || capacity < 0) {
        return HOST_ERR_ARGUMENT;
    }
    standin_stored_file_t *file = file_of(name, name_length);
    if (file == NULL) {
        return HOST_ERR_NOT_FOUND;
    }
    int32_t left = offset < file->size ? file->size - offset : 0;
    int32_t taken = left < capacity ? left : capacity;
    copy(bytes, file->bytes + offset * (taken > 0), (size_t)taken);
    return taken;
}

static bool ends_with(const char *name, int32_t length, const char *ending)
{
    int32_t ending_length = (int32_t)strlen(ending);
    return length >= ending_length && memcmp(name + length - ending_length, ending, (size_t)ending_length) == 0;
}

static bool is_named(const char *name, int32_t length, const char *wanted)
{
    return length == (int32_t)strlen(wanted) && memcmp(name, wanted, (size_t)length) == 0;
}

int32_t file_write(const char *name, int32_t name_length, int32_t offset, const void *bytes, int32_t length)
{
    standin_t *host = standin();
    if (!standin_subject.writes_files || !is_file_name(name, name_length) || ends_with(name, name_length, ".new") ||
        is_named(name, name_length, "app.wasm") || is_named(name, name_length, "manifest.json") ||
        offset < 0 || length < 0 || (bytes == NULL && length > 0)) {
        return refused_write(HOST_ERR_ARGUMENT, "file_write", name != NULL ? name : "", name != NULL ? name_length : 0, length);
    }
    standin_stored_file_t *file = file_of(name, name_length);
    if (offset > (file != NULL ? file->size : 0)) {
        return refused_write(HOST_ERR_ARGUMENT, "file_write", name, name_length, length);
    }
    host->handler_writes++;
    if (host->refuse_writes != HOST_OK) {
        return refused_write(host->refuse_writes, "file_write", name, name_length, length);
    }
    if (length == 0) {
        if (offset == 0 && file != NULL) {
            file->is_stored = false;
            host->file_writes++;
        }
        return HOST_OK;
    }
    int32_t size = file != NULL && file->size > offset + length ? file->size : offset + length;
    int32_t others = 0;
    for (int index = 0; index < STANDIN_FILES; index++) {
        if (host->files[index].is_stored && &host->files[index] != file) {
            others += blocks_of(host->files[index].size);
        }
    }
    if (others + blocks_of(size) > HOST_FILES_WRITER_MAX || size > STANDIN_FILE_BYTES) {
        return refused_write(HOST_ERR_NO_SPACE, "file_write", name, name_length, length);
    }
    for (int index = 0; file == NULL && index < STANDIN_FILES; index++) {
        if (!host->files[index].is_stored) {
            file = &host->files[index];
            copy(file->name, name, (size_t)name_length);
            file->name[name_length] = '\0';
            file->size = 0;
            file->is_stored = true;
        }
    }
    if (file == NULL) {
        return refused_write(HOST_ERR_NO_SPACE, "file_write", name, name_length, length);
    }
    copy(file->bytes + offset, bytes, (size_t)length);
    file->size = size;
    host->file_writes++;
    return HOST_OK;
}

/* ---- Time ---- */

#define SECONDS_PER_DAY 86400

static bool is_moment(int64_t unix_time)
{
    return unix_time >= HOST_TIME_MIN && unix_time <= HOST_TIME_MAX;
}

/* Days since 1970 to a date, and back (Gregorian, before its own beginning too). */
static void date_of(int64_t days, int32_t *year, int32_t *month, int32_t *day)
{
    days += 719468;
    int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    int64_t day_of_era = days - era * 146097;
    int64_t year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    int64_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    int64_t shifted_month = (5 * day_of_year + 2) / 153;
    *day = (int32_t)(day_of_year - (153 * shifted_month + 2) / 5 + 1);
    *month = (int32_t)(shifted_month < 10 ? shifted_month + 3 : shifted_month - 9);
    *year = (int32_t)(year_of_era + era * 400 + (*month <= 2));
}

static int64_t days_of(int64_t year, int64_t month, int64_t day)
{
    year -= month <= 2;
    int64_t era = (year >= 0 ? year : year - 399) / 400;
    int64_t year_of_era = year - era * 400;
    int64_t day_of_year = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    return era * 146097 + year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year - 719468;
}

/* The zone's offset at a moment: the change of the zone's rules that came last before it,
 * as the tools wrote them for the years around the subject's time; before the first,
 * zone_offset. */
static int32_t offset_at(int64_t unix_time)
{
    int32_t offset = world->zone_offset;
    for (int32_t index = 0; index < standin_subject.zone_change_count; index++) {
        if (standin_subject.zone_changes[index].at > unix_time) {
            break;
        }
        offset = standin_subject.zone_changes[index].offset;
    }
    return offset;
}

static void local_parts(int64_t unix_time, host_local_time_t *parts)
{
    int64_t local = unix_time + offset_at(unix_time);
    int64_t days = local >= 0 ? local / SECONDS_PER_DAY : -((-local + SECONDS_PER_DAY - 1) / SECONDS_PER_DAY);
    int64_t second_of_day = local - days * SECONDS_PER_DAY;
    date_of(days, &parts->year, &parts->month, &parts->day);
    parts->weekday = (int32_t)(((days % 7) + 11) % 7); /* 1970-01-01 was a Thursday */
    parts->hour = (int32_t)(second_of_day / 3600);
    parts->minute = (int32_t)(second_of_day / 60 % 60);
    parts->second = (int32_t)(second_of_day % 60);
}

int64_t time_now(void)
{
    return standin()->now;
}

int32_t time_zone_offset(void)
{
    return standin()->now != 0 ? offset_at(world->now) : 0;
}

int32_t time_local(host_local_time_t *now)
{
    if (now == NULL) {
        return HOST_ERR_ARGUMENT;
    }
    if (standin()->now == 0) {
        return HOST_ERR_NOT_FOUND;
    }
    local_parts(world->now, now);
    return HOST_OK;
}

int32_t time_local_at(int64_t unix_time, host_local_time_t *parts)
{
    standin();
    if (parts == NULL || !is_moment(unix_time)) {
        return HOST_ERR_ARGUMENT;
    }
    local_parts(unix_time, parts);
    return HOST_OK;
}

int32_t time_from_local(const host_local_time_t *parts, int64_t *unix_time)
{
    standin();
    if (parts == NULL || unix_time == NULL) {
        return HOST_ERR_ARGUMENT;
    }
    /* Out-of-range fields carry over: months into years here, the rest by the arithmetic. */
    int64_t month_index = (int64_t)parts->year * 12 + parts->month - 1;
    int64_t year = month_index >= 0 ? month_index / 12 : -((-month_index + 11) / 12);
    int64_t days = days_of(year, month_index - year * 12 + 1, 1) + parts->day - 1;
    int64_t as_utc = days * SECONDS_PER_DAY + (int64_t)parts->hour * 3600 + (int64_t)parts->minute * 60 +
                     parts->second;
    /* As app_api_time.c resolves it: the offsets two days either side each offer a moment; in
     * a fold the earlier of two that match, in a gap the first local time after it. */
    int64_t moment = 0, forward = INT64_MAX;
    bool is_found = false;
    for (int day = -2; day <= 2; day++) {
        int64_t candidate = as_utc - offset_at(as_utc + day * (int64_t)SECONDS_PER_DAY);
        if (!is_moment(candidate)) {
            continue;
        }
        int64_t local = candidate + offset_at(candidate);
        if (local == as_utc) {
            if (!is_found || candidate < moment) {
                moment = candidate;
            }
            is_found = true;
        } else if (!is_found && local > as_utc && local < forward) {
            forward = local;
            moment = candidate;
        }
    }
    if ((!is_found && forward == INT64_MAX) || !is_moment(moment)) {
        return HOST_ERR_ARGUMENT;
    }
    *unix_time = moment;
    return HOST_OK;
}

int32_t time_text(int64_t unix_time, int32_t style, char *text, int32_t capacity)
{
    static const char *const DAYS[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
    static const char *const MONTHS[] = { "January", "February", "March", "April", "May", "June", "July",
                                          "August", "September", "October", "November", "December" };
    standin();
    if (text == NULL || capacity <= 0 || !is_moment(unix_time) || style < HOST_TIME_CLOCK ||
        style > HOST_TIME_WEEKDAY_SHORT) {
        return HOST_ERR_ARGUMENT;
    }
    host_local_time_t when, today;
    local_parts(unix_time, &when);
    local_parts(world->now, &today);
    int64_t days_ahead = days_of(when.year, when.month, when.day) - days_of(today.year, today.month, today.day);
    const char *day = DAYS[when.weekday], *month = MONTHS[when.month - 1];
    char clock[8], near_day[16], written[48];
    snprintf(clock, sizeof clock, "%02d:%02d", when.hour, when.minute);
    if (days_ahead == 0) {
        snprintf(near_day, sizeof near_day, "Today");
    } else if (days_ahead == 1) {
        snprintf(near_day, sizeof near_day, "Tmrw");
    } else if (days_ahead > 1 && days_ahead < 7) {
        snprintf(near_day, sizeof near_day, "%.3s", day);
    } else {
        snprintf(near_day, sizeof near_day, "%d %.3s", when.day, month);
    }
    switch (style) {
    case HOST_TIME_CLOCK: snprintf(written, sizeof written, "%s", clock); break;
    case HOST_TIME_DATE: snprintf(written, sizeof written, "%.3s %d %.3s", day, when.day, month); break;
    case HOST_TIME_WEEKDAY: snprintf(written, sizeof written, "%s", day); break;
    case HOST_TIME_LONG_DATE: snprintf(written, sizeof written, "%d %s", when.day, month); break;
    case HOST_TIME_NEAR:
        if (days_ahead == 0) {
            snprintf(written, sizeof written, "%s", clock);
        } else if (days_ahead >= 1 && days_ahead < 7) {
            snprintf(written, sizeof written, "%s %s", near_day, clock);
        } else {
            snprintf(written, sizeof written, "%s", near_day);
        }
        break;
    case HOST_TIME_NEAR_DAY: snprintf(written, sizeof written, "%s", near_day); break;
    default: snprintf(written, sizeof written, "%.3s", day); break;
    }
    int32_t length = (int32_t)strlen(written);
    if (length > capacity) {
        length = capacity;
    }
    copy(text, written, (size_t)length);
    return length;
}

int64_t time_uptime_ms(void)
{
    return standin()->uptime_ms;
}

uint32_t time_boot_id(void)
{
    return standin()->boot_id;
}

/* ---- Alarms and timers ---- */

int32_t alarm_set(int32_t id, int32_t seconds)
{
    standin_t *host = standin();
    if (id < 0 || id >= HOST_ALARM_COUNT || seconds < 0) {
        return HOST_ERR_ARGUMENT;
    }
    host->alarms[id] = (standin_alarm_t){ .kind = seconds > 0 ? STANDIN_ALARM_IN : STANDIN_ALARM_NONE,
                                           .due_uptime_ms = host->uptime_ms + (int64_t)seconds * MS_PER_SECOND };
    return HOST_OK;
}

int32_t alarm_set_at(int32_t id, int64_t unix_time)
{
    standin_t *host = standin();
    if (id < 0 || id >= HOST_ALARM_COUNT || !is_moment(unix_time)) {
        return HOST_ERR_ARGUMENT;
    }
    host->alarms[id] = (standin_alarm_t){ .kind = STANDIN_ALARM_AT, .at = unix_time };
    return HOST_OK;
}

int32_t alarm_set_every(int32_t id, int32_t seconds, int32_t first_in)
{
    standin_t *host = standin();
    if (id < 0 || id >= HOST_ALARM_COUNT || seconds < HOST_ALARM_EVERY_MIN || seconds > HOST_ALARM_EVERY_MAX ||
        first_in < 0 || first_in > HOST_ALARM_EVERY_MAX) {
        return HOST_ERR_ARGUMENT;
    }
    host->alarms[id] = (standin_alarm_t){
        .kind = STANDIN_ALARM_EVERY, .every = seconds,
        .due_uptime_ms = host->uptime_ms + (int64_t)(first_in > 0 ? first_in : seconds) * MS_PER_SECOND };
    return HOST_OK;
}

int32_t timer_set(int32_t id, int32_t milliseconds)
{
    standin_t *host = standin();
    if (id < 0 || id >= HOST_TIMER_COUNT || milliseconds < 0) {
        return HOST_ERR_ARGUMENT;
    }
    host->timer_due_ms[id] = milliseconds > 0 ? host->uptime_ms + milliseconds : 0;
    host->timer_sets++;
    return HOST_OK;
}

uint32_t standin_timers(void)
{
    uint32_t waiting = 0;
    for (int id = 0; id < HOST_TIMER_COUNT; id++) {
        waiting |= (uint32_t)(standin()->timer_due_ms[id] != 0) << id;
    }
    return waiting;
}

uint32_t standin_alarms(void)
{
    uint32_t waiting = 0;
    for (int id = 0; id < HOST_ALARM_COUNT; id++) {
        waiting |= (uint32_t)(standin()->alarms[id].kind != STANDIN_ALARM_NONE) << id;
    }
    return waiting;
}

int32_t standin_timer_left(int32_t id)
{
    standin_t *host = standin();
    if (host->timer_due_ms[id] == 0) {
        return 0;
    }
    /* One that is overdue still waits: a millisecond, not "not set". */
    return host->timer_due_ms[id] > host->uptime_ms ? (int32_t)(host->timer_due_ms[id] - host->uptime_ms) : 1;
}

bool standin_alarm_due(int32_t id)
{
    const standin_alarm_t *alarm = &standin()->alarms[id];
    return alarm->kind == STANDIN_ALARM_AT ? world->now != 0 && alarm->at <= world->now
           : alarm->kind != STANDIN_ALARM_NONE && alarm->due_uptime_ms <= world->uptime_ms;
}

int32_t standin_alarm_left(int32_t id)
{
    standin_t *host = standin();
    const standin_alarm_t *alarm = &host->alarms[id];
    int64_t left = 0;
    if (alarm->kind == STANDIN_ALARM_NONE) {
        return 0;
    }
    if (alarm->kind == STANDIN_ALARM_AT) {
        left = alarm->at - host->now;
    } else {
        left = (alarm->due_uptime_ms - host->uptime_ms + MS_PER_SECOND - 1) / MS_PER_SECOND;
    }
    return left > 0 ? (int32_t)left : 1;
}

/* ---- Screen ---- */

void screen_redraw(int32_t flags)
{
    standin()->redraws++;
    world->redraw_flags = flags;
}

/* As app_api_ui.c keeps it: in the instance, and in the session while it may write it. */
int32_t screen_set_inverted(int32_t inverted)
{
    standin()->is_inverted = inverted != 0;
    if (world->is_session_writable) {
        world->session_inverted = world->is_inverted;
    }
    return HOST_OK;
}

int32_t header_set_text(int32_t side, const char *text, int32_t length)
{
    standin();
    if ((side != HOST_HEADER_LEFT && side != HOST_HEADER_RIGHT) || length < 0 || (text == NULL && length > 0)) {
        return HOST_ERR_ARGUMENT;
    }
    if (length > HOST_HEADER_TEXT_MAX) {
        return HOST_ERR_ARGUMENT;
    }
    copy(world->header_text[side], text, (size_t)length);
    world->header_text[side][length] = '\0';
    return HOST_OK;
}

int32_t header_set_status(int32_t side, int32_t icon, const char *text, int32_t length)
{
    standin();
    if ((side != HOST_HEADER_LEFT && side != HOST_HEADER_RIGHT) || length < 0 || (text == NULL && length > 0) ||
        icon < HOST_ICON_NONE || icon >= HOST_ICON_COUNT) {
        return HOST_ERR_ARGUMENT;
    }
    if (length > HOST_STATUS_TEXT_MAX) {
        return HOST_ERR_ARGUMENT;
    }
    copy(world->status_text[side], text, (size_t)length);
    world->status_text[side][length] = '\0';
    world->status_icon[side] = icon;
    return HOST_OK;
}

void screen_request(void)
{
    /* Ignored on the screen. */
    if (!standin()->is_on_screen) {
        world->wants_screen = true;
    }
}

static bool is_handler(const char *name)
{
    return world->handler != NULL && strcmp(world->handler, name) == 0;
}

void screen_leave(void)
{
    standin();
    /* host/screen.h: takes effect after these three, ignored in the others. */
    if (is_handler("app_on_button") || is_handler("app_on_back") || is_handler("app_on_timer")) {
        world->is_leaving = true;
    }
}

int32_t notification_post(const host_notice_t *notice)
{
    standin();
    if (notice == NULL) {
        return HOST_ERR_ARGUMENT;
    }
    world->last_notice = *notice;
    world->notices++;
    return HOST_OK;
}

/* ---- Network ---- */

static json_pick_t picked;   /* of the answer being delivered */
static bool has_picked;
static char picked_lists[HOST_LIST_BYTES];
static int32_t answer_status;

int32_t answer_value(int32_t index, char *text, int32_t capacity)
{
    standin();
    if (text == NULL || capacity < 0) {
        return HOST_ERR_ARGUMENT;
    }
    const char *value = has_picked ? json_pick_value(&picked, index) : NULL;
    if (value == NULL) {
        return HOST_ERR_NOT_FOUND;
    }
    int32_t length = (int32_t)strlen(value);
    if (length > capacity) {
        return HOST_ERR_NO_SPACE;
    }
    copy(text, value, (size_t)length);
    return length;
}

int32_t answer_count(int32_t index)
{
    standin();
    return has_picked ? json_pick_count(&picked, index) : 0;
}

int32_t answer_element(int32_t index, int32_t element, char *text, int32_t capacity)
{
    standin();
    if (text == NULL || capacity < 0) {
        return HOST_ERR_ARGUMENT;
    }
    const char *value = has_picked ? json_pick_element(&picked, index, element) : NULL;
    if (value == NULL) {
        return HOST_ERR_NOT_FOUND;
    }
    int32_t length = (int32_t)strlen(value);
    if (length > capacity) {
        return HOST_ERR_NO_SPACE;
    }
    copy(text, value, (size_t)length);
    return length;
}

int32_t answer_item(void)
{
    return standin()->is_delivering ? world->answer_item : -1;
}

int32_t answer_info(host_answer_info_t *response, int32_t capacity)
{
    standin();
    if (response == NULL || capacity < 0) {
        return HOST_ERR_ARGUMENT;
    }
    if (!world->is_delivering) {
        return HOST_ERR_NOT_FOUND;
    }
    /* No headers are stood in for: the status, and every other field absent. */
    host_answer_info_t info = { .status = answer_status, .length = -1 };
    int32_t length = capacity < (int32_t)sizeof info ? capacity : (int32_t)sizeof info;
    copy(response, &info, (size_t)length);
    return length;
}

int32_t answer_header(const char *name, int32_t name_length, int32_t occurrence, char *value, int32_t capacity)
{
    standin();
    if (name == NULL || name_length < 1 || name_length > 63 || occurrence < 0 || value == NULL || capacity < 0) {
        return HOST_ERR_ARGUMENT;
    }
    return HOST_ERR_NOT_FOUND;
}

int32_t poll_run(int32_t poll)
{
    standin();
    if (poll < 0 || poll >= standin_subject.poll_count) {
        return HOST_ERR_ARGUMENT;
    }
    world->polls_run[poll]++;
    return HOST_OK;
}

int32_t poll_set_interval(int32_t poll, int32_t seconds)
{
    standin();
    if (poll < 0 || poll >= standin_subject.poll_count || (seconds != 0 && (seconds < 10 || seconds > SECONDS_PER_DAY))) {
        return HOST_ERR_ARGUMENT;
    }
    world->poll_interval[poll] = seconds;
    return HOST_OK;
}

/* What request_queue.c counts of a request: its record as the device's 32-bit build lays it out,
 * the address and every header's name and value with their terminators, the body. The queue
 * takes 16 KB of them; here only the application's own are in it. */
#define REQUEST_RECORD_BYTES 40
#define REQUEST_QUEUE_BYTES  (16 * 1024)

static int32_t request_bytes(const char *url, const char *headers, int32_t headers_length, int32_t body_length)
{
    int32_t bytes = REQUEST_RECORD_BYTES + (int32_t)strlen(url) + 1 + body_length;
    for (int32_t at = 0; at < headers_length;) {
        int32_t line_end = at;
        while (line_end < headers_length && headers[line_end] != '\n') {
            line_end++;
        }
        if (line_end > at) {
            int32_t colon = at;
            while (colon < line_end && headers[colon] != ':') {
                colon++;
            }
            int32_t value = colon + 1;
            while (value < line_end && headers[value] == ' ') {
                value++;
            }
            bytes += (colon - at) + 1 + (line_end - value) + 1;
        }
        at = line_end + 1;
    }
    return bytes;
}

static int32_t queued_bytes(void)
{
    int32_t bytes = 0;
    for (int32_t index = 0; index < world->request_count; index++) {
        const standin_request_t *queued = &world->requests[index];
        bytes += request_bytes(queued->url, queued->headers, queued->headers_length, queued->body_length);
    }
    return bytes;
}

/* A request the device refuses for what the application asked: a finding of the network check. */
static int32_t refused_request(int32_t result, const char *why)
{
    say(world->refused_request, "http_send() in %s answered %s: %s", running(),
        result == HOST_ERR_NO_SPACE ? "HOST_ERR_NO_SPACE" : "HOST_ERR_ARGUMENT", why);
    return result;
}

int32_t http_send(int32_t tag, int32_t method, const char *url, int32_t url_length, const char *headers,
                  int32_t headers_length, const void *body, int32_t body_length)
{
    standin_t *host = standin();
    if (tag < HOST_REQUEST_FIRST || tag > HOST_REQUEST_LAST || method < 0 || method > HOST_HTTP_METHOD_LAST ||
        url == NULL || url_length < 1 || url_length >= STANDIN_URL_BYTES || headers_length < 0 ||
        headers_length > HOST_HTTP_HEADERS_MAX || (headers == NULL && headers_length > 0) || body_length < 0 ||
        body_length > HOST_HTTP_BODY_MAX || (body == NULL && body_length > 0)) {
        return refused_request(HOST_ERR_ARGUMENT, "a tag, a method or a length the contract does not take");
    }
    /* As app_api_data.c and poller.c take it: printable text, at most four headers of
     * "Name: value", the address filled and its host one of the manifest's before it is queued. */
    for (int32_t at = 0; at < url_length; at++) {
        if (url[at] < ' ' || url[at] > '~') {
            return refused_request(HOST_ERR_ARGUMENT, "a character that is not printable ASCII in the address");
        }
    }
    int32_t header_count = 0;
    for (int32_t at = 0; at < headers_length;) {
        int32_t line_end = at, colon = -1, value_start;
        while (line_end < headers_length && headers[line_end] != '\n') {
            if (headers[line_end] < ' ' || headers[line_end] > '~') {
                return refused_request(HOST_ERR_ARGUMENT, "a character that is not printable ASCII in a header");
            }
            if (colon < 0 && headers[line_end] == ':') {
                colon = line_end;
            }
            line_end++;
        }
        if (line_end > at) {
            for (value_start = colon + 1; colon >= 0 && value_start < line_end && headers[value_start] == ' ';) {
                value_start++;
            }
            if (colon < 0 || colon - at < 1 || colon - at > 31 || line_end - value_start > 255 || ++header_count > 4) {
                return refused_request(HOST_ERR_ARGUMENT, "a header that is not \"Name: value\", or a fifth");
            }
        }
        at = line_end + 1;
    }
    char written[STANDIN_URL_BYTES], filled[STANDIN_URL_BYTES];
    copy(written, url, (size_t)url_length);
    written[url_length] = '\0';
    if (!standin_fill_url(written, filled, sizeof filled)) {
        return HOST_ERR_ARGUMENT; /* a value it needs is not set: the user's to do, no fault */
    }
    if (!standin_url_allowed(filled)) {
        return refused_request(HOST_ERR_ARGUMENT, "a host the manifest's \"hosts\" does not list");
    }
    host->sends++;
    if (host->refuse_requests != HOST_OK) {
        return host->refuse_requests;
    }
    int32_t bytes = request_bytes(filled, headers, headers_length, body_length);
    if (host->request_count >= HOST_HTTP_PENDING_MAX || queued_bytes() + bytes > REQUEST_QUEUE_BYTES) {
        /* Requests left from scenes that answered none would have gone on the device: only
         * what one handler queued by itself and does not fit is the application's doing. */
        if (host->handler_requests + 1 > HOST_HTTP_PENDING_MAX ||
            host->handler_request_bytes + bytes > REQUEST_QUEUE_BYTES) {
            return refused_request(HOST_ERR_NO_SPACE, "one handler asks more than the device's queue takes - 16 "
                                                      "requests, and 16 KB of them with their addresses, headers and bodies");
        }
        return HOST_ERR_NO_SPACE;
    }
    host->handler_requests++;
    host->handler_request_bytes += bytes;
    standin_request_t *request = &host->requests[host->request_count++];
    memset(request, 0, sizeof *request);
    request->tag = tag;
    request->method = method;
    /* The address as it was filled now stays the request's. */
    copy(request->url, filled, strlen(filled));
    copy(request->headers, headers, (size_t)headers_length);
    request->headers_length = headers_length;
    copy(request->body, body, (size_t)body_length);
    request->body_length = body_length;
    return HOST_OK;
}

/* A text setting as a poll's address takes it: the stored value or the default. */
static int32_t setting_text(const char *key, int32_t key_length, char *text, int32_t capacity)
{
    int32_t length = kv_get(key, key_length, text, capacity - 1);
    if (length < 0) {
        length = 0;
    }
    text[length] = '\0';
    return length;
}

/* Item `index` of a list's text, as [start, end); false past its end. Empty lines are no items. */
static bool list_item(const char *list, int32_t index, const char **start, int32_t *length)
{
    const char *at = list;
    while (*at != '\0') {
        const char *end = strchr(at, '\n');
        int32_t item_length = end != NULL ? (int32_t)(end - at) : (int32_t)strlen(at);
        if (item_length > 0 && index-- == 0) {
            *start = at;
            *length = item_length;
            return true;
        }
        at += item_length + (end != NULL);
    }
    return false;
}

static bool has_prefix(const char *item, int32_t length, const char *prefix)
{
    int32_t prefix_length = (int32_t)strlen(prefix);
    return length >= prefix_length && memcmp(item, prefix, (size_t)prefix_length) == 0;
}

/* Whether poll `poll` asks for this item of its list: a poll with a prefix
 * claims the items that begin with it, one without takes what none claims. */
static bool claims(int32_t poll, const char *item, int32_t length)
{
    const standin_poll_t *asked = &standin_subject.polls[poll];
    if (asked->prefix != NULL) {
        return has_prefix(item, length, asked->prefix);
    }
    for (int32_t other = 0; other < standin_subject.poll_count; other++) {
        const standin_poll_t *another = &standin_subject.polls[other];
        if (another->prefix != NULL && another->each != NULL && strcmp(another->each, asked->each) == 0 &&
            has_prefix(item, length, another->prefix)) {
            return false;
        }
    }
    return true;
}

int32_t standin_poll_item(int32_t poll, int32_t index)
{
    standin();
    const standin_poll_t *asked = &standin_subject.polls[poll];
    char list[HOST_KV_VALUE_MAX + 1];
    const char *item;
    int32_t length;
    if (asked->each == NULL) {
        return -1;
    }
    setting_text(asked->each, (int32_t)strlen(asked->each), list, sizeof list);
    for (int32_t number = 0; list_item(list, number, &item, &length); number++) {
        if (claims(poll, item, length) && index-- == 0) {
            return number;
        }
    }
    return -1;
}

int32_t standin_poll_items(int32_t poll)
{
    if (standin_subject.polls[poll].each == NULL) {
        return -1;
    }
    int32_t count = 0;
    while (standin_poll_item(poll, count) >= 0) {
        count++;
    }
    return count;
}

/* {{key}} of an address filled from the store; for a poll over a list, its own key with the item. */
/* What a URL takes as it is (text_template.c); everything else goes in as %XX. */
static bool is_unreserved(char character)
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '-' || character == '.' || character == '_' ||
           character == '~';
}

/* As text_template.c fills a template: a value at the start of a URL - the server - and every
 * value of a header go in as they are and must be printable; any other value of a URL is
 * percent-encoded; a list's items are joined with commas. A key with no value and no default
 * fails it; an empty value goes in empty. */
static bool fill_text(const char *written, const standin_poll_t *asked, int32_t item, bool is_url, char *url,
                      int32_t capacity)
{
    standin();
    int32_t used = 0;
    for (const char *at = written; *at != '\0';) {
        const char *close = at[0] == '{' && at[1] == '{' ? strstr(at, "}}") : NULL;
        if (close == NULL) {
            if (used + 1 >= capacity) {
                return false;
            }
            url[used++] = *at++;
            continue;
        }
        const char *key = at + 2;
        int32_t key_length = (int32_t)(close - key);
        char value[HOST_KV_VALUE_MAX + 1];
        const char *filled = value;
        int32_t length;
        if (asked != NULL && asked->each != NULL && item >= 0 && is_named(key, key_length, asked->each)) {
            setting_text(key, key_length, value, sizeof value);
            if (!list_item(value, item, &filled, &length)) {
                return false;
            }
            if (asked->prefix != NULL) {
                filled += strlen(asked->prefix);
                length -= (int32_t)strlen(asked->prefix);
            }
        } else {
            length = key_length > 0 ? kv_get(key, key_length, value, sizeof value - 1) : HOST_ERR_ARGUMENT;
            if (length < 0) {
                return false;
            }
        }
        bool is_verbatim = !is_url || at == written;
        for (int32_t index = 0; index < length; index++) {
            bool is_join = filled[index] == '\n'; /* between a list's items */
            char character = is_join ? ',' : filled[index];
            bool is_kept = is_verbatim || is_join || is_unreserved(character);
            if (is_verbatim && (character < ' ' || character > '~')) {
                return false;
            }
            if (used + (is_kept ? 1 : 3) >= capacity) {
                return false;
            }
            if (is_kept) {
                url[used++] = character;
            } else {
                static const char HEX[] = "0123456789ABCDEF";
                url[used++] = '%';
                url[used++] = HEX[(uint8_t)character >> 4];
                url[used++] = HEX[(uint8_t)character & 0x0F];
            }
        }
        at = close + 2;
    }
    url[used] = '\0';
    /* A URL made of an empty setting alone is no request (text_template_prepare_request). */
    return !is_url || used > 0;
}

bool standin_poll_url(int32_t poll, int32_t item, char *url, int32_t capacity)
{
    return fill_text(standin_subject.polls[poll].url, &standin_subject.polls[poll], item, true, url, capacity);
}

bool standin_fill_url(const char *written, char *url, int32_t capacity)
{
    return fill_text(written, NULL, -1, true, url, capacity);
}

/* A host as app_manifest.c reads one: the name, and the port when one is written. */
typedef struct {
    const char *name;
    size_t      name_length;
    int32_t     port; /* 0: none written */
} host_t;

static host_t host_of(const char *text, size_t length)
{
    host_t host = { text, length, 0 };
    const char *colon = memchr(text, ':', length);
    if (colon != NULL) {
        host.name_length = (size_t)(colon - text);
        for (const char *digit = colon + 1; digit < text + length; digit++) {
            host.port = host.port * 10 + (*digit - '0');
        }
    }
    return host;
}

/* The host of an http or https address, its port the scheme's own when it names none; false
 * when it has none to be sure of - another scheme, or a name and password before it. */
static bool host_in_url(const char *url, host_t *host)
{
    bool is_https = strncmp(url, "https://", 8) == 0;
    const char *text = is_https ? url + 8 : strncmp(url, "http://", 7) == 0 ? url + 7 : NULL;
    if (text == NULL) {
        return false;
    }
    size_t length = strcspn(text, "/?#");
    if (length == 0 || memchr(text, '@', length) != NULL) {
        return false;
    }
    *host = host_of(text, length);
    if (host->port == 0) {
        host->port = is_https ? 443 : 80;
    }
    return host->name_length > 0;
}

/* Host names are the same in any case. */
static bool is_same_name(const host_t *host, const host_t *other)
{
    if (host->name_length != other->name_length) {
        return false;
    }
    for (size_t at = 0; at < host->name_length; at++) {
        char one = host->name[at], two = other->name[at];
        one = one >= 'A' && one <= 'Z' ? (char)(one + 32) : one;
        two = two >= 'A' && two <= 'Z' ? (char)(two + 32) : two;
        if (one != two) {
            return false;
        }
    }
    return true;
}

/* As app_manifest_allows_url() decides: a name listed without a port is that name at 80 or
 * 443; "{{server}}" is the host and the port of the address the user set. */
bool standin_url_allowed(const char *url)
{
    host_t host;
    if (!host_in_url(url, &host)) {
        return false;
    }
    for (int32_t index = 0; index < standin_subject.host_count; index++) {
        const char *entry = standin_subject.hosts[index];
        host_t named;
        if (strncmp(entry, "{{", 2) == 0) {
            char server[HOST_KV_VALUE_MAX + 1];
            size_t key_length = strlen(entry) - 4;
            int32_t length = key_length <= HOST_KV_KEY_MAX ? kv_get(entry + 2, (int32_t)key_length, server,
                                                                    sizeof server - 1)
                                                           : -1;
            if (length < 0) {
                continue;
            }
            server[length] = '\0';
            if (host_in_url(server, &named) && is_same_name(&named, &host) && named.port == host.port) {
                return true;
            }
            continue;
        }
        named = host_of(entry, strlen(entry));
        bool is_same_port = named.port == 0 ? host.port == 80 || host.port == 443 : named.port == host.port;
        if (is_same_name(&named, &host) && is_same_port) {
            return true;
        }
    }
    return false;
}

/* The values of a queued request's headers, filled when it is sent. */
bool standin_headers_ready(const standin_request_t *request)
{
    return standin_header_lines_ready(request->headers, request->headers_length);
}

bool standin_poll_ready(int32_t poll)
{
    const char *headers = standin_subject.polls[poll].headers;
    return headers == NULL || standin_header_lines_ready(headers, (int32_t)strlen(headers));
}

bool standin_header_lines_ready(const char *headers, int32_t length)
{
    const char *at = headers, *end = headers + length;
    while (at < end) {
        const char *line_end = memchr(at, '\n', (size_t)(end - at));
        line_end = line_end != NULL ? line_end : end;
        const char *colon = memchr(at, ':', (size_t)(line_end - at));
        if (colon != NULL) {
            char written[HOST_HTTP_HEADERS_MAX + 1], filled[256];
            const char *value = colon + 1;
            while (value < line_end && *value == ' ') {
                value++;
            }
            snprintf(written, sizeof written, "%.*s", (int)(line_end - value), value);
            if (!fill_text(written, NULL, -1, false, filled, sizeof filled)) {
                return false;
            }
        }
        at = line_end + 1;
    }
    return true;
}

/* ---- Drawing: recorded, not drawn ---- */

/* What the fonts are on the device with Departure Mono, which is monospaced:
 * the line, the capitals and a character's advance (host/gfx.h). */
static const struct {
    int32_t line, capitals, advance, ascent;
} FONTS[] = {
    [HOST_FONT_TEXT] = { 30, 16, 14, 22 },    [HOST_FONT_MEDIUM] = { 45, 24, 21, 33 },
    [HOST_FONT_LARGE] = { 60, 32, 28, 44 },   [HOST_FONT_DISPLAY] = { 90, 48, 42, 66 },
    [HOST_FONT_HUGE] = { 150, 80, 70, 110 },  [HOST_FONT_SMALL] = { 15, 8, 7, 11 },
};
#define FONT_COUNT ((int32_t)(sizeof FONTS / sizeof FONTS[0]))
/* The kit's styles in the fonts they are set in. */
static const int32_t STYLE_FONTS[] = {
    [HOST_UI_TEXT] = HOST_FONT_TEXT, [HOST_UI_TITLE] = HOST_FONT_MEDIUM, [HOST_UI_CAPTION] = HOST_FONT_SMALL,
    [HOST_UI_VALUE] = HOST_FONT_DISPLAY,
};
#define STYLE_COUNT ((int32_t)(sizeof STYLE_FONTS / sizeof STYLE_FONTS[0]))
#define LIST_ROW_HEIGHT 42
#define LIST_TOP_GAP    8
#define CELL_PADDING     12 /* kit.c */
#define CELL_PICTURE_GAP 14
#define TITLE_TOP        10 /* kit.c: the title's top, the gap under its letters, the rule, the gap after */
#define TITLE_RULE_GAP   8
#define TITLE_AFTER_RULE 10
#define QR_MODULES_MAX  57

static void mix(const void *bytes, size_t size)
{
    const uint8_t *at = bytes;
    for (size_t index = 0; index < size; index++) {
        world->picture = (world->picture ^ at[index]) * 0x100000001b3u;
    }
}

static void mix_number(int32_t number)
{
    mix(&number, sizeof number);
}

/* A fixed text field of a struct: up to its terminator, for what follows it is not text. */
static void mix_field(const char *field, size_t size)
{
    mix(field, strnlen(field, size));
    mix_number((int32_t)size);
}

/* True inside app_on_draw(); outside it the call is counted and answers HOST_ERR_NOT_DRAWING. */
static bool is_frame(const char *call)
{
    if (standin()->is_drawing) {
        mix(call, strlen(call));
        world->draw_calls++;
        return true;
    }
    world->stray_draws++;
    say(world->stray_draw, "%s() in %s", call, running());
    return false;
}

static int32_t refused_draw(const char *call)
{
    world->refused_draws++;
    say(world->refused_draw, "%s() answered HOST_ERR_ARGUMENT", call);
    return HOST_ERR_ARGUMENT;
}

static bool is_drawable(int32_t coordinate)
{
    return coordinate >= -COORDINATE_LIMIT && coordinate <= COORDINATE_LIMIT;
}

static bool is_font(int32_t font)
{
    font &= ~HOST_FONT_TABULAR;
    return font >= 0 && font < FONT_COUNT;
}

static int32_t advance_of(int32_t font)
{
    return FONTS[font & ~HOST_FONT_TABULAR].advance;
}

static int32_t characters(const char *text, int32_t length)
{
    int32_t count = 0;
    for (int32_t at = 0; at < length; at++) {
        count += ((uint8_t)text[at] & 0xC0) != 0x80;
    }
    return count;
}

static bool is_text(const char *text, int32_t length)
{
    return length >= 0 && (text != NULL || length == 0);
}

static int32_t text_width(int32_t font, const char *text, int32_t length)
{
    if (length > HOST_TEXT_MAX) {
        length = HOST_TEXT_MAX;
    }
    return characters(text, length) * advance_of(font);
}

/* The text cut with "..." to a width; returns its length. */
static int32_t fit(int32_t font, const char *text, int32_t length, int32_t max_width, char *fitted)
{
    if (length > HOST_TEXT_MAX) {
        length = HOST_TEXT_MAX;
    }
    if (text_width(font, text, length) <= max_width) {
        copy(fitted, text, (size_t)length);
        return length;
    }
    int32_t room = max_width / advance_of(font) - 3;
    if (room < 0) {
        return 0;
    }
    int32_t kept = 0;
    while (kept < length && characters(text, kept + 1) <= room) {
        kept++;
    }
    while (kept > 0 && kept < length && ((uint8_t)text[kept] & 0xC0) == 0x80) {
        kept--;
    }
    copy(fitted, text, (size_t)kept);
    copy(fitted + kept, "...", 3);
    return kept + 3;
}

static int32_t surface_width(void)
{
    return SURFACE_WIDTH;
}

static int32_t surface_height(void)
{
    return standin_subject.is_fullscreen ? SURFACE_HEIGHT : SURFACE_HEIGHT - HEADER_HEIGHT;
}

int32_t gfx_width(void)
{
    return is_frame("gfx_width") ? SURFACE_WIDTH : HOST_ERR_NOT_DRAWING;
}

int32_t gfx_height(void)
{
    return is_frame("gfx_height") ? surface_height() : HOST_ERR_NOT_DRAWING;
}

int32_t gfx_draw_text(int32_t x, int32_t y, int32_t font, const char *text, int32_t length)
{
    if (!is_frame("gfx_draw_text")) {
        return HOST_ERR_NOT_DRAWING;
    }
    if (!is_font(font) || !is_drawable(x) || !is_drawable(y) || !is_text(text, length)) {
        return refused_draw("gfx_draw_text");
    }
    if (length > HOST_TEXT_MAX) {
        length = HOST_TEXT_MAX;
    }
    mix_number(x);
    mix_number(y);
    mix_number(font);
    mix(text, (size_t)length);
    return text_width(font, text, length);
}

int32_t gfx_text_width(int32_t font, const char *text, int32_t length)
{
    standin();
    return is_font(font) && is_text(text, length) ? text_width(font, text, length) : HOST_ERR_ARGUMENT;
}

int32_t gfx_text_ink(int32_t font, const char *text, int32_t length, host_text_ink_t *ink)
{
    standin();
    if (!is_font(font) || !is_text(text, length) || ink == NULL) {
        return HOST_ERR_ARGUMENT;
    }
    int32_t width = text_width(font, text, length);
    *ink = (host_text_ink_t){ .first_x = width > 0 ? 1 : 0, .end_x = width > 0 ? width - 1 : 0 };
    return HOST_OK;
}

int32_t gfx_fit_text(int32_t font, const char *text, int32_t length, int32_t max_width, char *fitted,
                     int32_t capacity)
{
    standin();
    if (!is_font(font) || !is_text(text, length) || !is_drawable(max_width) || fitted == NULL || capacity < 0) {
        return HOST_ERR_ARGUMENT;
    }
    char cut[HOST_TEXT_MAX + 4];
    int32_t cut_length = fit(font, text, length, max_width, cut);
    if (cut_length > capacity) {
        return HOST_ERR_NO_SPACE;
    }
    copy(fitted, cut, (size_t)cut_length);
    return cut_length;
}

int32_t gfx_font_height(int32_t font)
{
    return is_font(font) ? FONTS[font & ~HOST_FONT_TABULAR].line : HOST_ERR_ARGUMENT;
}

int32_t gfx_font_cap_height(int32_t font)
{
    return is_font(font) ? FONTS[font & ~HOST_FONT_TABULAR].capitals : HOST_ERR_ARGUMENT;
}

int32_t gfx_font_ascent(int32_t font)
{
    if (!is_font(font)) {
        return HOST_ERR_ARGUMENT;
    }
    font &= ~HOST_FONT_TABULAR;
    return FONTS[font].capitals + (FONTS[font].line - FONTS[font].capitals) / 2;
}

int32_t gfx_set_color(int32_t color)
{
    if (!is_frame("gfx_set_color")) {
        return HOST_ERR_NOT_DRAWING;
    }
    if (color < HOST_PAPER || color > HOST_INK) {
        return refused_draw("gfx_set_color");
    }
    mix_number(color);
    return HOST_OK;
}

int32_t gfx_draw_rect(int32_t x, int32_t y, int32_t width, int32_t height, int32_t fill)
{
    if (!is_frame("gfx_draw_rect")) {
        return HOST_ERR_NOT_DRAWING;
    }
    if (!is_drawable(x) || !is_drawable(y) || !is_drawable(width) || !is_drawable(height)) {
        return refused_draw("gfx_draw_rect");
    }
    int32_t numbers[] = { x, y, width, height, fill };
    mix(numbers, sizeof numbers);
    return HOST_OK;
}

int32_t gfx_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if (!is_frame("gfx_draw_line")) {
        return HOST_ERR_NOT_DRAWING;
    }
    if (!is_drawable(x0) || !is_drawable(y0) || !is_drawable(x1) || !is_drawable(y1)) {
        return refused_draw("gfx_draw_line");
    }
    int32_t numbers[] = { x0, y0, x1, y1 };
    mix(numbers, sizeof numbers);
    return HOST_OK;
}

static bool is_bitmap_box(int32_t x, int32_t y, int32_t width, int32_t height)
{
    /* host/gfx.h: positive and no larger than the surface, even when clipping would hide the rest. */
    return is_drawable(x) && is_drawable(y) && width > 0 && height > 0 && width <= SURFACE_WIDTH &&
           height <= surface_height();
}

int32_t gfx_draw_bitmap(int32_t x, int32_t y, int32_t width, int32_t height, const uint8_t *bits)
{
    if (!is_frame("gfx_draw_bitmap")) {
        return HOST_ERR_NOT_DRAWING;
    }
    if (!is_bitmap_box(x, y, width, height) || bits == NULL) {
        return refused_draw("gfx_draw_bitmap");
    }
    int32_t numbers[] = { x, y, width, height };
    mix(numbers, sizeof numbers);
    mix(bits, (size_t)((width + 7) / 8) * (size_t)height);
    return HOST_OK;
}

int32_t gfx_draw_qr(int32_t x, int32_t y, int32_t cell, const char *text, int32_t length)
{
    standin();
    /* Cell 0 only measures: that is no drawing. */
    if (cell != 0 && !is_frame("gfx_draw_qr")) {
        return HOST_ERR_NOT_DRAWING;
    }
    if (!is_drawable(x) || !is_drawable(y) || cell < 0 || cell > 16 || !is_text(text, length) ||
        length > HOST_QR_TEXT_MAX) {
        return cell != 0 ? refused_draw("gfx_draw_qr") : HOST_ERR_ARGUMENT;
    }
    if (cell != 0) {
        int32_t numbers[] = { x, y, cell };
        mix(numbers, sizeof numbers);
        mix(text, (size_t)length);
    }
    /* A version every 16 bytes, four modules a version: near enough to lay a page out by. */
    int32_t modules = 21 + 4 * (length / 16);
    return modules < QR_MODULES_MAX ? modules : QR_MODULES_MAX;
}

int32_t gfx_draw_bitmap_file(int32_t x, int32_t y, int32_t width, int32_t height, const char *name,
                             int32_t name_length, int32_t offset)
{
    if (!is_frame("gfx_draw_bitmap_file")) {
        return HOST_ERR_NOT_DRAWING;
    }
    standin_stored_file_t *file = is_file_name(name, name_length) ? file_of(name, name_length) : NULL;
    int32_t size = is_bitmap_box(x, y, width, height) ? (width + 7) / 8 * height : 0;
    if (file == NULL || size == 0 || offset < 0 || offset > file->size - size) {
        return refused_draw("gfx_draw_bitmap_file");
    }
    int32_t numbers[] = { x, y, width, height };
    mix(numbers, sizeof numbers);
    mix(file->bytes + offset, (size_t)size);
    return HOST_OK;
}

static bool is_icon(int32_t icon)
{
    return icon >= 0 && icon < HOST_ICON_COUNT;
}

int32_t gfx_draw_icon(int32_t x, int32_t y, int32_t icon, int32_t cell)
{
    if (!is_frame("gfx_draw_icon")) {
        return HOST_ERR_NOT_DRAWING;
    }
    if (!is_drawable(x) || !is_drawable(y) || !is_icon(icon) || cell < 1 || cell > HOST_ICON_CELL_MAX) {
        return refused_draw("gfx_draw_icon");
    }
    int32_t numbers[] = { x, y, icon, cell };
    mix(numbers, sizeof numbers);
    return HOST_OK;
}

int32_t gfx_icon_ink(int32_t icon, int32_t cell, host_icon_ink_t *ink)
{
    standin();
    if (!is_icon(icon) || cell < 1 || cell > HOST_ICON_CELL_MAX || ink == NULL) {
        return HOST_ERR_ARGUMENT;
    }
    /* An icon keeps a cell clear at every side. */
    *ink = (host_icon_ink_t){ .first_x = cell, .last_x = cell * (HOST_ICON_CELLS - 1) - 1,
                              .first_y = cell, .last_y = cell * (HOST_ICON_CELLS - 1) - 1 };
    return HOST_OK;
}

/* A picture's bytes: its two sizes and a bit a cell. */
static size_t picture_bytes(const uint8_t *picture)
{
    return 2 + ((size_t)picture[0] + 7) / 8 * picture[1]; /* gfx_picture_bytes(): rows of whole bytes */
}

int32_t gfx_draw_picture(int32_t x, int32_t y, const uint8_t *picture, int32_t cell)
{
    if (!is_frame("gfx_draw_picture")) {
        return HOST_ERR_NOT_DRAWING;
    }
    if (picture == NULL || !is_drawable(x) || !is_drawable(y) || cell < 1 || cell > HOST_ICON_CELL_MAX) {
        return refused_draw("gfx_draw_picture");
    }
    int32_t numbers[] = { x, y, cell };
    mix(numbers, sizeof numbers);
    mix(picture, picture_bytes(picture));
    return HOST_OK;
}

/* ---- The kit ---- */

static bool is_style(int32_t style)
{
    return style >= 0 && style < STYLE_COUNT;
}

#define LIST_ROWS_MAX 10000 /* host/ui.h */

/* As the firmware reads a list (app_api_ui.c, read_list). A row's index is
 * not held to the list: a row outside it is the scroll bar alone. */
static bool is_list(const host_list_t *list)
{
    return list != NULL && list->count >= 0 && list->count <= LIST_ROWS_MAX && list->selected >= -1 &&
           list->selected <= LIST_ROWS_MAX && list->first >= -LIST_ROWS_MAX && list->first <= LIST_ROWS_MAX &&
           is_drawable(list->top) && list->top >= 0 && is_drawable(list->height) && list->height >= 0;
}

/* list.c: rows of 42 pixels under a gap of 8, in the list's height or what the surface has below its top. */
static int32_t list_rows(const host_list_t *list)
{
    int32_t room = surface_height() - list->top;
    int32_t height = list->height > 0 && list->height < room ? list->height : room;
    int32_t rows = (height - LIST_TOP_GAP) / LIST_ROW_HEIGHT;
    return rows > 0 ? rows : 0;
}

/* A call that measures for a frame: the device answers it only while one is drawn. */
static bool is_measuring(const char *call)
{
    if (standin()->is_drawing) {
        return true;
    }
    world->stray_draws++;
    say(world->stray_draw, "%s() in %s", call, running());
    return false;
}

int32_t ui_list_visible(const host_list_t *list)
{
    if (!is_measuring("ui_list_visible")) {
        return HOST_ERR_NOT_DRAWING;
    }
    return is_list(list) ? list_rows(list) : HOST_ERR_ARGUMENT;
}

int32_t ui_list_first(const host_list_t *list)
{
    if (!is_measuring("ui_list_first")) {
        return HOST_ERR_NOT_DRAWING;
    }
    if (!is_list(list)) {
        return HOST_ERR_ARGUMENT;
    }
    int32_t rows = list_rows(list), first = list->first;
    int32_t last_first = list->count > rows ? list->count - rows : 0;
    if (list->selected >= 0 && list->selected < first) {
        first = list->selected;
    } else if (list->selected >= 0 && rows > 0 && list->selected >= first + rows) {
        first = list->selected - rows + 1;
    }
    if (first > last_first) {
        first = last_first;
    }
    return first > 0 ? first : 0;
}

int32_t ui_draw_list_row(const host_list_t *list, int32_t index, const char *label, int32_t label_length,
                         const char *value, int32_t value_length, int32_t icon)
{
    if (!is_frame("ui_draw_list_row")) {
        return HOST_ERR_NOT_DRAWING;
    }
    if (!is_list(list) || !is_text(label, label_length) ||
        !is_text(value, value_length) || (icon != HOST_ICON_NONE && !is_icon(icon))) {
        return refused_draw("ui_draw_list_row");
    }
    mix(list, sizeof *list);
    mix_number(index);
    mix(label, (size_t)label_length);
    mix_number(icon);
    mix(value, (size_t)value_length);
    return HOST_OK;
}

static void mix_notice(const host_notice_t *notice)
{
    mix_field(notice->first, sizeof notice->first);
    mix_field(notice->second, sizeof notice->second);
    mix_field(notice->detail, sizeof notice->detail);
    mix_field(notice->ok, sizeof notice->ok);
    mix_field(notice->back, sizeof notice->back);
    mix_number(notice->icon);
    mix_number(notice->is_working);
}

int32_t ui_draw_notice(const host_notice_t *notice)
{
    if (!is_frame("ui_draw_notice")) {
        return HOST_ERR_NOT_DRAWING;
    }
    if (notice == NULL || (notice->icon != HOST_ICON_NONE && !is_icon(notice->icon)) || notice->first[0] == '\0') {
        return refused_draw("ui_draw_notice");
    }
    mix_notice(notice);
    return HOST_OK;
}

int32_t ui_draw_text(int32_t x, int32_t y, int32_t style, const char *text, int32_t length, int32_t max_width)
{
    if (!is_frame("ui_draw_text")) {
        return HOST_ERR_NOT_DRAWING;
    }
    if (!is_style(style) || !is_drawable(x) || !is_drawable(y) || !is_text(text, length) || max_width < 0 ||
        !is_drawable(max_width)) {
        return refused_draw("ui_draw_text");
    }
    char cut[HOST_TEXT_MAX + 4];
    int32_t cut_length = max_width > 0 ? fit(STYLE_FONTS[style], text, length, max_width, cut)
                                       : fit(STYLE_FONTS[style], text, length, COORDINATE_LIMIT * HOST_TEXT_MAX, cut);
    int32_t numbers[] = { x, y, style };
    mix(numbers, sizeof numbers);
    mix(cut, (size_t)cut_length);
    return text_width(STYLE_FONTS[style], cut, cut_length);
}

int32_t ui_draw_text_lines(int32_t x, int32_t y, int32_t style, const char *text, int32_t length, int32_t width,
                           int32_t max_lines)
{
    if (!is_frame("ui_draw_text_lines")) {
        return HOST_ERR_NOT_DRAWING;
    }
    if (!is_style(style) || !is_drawable(x) || !is_drawable(y) || !is_text(text, length) || width <= 0 ||
        !is_drawable(width) || max_lines < 1 || max_lines > HOST_UI_LINES_MAX) {
        return refused_draw("ui_draw_text_lines");
    }
    int32_t numbers[] = { x, y, style, width, max_lines };
    mix(numbers, sizeof numbers);
    mix(text, (size_t)length);
    int32_t per_line = width / advance_of(STYLE_FONTS[style]);
    int32_t lines = per_line > 0 ? (characters(text, length) + per_line - 1) / per_line : max_lines;
    return lines < max_lines ? (lines > 0 ? lines : 1) : max_lines;
}

int32_t ui_text_width(int32_t style, const char *text, int32_t length)
{
    standin();
    return is_style(style) && is_text(text, length) ? text_width(STYLE_FONTS[style], text, length) : HOST_ERR_ARGUMENT;
}

int32_t ui_text_height(int32_t style)
{
    return is_style(style) ? FONTS[STYLE_FONTS[style]].line : HOST_ERR_ARGUMENT;
}

int32_t ui_draw_title(const char *text, int32_t length)
{
    if (!is_frame("ui_draw_title")) {
        return HOST_ERR_NOT_DRAWING;
    }
    if (!is_text(text, length)) {
        return refused_draw("ui_draw_title");
    }
    mix(text, (size_t)length);
    return TITLE_TOP + FONTS[HOST_FONT_MEDIUM].ascent + TITLE_RULE_GAP + 1 + TITLE_AFTER_RULE;
}

int32_t ui_draw_icon(int32_t x, int32_t y, int32_t icon, int32_t size)
{
    if (!is_frame("ui_draw_icon")) {
        return HOST_ERR_NOT_DRAWING;
    }
    if (!is_drawable(x) || !is_drawable(y) || !is_icon(icon) || size < HOST_UI_ICON_SMALL || size > HOST_UI_ICON_LARGE) {
        return refused_draw("ui_draw_icon");
    }
    int32_t numbers[] = { x, y, icon, size };
    mix(numbers, sizeof numbers);
    return HOST_OK;
}

int32_t ui_draw_button(int32_t x, int32_t y, const char *name, int32_t name_length, const char *action,
                       int32_t action_length, int32_t mirrored)
{
    if (!is_frame("ui_draw_button")) {
        return HOST_ERR_NOT_DRAWING;
    }
    /* The name of a button is a word: "BACK" is the longest there is (app_api_ui.c). */
    if (!is_drawable(x) || !is_drawable(y) || !is_text(name, name_length) || name_length < 1 || name_length > 8 ||
        !is_text(action, action_length)) {
        return refused_draw("ui_draw_button");
    }
    int32_t numbers[] = { x, y, mirrored };
    mix(numbers, sizeof numbers);
    mix(name, (size_t)name_length);
    mix_number(0);
    mix(action, (size_t)action_length);
    return text_width(HOST_FONT_TEXT, name, name_length) + text_width(HOST_FONT_TEXT, action, action_length) +
           advance_of(HOST_FONT_TEXT);
}

int32_t ui_draw_actions(const char *ok, int32_t ok_length, const char *back, int32_t back_length)
{
    if (!is_frame("ui_draw_actions")) {
        return HOST_ERR_NOT_DRAWING;
    }
    if (!is_text(ok, ok_length) || !is_text(back, back_length)) {
        return refused_draw("ui_draw_actions");
    }
    mix(ok, (size_t)ok_length);
    mix_number(0);
    mix(back, (size_t)back_length);
    return HOST_OK;
}

int32_t ui_draw_bar(int32_t x, int32_t y, int32_t width, int32_t value, int32_t maximum)
{
    if (!is_frame("ui_draw_bar")) {
        return HOST_ERR_NOT_DRAWING;
    }
    if (!is_drawable(x) || !is_drawable(y) || !is_drawable(width) || width < 1) {
        return refused_draw("ui_draw_bar");
    }
    int32_t numbers[] = { x, y, width, value, maximum };
    mix(numbers, sizeof numbers);
    return HOST_OK;
}

/* kit_value(): beside digits at least twice the text's capitals a unit is the text, 12 pixels
 * after the value; beside smaller ones it is the caption, 5 after. */
static bool is_large_value(int32_t font)
{
    return FONTS[font & ~HOST_FONT_TABULAR].capitals >= 2 * FONTS[HOST_FONT_TEXT].capitals;
}

static int32_t unit_font(int32_t font)
{
    return is_large_value(font) ? HOST_FONT_TEXT : HOST_FONT_SMALL;
}

static int32_t value_width(int32_t font, const char *value, int32_t value_length, const char *unit,
                           int32_t unit_length)
{
    if (unit_length <= 0) {
        return text_width(font, value, value_length);
    }
    int32_t gap = is_large_value(font) ? 12 : 5;
    return text_width(font, value, value_length) + gap + text_width(unit_font(font), unit, unit_length);
}

int32_t ui_value_width(int32_t font, const char *value, int32_t value_length, const char *unit, int32_t unit_length)
{
    standin();
    if (!is_font(font) || !is_text(value, value_length) || !is_text(unit, unit_length)) {
        return HOST_ERR_ARGUMENT;
    }
    return value_width(font, value, value_length, unit, unit_length);
}

int32_t ui_draw_value(int32_t x, int32_t y, int32_t font, const char *value, int32_t value_length,
                      const char *unit, int32_t unit_length)
{
    if (!is_frame("ui_draw_value")) {
        return HOST_ERR_NOT_DRAWING;
    }
    if (!is_font(font) || !is_drawable(x) || !is_drawable(y) || !is_text(value, value_length) ||
        !is_text(unit, unit_length)) {
        return refused_draw("ui_draw_value");
    }
    int32_t numbers[] = { x, y, font };
    mix(numbers, sizeof numbers);
    mix(value, (size_t)value_length);
    mix_number(0);
    mix(unit, (size_t)unit_length);
    return value_width(font, value, value_length, unit, unit_length);
}

int32_t ui_draw_reason(int32_t status)
{
    if (!is_frame("ui_draw_reason")) {
        return HOST_ERR_NOT_DRAWING;
    }
    mix_number(status);
    /* An answer came: what is wrong, if anything, is the application's to say. Otherwise the
     * host drew the reason. */
    return status >= 200 && status < 300 ? 0 : 1;
}

int32_t ui_draw_frame(int32_t x, int32_t y, int32_t width, int32_t height, int32_t arrows)
{
    if (!is_frame("ui_draw_frame")) {
        return HOST_ERR_NOT_DRAWING;
    }
    if (!is_drawable(x) || !is_drawable(y) || !is_drawable(width) || !is_drawable(height) || width < 1 || height < 1) {
        return refused_draw("ui_draw_frame");
    }
    int32_t numbers[] = { x, y, width, height, arrows };
    mix(numbers, sizeof numbers);
    return HOST_OK;
}

static bool is_cell(const host_cell_t *cell, int32_t icon)
{
    return cell != NULL && is_drawable(cell->x) && is_drawable(cell->y) && is_drawable(cell->width) &&
           is_drawable(cell->height) && cell->width > 0 && cell->height > 0 &&
           (icon == HOST_ICON_NONE || is_icon(icon));
}

/* kit_cell_fit(): the size class, 1 the largest, whose font takes the value in the cell's width
 * beside its picture; a cell that is not the whole surface begins at 3. */
static int32_t cell_size(const host_cell_t *cell, const uint8_t *picture, int32_t icon)
{
    static const int32_t FONTS_BY_SIZE[] = { HOST_FONT_HUGE, HOST_FONT_DISPLAY, HOST_FONT_LARGE, HOST_FONT_MEDIUM,
                                             HOST_FONT_TEXT };
    const int32_t sizes = (int32_t)(sizeof FONTS_BY_SIZE / sizeof FONTS_BY_SIZE[0]);
    bool is_large = cell->width >= surface_width() && cell->height >= surface_height();
    int32_t side = picture == NULL && icon == HOST_ICON_NONE ? 0
                   : is_large                              ? HOST_UI_CELL_PICTURE_LARGE
                                                           : HOST_UI_CELL_PICTURE_SMALL;
    int32_t room = cell->width - 2 * CELL_PADDING - (side > 0 ? side + CELL_PICTURE_GAP : 0);
    int32_t length = (int32_t)strnlen(cell->value, sizeof cell->value);
    int32_t size = is_large ? 1 : 3;
    while (size < sizes && text_width(FONTS_BY_SIZE[size - 1] | HOST_FONT_TABULAR, cell->value, length) > room) {
        size++;
    }
    return size;
}

int32_t ui_cell_fit(const host_cell_t *cell, const uint8_t *picture, int32_t icon)
{
    if (!is_measuring("ui_cell_fit")) {
        return HOST_ERR_NOT_DRAWING;
    }
    return is_cell(cell, icon) ? cell_size(cell, picture, icon) : HOST_ERR_ARGUMENT;
}

int32_t ui_draw_cell(const host_cell_t *cell, const uint8_t *picture, int32_t icon)
{
    if (!is_frame("ui_draw_cell")) {
        return HOST_ERR_NOT_DRAWING;
    }
    if (!is_cell(cell, icon)) {
        return refused_draw("ui_draw_cell");
    }
    int32_t numbers[] = { cell->x, cell->y, cell->width, cell->height, cell->size, icon };
    mix(numbers, sizeof numbers);
    mix_field(cell->name, sizeof cell->name);
    mix_field(cell->value, sizeof cell->value);
    if (picture != NULL) {
        mix(picture, picture_bytes(picture));
    }
    return HOST_OK;
}

int32_t ui_draw_chart(const host_chart_t *chart, const int32_t *values)
{
    if (!is_frame("ui_draw_chart")) {
        return HOST_ERR_NOT_DRAWING;
    }
    if (chart == NULL || values == NULL || chart->count < 1 || chart->count > HOST_CHART_VALUES_MAX ||
        !is_drawable(chart->x) || !is_drawable(chart->y) || !is_drawable(chart->width) ||
        !is_drawable(chart->height) || chart->width <= 0 || chart->height <= 0 || chart->kind < HOST_CHART_LINE ||
        chart->kind > HOST_CHART_STEPS || chart->decimals < 0 || chart->decimals > 9) {
        return refused_draw("ui_draw_chart");
    }
    mix(chart, sizeof *chart);
    mix(values, (size_t)chart->count * sizeof values[0]);
    return HOST_OK;
}

int32_t ui_draw_picker(const host_picker_t *picker)
{
    if (!is_frame("ui_draw_picker")) {
        return HOST_ERR_NOT_DRAWING;
    }
    if (picker == NULL) {
        return refused_draw("ui_draw_picker");
    }
    mix_field(picker->above, sizeof picker->above);
    mix_field(picker->chosen, sizeof picker->chosen);
    mix_field(picker->below, sizeof picker->below);
    mix_field(picker->ok, sizeof picker->ok);
    mix_field(picker->back, sizeof picker->back);
    return HOST_OK;
}

int32_t ui_draw_date(const host_date_t *date)
{
    if (!is_frame("ui_draw_date")) {
        return HOST_ERR_NOT_DRAWING;
    }
    /* As app_api_ui.c reads it: a date of the calendar, a time of the day, and at least one of them shown. */
    if (date == NULL || date->year < 0 || date->year > 9999 || date->month < 1 || date->month > 12 || date->day < 1 ||
        date->day > 31 || date->hour < 0 || date->hour > 23 || date->minute < 0 || date->minute > 59 ||
        (date->has & (HOST_DATE_HAS_TIME | HOST_DATE_HAS_DATE)) == 0 || date->selected < HOST_DATE_HOUR ||
        date->selected > HOST_DATE_NONE) {
        return refused_draw("ui_draw_date");
    }
    int32_t numbers[] = { date->year, date->month, date->day, date->hour, date->minute, date->has, date->selected };
    mix(numbers, sizeof numbers);
    mix_field(date->ok, sizeof date->ok);
    mix_field(date->back, sizeof date->back);
    return HOST_OK;
}

/* ---- The SDK's arena (sdk/lib/app_alloc.h), which on the device is what the
 * module's page has left: here a room of its own, as large as the built module leaves ---- */

#define ARENA_BYTES     (64 * 1024)
#define ARENA_ALIGNMENT 8

static _Alignas(ARENA_ALIGNMENT) uint8_t arena[ARENA_BYTES];
static int32_t arena_taken;

/* What the tools read off the built module (the end of its data), or the whole room. */
static int32_t arena_bytes(void)
{
    /* Negative: the tools could not tell, and the whole page is the arena's. */
    return standin_subject.arena_bytes >= 0 && standin_subject.arena_bytes <= ARENA_BYTES ? standin_subject.arena_bytes
                                                                                          : ARENA_BYTES;
}

int32_t app_alloc_left(void)
{
    return arena_bytes() - arena_taken;
}

void *app_alloc(int32_t bytes)
{
    if (bytes <= 0 || bytes > app_alloc_left()) {
        return NULL;
    }
    void *piece = arena + arena_taken;
    arena_taken += (bytes + ARENA_ALIGNMENT - 1) & ~(ARENA_ALIGNMENT - 1);
    return piece;
}

int32_t app_alloc_mark(void)
{
    return arena_taken;
}

void app_alloc_release(int32_t mark)
{
    if (mark >= 0 && mark <= arena_taken) {
        arena_taken = mark;
    }
}

/* ---- The handlers, called as the device calls them ---- */

static void begin(const char *handler)
{
    standin()->handler = handler;
    world->handler_writes = 0;
    world->handler_requests = world->handler_request_bytes = 0;
    alarm(HANDLER_SECONDS);
}

static void end(void)
{
    alarm(0);
    if (world->handler_writes > world->most_handler_writes) {
        world->most_handler_writes = world->handler_writes;
    }
    world->handler = NULL;
}

#define CALL(handler, ...)                    \
    do {                                      \
        begin("app_" #handler);               \
        if (standin_handlers.handler != NULL) { \
            standin_handlers.handler(__VA_ARGS__); \
        }                                     \
        end();                                \
    } while (0)

void standin_load(void)
{
    standin_t *host = standin();
    host->is_loaded = host->is_loading = true;
    host->is_on_screen = host->is_session_writable = host->is_leaving = host->wants_screen = false;
    memset(host->timer_due_ms, 0, sizeof host->timer_due_ms);
    memset(host->header_text, 0, sizeof host->header_text);
    host->is_inverted = host->session_inverted;
    arena_taken = 0;
    CALL(on_load);
    host->is_loading = false;
    /* host/app.h: once, on or off the screen, after app_on_load(). */
    if (host->is_new) {
        host->is_new = false;
        CALL(on_installed);
    }
}

void standin_focus(void)
{
    standin()->is_on_screen = world->is_session_writable = true;
    CALL(on_focus);
}

void standin_terminate(void)
{
    CALL(on_terminate);
    world->is_loaded = world->is_on_screen = world->is_session_writable = false;
    memset(world->timer_due_ms, 0, sizeof world->timer_due_ms);
}

void standin_leave(void)
{
    CALL(on_blur);
    world->is_on_screen = false;
    standin_terminate();
}

static void button(int32_t pressed, int32_t action)
{
    CALL(on_button, BTN_EVENT(pressed, action));
}

void standin_press(int32_t pressed)
{
    button(pressed, BTN_KEYDOWN);
    button(pressed, BTN_KEYUP);
    if (pressed != BTN_BACK) {
        button(pressed, BTN_PRESSED);
    } else if (standin_handlers.on_back != NULL) {
        CALL(on_back);
    } else {
        /* BACK is the way out and needs no code. */
        world->is_leaving = true;
    }
}

void standin_long_press(int32_t pressed)
{
    button(pressed, BTN_KEYDOWN);
    /* A long BACK is the menu's: no application is told. */
    if (pressed != BTN_BACK) {
        button(pressed, BTN_LONG_PRESSED);
    }
    button(pressed, BTN_KEYUP);
}

void standin_timer(int32_t id)
{
    standin()->timer_due_ms[id] = 0;
    CALL(on_timer, id);
}

void standin_alarm(int32_t id)
{
    standin_alarm_t *alarm = &standin()->alarms[id];
    if (alarm->kind == STANDIN_ALARM_EVERY) {
        /* As app_host.c keeps it: a period after the time it was due, or a period from now when
         * that is past already or the clock was set meanwhile. */
        int64_t every_ms = (int64_t)alarm->every * MS_PER_SECOND;
        int64_t next_ms = alarm->due_uptime_ms + every_ms;
        if (next_ms <= world->uptime_ms || next_ms > world->uptime_ms + every_ms) {
            next_ms = world->uptime_ms + every_ms;
        }
        alarm->due_uptime_ms = next_ms;
    } else {
        alarm->kind = STANDIN_ALARM_NONE;
    }
    CALL(on_alarm, id);
}

void standin_intent(int32_t intent)
{
    if ((standin_subject.intents & (1u << intent)) != 0) {
        CALL(on_intent, intent);
    }
}

void standin_settings(void)
{
    CALL(on_settings);
}

void standin_updated(void)
{
    /* host/app.h: a new version does not inherit the alarms of the old one. */
    memset(standin()->alarms, 0, sizeof world->alarms);
    CALL(on_updated);
}

uint64_t standin_draw(void)
{
    standin_t *host = standin();
    host->picture = 0xcbf29ce484222325u;
    host->draw_calls = 0;
    host->is_drawing = true;
    CALL(on_draw);
    host->is_drawing = false;
    host->draws++;
    return host->picture;
}

uint64_t standin_screen(uint64_t frame)
{
    standin_t *host = standin();
    uint64_t picture = host->picture;
    host->picture = frame;
    /* The header Perinkpheral OS draws above the frame, unless the application is fullscreen,
     * and the inversion of the whole screen. */
    if (!standin_subject.is_fullscreen) {
        for (int side = 0; side < 2; side++) {
            mix_field(host->header_text[side], sizeof host->header_text[side]);
            mix_field(host->status_text[side], sizeof host->status_text[side]);
            mix_number(host->status_icon[side]);
        }
    }
    mix_number(host->is_inverted);
    uint64_t screen = host->picture;
    host->picture = picture;
    return screen;
}

/* ---- Answers ---- */

void standin_data(int32_t request, int32_t item, int32_t status, const void *body, int32_t length,
                  int32_t piece, int32_t ending, int32_t failure)
{
    standin_t *host = standin();
    if (standin_handlers.on_data == NULL) {
        return;
    }
    host->is_delivering = true;
    host->answer_item = item;
    answer_status = status;
    has_picked = false;
    if (status < 0) {
        CALL(on_data, request, NULL, 0, HOST_DATA_FAILED, status);
        host->is_delivering = false;
        return;
    }
    if (piece < 1 || piece > HOST_DATA_CHUNK_MAX) {
        piece = HOST_DATA_CHUNK_MAX;
    }
    /* An answer that does not end whole gives half of itself, in whole pieces, and at least one. */
    int32_t given = ending == STANDIN_WHOLE ? length : (length / 2 + piece - 1) / piece * piece;
    if (given > length) {
        given = length;
    }
    /* The piece is put where the application says, as on the device. */
    extern uint8_t *app_data_buffer(void);
    uint8_t *buffer = app_data_buffer();
    int32_t at = 0;
    do {
        int32_t taken = given - at < piece ? given - at : piece;
        int32_t flags = (at == 0 ? HOST_DATA_FIRST : 0) |
                        (ending == STANDIN_WHOLE && at + taken == given ? HOST_DATA_LAST : 0);
        if (taken > 0) {
            copy(buffer, (const uint8_t *)body + at, (size_t)taken);
        }
        CALL(on_data, request, buffer, taken, flags, status);
        at += taken;
    } while (at < given);
    if (ending == STANDIN_FAILED) {
        answer_status = failure;
        CALL(on_data, request, NULL, 0, HOST_DATA_FAILED, failure);
    }
    host->is_delivering = false;
}

void standin_picked(int32_t poll, int32_t item, int32_t status, const void *body, int32_t length)
{
    standin_t *host = standin();
    const standin_poll_t *asked = &standin_subject.polls[poll];
    if (standin_handlers.on_picked == NULL) {
        return;
    }
    has_picked = false;
    if (status >= 0) {
        json_pick_init(&picked, asked->picks, asked->pick_count);
        json_pick_lists(&picked, picked_lists, sizeof picked_lists);
        /* As poller_net.c reads it: an answer the picker cannot read whole is no answer, whatever
         * its status; an empty one is a whole document of no values, and keeps its status. */
        if (json_pick_feed(&picked, body != NULL ? body : "", (size_t)length) && json_pick_finish(&picked)) {
            has_picked = true;
        } else {
            status = HOST_FAILED_NO_ANSWER;
        }
    }
    host->is_delivering = true;
    host->answer_item = item;
    answer_status = status;
    CALL(on_picked, poll, status);
    host->is_delivering = false;
    has_picked = false;
}

/* ---- Runs ---- */

static char fault[2 * STANDIN_NOTE_BYTES];

const char *standin_fault(void)
{
    return fault;
}

/* The line of what a sanitizer or an assertion said that names the fault. */
static void fault_line(FILE *said, char *line, size_t size)
{
    static const char *const TELLING[] = { "runtime error:", "ERROR: AddressSanitizer", "Assertion", "assertion" };
    line[0] = '\0';
    rewind(said);
    char read[512];
    while (line[0] == '\0' && fgets(read, sizeof read, said) != NULL) {
        for (size_t index = 0; index < sizeof TELLING / sizeof TELLING[0]; index++) {
            if (strstr(read, TELLING[index]) != NULL) {
                /* Cut to the line's room: copied by hand, as GCC takes a cut snprintf() for a mistake. */
                size_t length = strcspn(read, "\n");
                length = length < size - 1 ? length : size - 1;
                copy(line, read, length);
                line[length] = '\0';
                break;
            }
        }
    }
}

bool standin_live(void (*run)(void *context), void *context)
{
    standin();
    fflush(NULL);
    /* What the run says of its end is read here, not printed over the report. */
    FILE *said = tmpfile();
    pid_t child = fork();
    if (child < 0 || said == NULL) {
        perror("standin: cannot start a run");
        exit(2);
    }
    if (child == 0) {
        dup2(fileno(said), STDERR_FILENO);
        run(context);
        fflush(NULL);
        _exit(0);
    }
    int status = 0;
    waitpid(child, &status, 0);
    bool is_whole = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!is_whole) {
        char line[300];
        fault_line(said, line, sizeof line);
        if (WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM) {
            snprintf(fault, sizeof fault, "%s did not return in %d s", running(), HANDLER_SECONDS);
        } else {
            snprintf(fault, sizeof fault, "stopped in %s%s%s", running(), line[0] != '\0' ? ": " : "", line);
        }
        world->handler = NULL;
        world->is_drawing = world->is_delivering = world->is_loading = false;
    }
    fclose(said);
    return is_whole;
}

bool standin_try(void (*run)(void *context), void *context)
{
    standin_t *before = malloc(sizeof *before);
    if (before == NULL) {
        perror("standin: no memory");
        exit(2);
    }
    copy(before, standin(), sizeof *before);
    bool is_whole = standin_live(run, context);
    copy(before->marks, world->marks, sizeof before->marks);
    copy(before->notes, world->notes, sizeof before->notes);
    before->note_count = world->note_count;
    copy(world, before, sizeof *world);
    free(before);
    return is_whole;
}
