/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "json_tree.h"

#include <stdlib.h>
#include <string.h>

/* ---- parsing ---- */

typedef struct {
    const char *at;
    int         depth;
} reader_t;

static void skip_space(reader_t *reader)
{
    while (*reader->at == ' ' || *reader->at == '\t' || *reader->at == '\n' || *reader->at == '\r') {
        reader->at++;
    }
}

static json_t *new_node(json_type_t type)
{
    json_t *node = calloc(1, sizeof *node);
    if (node != NULL) {
        node->type = type;
    }
    return node;
}

static int hex_digit(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

/* Four hex digits at `text`; -1 when they are not. */
static int32_t hex4(const char *text)
{
    int32_t value = 0;
    for (int i = 0; i < 4; i++) {
        int digit = hex_digit(text[i]);
        if (digit < 0) {
            return -1;
        }
        value = value * 16 + digit;
    }
    return value;
}

static char *put_utf8(char *out, uint32_t code)
{
    if (code < 0x80) {
        *out++ = (char)code;
    } else if (code < 0x800) {
        *out++ = (char)(0xC0 | (code >> 6));
        *out++ = (char)(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        *out++ = (char)(0xE0 | (code >> 12));
        *out++ = (char)(0x80 | ((code >> 6) & 0x3F));
        *out++ = (char)(0x80 | (code & 0x3F));
    } else {
        *out++ = (char)(0xF0 | (code >> 18));
        *out++ = (char)(0x80 | ((code >> 12) & 0x3F));
        *out++ = (char)(0x80 | ((code >> 6) & 0x3F));
        *out++ = (char)(0x80 | (code & 0x3F));
    }
    return out;
}

/* A string at the opening quote: a terminated copy, unescaped, or NULL.
 * The copy is never longer than the text it is made of. */
static char *read_string(reader_t *reader)
{
    const char *end = reader->at + 1;
    while (*end != '"') {
        if (*end == '\0' || (unsigned char)*end < 0x20) {
            return NULL;
        }
        end += *end == '\\' && end[1] != '\0' ? 2 : 1;
    }
    char *copy = malloc((size_t)(end - reader->at));
    if (copy == NULL) {
        return NULL;
    }
    char *out = copy;
    for (const char *in = reader->at + 1; in < end;) {
        if (*in != '\\') {
            *out++ = *in++;
            continue;
        }
        char escape = in[1];
        in += 2;
        switch (escape) {
        case '"': case '\\': case '/': *out++ = escape; break;
        case 'b': *out++ = '\b'; break;
        case 'f': *out++ = '\f'; break;
        case 'n': *out++ = '\n'; break;
        case 'r': *out++ = '\r'; break;
        case 't': *out++ = '\t'; break;
        case 'u': {
            int32_t code = end - in >= 4 ? hex4(in) : -1;
            in += 4;
            if (code >= 0xD800 && code <= 0xDBFF) {
                /* The first half of a pair: the second must follow. */
                int32_t low = end - in >= 6 && in[0] == '\\' && in[1] == 'u' ? hex4(in + 2) : -1;
                if (low < 0xDC00 || low > 0xDFFF) {
                    code = -1;
                } else {
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    in += 6;
                }
            } else if (code >= 0xDC00 && code <= 0xDFFF) {
                code = -1;
            }
            /* A zero would end the text early: no string holds one. */
            if (code <= 0) {
                free(copy);
                return NULL;
            }
            out = put_utf8(out, (uint32_t)code);
            break;
        }
        default:
            free(copy);
            return NULL;
        }
    }
    *out = '\0';
    reader->at = end + 1;
    return copy;
}

static bool is_digit(char character)
{
    return character >= '0' && character <= '9';
}

static json_t *read_number(reader_t *reader)
{
    const char *at = reader->at;
    bool        is_negative = *at == '-';
    if (is_negative) {
        at++;
    }
    if (!is_digit(*at) || (*at == '0' && is_digit(at[1]))) {
        return NULL;
    }
    uint64_t magnitude = 0;
    while (is_digit(*at)) {
        uint64_t digit = (uint64_t)(*at++ - '0');
        if (magnitude > (INT64_MAX - digit) / 10) {
            return NULL; /* more than a number of 64 bits holds */
        }
        magnitude = magnitude * 10 + digit;
    }
    bool is_whole = true;
    if (*at == '.') {
        at++;
        if (!is_digit(*at)) {
            return NULL;
        }
        while (is_digit(*at)) {
            is_whole = is_whole && *at == '0';
            at++;
        }
    }
    if (*at == 'e' || *at == 'E') {
        return NULL;
    }
    json_t *node = new_node(JSON_NUMBER);
    if (node != NULL) {
        node->number = is_negative ? -(int64_t)magnitude : (int64_t)magnitude;
        node->is_whole = is_whole;
        reader->at = at;
    }
    return node;
}

static json_t *read_value(reader_t *reader);

/* The members of an array or an object, after its opening bracket. */
static bool read_members(reader_t *reader, json_t *container)
{
    char     closing = container->type == JSON_ARRAY ? ']' : '}';
    json_t **link = &container->child;
    skip_space(reader);
    if (*reader->at == closing) {
        reader->at++;
        return true;
    }
    for (;;) {
        char *name = NULL;
        if (container->type == JSON_OBJECT) {
            skip_space(reader);
            if (*reader->at != '"' || (name = read_string(reader)) == NULL) {
                return false;
            }
            skip_space(reader);
            if (*reader->at != ':') {
                free(name);
                return false;
            }
            reader->at++;
        }
        json_t *member = read_value(reader);
        if (member == NULL) {
            free(name);
            return false;
        }
        member->name = name;
        *link = member;
        link = &member->next;
        skip_space(reader);
        if (*reader->at == closing) {
            reader->at++;
            return true;
        }
        if (*reader->at != ',') {
            return false;
        }
        reader->at++;
    }
}

static json_t *read_value(reader_t *reader)
{
    skip_space(reader);
    json_t *node = NULL;
    char    first = *reader->at;
    if (first == '{' || first == '[') {
        if (reader->depth >= JSON_DEPTH_MAX || (node = new_node(first == '{' ? JSON_OBJECT : JSON_ARRAY)) == NULL) {
            return NULL;
        }
        reader->at++;
        reader->depth++;
        bool is_read = read_members(reader, node);
        reader->depth--;
        if (!is_read) {
            json_free(node);
            return NULL;
        }
    } else if (first == '"') {
        char *text = read_string(reader);
        if (text == NULL || (node = new_node(JSON_STRING)) == NULL) {
            free(text);
            return NULL;
        }
        node->text = text;
    } else if (first == '-' || is_digit(first)) {
        node = read_number(reader);
    } else if (strncmp(reader->at, "true", 4) == 0) {
        node = new_node(JSON_TRUE);
        reader->at += 4;
    } else if (strncmp(reader->at, "false", 5) == 0) {
        node = new_node(JSON_FALSE);
        reader->at += 5;
    } else if (strncmp(reader->at, "null", 4) == 0) {
        node = new_node(JSON_NULL);
        reader->at += 4;
    }
    return node;
}

json_t *json_parse(const char *text)
{
    if (text == NULL) {
        return NULL;
    }
    reader_t reader = { .at = text };
    json_t  *root = read_value(&reader);
    if (root != NULL) {
        skip_space(&reader);
        if (*reader.at != '\0') {
            json_free(root);
            root = NULL;
        }
    }
    return root;
}

void json_free(json_t *node)
{
    while (node != NULL) {
        json_t *next = node->next;
        json_free(node->child);
        free(node->name);
        free(node->text);
        free(node);
        node = next;
    }
}

/* ---- reading ---- */

static bool is_container(const json_t *node)
{
    return node != NULL && (node->type == JSON_ARRAY || node->type == JSON_OBJECT);
}

json_t *json_get(const json_t *object, const char *name)
{
    if (!json_is_object(object) || name == NULL) {
        return NULL;
    }
    for (json_t *member = object->child; member != NULL; member = member->next) {
        if (member->name != NULL && strcmp(member->name, name) == 0) {
            return member;
        }
    }
    return NULL;
}

int json_count(const json_t *container)
{
    int count = 0;
    for (const json_t *member = is_container(container) ? container->child : NULL; member != NULL;
         member = member->next) {
        count++;
    }
    return count;
}

json_t *json_at(const json_t *container, int index)
{
    json_t *member = index >= 0 && is_container(container) ? container->child : NULL;
    for (int i = 0; member != NULL && i < index; i++) {
        member = member->next;
    }
    return member;
}

/* ---- building ---- */

static char *copy_of(const char *text)
{
    size_t size = strlen(text) + 1;
    char  *copy = malloc(size);
    if (copy != NULL) {
        memcpy(copy, text, size);
    }
    return copy;
}

json_t *json_new_object(void)
{
    return new_node(JSON_OBJECT);
}

json_t *json_new_array(void)
{
    return new_node(JSON_ARRAY);
}

json_t *json_new_string(const char *text)
{
    char   *copy = text != NULL ? copy_of(text) : NULL;
    json_t *node = copy != NULL ? new_node(JSON_STRING) : NULL;
    if (node == NULL) {
        free(copy);
        return NULL;
    }
    node->text = copy;
    return node;
}

static void append(json_t *container, json_t *item)
{
    json_t **link = &container->child;
    while (*link != NULL) {
        link = &(*link)->next;
    }
    item->next = NULL;
    *link = item;
}

void json_array_add(json_t *array, json_t *item)
{
    if (!json_is_array(array) || item == NULL) {
        json_free(item);
        return;
    }
    append(array, item);
}

json_t *json_add_item(json_t *object, const char *name, json_t *item)
{
    char *copy = json_is_object(object) && name != NULL && item != NULL ? copy_of(name) : NULL;
    if (copy == NULL) {
        json_free(item);
        return NULL;
    }
    free(item->name);
    item->name = copy;
    append(object, item);
    return item;
}

json_t *json_add_string(json_t *object, const char *name, const char *text)
{
    return json_add_item(object, name, json_new_string(text));
}

json_t *json_add_number(json_t *object, const char *name, int64_t number)
{
    json_t *node = new_node(JSON_NUMBER);
    if (node != NULL) {
        node->number = number;
        node->is_whole = true;
    }
    return json_add_item(object, name, node);
}

json_t *json_add_bool(json_t *object, const char *name, bool value)
{
    return json_add_item(object, name, new_node(value ? JSON_TRUE : JSON_FALSE));
}

json_t *json_detach(json_t *object, const char *name)
{
    if (!json_is_object(object) || name == NULL) {
        return NULL;
    }
    for (json_t **link = &object->child; *link != NULL; link = &(*link)->next) {
        json_t *member = *link;
        if (member->name != NULL && strcmp(member->name, name) == 0) {
            *link = member->next;
            member->next = NULL;
            return member;
        }
    }
    return NULL;
}

void json_remove(json_t *object, const char *name)
{
    json_free(json_detach(object, name));
}

/* ---- printing ---- */

typedef struct {
    char  *text;
    size_t length;
    size_t capacity;
    bool   has_failed;
} writer_t;

static void put(writer_t *writer, const char *text, size_t length)
{
    if (writer->has_failed) {
        return;
    }
    if (writer->length + length + 1 > writer->capacity) {
        size_t capacity = writer->capacity * 2 > writer->length + length + 1
                              ? writer->capacity * 2
                              : writer->length + length + 64;
        char *grown = realloc(writer->text, capacity);
        if (grown == NULL) {
            writer->has_failed = true;
            return;
        }
        writer->text = grown;
        writer->capacity = capacity;
    }
    memcpy(writer->text + writer->length, text, length);
    writer->length += length;
    writer->text[writer->length] = '\0';
}

static void put_string(writer_t *writer, const char *text)
{
    static const char HEX[] = "0123456789abcdef";
    put(writer, "\"", 1);
    for (const char *at = text; *at != '\0'; at++) {
        unsigned char character = (unsigned char)*at;
        if (character == '"' || character == '\\') {
            char escaped[2] = { '\\', (char)character };
            put(writer, escaped, 2);
        } else if (character < 0x20) {
            char escaped[6] = { '\\', 'u', '0', '0', HEX[character >> 4], HEX[character & 15] };
            put(writer, escaped, 6);
        } else {
            put(writer, at, 1);
        }
    }
    put(writer, "\"", 1);
}

static void put_number(writer_t *writer, int64_t number)
{
    char     digits[24];
    char    *at = digits + sizeof digits;
    uint64_t magnitude = number < 0 ? 0 - (uint64_t)number : (uint64_t)number;
    do {
        *--at = (char)('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude != 0);
    if (number < 0) {
        *--at = '-';
    }
    put(writer, at, (size_t)(digits + sizeof digits - at));
}

static void put_value(writer_t *writer, const json_t *node)
{
    switch (node->type) {
    case JSON_NULL: put(writer, "null", 4); break;
    case JSON_FALSE: put(writer, "false", 5); break;
    case JSON_TRUE: put(writer, "true", 4); break;
    case JSON_NUMBER: put_number(writer, node->number); break;
    case JSON_STRING: put_string(writer, node->text != NULL ? node->text : ""); break;
    case JSON_ARRAY:
    case JSON_OBJECT:
        put(writer, node->type == JSON_ARRAY ? "[" : "{", 1);
        for (const json_t *member = node->child; member != NULL; member = member->next) {
            if (node->type == JSON_OBJECT) {
                put_string(writer, member->name != NULL ? member->name : "");
                put(writer, ":", 1);
            }
            put_value(writer, member);
            if (member->next != NULL) {
                put(writer, ",", 1);
            }
        }
        put(writer, node->type == JSON_ARRAY ? "]" : "}", 1);
        break;
    }
}

char *json_print(const json_t *node)
{
    if (node == NULL) {
        return NULL;
    }
    writer_t writer = { 0 };
    put_value(&writer, node);
    if (writer.has_failed) {
        free(writer.text);
        return NULL;
    }
    return writer.text;
}
