/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "scenes.h"

#include <stddef.h>

#include "gfx_icons.h"
#include "notice.h"

/* The partition of the applications cannot be read. Nothing is erased
 * behind the user's back: the device says so and asks. */
typedef struct {
    scene_t          scene;
    scene_t         *next;
    scenes_action_fn format;
} storage_scene_t;

static scene_reaction_t storage_button(scene_t *scene, scene_button_t button, scene_press_t press)
{
    const storage_scene_t *storage = (const storage_scene_t *)scene;
    scene_reaction_t reaction = { SCENE_IGNORED, NULL };
    if (press != SCENE_PRESS_SHORT) {
        return reaction;
    }
    if (button == SCENE_BUTTON_OK) {
        storage->format();
    } else if (button == SCENE_BUTTON_BACK) {
        reaction.kind = SCENE_OPEN;
        reaction.open = storage->next;
    }
    return reaction;
}

static void storage_draw(scene_t *scene, gfx_t *surface)
{
    (void)scene;
    const notice_t notice = {
        .first = "Storage",
        .second = "error",
        .icon = GFX_ICON_WARNING,
        .ok = "Format",
        .back = "Skip",
    };
    notice_draw(surface, &notice);
}

static const scene_ops_t STORAGE_OPS = {
    .button = storage_button,
    .draw = storage_draw,
};

scene_t *scene_storage_error(scene_t *next, scenes_action_fn format)
{
    static storage_scene_t storage = { .scene = { .ops = &STORAGE_OPS, .fullscreen = true } };
    storage.next = next;
    storage.format = format;
    return &storage.scene;
}
