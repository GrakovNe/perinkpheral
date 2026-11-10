/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_scene.h"

#include <stdlib.h>

#include "host_api.h"

#define MESSAGE_MARGIN      16
#define MESSAGE_LINE_HEIGHT 30

typedef struct {
    scene_t               scene;
    const app_image_t    *image;
    const app_services_t *services;
    app_t                *app;         /* NULL while not on the screen or not loadable */
    const char           *load_error;  /* why app is NULL although the scene is on the screen */
} app_scene_t;

static void cancel_timers(const app_services_t *services)
{
    for (int id = 0; id < HOST_TIMER_COUNT; id++) {
        services->timer_set(id, 0);
    }
}

static void app_scene_enter(scene_t *scene)
{
    app_scene_t *self = (app_scene_t *)scene;
    self->load_error = NULL;
    switch (app_load(self->image->wasm, self->image->wasm_size, self->services, &self->app)) {
    case APP_OK:
        app_focus(self->app);
        break;
    case APP_ERR_NO_MEMORY:
        self->load_error = "Not enough memory.";
        break;
    case APP_ERR_BAD_MODULE:
        self->load_error = "Not a valid application.";
        break;
    }
}

static void app_scene_leave(scene_t *scene)
{
    app_scene_t *self = (app_scene_t *)scene;
    if (self->app == NULL) {
        return;
    }
    app_blur(self->app);
    cancel_timers(self->services);
    app_unload(self->app);
    self->app = NULL;
}

static bool is_running(const app_scene_t *self)
{
    return self->app != NULL && !app_has_stopped(self->app);
}

static scene_reaction_t redraw_if_requested(app_scene_t *self)
{
    scene_reaction_t reaction = { SCENE_IGNORED, NULL };
    if (app_take_redraw_request(self->app)) {
        reaction.kind = SCENE_REDRAW;
    }
    return reaction;
}

static scene_reaction_t app_scene_button(scene_t *scene, scene_button_t button,
                                         scene_press_t press)
{
    app_scene_t *self = (app_scene_t *)scene;
    if (!is_running(self)) {
        /* Nothing answers there any more: any press leaves. */
        scene_reaction_t leave = { SCENE_BACK, NULL };
        return leave;
    }
    /* Applications count buttons 1..4, as they sit on the device. */
    app_button(self->app, (int)button + 1,
               press == SCENE_PRESS_LONG ? HOST_PRESS_LONG : HOST_PRESS_SHORT);
    return redraw_if_requested(self);
}

static scene_reaction_t app_scene_timer(scene_t *scene, int timer_id)
{
    app_scene_t *self = (app_scene_t *)scene;
    scene_reaction_t ignored = { SCENE_IGNORED, NULL };
    if (!is_running(self)) {
        return ignored;
    }
    app_timer(self->app, timer_id);
    return redraw_if_requested(self);
}

static void draw_message(gfx_t *surface, const char *title, const char *first, const char *second)
{
    const gfx_font_t *font = gfx_font_system();
    const char *lines[] = { title, first, second };
    for (int line = 0; line < 3; line++) {
        gfx_text_font(surface, MESSAGE_MARGIN, (line + 1) * MESSAGE_LINE_HEIGHT, font, lines[line],
                      true);
    }
}

static void app_scene_draw(scene_t *scene, gfx_t *surface)
{
    app_scene_t *self = (app_scene_t *)scene;
    if (self->app == NULL) {
        draw_message(surface, self->image->title, "Cannot start.", self->load_error);
        return;
    }
    if (!app_has_stopped(self->app)) {
        app_draw(self->app, surface);
        /* Drawing is itself the answer to any request made so far. */
        app_take_redraw_request(self->app);
    }
    if (app_has_stopped(self->app)) {
        /* Over whatever it managed to draw before it stopped. */
        gfx_clear(surface, false);
        draw_message(surface, self->image->title, "Stopped:", app_stop_reason(self->app));
    }
}

static const scene_ops_t APP_SCENE_OPS = {
    .enter = app_scene_enter,
    .leave = app_scene_leave,
    .button = app_scene_button,
    .timer = app_scene_timer,
    .draw = app_scene_draw,
};

scene_t *app_scene_create(const app_image_t *image, const app_services_t *services)
{
    app_scene_t *self = calloc(1, sizeof *self);
    if (self == NULL) {
        return NULL;
    }
    self->scene.ops = &APP_SCENE_OPS;
    self->scene.fullscreen = image->fullscreen;
    self->image = image;
    self->services = services;
    return &self->scene;
}
