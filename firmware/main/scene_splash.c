/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "scenes.h"

#include <stdbool.h>
#include <stddef.h>


/* How long the splash stays at power-up before the device moves on by
 * itself. The panel takes three of these seconds to draw it. */
#define BOOT_LINGER_MS  6000
#define BOOT_TIMER      0
#define NO_PICTURE_MS   1 /* nothing to look at: as soon as a timer can fire */

/* The splash is the About application's picture: one screen says what the
 * device is, at power-up and from the menu. The application is somebody
 * else's scene, held here for as long as the splash has the screen. When
 * it cannot be shown there is no splash: the device moves on at once. */
typedef struct {
    scene_t             scene;
    scene_t            *next;
    scenes_set_timer_fn set_timer;
    scene_t            *picture;
    scenes_is_shown_fn  picture_is_shown;
    bool                was_suspended;
} splash_scene_t;

/* Any press is enough to leave it: move on now. */
static scene_reaction_t splash_button(scene_t *scene, scene_button_t button, scene_press_t press)
{
    (void)button;
    const splash_scene_t *splash = (const splash_scene_t *)scene;
    scene_reaction_t reaction = { SCENE_IGNORED, NULL };
    if (press == SCENE_PRESS_SHORT) {
        reaction.kind = SCENE_REPLACE;
        reaction.open = splash->next;
    }
    return reaction;
}

/* The splash lingers once. It is set aside whenever an application takes an
 * answer off the screen, and a device with a few of them has no six quiet
 * seconds after a start: lingering anew each time, it never moved on. */
static void splash_enter(scene_t *scene)
{
    const splash_scene_t *splash = (const splash_scene_t *)scene;
    if (splash->picture->ops->enter != NULL) {
        splash->picture->ops->enter(splash->picture);
    }
    bool lingers = splash->picture_is_shown(splash->picture) && !splash->was_suspended;
    splash->set_timer(BOOT_TIMER, lingers ? BOOT_LINGER_MS : NO_PICTURE_MS);
}

static void splash_leave(scene_t *scene)
{
    splash_scene_t *splash = (splash_scene_t *)scene;
    splash->was_suspended = true;
    splash->set_timer(BOOT_TIMER, 0);
    if (splash->picture->ops->leave != NULL) {
        splash->picture->ops->leave(splash->picture);
    }
}

static scene_reaction_t splash_timer(scene_t *scene, int timer_id)
{
    (void)timer_id;
    const splash_scene_t *splash = (const splash_scene_t *)scene;
    scene_reaction_t reaction = { SCENE_REPLACE, splash->next };
    return reaction;
}

static void splash_draw(scene_t *scene, gfx_t *surface)
{
    const splash_scene_t *splash = (const splash_scene_t *)scene;
    if (splash->picture_is_shown(splash->picture)) {
        splash->picture->ops->draw(splash->picture, surface);
    }
}

static const scene_ops_t SPLASH_OPS = {
    .enter = splash_enter,
    .leave = splash_leave,
    .button = splash_button,
    .timer = splash_timer,
    .draw = splash_draw,
};

scene_t *scene_boot_splash(scene_t *next, scenes_set_timer_fn set_timer, scene_t *picture,
                           scenes_is_shown_fn picture_is_shown)
{
    static splash_scene_t boot = { .scene = { .ops = &SPLASH_OPS, .fullscreen = true } };
    boot.next = next;
    boot.set_timer = set_timer;
    boot.picture = picture;
    boot.picture_is_shown = picture_is_shown;
    return &boot.scene;
}
