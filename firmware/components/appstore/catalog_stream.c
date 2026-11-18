/* SPDX-License-Identifier: MIT */
#include "catalog_stream.h"

#include <string.h>

enum { OBJECT = 1, ARRAY, STRING, NUMBER, TRUE_VALUE, FALSE_VALUE, NULL_VALUE, KEY };
enum { KEY_OR_END, KEY_REQUIRED, COLON, VALUE, VALUE_OR_END, COMMA_OR_END };
enum { SKIP, ROOT, APPS, APP, FILES, FILE_ITEM, TRUSTS };
enum { UNKNOWN, CATALOG, APPS_FIELD, NAME, VERSION, ABI, FIRMWARE, AUTHOR,
       SYSTEM, WRITES_FILES, FILES_FIELD, TRUSTS_FIELD, SIZE, SHA256 };
enum { MINUS, ZERO, INTEGER, DOT, FRACTION, EXPONENT, EXP_SIGN, EXP_DIGITS };

static bool whitespace(unsigned char byte)
{
    return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
}

static bool token_is(const catalog_stream_t *stream, const char *value)
{
    return !stream->token_overflow && strlen(value) == stream->token_length &&
           memcmp(stream->token, value, stream->token_length) == 0;
}

static bool identifier(const char *value, size_t length, bool file)
{
    if (!length || length > (file ? MANIFEST_FILE_NAME_MAX : MANIFEST_NAME_MAX)) return false;
    for (size_t index = 0; index < length; ++index) {
        unsigned char byte = (unsigned char)value[index];
        if ((byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
            byte == '_' || byte == '-' || (file && index && byte == '.')) continue;
        return false;
    }
    return !file || length < 4 || memcmp(value + length - 4, ".new", 4) != 0;
}

static bool text_value(catalog_stream_t *stream, char *output, size_t capacity, bool author)
{
    size_t length = stream->token_length;
    if (!length || length >= capacity || stream->token_overflow) return false;
    for (size_t index = 0; index < length; ++index)
        if ((unsigned char)stream->token[index] < 32 || stream->token[index] == 127) return false;
    if (author && (stream->token[0] == ' ' || stream->token[length - 1] == ' ')) return false;
    memcpy(output, stream->token, length);
    output[length] = '\0';
    return true;
}

static bool unsigned_value(const catalog_stream_t *stream, uint32_t *value)
{
    if (!stream->token_length || stream->token_length > 10 || stream->token_overflow) return false;
    uint32_t result = 0;
    for (size_t index = 0; index < stream->token_length; ++index) {
        unsigned char byte = (unsigned char)stream->token[index];
        if (byte < '0' || byte > '9' || result > (UINT32_MAX - (byte - '0')) / 10) return false;
        result = result * 10 + byte - '0';
    }
    *value = result;
    return true;
}

static int hex_digit(unsigned char byte)
{
    if (byte >= '0' && byte <= '9') return byte - '0';
    if (byte >= 'a' && byte <= 'f') return byte - 'a' + 10;
    if (byte >= 'A' && byte <= 'F') return byte - 'A' + 10;
    return -1;
}

static uint8_t field_of(const catalog_stream_t *stream, uint8_t role)
{
    if (role == ROOT) {
        if (token_is(stream, "catalog")) return CATALOG;
        if (token_is(stream, "apps")) return APPS_FIELD;
    } else if (role == APP) {
        static const char *const names[] = {
            "name", "version", "abi", "firmware", "author", "system", "writes_files", "files", "trusts"
        };
        for (size_t index = 0; index < sizeof names / sizeof names[0]; ++index)
            if (token_is(stream, names[index])) return (uint8_t)(NAME + index);
    } else if (role == FILE_ITEM) {
        if (token_is(stream, "name")) return NAME;
        if (token_is(stream, "size")) return SIZE;
        if (token_is(stream, "sha256")) return SHA256;
    }
    return UNKNOWN;
}

static uint8_t expected_type(uint8_t field)
{
    switch (field) {
    case CATALOG: case ABI: case SIZE: return NUMBER;
    case APPS_FIELD: case FILES_FIELD: case TRUSTS_FIELD: return ARRAY;
    case SYSTEM: case WRITES_FILES: return TRUE_VALUE;
    default: return STRING;
    }
}

static bool prepare_value(catalog_stream_t *stream, uint8_t type, uint8_t *role)
{
    *role = SKIP;
    if (!stream->depth) {
        if (stream->started || type != OBJECT) return false;
        stream->started = true;
        *role = ROOT;
        return true;
    }
    catalog_stream_level_t *parent = &stream->levels[stream->depth - 1];
    if (parent->state != VALUE && parent->state != VALUE_OR_END) return false;
    parent->state = COMMA_OR_END;
    if (parent->role == APPS) {
        if (type != OBJECT) return false;
        memset(&stream->current, 0, sizeof stream->current);
        stream->current.valid = true;
        stream->entry_fields = 0;
        stream->current_matches = 0;
        *role = APP;
    } else if (parent->role == FILES) {
        ++stream->current.file_count;
        stream->file_fields = 0;
        if (type != OBJECT || stream->current.file_count > CATALOG_APP_FILES_MAX)
            stream->current.valid = false;
        if (type == OBJECT) *role = FILE_ITEM;
    } else if (parent->role == TRUSTS) {
        stream->current.has_trusts = true;
        if (type != STRING) stream->current.valid = false;
    } else if (parent->kind == OBJECT && parent->field != UNKNOWN) {
        uint8_t field = parent->field;
        uint8_t expected = expected_type(field);
        bool correct_type = type == expected || (expected == TRUE_VALUE && type == FALSE_VALUE);
        if (parent->role == ROOT) {
            bool *seen = field == CATALOG ? &stream->saw_catalog : &stream->saw_apps;
            if (*seen || !correct_type) return false;
            *seen = true;
        } else {
            uint32_t *fields = parent->role == APP ? &stream->entry_fields : &stream->file_fields;
            if ((*fields & (1u << field)) || !correct_type) stream->current.valid = false;
            *fields |= 1u << field;
        }
        if (correct_type && type == ARRAY) {
            if (field == APPS_FIELD) *role = APPS;
            if (field == FILES_FIELD) *role = FILES;
            if (field == TRUSTS_FIELD) *role = TRUSTS;
        }
    }
    return true;
}

static void scalar_finished(catalog_stream_t *stream)
{
    catalog_stream_level_t *parent = &stream->levels[stream->depth - 1];
    uint8_t field = parent->field;
    uint8_t type = stream->token_kind;
    if (parent->role == ROOT && field == CATALOG)
        stream->catalog_version_valid = type == NUMBER && token_is(stream, "1");
    if (parent->role != APP && parent->role != FILE_ITEM) return;
    uint8_t expected = expected_type(field);
    if (field == UNKNOWN || (type != expected && !(expected == TRUE_VALUE && type == FALSE_VALUE))) return;
    bool valid = true;
    catalog_stream_entry_t *entry = &stream->current;
    if (parent->role == APP) {
        switch (field) {
        case NAME:
            valid = identifier(stream->token, stream->token_length, false) &&
                    text_value(stream, entry->name, sizeof entry->name, false);
            if (valid) {
                for (size_t index = 0; index < stream->wanted_count; ++index)
                    if (token_is(stream, stream->wanted[index])) stream->current_matches |= 1u << index;
            }
            break;
        case VERSION: valid = text_value(stream, entry->version, sizeof entry->version, false); break;
        case FIRMWARE: valid = text_value(stream, entry->firmware, sizeof entry->firmware, false); break;
        case AUTHOR: valid = text_value(stream, entry->author, sizeof entry->author, true); break;
        case ABI: valid = unsigned_value(stream, &entry->abi) && entry->abi != 0; break;
        case SYSTEM: entry->system = type == TRUE_VALUE; break;
        case WRITES_FILES: entry->writes_files = type == TRUE_VALUE; break;
        default: break;
        }
    } else if (entry->file_count && entry->file_count <= CATALOG_APP_FILES_MAX) {
        catalog_stream_file_t *file = &entry->files[entry->file_count - 1];
        switch (field) {
        case NAME:
            valid = identifier(stream->token, stream->token_length, true) &&
                    text_value(stream, file->name, sizeof file->name, false);
            break;
        case SIZE: valid = unsigned_value(stream, &file->size) && file->size != 0; break;
        case SHA256:
            valid = stream->token_length == 64 && !stream->token_overflow;
            for (size_t index = 0; valid && index < 32; ++index) {
                int high = hex_digit((unsigned char)stream->token[index * 2]);
                int low = hex_digit((unsigned char)stream->token[index * 2 + 1]);
                valid = high >= 0 && low >= 0;
                if (valid) file->sha256[index] = (uint8_t)(high * 16 + low);
            }
            break;
        default: break;
        }
    }
    entry->valid = entry->valid && valid;
}

static void container_finished(catalog_stream_t *stream, uint8_t role)
{
    catalog_stream_entry_t *entry = &stream->current;
    if (role == FILE_ITEM) {
        uint32_t required = (1u << NAME) | (1u << SIZE) | (1u << SHA256);
        if ((stream->file_fields & required) != required) entry->valid = false;
    }
    if (role != APP) return;
    uint32_t required = (1u << NAME) | (1u << VERSION) | (1u << ABI) | (1u << FILES_FIELD);
    if ((stream->entry_fields & required) != required || entry->file_count < 2 ||
        entry->file_count > CATALOG_APP_FILES_MAX) entry->valid = false;
    if (entry->valid) {
        if (strcmp(entry->files[0].name, "app.wasm") ||
            strcmp(entry->files[entry->file_count - 1].name, "manifest.json")) entry->valid = false;
        for (size_t index = 0; index < entry->file_count; ++index)
            for (size_t previous = 0; previous < index; ++previous)
                if (!strcmp(entry->files[index].name, entry->files[previous].name)) entry->valid = false;
    }
    for (size_t index = 0; index < stream->wanted_count; ++index) {
        if (!(stream->current_matches & (1u << index))) continue;
        if (stream->results[index].found) {
            stream->results[index].valid = false;
        } else {
            stream->results[index] = *entry;
            stream->results[index].found = true;
            strcpy(stream->results[index].name, stream->wanted[index]);
        }
    }
}

static void append_byte(catalog_stream_t *stream, unsigned char byte)
{
    if (stream->token_length < CATALOG_STREAM_TOKEN_MAX) stream->token[stream->token_length] = (char)byte;
    else stream->token_overflow = true;
    ++stream->token_length;
}

static void append_codepoint(catalog_stream_t *stream, uint32_t point)
{
    if (point < 0x80) append_byte(stream, (unsigned char)point);
    else if (point < 0x800) {
        append_byte(stream, (unsigned char)(0xc0 | (point >> 6)));
        append_byte(stream, (unsigned char)(0x80 | (point & 63)));
    } else if (point < 0x10000) {
        append_byte(stream, (unsigned char)(0xe0 | (point >> 12)));
        append_byte(stream, (unsigned char)(0x80 | ((point >> 6) & 63)));
        append_byte(stream, (unsigned char)(0x80 | (point & 63)));
    } else {
        append_byte(stream, (unsigned char)(0xf0 | (point >> 18)));
        append_byte(stream, (unsigned char)(0x80 | ((point >> 12) & 63)));
        append_byte(stream, (unsigned char)(0x80 | ((point >> 6) & 63)));
        append_byte(stream, (unsigned char)(0x80 | (point & 63)));
    }
}

static bool string_byte(catalog_stream_t *stream, unsigned char byte)
{
    if (stream->unicode_digits) {
        int digit = hex_digit(byte);
        if (digit < 0) return false;
        stream->unicode_value = stream->unicode_value * 16 + (uint32_t)digit;
        if (--stream->unicode_digits) return true;
        uint32_t point = stream->unicode_value;
        if (stream->high_surrogate) {
            if (point < 0xdc00 || point > 0xdfff) return false;
            point = 0x10000 + ((stream->high_surrogate - 0xd800) << 10) + point - 0xdc00;
            stream->high_surrogate = 0;
        } else if (point >= 0xd800 && point <= 0xdbff) {
            stream->high_surrogate = point;
            return true;
        } else if (point >= 0xdc00 && point <= 0xdfff) return false;
        append_codepoint(stream, point);
        return true;
    }
    if (stream->escaped) {
        stream->escaped = false;
        if (byte == 'u') {
            stream->unicode_digits = 4;
            stream->unicode_value = 0;
            return true;
        }
        if (stream->high_surrogate) return false;
        switch (byte) {
        case '"': case '\\': case '/': append_byte(stream, byte); break;
        case 'b': append_byte(stream, '\b'); break;
        case 'f': append_byte(stream, '\f'); break;
        case 'n': append_byte(stream, '\n'); break;
        case 'r': append_byte(stream, '\r'); break;
        case 't': append_byte(stream, '\t'); break;
        default: return false;
        }
        return true;
    }
    if (stream->utf8_left) {
        if (byte < 0x80 || byte > 0xbf) return false;
        stream->utf8_value = (stream->utf8_value << 6) | (byte & 63);
        append_byte(stream, byte);
        if (--stream->utf8_left == 0 && (stream->utf8_value < stream->utf8_minimum ||
            stream->utf8_value > 0x10ffff || (stream->utf8_value >= 0xd800 && stream->utf8_value <= 0xdfff))) return false;
        return true;
    }
    if (stream->high_surrogate && byte != '\\') return false;
    if (byte == '\\') { stream->escaped = true; return true; }
    if (byte == '"') {
        stream->token[stream->token_length < CATALOG_STREAM_TOKEN_MAX ? stream->token_length : CATALOG_STREAM_TOKEN_MAX] = '\0';
        if (stream->token_kind == KEY) {
            catalog_stream_level_t *parent = &stream->levels[stream->depth - 1];
            parent->field = field_of(stream, parent->role);
            parent->state = COLON;
        } else scalar_finished(stream);
        stream->token_kind = 0;
        return true;
    }
    if (byte < 32) return false;
    if (byte >= 0x80) {
        if (byte >= 0xc2 && byte <= 0xdf) { stream->utf8_left = 1; stream->utf8_minimum = 0x80; stream->utf8_value = byte & 31; }
        else if (byte >= 0xe0 && byte <= 0xef) { stream->utf8_left = 2; stream->utf8_minimum = 0x800; stream->utf8_value = byte & 15; }
        else if (byte >= 0xf0 && byte <= 0xf4) { stream->utf8_left = 3; stream->utf8_minimum = 0x10000; stream->utf8_value = byte & 7; }
        else return false;
    }
    append_byte(stream, byte);
    return true;
}

static bool number_byte(catalog_stream_t *stream, unsigned char byte)
{
    bool digit = byte >= '0' && byte <= '9';
    switch (stream->number_state) {
    case MINUS: if (digit) { stream->number_state = byte == '0' ? ZERO : INTEGER; return true; } break;
    case ZERO: case INTEGER:
        if (digit && stream->number_state == INTEGER) return true;
        if (byte == '.') { stream->number_state = DOT; return true; }
        if (byte == 'e' || byte == 'E') { stream->number_state = EXPONENT; return true; }
        break;
    case DOT: if (digit) { stream->number_state = FRACTION; return true; } break;
    case FRACTION:
        if (digit) return true;
        if (byte == 'e' || byte == 'E') { stream->number_state = EXPONENT; return true; }
        break;
    case EXPONENT:
        if (byte == '+' || byte == '-') { stream->number_state = EXP_SIGN; return true; }
        /* fall through */
    case EXP_SIGN: if (digit) { stream->number_state = EXP_DIGITS; return true; } break;
    case EXP_DIGITS: if (digit) return true; break;
    }
    return false;
}

static bool consume_byte(catalog_stream_t *stream, unsigned char byte)
{
    if (stream->token_kind == STRING || stream->token_kind == KEY) return string_byte(stream, byte);
    if (stream->token_kind == NUMBER) {
        if (number_byte(stream, byte)) { append_byte(stream, byte); return true; }
        if (stream->number_state != ZERO && stream->number_state != INTEGER &&
            stream->number_state != FRACTION && stream->number_state != EXP_DIGITS) return false;
        scalar_finished(stream);
        stream->token_kind = 0;
    } else if (stream->token_kind) {
        const char *literal = stream->token_kind == TRUE_VALUE ? "true" : stream->token_kind == FALSE_VALUE ? "false" : "null";
        if (literal[stream->literal_at]) {
            if (literal[stream->literal_at++] != (char)byte) return false;
            return true;
        }
        scalar_finished(stream);
        stream->token_kind = 0;
    }
    if (whitespace(byte)) return true;
    if (stream->complete) return false;
    catalog_stream_level_t *parent = stream->depth ? &stream->levels[stream->depth - 1] : NULL;
    if (byte == '}' || byte == ']') {
        if (!parent || parent->kind != (byte == '}' ? OBJECT : ARRAY) ||
            (parent->state != COMMA_OR_END && parent->state != KEY_OR_END && parent->state != VALUE_OR_END)) return false;
        container_finished(stream, parent->role);
        if (--stream->depth == 0) stream->complete = true;
        return true;
    }
    if (byte == ',') {
        if (!parent || parent->state != COMMA_OR_END) return false;
        parent->state = parent->kind == OBJECT ? KEY_REQUIRED : VALUE;
        return true;
    }
    if (byte == ':') {
        if (!parent || parent->state != COLON) return false;
        parent->state = VALUE;
        return true;
    }
    stream->token_length = 0;
    stream->token_overflow = false;
    if (byte == '"' && parent && (parent->state == KEY_OR_END || parent->state == KEY_REQUIRED)) {
        stream->token_kind = KEY;
        return true;
    }
    uint8_t type = byte == '{' ? OBJECT : byte == '[' ? ARRAY : byte == '"' ? STRING :
                   byte == 't' ? TRUE_VALUE : byte == 'f' ? FALSE_VALUE : byte == 'n' ? NULL_VALUE :
                   (byte == '-' || (byte >= '0' && byte <= '9')) ? NUMBER : 0;
    uint8_t role;
    if (!type || !prepare_value(stream, type, &role)) return false;
    if (type == OBJECT || type == ARRAY) {
        if (stream->depth == CATALOG_STREAM_DEPTH_MAX) return false;
        stream->levels[stream->depth++] = (catalog_stream_level_t){
            .kind = type, .state = type == OBJECT ? KEY_OR_END : VALUE_OR_END, .role = role
        };
    } else {
        stream->token_kind = type;
        if (type == NUMBER) {
            stream->number_state = byte == '-' ? MINUS : byte == '0' ? ZERO : INTEGER;
            append_byte(stream, byte);
        } else stream->literal_at = 1;
    }
    return true;
}

bool catalog_stream_init(catalog_stream_t *stream, const char *const *wanted,
                         size_t wanted_count, catalog_stream_entry_t *results)
{
    if (!stream) return false;
    memset(stream, 0, sizeof *stream);
    stream->broken = true;
    if (wanted_count > CATALOG_INSTALLED_MAX || (wanted_count && (!wanted || !results))) return false;
    for (size_t index = 0; index < wanted_count; ++index) {
        if (!wanted[index] || !identifier(wanted[index], strlen(wanted[index]), false)) return false;
        for (size_t previous = 0; previous < index; ++previous)
            if (!strcmp(wanted[index], wanted[previous])) return false;
    }
    stream->wanted = wanted;
    stream->wanted_count = wanted_count;
    stream->results = results;
    if (wanted_count) memset(results, 0, wanted_count * sizeof *results);
    stream->broken = false;
    return true;
}

bool catalog_stream_feed(catalog_stream_t *stream, const char *bytes, size_t length)
{
    if (!stream || stream->broken) return false;
    if ((!bytes && length) || length > CATALOG_STREAM_MAX_BYTES - stream->total_bytes) {
        stream->broken = true;
        return false;
    }
    stream->total_bytes += length;
    for (size_t index = 0; index < length; ++index) {
        if (!consume_byte(stream, (unsigned char)bytes[index])) {
            stream->broken = true;
            return false;
        }
    }
    return true;
}

bool catalog_stream_finish(catalog_stream_t *stream)
{
    if (!stream) return false;
    bool valid = !stream->broken && stream->complete && !stream->token_kind &&
                 stream->saw_catalog && stream->saw_apps && stream->catalog_version_valid;
    if (!valid) stream->broken = true;
    return valid;
}
