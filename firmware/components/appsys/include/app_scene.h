/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include "app.h"
#include "app_manifest.h"
#include "scene.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An installed application as a scene: to the window manager it is a scene
 * like the menu. The module is read from the store and loaded when the
 * scene takes the screen, and unloaded when it leaves, so there is never
 * more than one application in memory. */

/* NULL when there is no memory for it. store_root and services must
 * outlive the scene; the manifest is copied. */
scene_t *app_scene_create(const app_manifest_t *manifest, const char *store_root,
                          const app_services_t *services);

/* The application of this scene while it is loaded and running; NULL
 * for a scene that is not an application's, one that is not loaded and
 * one that has stopped. */
app_t *app_scene_app(scene_t *scene);

/* What the application asked for in a handler the host called past the
 * scene interface - data arrived - as a reaction for the window manager. */
scene_reaction_t app_scene_take_reaction(scene_t *scene);

/* For an application that is not on the screen and has something to
 * take: loads it without giving it the screen - no app_on_focus(), no
 * timers - and unloads it again. There is room for one application in
 * memory, so whatever is on the screen has to be suspended first
 * (wm_suspend). NULL when it is on the screen already or cannot be
 * loaded. */
app_t *app_scene_begin_background(scene_t *scene);
void   app_scene_end_background(scene_t *scene);

#ifdef __cplusplus
}
#endif
