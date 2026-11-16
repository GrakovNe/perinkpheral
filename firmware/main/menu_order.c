/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "menu_order.h"

#include <string.h>

/* Where `name` stands among the lines of `order`, or -1. */
static int position_of(const char *order, const char *name)
{
    size_t length = strlen(name);
    int    line = 0;
    for (const char *at = order; at != NULL && *at != '\0'; line++) {
        const char *end = strchr(at, '\n');
        size_t      line_length = end != NULL ? (size_t)(end - at) : strlen(at);
        if (line_length == length && strncmp(at, name, length) == 0) {
            return line;
        }
        at = end != NULL ? end + 1 : NULL;
    }
    return -1;
}

void menu_order_apply(menu_entry_t *entries, int count, const char *order)
{
    if (order == NULL || order[0] == '\0') {
        return;
    }
    /* Insertion: stable, and a menu is a couple of dozen entries. What has no
     * place is -1, before every place, so it stays at the top as it came. */
    for (int i = 1; i < count; i++) {
        menu_entry_t moving = entries[i];
        int          place = entries[i].name != NULL ? position_of(order, entries[i].name) : -1;
        int          j = i;
        while (j > 0) {
            const char *before = entries[j - 1].name;
            int         its = before != NULL ? position_of(order, before) : -1;
            if (its <= place) {
                break;
            }
            entries[j] = entries[j - 1];
            j--;
        }
        entries[j] = moving;
    }
}

bool menu_order_forget(char *order, const char *name)
{
    size_t length = strlen(name);
    for (char *at = order; *at != '\0';) {
        char  *end = strchr(at, '\n');
        size_t line_length = end != NULL ? (size_t)(end - at) : strlen(at);
        if (line_length == length && strncmp(at, name, length) == 0) {
            char *rest = end != NULL ? end + 1 : at + line_length;
            memmove(at, rest, strlen(rest) + 1);
            return true;
        }
        at = end != NULL ? end + 1 : at + line_length;
    }
    return false;
}

bool menu_order_write(const menu_entry_t *entries, int count, char *order, size_t size)
{
    size_t used = 0;
    if (size == 0) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        const char *name = entries[i].name != NULL ? entries[i].name : "";
        size_t      length = strlen(name);
        if (length == 0) {
            continue;
        }
        if (used + length + 1 >= size) {
            order[0] = '\0';
            return false;
        }
        memcpy(order + used, name, length);
        used += length;
        order[used++] = '\n';
    }
    order[used] = '\0';
    return true;
}
