/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "scenes.h"

#include <stddef.h>

#include "screens.h"
#include "wifi.h"

/* Any press is enough to leave it. */
static scene_reaction_t setup_button(scene_t *scene, scene_button_t button, scene_press_t press)
{
    (void)scene;
    (void)button;
    scene_reaction_t reaction = { press == SCENE_PRESS_SHORT ? SCENE_BACK : SCENE_IGNORED, NULL };
    return reaction;
}

static void setup_draw(scene_t *scene, gfx_t *surface)
{
    (void)scene;
    wifi_status_t wifi;
    wifi_get_status(&wifi);
    screens_setup(surface, wifi.access_point_name, WIFI_SETUP_ADDRESS);
}

static const scene_ops_t SETUP_OPS = { .button = setup_button, .draw = setup_draw };

scene_t *scene_setup(void)
{
    static scene_t setup = { .ops = &SETUP_OPS, .fullscreen = false };
    return &setup;
}
