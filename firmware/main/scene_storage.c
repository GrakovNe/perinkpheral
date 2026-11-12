/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "scenes.h"

#include <stddef.h>

#include "screens.h"

/* The partition of the applications cannot be read. Nothing is erased
 * behind the user's back: the device says so and asks. Cancel is where
 * the selection starts, and BACK means the same. */
typedef struct {
    scene_t           scene;
    scene_t          *next;
    scenes_action_fn  format;
    bool              format_is_selected;
} storage_scene_t;

static scene_reaction_t storage_button(scene_t *scene, scene_button_t button, scene_press_t press)
{
    storage_scene_t *storage = (storage_scene_t *)scene;
    scene_reaction_t reaction = { SCENE_IGNORED, NULL };
    if (press != SCENE_PRESS_SHORT) {
        return reaction;
    }
    switch (button) {
    case SCENE_BUTTON_UP:
    case SCENE_BUTTON_DOWN:
        storage->format_is_selected = !storage->format_is_selected;
        reaction.kind = SCENE_REDRAW_NO_BLINK;
        break;
    case SCENE_BUTTON_OK:
        if (storage->format_is_selected) {
            storage->format();
            break;
        }
        /* fall through */
    case SCENE_BUTTON_BACK:
        reaction.kind = SCENE_OPEN;
        reaction.open = storage->next;
        break;
    }
    return reaction;
}

static void storage_draw(scene_t *scene, gfx_t *surface)
{
    const storage_scene_t *storage = (const storage_scene_t *)scene;
    const screens_question_t question = {
        .cancel = "Cancel",
        .action = "Format",
        .action_is_selected = storage->format_is_selected,
    };
    screens_question(surface, &question);
}

static const scene_ops_t STORAGE_OPS = {
    .button = storage_button,
    .draw = storage_draw,
};

scene_t *scene_storage_error(scene_t *next, scenes_action_fn format)
{
    static storage_scene_t storage = {
        .scene = { .ops = &STORAGE_OPS, .header_left = "Storage error", .header_right = " " },
    };
    storage.next = next;
    storage.format = format;
    storage.format_is_selected = false;
    return &storage.scene;
}
