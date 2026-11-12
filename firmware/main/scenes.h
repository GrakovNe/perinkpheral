/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "scene.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The host's own scenes. Each one ties a model to a view and decides what
 * the buttons do; none of them draws a pixel itself or knows about the
 * panel. */

/* Something the menu can open. */
typedef struct {
    const char     *title;
    const uint16_t *icon; /* APP_ICON_CELLS rows, or NULL: see app_manifest.h */
    scene_t        *scene;
} menu_entry_t;

/* entries must outlive the scene. */
scene_t *scene_menu(const menu_entry_t *entries, int count);

/* A scene sets a timer through this; 0 ms cancels. */
typedef void (*scenes_set_timer_fn)(int id, int32_t milliseconds);

/* Whether a scene held by another has a picture of its own to draw now. */
typedef bool (*scenes_is_shown_fn)(scene_t *scene);

/* The splash at power-up: it gives the screen to `next` by itself after
 * a few seconds, or at once on a press. What it shows is `picture`, a
 * full-screen scene - the About application; when `picture_is_shown` says
 * that one cannot draw, it shows nothing and moves on at once. */
scene_t *scene_boot_splash(scene_t *next, scenes_set_timer_fn set_timer, scene_t *picture,
                           scenes_is_shown_fn picture_is_shown);

/* What a scene asks the host to do when the user chose it. */
typedef void (*scenes_action_fn)(void);

/* The applications' partition cannot be read: says so and offers to
 * format it. Cancel, or BACK, gives the screen to `next`; Format calls
 * `format`, which does not come back with the screen. */
scene_t *scene_storage_error(scene_t *next, scenes_action_fn format);

/* How a question was answered. */
typedef void (*scenes_answer_fn)(bool is_confirmed);

/* A question from the network that only somebody at the device may
 * answer: `question` in the header ("Update firmware?"), and Cancel or
 * `action` to choose between. `answer`
 * is called with the choice, and with false whenever the scene loses the
 * screen: the first call is the answer. The texts must outlive the scene's time on the
 * screen. */
scene_t *scene_confirm(const char *question, const char *action, scenes_answer_fn answer);

#ifdef __cplusplus
}
#endif
