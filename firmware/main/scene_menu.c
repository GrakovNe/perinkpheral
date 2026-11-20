/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "scenes.h"

#include <stddef.h>
#include <string.h>

#include "screens.h"

typedef struct {
    scene_t       scene;
    menu_entry_t *entries;
    int           count;
    int           selected;
    screens_menu_view_t view; /* where the list stands, as it was drawn last */
    scene_t      *back_target; /* what BACK leads to: the default application, or NULL */
    /* Moving a row: where it was taken from, to put it back when BACK says so. */
    bool          is_moving;
    int           moved_from;
} menu_scene_t;

/* Stepping through the rows is a row of pictures: none of them is a full
 * refresh, or the screen seems to rewind every few presses. Coming here
 * and leaving are full ones anyway. */
#define MOVED SCENE_REDRAW_NO_BLINK

static scene_menu_order_fn s_on_order;

void scene_menu_set_on_order(scene_menu_order_fn on_order)
{
    s_on_order = on_order;
}

static void swap(menu_scene_t *menu, int one, int other)
{
    menu_entry_t kept = menu->entries[one];
    menu->entries[one] = menu->entries[other];
    menu->entries[other] = kept;
}

/* The row being moved goes back to where it was taken from. */
static void put_back(menu_scene_t *menu)
{
    while (menu->selected > menu->moved_from) {
        swap(menu, menu->selected, menu->selected - 1);
        menu->selected--;
    }
    while (menu->selected < menu->moved_from) {
        swap(menu, menu->selected, menu->selected + 1);
        menu->selected++;
    }
    menu->is_moving = false;
}

/* UP and DOWN carry the row a step, and stop at the ends: a row that went
 * round from the top to the bottom would be lost to the eye. OK keeps the
 * order, BACK puts the row back. */
static scene_reaction_t moving_button(menu_scene_t *menu, scene_button_t button)
{
    scene_reaction_t reaction = { MOVED, NULL };
    switch (button) {
    case SCENE_BUTTON_UP:
        if (menu->selected > 0) {
            swap(menu, menu->selected, menu->selected - 1);
            menu->selected--;
        }
        break;
    case SCENE_BUTTON_DOWN:
        if (menu->selected < menu->count - 1) {
            swap(menu, menu->selected, menu->selected + 1);
            menu->selected++;
        }
        break;
    case SCENE_BUTTON_OK:
        menu->is_moving = false;
        if (s_on_order != NULL && menu->selected != menu->moved_from) {
            s_on_order(menu->entries, menu->count);
        }
        break;
    case SCENE_BUTTON_BACK:
        put_back(menu);
        break;
    }
    return reaction;
}

