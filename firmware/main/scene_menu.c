/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "scenes.h"

#include <stddef.h>

#include "screens.h"

typedef struct {
    scene_t             scene;
    const menu_entry_t *entries;
    int                 count;
    int                 selected;
} menu_scene_t;

/* Stepping through the rows is a row of pictures: none of them is a full
 * refresh, or the screen seems to rewind every few presses. Coming here
 * and leaving are full ones anyway. */
#define MOVED SCENE_REDRAW_NO_BLINK

static scene_reaction_t menu_button(scene_t *scene, scene_button_t button, scene_press_t press)
{
    menu_scene_t *menu = (menu_scene_t *)scene;
    scene_reaction_t reaction = { SCENE_IGNORED, NULL };
    if (press != SCENE_PRESS_SHORT) {
        return reaction;
    }
    if (button == SCENE_BUTTON_BACK) {
        reaction.kind = SCENE_BACK;
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

static void menu_row_at(void *context, int index, screens_menu_row_t *row)
{
    const menu_scene_t *menu = context;
    row->title = menu->entries[index].title;
    row->icon = menu->entries[index].icon;
}

static void menu_draw(scene_t *scene, gfx_t *surface)
{
    menu_scene_t *menu = (menu_scene_t *)scene;
    screens_menu(surface, menu_row_at, menu, menu->count, menu->selected);
}

static const scene_ops_t MENU_OPS = { .button = menu_button, .draw = menu_draw };

scene_t *scene_menu(const menu_entry_t *entries, int count)
{
    static menu_scene_t menu;
    menu.scene.ops = &MENU_OPS;
    menu.scene.fullscreen = false;
    menu.entries = entries;
    menu.count = count;
    menu.selected = 0;
    return &menu.scene;
}
