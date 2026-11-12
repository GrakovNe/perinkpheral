/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "scenes.h"

#include <stddef.h>

#include "screens.h"

/* The device is busy with something that takes a while and must not be
 * interrupted: it says what, and the buttons do nothing. One picture - no
 * bar that moves, the panel is not for that. */
typedef struct {
    scene_t     scene;
    const char *first;
    const char *second;
} working_scene_t;

static scene_reaction_t working_button(scene_t *scene, scene_button_t button, scene_press_t press)
{
    (void)scene;
    (void)button;
    (void)press;
    scene_reaction_t reaction = { SCENE_IGNORED, NULL };
    return reaction;
}

static void working_draw(scene_t *scene, gfx_t *surface)
{
    const working_scene_t *working = (const working_scene_t *)scene;
    const screens_notice_t notice = {
        .first = working->first,
        .second = working->second,
        .icon = SCREENS_NO_ICON,
        .is_working = true,
    };
    screens_notice(surface, &notice);
}

static const scene_ops_t WORKING_OPS = {
    .button = working_button,
    .draw = working_draw,
};

scene_t *scene_working(const char *first, const char *second)
{
    static working_scene_t working = { .scene = { .ops = &WORKING_OPS, .fullscreen = true } };
    working.first = first;
    working.second = second;
    return &working.scene;
}
