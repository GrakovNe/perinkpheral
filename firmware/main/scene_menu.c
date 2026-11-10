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
        reaction.kind = SCENE_REDRAW;
        break;
    case SCENE_BUTTON_DOWN:
        menu->selected = (menu->selected + 1) % menu->count;
        reaction.kind = SCENE_REDRAW;
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

static void menu_item_at(void *context, int index, screens_menu_item_t *item)
{
    const menu_scene_t *menu = context;
    item->title = menu->entries[index].title;
    item->hint = menu->entries[index].hint;
}

static void menu_draw(scene_t *scene, gfx_t *surface)
{
    menu_scene_t *menu = (menu_scene_t *)scene;
    screens_menu(surface, menu_item_at, menu, menu->count, menu->selected);
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
