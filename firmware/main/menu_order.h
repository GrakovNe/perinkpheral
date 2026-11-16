/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stddef.h>

#include "scenes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The order of the menu, as the user set it: the names of what it lists, a
 * line each. Pure. */

/* The longest order there is: every application and the system ones, each
 * a name with "sys." before it and a line break. */
#define MENU_ORDER_MAX 512

/* Puts `entries` in the order `order` names them. What it does not name -
 * an application installed since - comes first, in the order it came: a new
 * application is at the top. A name that is no entry is passed over. */
void menu_order_apply(menu_entry_t *entries, int count, const char *order);

/* Takes `name` out of `order`: an application removed is new again when it
 * comes back, and comes first. True when it was there. */
bool menu_order_forget(char *order, const char *name);

/* The names of `entries` as they stand, a line each, into `order` of `size`
 * bytes. False when they do not fit: `order` is then empty. */
bool menu_order_write(const menu_entry_t *entries, int count, char *order, size_t size);

#ifdef __cplusplus
}
#endif
