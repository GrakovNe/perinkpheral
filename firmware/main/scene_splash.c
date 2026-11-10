/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "scenes.h"

#include <stddef.h>

#include "screens.h"

/* Any press is enough to leave it. */
static scene_reaction_t splash_button(scene_t *scene, scene_button_t button, scene_press_t press)
{
    (void)scene;
    (void)button;
    scene_reaction_t reaction = { press == SCENE_PRESS_SHORT ? SCENE_BACK : SCENE_IGNORED, NULL };
    return reaction;
}

static void splash_draw(scene_t *scene, gfx_t *surface)
{
    (void)scene;
    screens_splash(surface);
}

static const scene_ops_t SPLASH_OPS = { .button = splash_button, .draw = splash_draw };

scene_t *scene_splash(void)
{
    static scene_t splash = { .ops = &SPLASH_OPS, .fullscreen = true };
    return &splash;
}
