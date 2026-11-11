/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "app_scene.h"

#include <stdlib.h>

#include "app_store.h"
#include "host_api.h"

#define MESSAGE_MARGIN      16
#define MESSAGE_LINE_HEIGHT 30

typedef struct {
    scene_t               scene;
    app_manifest_t        manifest;
    const char           *store_root;
    const app_services_t *services;
    uint8_t              *module;     /* the file's bytes, kept while the application runs */
    app_t                *app;        /* NULL while not on the screen or not loadable */
    const char           *load_error; /* why app is NULL although the scene is on the screen */
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
    if (self->module != NULL) {
        return; /* already on the screen: nothing to load twice */
    }
    self->load_error = NULL;
    size_t module_size;
    self->module = app_store_read_module(self->store_root, self->manifest.name, &module_size);
    if (self->module == NULL) {
        self->load_error = "Its file cannot be read.";
        return;
    }
    switch (app_load(self->manifest.name, self->module, module_size, self->services,
                     &self->app)) {
    case APP_OK:
        app_focus(self->app);
        return;
    case APP_ERR_NO_MEMORY:
        self->load_error = "Not enough memory.";
        break;
    case APP_ERR_BAD_MODULE:
        self->load_error = "Not a valid application.";
        break;
    }
    free(self->module);
    self->module = NULL;
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
    free(self->module);
    self->module = NULL;
}

static bool is_running(const app_scene_t *self)
{
    return self->app != NULL && !app_has_stopped(self->app);
}

/* What the application asked for while its handler ran. */
static scene_reaction_t reaction_of(app_scene_t *self)
{
    scene_reaction_t reaction = { SCENE_IGNORED, NULL };
    if (app_take_exit_request(self->app)) {
        reaction.kind = SCENE_BACK;
    } else if (app_take_redraw_request(self->app)) {
        int flags = app_take_redraw_flags(self->app);
        reaction.kind = (flags & HOST_REDRAW_CLEAN)  ? SCENE_REDRAW_CLEAN
                        : (flags & HOST_REDRAW_SOON) ? SCENE_REDRAW_SOON
                                                     : SCENE_REDRAW;
    }
    return reaction;
}

static scene_reaction_t app_scene_button(scene_t *scene, scene_button_t button,
                                         scene_press_t press)
{
    app_scene_t *self = (app_scene_t *)scene;
    if (!is_running(self)) {
        /* Nothing answers there any more: a press leaves. Only the press as
         * a whole - leaving on the way down would hand the rest of it to
         * the menu, which would open this application again. */
        scene_reaction_t reaction = { press == SCENE_PRESS_SHORT ? SCENE_BACK : SCENE_IGNORED,
                                      NULL };
        return reaction;
    }
    static const int HOST_PRESS_OF[] = {
        [SCENE_PRESS_DOWN] = HOST_PRESS_DOWN,
        [SCENE_PRESS_UP] = HOST_PRESS_UP,
        [SCENE_PRESS_SHORT] = HOST_PRESS_SHORT,
        [SCENE_PRESS_LONG] = HOST_PRESS_LONG,
    };
    /* Applications count buttons 1..4, as they sit on the device. */
    app_button(self->app, (int)button + 1, HOST_PRESS_OF[press]);
    return reaction_of(self);
}

static scene_reaction_t app_scene_timer(scene_t *scene, int timer_id)
{
    app_scene_t *self = (app_scene_t *)scene;
    scene_reaction_t ignored = { SCENE_IGNORED, NULL };
    if (!is_running(self)) {
        return ignored;
    }
    app_timer(self->app, timer_id);
    return reaction_of(self);
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
        draw_message(surface, self->manifest.title, "Cannot start.", self->load_error);
        return;
    }
    if (!app_has_stopped(self->app)) {
        app_draw(self->app, surface);
        /* Drawing is itself the answer to any request made so far. */
        app_take_redraw_request(self->app);
        app_take_redraw_flags(self->app);
    }
    if (app_has_stopped(self->app)) {
        /* Over whatever it managed to draw before it stopped. */
        gfx_clear(surface, false);
        draw_message(surface, self->manifest.title, "Stopped:", app_stop_reason(self->app));
    }
}

static const scene_ops_t APP_SCENE_OPS = {
    .enter = app_scene_enter,
    .leave = app_scene_leave,
    .button = app_scene_button,
    .timer = app_scene_timer,
    .draw = app_scene_draw,
};

scene_t *app_scene_create(const app_manifest_t *manifest, const char *store_root,
                          const app_services_t *services)
{
    app_scene_t *self = calloc(1, sizeof *self);
    if (self == NULL) {
        return NULL;
    }
    self->scene.ops = &APP_SCENE_OPS;
    self->scene.fullscreen = manifest->fullscreen;
    self->manifest = *manifest;
    self->store_root = store_root;
    self->services = services;
    return &self->scene;
}
