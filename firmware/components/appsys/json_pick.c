/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "json_pick.h"

#include <stdio.h>
#include <string.h>

#define NO_TARGET     (-1)
#define NOT_ASCII     '?'
#define ASCII_MAX     0x7F
#define UNICODE_DIGITS 4

void json_pick_init(json_pick_t *pick, const char *const *paths, int path_count)
{
    memset(pick, 0, sizeof *pick);
    pick->paths = paths;
    pick->path_count = path_count < JSON_PICK_PATHS_MAX ? path_count : JSON_PICK_PATHS_MAX;
    pick->target = NO_TARGET;
}

static bool is_recorded(const json_pick_t *pick)
{
    return pick->depth >= 1 && pick->depth <= JSON_PICK_DEPTH_MAX;
}

static json_pick_level_t *innermost(json_pick_t *pick)
{
    return &pick->levels[pick->depth - 1];
}

/* Where the value about to be read sits, written the way a manifest
 * writes it. False when it does not fit: such a path matches nothing. */
static bool current_path(const json_pick_t *pick, char path[JSON_PICK_PATH_MAX + 1])
{
    size_t length = 0;
    path[0] = '\0';
    for (int level = 0; level < pick->depth; level++) {
        const json_pick_level_t *at = &pick->levels[level];
        int written;
        if (at->is_array) {
            written = snprintf(path + length, JSON_PICK_PATH_MAX + 1 - length, "[%d]", at->index);
        } else if (at->key[0] == '\0') {
            return false;
        } else {
            written = snprintf(path + length, JSON_PICK_PATH_MAX + 1 - length, "%s%s",
                               length > 0 ? "." : "", at->key);
        }
        if (written < 0 || (size_t)written > JSON_PICK_PATH_MAX - length) {
            return false;
        }
        length += (size_t)written;
    }
    return true;
}

/* A scalar starts here: is it one of the wanted ones? */
static void aim(json_pick_t *pick)
{
    pick->target = NO_TARGET;
    pick->length = 0;
    char path[JSON_PICK_PATH_MAX + 1];
    if (!is_recorded(pick) || !current_path(pick, path)) {
        return;
    }
    for (int i = 0; i < pick->path_count; i++) {
        /* The first occurrence wins: a document that repeats a key does
         * not get to overwrite what was read. */
        if (!pick->found[i] && strcmp(pick->paths[i], path) == 0) {
            pick->target = i;
            pick->values[i][0] = '\0';
            return;
        }
    }
}

static void collect(json_pick_t *pick, char character)
{
    if (pick->state == JSON_PICK_IN_KEY) {
        json_pick_level_t *level = innermost(pick);
        if (pick->length < JSON_PICK_KEY_MAX) {
            level->key[pick->length] = character;
            level->key[pick->length + 1] = '\0';
        } else {
            level->key[0] = '\0'; /* too long to be anybody's path */
        }
        pick->length++;
        return;
    }
    if (pick->target != NO_TARGET && pick->length < JSON_PICK_VALUE_MAX) {
        pick->values[pick->target][pick->length++] = character;
        pick->values[pick->target][pick->length] = '\0';
    }
}

/* A whole character into the value, as UTF-8 - or none of it: a value is
 * cut between characters, never inside one. A key takes ASCII only, and
 * anything else in it as a '?', which no path is spelled with. */
static void collect_character(json_pick_t *pick, uint32_t code_point)
{
    if (code_point <= ASCII_MAX) {
        collect(pick, (char)code_point);
        return;
    }
    if (pick->state == JSON_PICK_IN_KEY) {
        collect(pick, NOT_ASCII);
        return;
    }
    char encoded[4];
    int  count;
    if (code_point < 0x800) {
        encoded[0] = (char)(0xC0 | code_point >> 6);
        encoded[1] = (char)(0x80 | (code_point & 0x3F));
        count = 2;
    } else if (code_point < 0x10000) {
        encoded[0] = (char)(0xE0 | code_point >> 12);
        encoded[1] = (char)(0x80 | (code_point >> 6 & 0x3F));
        encoded[2] = (char)(0x80 | (code_point & 0x3F));
        count = 3;
    } else {
        encoded[0] = (char)(0xF0 | code_point >> 18);
        encoded[1] = (char)(0x80 | (code_point >> 12 & 0x3F));
        encoded[2] = (char)(0x80 | (code_point >> 6 & 0x3F));
        encoded[3] = (char)(0x80 | (code_point & 0x3F));
        count = 4;
    }
    if (pick->target == NO_TARGET) {
        return;
    }
    if (pick->length + count > JSON_PICK_VALUE_MAX) {
        pick->length = JSON_PICK_VALUE_MAX; /* full: nothing shorter slips in after it */
        return;
    }
    for (int i = 0; i < count; i++) {
        pick->values[pick->target][pick->length++] = encoded[i];
    }
    pick->values[pick->target][pick->length] = '\0';
}

static void value_ended(json_pick_t *pick)
{
    if (pick->target != NO_TARGET) {
        pick->found[pick->target] = true;
        pick->target = NO_TARGET;
    }
    pick->state = JSON_PICK_BETWEEN;
}

static int hex_digit(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    char lower = (char)(character | 0x20);
    return lower >= 'a' && lower <= 'f' ? lower - 'a' + 10 : -1;
}

