/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A JSON document as a tree: parsed from text, built in code, printed as
 * text. Numbers are integers of 64 bits: the fraction of a number is
 * dropped and `is_whole` says so; a number with an exponent is refused.
 * No floating point is used. Pure, host-tested. For an answer that
 * arrives in pieces see sdk/lib/json_pick.h. */

#define JSON_DEPTH_MAX 16

typedef enum {
    JSON_NULL,
    JSON_FALSE,
    JSON_TRUE,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
} json_type_t;

typedef struct json {
    struct json *next;  /* the next member of the same array or object */
    struct json *child; /* JSON_ARRAY, JSON_OBJECT: the first member */
    char        *name;  /* a member of an object: its key; else NULL */
    char        *text;  /* JSON_STRING: the value, unescaped, terminated */
    int64_t      number;   /* JSON_NUMBER */
    bool         is_whole; /* JSON_NUMBER: the text had no fraction */
    json_type_t  type;
} json_t;

/* Parses a terminated text. Returns NULL when it is not one JSON value,
 * is nested deeper than JSON_DEPTH_MAX or memory ran out. The tree is
 * freed with json_free(). */
json_t *json_parse(const char *text);

/* Frees a tree; NULL is allowed. */
void json_free(json_t *node);

/* The member `name` of an object, or NULL. `object` may be NULL or of
 * another type. */
json_t *json_get(const json_t *object, const char *name);

/* The number of members; 0 for anything but an array or an object. */
int json_count(const json_t *container);

/* Member number `index`, or NULL. */
json_t *json_at(const json_t *container, int index);

/* Every member of an array or an object, in order. `container` may be NULL. */
#define json_for_each(member, container) \
    for ((member) = (container) != NULL ? (container)->child : NULL; (member) != NULL; \
         (member) = (member)->next)

static inline bool json_is_string(const json_t *node)
{
    return node != NULL && node->type == JSON_STRING;
}
static inline bool json_is_number(const json_t *node)
{
    return node != NULL && node->type == JSON_NUMBER;
}
static inline bool json_is_array(const json_t *node)
{
    return node != NULL && node->type == JSON_ARRAY;
}
static inline bool json_is_object(const json_t *node)
{
    return node != NULL && node->type == JSON_OBJECT;
}
static inline bool json_is_bool(const json_t *node)
{
    return node != NULL && (node->type == JSON_TRUE || node->type == JSON_FALSE);
}
static inline bool json_is_true(const json_t *node)
{
    return node != NULL && node->type == JSON_TRUE;
}

/* ---- building ---- */

json_t *json_new_object(void);
json_t *json_new_array(void);
json_t *json_new_string(const char *text);

/* Appends `item` to an array. The array owns it afterwards. */
void json_array_add(json_t *array, json_t *item);

/* Adds a member to an object. Each returns the new node, or NULL when
 * memory ran out or an argument is NULL; json_add_item() owns `item`
 * either way. */
json_t *json_add_item(json_t *object, const char *name, json_t *item);
json_t *json_add_string(json_t *object, const char *name, const char *text);
json_t *json_add_number(json_t *object, const char *name, int64_t number);
json_t *json_add_bool(json_t *object, const char *name, bool value);

/* Takes the member `name` out of an object and returns it; the caller
 * owns it. NULL when there is none. */
json_t *json_detach(json_t *object, const char *name);

/* Removes and frees the member `name`. */
void json_remove(json_t *object, const char *name);

/* The tree as text without spaces, to be released with free(). NULL when
 * memory ran out. */
char *json_print(const json_t *node);

#ifdef __cplusplus
}
#endif
