/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "scenes.h"

#include <stddef.h>

#include "screens.h"

/* How long the splash stays at power-up before the device moves on by
 * itself. The panel takes three of these seconds to draw it. */
#define BOOT_LINGER_MS  6000
#define BOOT_TIMER      0

typedef struct {
    scene_t            scene;
    scene_t           *next;      /* NULL: the splash stays until a press */
    scenes_set_timer_fn set_timer;
} splash_scene_t;

/* Any press is enough to leave it; at power-up that means: move on now. */
static scene_reaction_t splash_button(scene_t *scene, scene_button_t button, scene_press_t press)
{
    (void)button;
    const splash_scene_t *splash = (const splash_scene_t *)scene;
    scene_reaction_t reaction = { SCENE_IGNORED, NULL };
    if (press != SCENE_PRESS_SHORT) {
        return reaction;
    }
    if (splash->next != NULL) {
        reaction.kind = SCENE_OPEN;
        reaction.open = splash->next;
    } else {
        reaction.kind = SCENE_BACK;
    }
    return reaction;
}

static void splash_enter(scene_t *scene)
{
    const splash_scene_t *splash = (const splash_scene_t *)scene;
    if (splash->next != NULL) {
        splash->set_timer(BOOT_TIMER, BOOT_LINGER_MS);
    }
}

static void splash_leave(scene_t *scene)
{
    const splash_scene_t *splash = (const splash_scene_t *)scene;
    if (splash->next != NULL) {
        splash->set_timer(BOOT_TIMER, 0);
    }
}

static scene_reaction_t splash_timer(scene_t *scene, int timer_id)
{
    (void)timer_id;
    const splash_scene_t *splash = (const splash_scene_t *)scene;
    scene_reaction_t reaction = { SCENE_OPEN, splash->next };
    return reaction;
}

static void splash_draw(scene_t *scene, gfx_t *surface)
{
    (void)scene;
    screens_splash(surface);
}

static const scene_ops_t SPLASH_OPS = {
    .enter = splash_enter,
    .leave = splash_leave,
    .button = splash_button,
    .timer = splash_timer,
    .draw = splash_draw,
};

scene_t *scene_splash(void)
{
    static splash_scene_t about = { .scene = { .ops = &SPLASH_OPS, .fullscreen = true } };
    return &about.scene;
}

scene_t *scene_boot_splash(scene_t *next, scenes_set_timer_fn set_timer)
{
    static splash_scene_t boot = { .scene = { .ops = &SPLASH_OPS, .fullscreen = true } };
    boot.next = next;
    boot.set_timer = set_timer;
    return &boot.scene;
}