static void in_string(json_pick_t *pick, char character)
{
    bool collects = pick->state == JSON_PICK_IN_KEY ? is_recorded(pick) : true;
    if (pick->unicode_digits > 0) {
        int digit = hex_digit(character);
        if (digit < 0) {
            pick->broken = true;
            return;
        }
        pick->unicode_value = pick->unicode_value << 4 | (uint32_t)digit;
        if (--pick->unicode_digits == 0 && collects) {
            uint32_t unit = pick->unicode_value;
            /* A character beyond the first 65536 comes as two halves. */
            if (unit >= 0xD800 && unit <= 0xDBFF) {
                pick->high_half = unit;
            } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
                if (pick->high_half != 0) {
                    collect_character(pick, 0x10000 + ((pick->high_half - 0xD800) << 10) +
                                                (unit - 0xDC00));
                }
                pick->high_half = 0;
            } else {
                pick->high_half = 0;
                collect_character(pick, unit);
            }
        }
        return;
    }
    if (pick->escaped) {
        pick->escaped = false;
        if (character == 'u') {
            pick->unicode_digits = UNICODE_DIGITS;
            pick->unicode_value = 0;
        } else if (collects) {
            bool is_itself = character == '"' || character == '\\' || character == '/';
            collect(pick, is_itself ? character : ' '); /* \n \t and the like: a space */
        }
        return;
    }
    if (character == '\\') {
        pick->escaped = true;
        return;
    }
    if (character == '"') {
        if (pick->state == JSON_PICK_IN_KEY) {
            pick->state = JSON_PICK_BETWEEN;
        } else {
            value_ended(pick);
        }
        return;
    }
    if (!collects) {
        return;
    }
    /* The text is UTF-8: a character of several bytes is put together and
     * taken whole. Bytes that are not UTF-8 are passed over. */
    unsigned char byte = (unsigned char)character;
    if (byte <= ASCII_MAX) {
        pick->utf8_left = 0;
        collect(pick, character);
    } else if ((byte & 0xC0) == 0x80) {
        if (pick->utf8_left > 0) {
            pick->utf8_value = pick->utf8_value << 6 | (byte & 0x3F);
            if (--pick->utf8_left == 0) {
                collect_character(pick, pick->utf8_value);
            }
        }
    } else if ((byte & 0xE0) == 0xC0) {
        pick->utf8_left = 1;
        pick->utf8_value = byte & 0x1F;
    } else if ((byte & 0xF0) == 0xE0) {
        pick->utf8_left = 2;
        pick->utf8_value = byte & 0x0F;
    } else if ((byte & 0xF8) == 0xF0) {
        pick->utf8_left = 3;
        pick->utf8_value = byte & 0x07;
    } else {
        pick->utf8_left = 0;
    }
}

static void open_level(json_pick_t *pick, bool is_array)
{
    pick->depth++;
    if (is_recorded(pick)) {
        json_pick_level_t *level = innermost(pick);
        level->is_array = is_array;
        level->index = 0;
        level->expects_key = !is_array;
        level->key[0] = '\0';
    }
}

static void between(json_pick_t *pick, char character)
{
    switch (character) {
    case ' ':
    case '\t':
    case '\n':
    case '\r':
        break;
    case '{':
    case '[':
        open_level(pick, character == '[');
        break;
    case '}':
    case ']':
        if (pick->depth == 0) {
            pick->broken = true;
            break;
        }
        pick->depth--;
        break;
    case ',':
        if (is_recorded(pick)) {
            json_pick_level_t *level = innermost(pick);
            if (level->is_array) {
                level->index++;
            } else {
                level->expects_key = true;
            }
        }
        break;
    case ':':
        if (is_recorded(pick)) {
            innermost(pick)->expects_key = false;
        }
        break;
    case '"':
        if (pick->depth >= 1 && is_recorded(pick) && innermost(pick)->expects_key) {
            pick->state = JSON_PICK_IN_KEY;
            pick->length = 0;
            innermost(pick)->key[0] = '\0';
        } else if (pick->depth > JSON_PICK_DEPTH_MAX) {
            /* Too deep to tell keys from values, and nothing is picked
             * there anyway: read it as a string and drop it. */
            pick->state = JSON_PICK_IN_STRING;
            pick->target = NO_TARGET;
        } else {
            aim(pick);
            pick->state = JSON_PICK_IN_STRING;
        }
        break;
    default:
        aim(pick);
        pick->state = JSON_PICK_IN_LITERAL;
        collect(pick, character);
        break;
    }
}

static bool ends_a_literal(char character)
{
    return character == ',' || character == '}' || character == ']' || character == ' ' ||
           character == '\t' || character == '\n' || character == '\r';
}

bool json_pick_feed(json_pick_t *pick, const char *bytes, size_t length)
{
    for (size_t i = 0; i < length && !pick->broken; i++) {
        char character = bytes[i];
        switch (pick->state) {
        case JSON_PICK_IN_KEY:
        case JSON_PICK_IN_STRING:
            in_string(pick, character);
            break;
        case JSON_PICK_IN_LITERAL:
            if (ends_a_literal(character)) {
                value_ended(pick);
                between(pick, character);
            } else {
                collect(pick, character);
            }
            break;
        case JSON_PICK_BETWEEN:
            between(pick, character);
            break;
        }
    }
    return !pick->broken;
}

void json_pick_finish(json_pick_t *pick)
{
    if (pick->state == JSON_PICK_IN_LITERAL && !pick->broken) {
        value_ended(pick);
    }
    /* A string cut off by the end of the document was never a value. */
    pick->target = NO_TARGET;
}

const char *json_pick_value(const json_pick_t *pick, int index)
{
    bool has_value = index >= 0 && index < pick->path_count && pick->found[index];
    return has_value ? pick->values[index] : NULL;
}