static scene_reaction_t menu_button(scene_t *scene, scene_button_t button, scene_press_t press)
{
    menu_scene_t *menu = (menu_scene_t *)scene;
    scene_reaction_t reaction = { SCENE_IGNORED, NULL };
    /* A long press of OK takes the row to move. */
    if (press == SCENE_PRESS_LONG && button == SCENE_BUTTON_OK && !menu->is_moving &&
        menu->count > 1) {
        menu->is_moving = true;
        menu->moved_from = menu->selected;
        reaction.kind = MOVED;
        return reaction;
    }
    /* A long BACK - the beginning again - ends a move as BACK does. */
    if (press == SCENE_PRESS_LONG && button == SCENE_BUTTON_BACK && menu->is_moving) {
        put_back(menu);
        reaction.kind = MOVED;
        return reaction;
    }
    /* A long UP or DOWN: a page of rows at a time, to the ends and no further. A row being
     * moved is moved a row at a time. */
    if (press == SCENE_PRESS_LONG && (button == SCENE_BUTTON_UP || button == SCENE_BUTTON_DOWN) &&
        !menu->is_moving && menu->count > 0) {
        int page = menu->view.visible > 0 ? menu->view.visible : 1;
        int to = menu->selected + (button == SCENE_BUTTON_UP ? -page : page);
        to = to < 0 ? 0 : to > menu->count - 1 ? menu->count - 1 : to;
        if (to != menu->selected) {
            menu->view.first += to - menu->selected; /* the page moves with it */
            menu->selected = to;
            reaction.kind = MOVED;
        }
        return reaction;
    }
    if (press != SCENE_PRESS_SHORT) {
        return reaction;
    }
    if (menu->is_moving) {
        return moving_button(menu, button);
    }
    if (button == SCENE_BUTTON_BACK) {
        /* Nothing is under the menu: BACK is the default application when
         * there is one, and nothing when there is not. */
        reaction.kind = menu->back_target != NULL ? SCENE_OPEN : SCENE_BACK;
        reaction.open = menu->back_target;
        return reaction;
    }
    if (menu->count == 0) {
        return reaction;
    }
    switch (button) {
    case SCENE_BUTTON_UP:
        menu->selected = (menu->selected + menu->count - 1) % menu->count;
        reaction.kind = MOVED;
        break;
    case SCENE_BUTTON_DOWN:
        menu->selected = (menu->selected + 1) % menu->count;
        reaction.kind = MOVED;
        break;
    case SCENE_BUTTON_OK:
        reaction.kind = SCENE_OPEN;
        reaction.open = menu->entries[menu->selected].scene;
        break;
    case SCENE_BUTTON_BACK:
        break;
    }
    return reaction;
}

/* Whatever takes the screen from a row being moved - the menu left alone,
 * an alarm - puts the row back: an order is kept only by OK. */
static void menu_leave(scene_t *scene)
{
    menu_scene_t *menu = (menu_scene_t *)scene;
    if (menu->is_moving) {
        put_back(menu);
    }
}

static void menu_row_at(void *context, int index, screens_menu_row_t *row)
{
    const menu_scene_t *menu = context;
    row->title = menu->entries[index].title;
    row->icon = menu->entries[index].icon;
}

static void menu_draw(scene_t *scene, gfx_t *surface)
{
    menu_scene_t *menu = (menu_scene_t *)scene;
    screens_menu(surface, menu_row_at, menu, menu->count, menu->selected, menu->is_moving, &menu->view);
}

static const scene_ops_t MENU_OPS = { .button = menu_button, .draw = menu_draw, .leave = menu_leave };

static menu_scene_t s_menu_scene;

void scene_menu_set_back_target(scene_t *back_target)
{
    s_menu_scene.back_target = back_target;
}

bool scene_menu_lists(const scene_t *scene)
{
    const menu_scene_t *menu = &s_menu_scene;
    for (int i = 0; i < menu->count; i++) {
        if (menu->entries[i].scene == scene) {
            return true;
        }
    }
    return false;
}

scene_t *scene_menu_neighbour(const scene_t *from, int step)
{
    const menu_scene_t *menu = &s_menu_scene;
    int at = -1;
    for (int i = 0; i < menu->count; i++) {
        if (menu->entries[i].scene == from) {
            at = i;
        }
    }
    /* Off the list - a system page: one step forward is its first row, one back its last. */
    if (at < 0) {
        at = step > 0 ? -1 : menu->count;
    }
    for (int tried = 0; tried < menu->count; tried++) {
        at = (at + step + menu->count) % menu->count;
        const menu_entry_t *entry = &menu->entries[at];
        if (entry->scene != from && strncmp(entry->name, "sys.", 4) != 0) {
            return entry->scene;
        }
    }
    return NULL;
}

scene_t *scene_menu(menu_entry_t *entries, int count)
{
    s_menu_scene.scene.ops = &MENU_OPS;
    s_menu_scene.scene.fullscreen = false;
    s_menu_scene.entries = entries;
    s_menu_scene.count = count;
    s_menu_scene.selected = 0;
    s_menu_scene.is_moving = false;
    return &s_menu_scene.scene;
}
